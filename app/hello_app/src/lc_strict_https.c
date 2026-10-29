#include "lc_strict_https.h"
#include "lc_http_framing.h"

#ifdef __NuttX__

#include <nuttx/config.h>

#include "agent_compat.h"
#include "agent_config.h"
#include "infra/http_proxy.h"

#ifdef CONFIG_AI_AGENT_NET_RPMSG
#  include "network/network_manager.h"
#endif

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"

#define LC_STRICT_HTTPS_TIMEOUT_SECONDS 30
#define LC_STRICT_HTTPS_RESOLVER_TASKS_MAX 3u

/* getaddrinfo() is synchronous on NuttX. The request therefore runs it in one
 * bounded, separately owned resolver task and waits on the same monotonic
 * deadline used by TCP and TLS. If DNS overruns, the caller fails closed at
 * the deadline while that one task owns and eventually releases its result.
 * A small global gate allows normal product/accessory overlap but prevents
 * timed-out resolver tasks from accumulating without bound. The
 * target resolver must still have finite platform timeouts so the abandoned
 * task is eventually reclaimed. */
#if !defined(CONFIG_NETDB_DNSCLIENT) || \
    !defined(CONFIG_NETDB_DNSCLIENT_SEND_TIMEOUT) || \
    !defined(CONFIG_NETDB_DNSCLIENT_RECV_TIMEOUT) || \
    !defined(CONFIG_NETDB_DNSCLIENT_RETRIES)
#  error "strict HTTPS requires the bounded NuttX DNS client"
#endif
#if CONFIG_NETDB_DNSCLIENT_SEND_TIMEOUT <= 0 || \
    CONFIG_NETDB_DNSCLIENT_RECV_TIMEOUT <= 0 || \
    CONFIG_NETDB_DNSCLIENT_RETRIES <= 0
#  error "strict HTTPS requires finite positive NuttX DNS timeouts"
#endif

typedef struct
{
  pthread_mutex_t mutex;
  pthread_cond_t condition;
  char host[96];
  char port[6];
  struct addrinfo *addresses;
  int result;
  bool done;
  bool abandoned;
} resolver_task_t;

static pthread_mutex_t g_resolver_gate = PTHREAD_MUTEX_INITIALIZER;
static unsigned int g_resolver_tasks;

typedef struct
{
  mbedtls_ssl_context ssl;
  mbedtls_ssl_config configuration;
  mbedtls_net_context network;
  mbedtls_ctr_drbg_context random;
  mbedtls_x509_crt trust;
  const char *ca_pem;
  size_t ca_pem_length;
  struct timespec deadline;
  bool deadline_ready;
  bool initialized;
  bool resource_acquired;
} lc_strict_tls_t;

static bool resolver_claim_slot(void)
{
  bool claimed = false;
  if (pthread_mutex_lock(&g_resolver_gate) != 0) return false;
  if (g_resolver_tasks < LC_STRICT_HTTPS_RESOLVER_TASKS_MAX)
    {
      g_resolver_tasks++;
      claimed = true;
    }
  (void)pthread_mutex_unlock(&g_resolver_gate);
  return claimed;
}

static void resolver_release_slot(void)
{
  if (pthread_mutex_lock(&g_resolver_gate) != 0) return;
  if (g_resolver_tasks > 0u) g_resolver_tasks--;
  (void)pthread_mutex_unlock(&g_resolver_gate);
}

static void resolver_task_dispose(resolver_task_t *task,
                                  bool release_addresses)
{
  if (task == NULL) return;
  if (release_addresses && task->addresses != NULL)
    freeaddrinfo(task->addresses);
  (void)pthread_cond_destroy(&task->condition);
  (void)pthread_mutex_destroy(&task->mutex);
  free(task);
  resolver_release_slot();
}

static void *resolver_worker(void *raw_task)
{
  resolver_task_t *task = raw_task;
  struct addrinfo hints;
  struct addrinfo *addresses = NULL;
  int result;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  result = getaddrinfo(task->host, task->port, &hints, &addresses);

  if (pthread_mutex_lock(&task->mutex) != 0)
    {
      if (addresses != NULL) freeaddrinfo(addresses);
      resolver_task_dispose(task, false);
      return NULL;
    }
  if (task->abandoned)
    {
      (void)pthread_mutex_unlock(&task->mutex);
      if (addresses != NULL) freeaddrinfo(addresses);
      resolver_task_dispose(task, false);
      return NULL;
    }
  task->addresses = addresses;
  task->result = result;
  task->done = true;
  (void)pthread_cond_signal(&task->condition);
  (void)pthread_mutex_unlock(&task->mutex);
  return NULL;
}

static int resolve_with_deadline(lc_strict_tls_t *tls, const char *host,
                                 const char *port,
                                 struct addrinfo **addresses)
{
  pthread_condattr_t condition_attributes;
  pthread_attr_t thread_attributes;
  resolver_task_t *task;
  pthread_t thread;
  int result;

  if (tls == NULL || host == NULL || port == NULL || addresses == NULL ||
      strlen(host) >= sizeof(((resolver_task_t *)0)->host) ||
      strlen(port) >= sizeof(((resolver_task_t *)0)->port) ||
      !resolver_claim_slot()) return -1;
  *addresses = NULL;
  task = calloc(1u, sizeof(*task));
  if (task == NULL)
    {
      resolver_release_slot();
      return -1;
    }
  if (pthread_mutex_init(&task->mutex, NULL) != 0)
    {
      free(task);
      resolver_release_slot();
      return -1;
    }
  if (pthread_condattr_init(&condition_attributes) != 0)
    {
      (void)pthread_mutex_destroy(&task->mutex);
      free(task);
      resolver_release_slot();
      return -1;
    }
  result = pthread_condattr_setclock(&condition_attributes, CLOCK_MONOTONIC);
  if (result == 0)
    result = pthread_cond_init(&task->condition, &condition_attributes);
  (void)pthread_condattr_destroy(&condition_attributes);
  if (result != 0)
    {
      (void)pthread_mutex_destroy(&task->mutex);
      free(task);
      resolver_release_slot();
      return -1;
    }
  (void)snprintf(task->host, sizeof(task->host), "%s", host);
  (void)snprintf(task->port, sizeof(task->port), "%s", port);
  if (pthread_attr_init(&thread_attributes) != 0)
    {
      resolver_task_dispose(task, false);
      return -1;
    }
  if (pthread_attr_setdetachstate(&thread_attributes,
                                  PTHREAD_CREATE_DETACHED) != 0)
    {
      (void)pthread_attr_destroy(&thread_attributes);
      resolver_task_dispose(task, false);
      return -1;
    }
  if (pthread_mutex_lock(&task->mutex) != 0)
    {
      (void)pthread_attr_destroy(&thread_attributes);
      resolver_task_dispose(task, false);
      return -1;
    }
  result = pthread_create(&thread, &thread_attributes, resolver_worker, task);
  (void)pthread_attr_destroy(&thread_attributes);
  if (result != 0)
    {
      (void)pthread_mutex_unlock(&task->mutex);
      resolver_task_dispose(task, false);
      return -1;
    }
  while (!task->done)
    {
      result = pthread_cond_timedwait(&task->condition, &task->mutex,
                                      &tls->deadline);
      if (result == EINTR) continue;
      if (result != 0 && !task->done)
        {
          task->abandoned = true;
          (void)pthread_mutex_unlock(&task->mutex);
          return -1;
        }
    }
  result = task->result;
  *addresses = task->addresses;
  task->addresses = NULL;
  (void)pthread_mutex_unlock(&task->mutex);
  resolver_task_dispose(task, false);
  if (result != 0 || *addresses == NULL)
    {
      if (*addresses != NULL) freeaddrinfo(*addresses);
      *addresses = NULL;
      return -1;
    }
  return 0;
}

static bool deadline_start(lc_strict_tls_t *tls)
{
  if (tls == NULL || clock_gettime(CLOCK_MONOTONIC, &tls->deadline) != 0)
    return false;
  tls->deadline.tv_sec += LC_STRICT_HTTPS_TIMEOUT_SECONDS;
  tls->deadline_ready = true;
  return true;
}

static bool deadline_remaining(const lc_strict_tls_t *tls,
                               struct timeval *remaining)
{
  struct timespec now;
  time_t seconds;
  long nanoseconds;

  if (tls == NULL || remaining == NULL || !tls->deadline_ready ||
      clock_gettime(CLOCK_MONOTONIC, &now) != 0) return false;
  seconds = tls->deadline.tv_sec - now.tv_sec;
  nanoseconds = tls->deadline.tv_nsec - now.tv_nsec;
  if (nanoseconds < 0)
    {
      seconds--;
      nanoseconds += 1000000000L;
    }
  if (seconds < 0 || (seconds == 0 && nanoseconds == 0)) return false;
  remaining->tv_sec = seconds;
  remaining->tv_usec = (suseconds_t)((nanoseconds + 999L) / 1000L);
  if (remaining->tv_usec >= 1000000)
    {
      remaining->tv_sec++;
      remaining->tv_usec -= 1000000;
    }
  return true;
}

static int deadline_remaining_milliseconds(const lc_strict_tls_t *tls)
{
  struct timeval remaining;
  long long milliseconds;
  if (!deadline_remaining(tls, &remaining)) return 0;
  milliseconds = (long long)remaining.tv_sec * 1000LL +
                 ((long long)remaining.tv_usec + 999LL) / 1000LL;
  return milliseconds > INT_MAX ? INT_MAX : (int)milliseconds;
}

static int strict_tls_ready(lc_strict_tls_t *tls, bool writable)
{
  for (;;)
    {
      fd_set read_set;
      fd_set write_set;
      struct timeval remaining;
      int selected;

      if (tls == NULL || tls->network.fd < 0 ||
          !deadline_remaining(tls, &remaining)) return -1;
      FD_ZERO(&read_set);
      FD_ZERO(&write_set);
      if (writable) FD_SET(tls->network.fd, &write_set);
      else FD_SET(tls->network.fd, &read_set);
      selected = select(tls->network.fd + 1,
                        writable ? NULL : &read_set,
                        writable ? &write_set : NULL, NULL, &remaining);
      if (selected < 0 && errno == EINTR) continue;
      if (selected <= 0) return -1;
      return FD_ISSET(tls->network.fd,
                      writable ? &write_set : &read_set) ? 0 : -1;
    }
}

static int strict_tls_bio_send(void *context, const unsigned char *bytes,
                               size_t length)
{
  lc_strict_tls_t *tls = context;
  ssize_t amount;
  if (tls == NULL || bytes == NULL || length == 0u ||
      strict_tls_ready(tls, true) != 0) return MBEDTLS_ERR_NET_SEND_FAILED;
#ifdef MSG_NOSIGNAL
  amount = send(tls->network.fd, bytes, length, MSG_NOSIGNAL);
#else
  amount = send(tls->network.fd, bytes, length, 0);
#endif
  if (amount > 0) return (int)amount;
  if (amount < 0 && errno == EINTR) return MBEDTLS_ERR_SSL_WANT_WRITE;
  if (amount < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
    return MBEDTLS_ERR_SSL_WANT_WRITE;
  return MBEDTLS_ERR_NET_SEND_FAILED;
}

static int strict_tls_bio_receive(void *context, unsigned char *bytes,
                                  size_t capacity)
{
  lc_strict_tls_t *tls = context;
  ssize_t amount;
  if (tls == NULL || bytes == NULL || capacity == 0u ||
      strict_tls_ready(tls, false) != 0) return MBEDTLS_ERR_NET_RECV_FAILED;
  amount = recv(tls->network.fd, bytes, capacity, 0);
  if (amount >= 0) return (int)amount;
  if (errno == EINTR) return MBEDTLS_ERR_SSL_WANT_READ;
  if (errno == EAGAIN || errno == EWOULDBLOCK)
    return MBEDTLS_ERR_SSL_WANT_READ;
  return MBEDTLS_ERR_NET_RECV_FAILED;
}

static int strict_tls_connect(lc_strict_tls_t *tls, const char *host,
                              const char *port)
{
  struct addrinfo *addresses = NULL;
  struct addrinfo *candidate;

  if (resolve_with_deadline(tls, host, port, &addresses) != 0) return -1;
  if (deadline_remaining_milliseconds(tls) <= 0)
    {
      freeaddrinfo(addresses);
      return -1;
    }

  for (candidate = addresses; candidate != NULL;
       candidate = candidate->ai_next)
    {
      socklen_t error_length;
      int socket_error = 0;
      int flags;
      int connected;

      tls->network.fd = socket(candidate->ai_family, candidate->ai_socktype,
                               candidate->ai_protocol);
      if (tls->network.fd < 0) continue;
      flags = fcntl(tls->network.fd, F_GETFL, 0);
      if (flags < 0 ||
          fcntl(tls->network.fd, F_SETFL, flags | O_NONBLOCK) != 0)
        {
          mbedtls_net_free(&tls->network);
          mbedtls_net_init(&tls->network);
          continue;
        }
      connected = connect(tls->network.fd, candidate->ai_addr,
                          candidate->ai_addrlen);
      if (connected == 0) break;
      if (errno != EINPROGRESS && errno != EAGAIN && errno != EWOULDBLOCK)
        {
          mbedtls_net_free(&tls->network);
          mbedtls_net_init(&tls->network);
          continue;
        }
      if (strict_tls_ready(tls, true) == 0)
        {
          error_length = sizeof(socket_error);
          if (getsockopt(tls->network.fd, SOL_SOCKET, SO_ERROR,
                         &socket_error, &error_length) == 0 &&
              socket_error == 0) break;
        }
      mbedtls_net_free(&tls->network);
      mbedtls_net_init(&tls->network);
    }
  freeaddrinfo(addresses);
  return candidate != NULL && tls->network.fd >= 0 ? 0 : -1;
}

static int secure_entropy(void *context, unsigned char *output, size_t length)
{
  (void)context;
  return agent_secure_random(output, length) == 0 ? 0 : -1;
}

static void strict_tls_cleanup(lc_strict_tls_t *tls)
{
  if (tls == NULL || !tls->initialized) return;
  /* Connection: close is already required by the framing layer. Do not issue
   * a potentially blocking TLS close-notify after the request deadline. */
  mbedtls_net_free(&tls->network);
  mbedtls_ssl_free(&tls->ssl);
  mbedtls_ssl_config_free(&tls->configuration);
  mbedtls_ctr_drbg_free(&tls->random);
  mbedtls_x509_crt_free(&tls->trust);
#ifdef CONFIG_AI_AGENT_NET_RPMSG
  if (tls->resource_acquired) network_release_resource();
#endif
  tls->initialized = false;
  tls->resource_acquired = false;
  tls->deadline_ready = false;
}

static int fail_open(lc_strict_tls_t *tls)
{
  strict_tls_cleanup(tls);
  return -1;
}

static intptr_t strict_tls_open(void *context, const char *host,
                                unsigned int numeric_port)
{
  lc_strict_tls_t *tls = context;
  const char personalization[] = "living-canvas-strict-http";
  const char *port;
  char port_buffer[6];
  int result;

  if (tls == NULL || tls->ca_pem == NULL || tls->ca_pem_length <= 1u ||
      tls->ca_pem[tls->ca_pem_length - 1u] != '\0') return -1;
  memset(&tls->ssl, 0, sizeof(tls->ssl));
  mbedtls_ssl_init(&tls->ssl);
  mbedtls_ssl_config_init(&tls->configuration);
  mbedtls_net_init(&tls->network);
  mbedtls_ctr_drbg_init(&tls->random);
  mbedtls_x509_crt_init(&tls->trust);
  tls->initialized = true;
  if (!deadline_start(tls)) return fail_open(tls);

  if (numeric_port == 0u || numeric_port > 65535u ||
      snprintf(port_buffer, sizeof(port_buffer), "%u", numeric_port) <= 0)
    return fail_open(tls);
  port = port_buffer;
  if (mbedtls_ctr_drbg_seed(&tls->random, secure_entropy, NULL,
      (const unsigned char *)personalization,
      sizeof(personalization) - 1u) != 0) return fail_open(tls);

#ifdef CONFIG_AI_AGENT_NET_RPMSG
  result = deadline_remaining_milliseconds(tls);
  if (result <= 0 || network_acquire_resource(result) != 0)
    return fail_open(tls);
  tls->resource_acquired = true;
#endif

  /* The SDK proxy tunnel applies its timeout independently to each response
   * line, so it cannot honor this adapter's one end-to-end deadline. Fail
   * closed instead of silently weakening the formal product transport. */
  if (http_proxy_is_enabled())
    return fail_open(tls);
  if (strict_tls_connect(tls, host, port) != 0) return fail_open(tls);
  if (mbedtls_ssl_config_defaults(&tls->configuration,
      MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
      MBEDTLS_SSL_PRESET_DEFAULT) != 0) return fail_open(tls);
  mbedtls_ssl_conf_min_tls_version(&tls->configuration,
                                   MBEDTLS_SSL_VERSION_TLS1_2);
#if defined(MBEDTLS_SSL_PROTO_TLS1_3)
  mbedtls_ssl_conf_max_tls_version(&tls->configuration,
                                   MBEDTLS_SSL_VERSION_TLS1_3);
#else
  mbedtls_ssl_conf_max_tls_version(&tls->configuration,
                                   MBEDTLS_SSL_VERSION_TLS1_2);
#endif
#if defined(MBEDTLS_SSL_ALPN)
  {
    static const char *protocols[] = {"http/1.1", NULL};
    if (mbedtls_ssl_conf_alpn_protocols(&tls->configuration, protocols) != 0)
      return fail_open(tls);
  }
#endif
  if (mbedtls_x509_crt_parse(&tls->trust,
      (const unsigned char *)tls->ca_pem, tls->ca_pem_length) != 0)
    return fail_open(tls);
  mbedtls_ssl_conf_ca_chain(&tls->configuration, &tls->trust, NULL);
  mbedtls_ssl_conf_authmode(&tls->configuration, MBEDTLS_SSL_VERIFY_REQUIRED);
  mbedtls_ssl_conf_rng(&tls->configuration, mbedtls_ctr_drbg_random,
                       &tls->random);
  if (mbedtls_ssl_setup(&tls->ssl, &tls->configuration) != 0 ||
      mbedtls_ssl_set_hostname(&tls->ssl, host) != 0) return fail_open(tls);
  mbedtls_ssl_set_bio(&tls->ssl, tls,
                      strict_tls_bio_send, strict_tls_bio_receive, NULL);
  do
    {
      if (deadline_remaining_milliseconds(tls) <= 0) return fail_open(tls);
      result = mbedtls_ssl_handshake(&tls->ssl);
    }
  while (result == MBEDTLS_ERR_SSL_WANT_READ ||
         result == MBEDTLS_ERR_SSL_WANT_WRITE);
  if (result != 0 || mbedtls_ssl_get_verify_result(&tls->ssl) != 0)
    return fail_open(tls);
  return 1;
}

static int strict_tls_send(void *context, intptr_t handle,
                           const void *bytes, size_t length)
{
  lc_strict_tls_t *tls = context;
  int result;
  if (tls == NULL || handle != 1 || length == 0u) return -1;
  do
    {
      if (deadline_remaining_milliseconds(tls) <= 0) return -1;
      result = mbedtls_ssl_write(&tls->ssl, bytes, length);
    }
  while (result == MBEDTLS_ERR_SSL_WANT_READ ||
         result == MBEDTLS_ERR_SSL_WANT_WRITE);
  return result > 0 && (size_t)result <= length ? result : -1;
}

static int strict_tls_receive(void *context, intptr_t handle,
                              void *bytes, size_t capacity)
{
  lc_strict_tls_t *tls = context;
  int result;
  if (tls == NULL || handle != 1 || capacity == 0u) return -1;
  do
    {
      if (deadline_remaining_milliseconds(tls) <= 0) return -1;
      result = mbedtls_ssl_read(&tls->ssl, bytes, capacity);
    }
  while (result == MBEDTLS_ERR_SSL_WANT_READ ||
         result == MBEDTLS_ERR_SSL_WANT_WRITE);
  if (result == 0 || result == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) return 0;
  return result > 0 && (size_t)result <= capacity ? result : -1;
}

static void strict_tls_close(void *context, intptr_t handle)
{
  (void)handle;
  strict_tls_cleanup(context);
}

int lc_strict_https_request(
  const char *host, const char *port, const char *method, const char *path,
  const lc_private_http_header_t *headers,
  const void *body, size_t body_length,
  const char *ca_pem, size_t ca_pem_length,
  void *response, size_t response_capacity, size_t *response_length)
{
  static const lc_private_http_ops_t operations = {
    strict_tls_open, strict_tls_send, strict_tls_receive, strict_tls_close
  };
  lc_strict_tls_t context;
  memset(&context, 0, sizeof(context));
  context.ca_pem = ca_pem;
  context.ca_pem_length = ca_pem_length;
  return lc_http_framed_request_with_ops(
    &operations, &context, false, host, port, method, path, headers,
    body, body_length, response, response_capacity, response_length);
}

#else

int lc_strict_https_request(
  const char *host, const char *port, const char *method, const char *path,
  const lc_private_http_header_t *headers,
  const void *body, size_t body_length,
  const char *ca_pem, size_t ca_pem_length,
  void *response, size_t response_capacity, size_t *response_length)
{
  (void)host; (void)port; (void)method; (void)path; (void)headers;
  (void)body; (void)body_length; (void)ca_pem; (void)ca_pem_length;
  (void)response; (void)response_capacity;
  if (response_length != NULL) *response_length = 0u;
  return LC_PRIVATE_HTTP_CONNECT;
}

#endif
