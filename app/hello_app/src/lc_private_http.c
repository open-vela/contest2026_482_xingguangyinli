#define _POSIX_C_SOURCE 200809L
#include "lc_private_http.h"
#include "lc_http_framing.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define LC_PRIVATE_HTTP_HEADER_MAX 4096u
#define LC_PRIVATE_HTTP_REQUEST_HEADER_MAX 2048u
#define LC_PRIVATE_HTTP_BODY_MAX 1048576u
#define LC_PRIVATE_HTTP_HEADER_COUNT_MAX 8u
#ifndef LC_PRIVATE_HTTP_TOTAL_TIMEOUT_SECONDS
#  define LC_PRIVATE_HTTP_TOTAL_TIMEOUT_SECONDS 30
#endif
#define LC_PRIVATE_HTTP_CONNECT_TIMEOUT_SECONDS 5

typedef struct
{
  struct timespec deadline;
  bool deadline_ready;
} socket_context_t;

static bool ascii_equal_ci(const char *left, size_t left_length,
                           const char *right)
{
  size_t right_length = strlen(right);
  size_t index;
  if (left_length != right_length) return false;
  for (index = 0u; index < left_length; index++)
    if (tolower((unsigned char)left[index]) !=
        tolower((unsigned char)right[index])) return false;
  return true;
}

static bool parse_ipv4(const char *host, unsigned int octets[4])
{
  const char *cursor = host;
  unsigned int index;

  if (host == NULL || *host == '\0') return false;
  for (index = 0u; index < 4u; index++)
    {
      unsigned int value = 0u;
      unsigned int digits = 0u;
      if (*cursor < '0' || *cursor > '9') return false;
      if (*cursor == '0' && cursor[1] >= '0' && cursor[1] <= '9') return false;
      while (*cursor >= '0' && *cursor <= '9')
        {
          value = value * 10u + (unsigned int)(*cursor++ - '0');
          if (++digits > 3u || value > 255u) return false;
        }
      octets[index] = value;
      if (index < 3u)
        {
          if (*cursor++ != '.') return false;
        }
      else if (*cursor != '\0') return false;
    }
  return true;
}

bool lc_private_http_host_allowed(const char *host)
{
  unsigned int octets[4];
  if (!parse_ipv4(host, octets)) return false;
  return octets[0] == 10u ||
         (octets[0] == 172u && octets[1] >= 16u && octets[1] <= 31u) ||
         (octets[0] == 192u && octets[1] == 168u);
}

static bool valid_http_host(const char *host)
{
  size_t index;
  if (host == NULL || host[0] == '\0') return false;
  for (index = 0u; host[index] != '\0'; index++)
    if (index >= 95u || !(isalnum((unsigned char)host[index]) ||
                          host[index] == '.' || host[index] == '-')) return false;
  return index > 0u && host[0] != '.' && host[index - 1u] != '.';
}

static bool parse_port(const char *text, unsigned int minimum,
                       unsigned int *port)
{
  unsigned int value = 0u;
  size_t length = 0u;
  if (text == NULL || port == NULL || *text == '\0') return false;
  while (text[length] != '\0')
    {
      if (text[length] < '0' || text[length] > '9' || length >= 5u)
        return false;
      value = value * 10u + (unsigned int)(text[length++] - '0');
    }
  if (value < minimum || value > 65535u) return false;
  *port = value;
  return true;
}

static bool valid_path(const char *path)
{
  size_t index;
  if (path == NULL || path[0] != '/') return false;
  for (index = 0u; path[index] != '\0'; index++)
    if (index >= 511u || (unsigned char)path[index] <= 0x20u ||
        (unsigned char)path[index] >= 0x7fu || path[index] == '#') return false;
  return index > 0u;
}

static bool valid_header_name(const char *name, size_t *length)
{
  size_t index;
  if (name == NULL || *name == '\0') return false;
  for (index = 0u; name[index] != '\0'; index++)
    if (index >= 63u || !(isalnum((unsigned char)name[index]) ||
                          name[index] == '-')) return false;
  if (ascii_equal_ci(name, index, "Host") ||
      ascii_equal_ci(name, index, "Content-Length") ||
      ascii_equal_ci(name, index, "Connection") ||
      ascii_equal_ci(name, index, "Transfer-Encoding") ||
      ascii_equal_ci(name, index, "Trailer") ||
      ascii_equal_ci(name, index, "Expect")) return false;
  *length = index;
  return true;
}

static void wipe_sensitive(void *buffer, size_t length)
{
  volatile unsigned char *cursor = buffer;
  while (length-- != 0u) *cursor++ = 0u;
}

static bool valid_header_value(const char *value)
{
  size_t index;
  if (value == NULL) return false;
  for (index = 0u; value[index] != '\0'; index++)
    if (index >= 255u || (unsigned char)value[index] < 0x20u ||
        (unsigned char)value[index] >= 0x7fu) return false;
  return true;
}

static bool append_text(char *buffer, size_t capacity, size_t *used,
                        const char *format, const char *first,
                        const char *second)
{
  int written = snprintf(buffer + *used, capacity - *used,
                         format, first, second);
  if (written < 0 || (size_t)written >= capacity - *used) return false;
  *used += (size_t)written;
  return true;
}

static bool append_length(char *buffer, size_t capacity, size_t *used,
                          size_t length)
{
  int written = snprintf(buffer + *used, capacity - *used,
                         "Content-Length: %zu\r\n", length);
  if (written < 0 || (size_t)written >= capacity - *used) return false;
  *used += (size_t)written;
  return true;
}

static size_t delimiter(const unsigned char *bytes, size_t length,
                        const char *needle, size_t needle_length)
{
  size_t index;
  if (needle_length == 0u || length < needle_length) return SIZE_MAX;
  for (index = 0u; index <= length - needle_length; index++)
    if (memcmp(bytes + index, needle, needle_length) == 0) return index;
  return SIZE_MAX;
}

static int send_all(const lc_private_http_ops_t *ops, void *context,
                    intptr_t handle, const void *bytes, size_t length)
{
  const unsigned char *cursor = bytes;
  size_t sent = 0u;
  while (sent < length)
    {
      int amount = ops->send(context, handle, cursor + sent, length - sent);
      if (amount <= 0 || (size_t)amount > length - sent)
        return LC_PRIVATE_HTTP_WRITE;
      sent += (size_t)amount;
    }
  return 0;
}

static bool parse_content_length(const unsigned char *value, size_t length,
                                 size_t *result)
{
  size_t parsed = 0u;
  size_t index;
  if (length == 0u) return false;
  for (index = 0u; index < length; index++)
    {
      unsigned int digit;
      if (value[index] < '0' || value[index] > '9') return false;
      digit = (unsigned int)(value[index] - '0');
      if (parsed > (LC_PRIVATE_HTTP_BODY_MAX - digit) / 10u) return false;
      parsed = parsed * 10u + digit;
    }
  *result = parsed;
  return true;
}

static bool valid_response_field_name(const unsigned char *name,
                                      size_t length)
{
  static const char punctuation[] = "!#$%&'*+-.^_`|~";
  size_t index;
  if (name == NULL || length == 0u) return false;
  for (index = 0u; index < length; index++)
    if (!isalnum(name[index]) &&
        strchr(punctuation, (int)name[index]) == NULL) return false;
  return true;
}

static int parse_response_header(const unsigned char *header,
                                 size_t header_length,
                                 int *status, size_t *content_length)
{
  size_t first_end = delimiter(header, header_length, "\r\n", 2u);
  size_t cursor;
  bool found_length = false;

  if (first_end == SIZE_MAX || first_end < 12u ||
      !(memcmp(header, "HTTP/1.1 ", 9u) == 0 ||
        memcmp(header, "HTTP/1.0 ", 9u) == 0) ||
      header[9] < '1' || header[9] > '5' ||
      !isdigit(header[10]) || !isdigit(header[11]) ||
      (first_end > 12u && header[12] != ' ')) return LC_PRIVATE_HTTP_PROTOCOL;
  *status = (header[9] - '0') * 100 + (header[10] - '0') * 10 +
            (header[11] - '0');

  cursor = first_end + 2u;
  while (cursor < header_length)
    {
      size_t end = delimiter(header + cursor, header_length - cursor,
                             "\r\n", 2u);
      size_t colon;
      size_t value_start;
      size_t value_end;
      if (end == SIZE_MAX) return LC_PRIVATE_HTTP_PROTOCOL;
      if (end == 0u) break;
      colon = delimiter(header + cursor, end, ":", 1u);
      if (colon == SIZE_MAX ||
          !valid_response_field_name(header + cursor, colon))
        return LC_PRIVATE_HTTP_PROTOCOL;
      value_start = cursor + colon + 1u;
      value_end = cursor + end;
      while (value_start < value_end &&
             (header[value_start] == ' ' || header[value_start] == '\t'))
        value_start++;
      while (value_end > value_start &&
             (header[value_end - 1u] == ' ' || header[value_end - 1u] == '\t'))
        value_end--;
      if (ascii_equal_ci((const char *)header + cursor, colon,
                         "Content-Length"))
        {
          if (found_length || !parse_content_length(header + value_start,
              value_end - value_start, content_length))
            return LC_PRIVATE_HTTP_PROTOCOL;
          found_length = true;
        }
      else if (ascii_equal_ci((const char *)header + cursor, colon,
                              "Transfer-Encoding"))
        return LC_PRIVATE_HTTP_PROTOCOL;
      cursor += end + 2u;
    }
  return found_length ? 0 : LC_PRIVATE_HTTP_PROTOCOL;
}

int lc_http_framed_request_with_ops(
  const lc_private_http_ops_t *ops, void *context,
  bool private_only,
  const char *host, const char *port, const char *method, const char *path,
  const lc_private_http_header_t *headers,
  const void *body, size_t body_length,
  void *response, size_t response_capacity, size_t *response_length)
{
  char request_header[LC_PRIVATE_HTTP_REQUEST_HEADER_MAX];
  unsigned char *response_header = NULL;
  unsigned char *response_bytes = response;
  size_t request_length = 0u;
  size_t header_used = 0u;
  size_t header_end = SIZE_MAX;
  size_t content_length = 0u;
  size_t copied = 0u;
  unsigned int numeric_port;
  unsigned int header_count = 0u;
  intptr_t handle;
  int status = 0;
  int result = LC_PRIVATE_HTTP_INVALID;

  if (response_length != NULL) *response_length = 0u;
  if (response != NULL && response_capacity != 0u) response_bytes[0] = 0u;
  if (ops == NULL || ops->open == NULL || ops->send == NULL ||
      ops->receive == NULL || ops->close == NULL ||
      !valid_http_host(host) ||
      (private_only && !lc_private_http_host_allowed(host)) ||
      !parse_port(port, private_only ? 1024u : 1u, &numeric_port) ||
      method == NULL ||
      !(strcmp(method, "POST") == 0 ||
        (!private_only && strcmp(method, "GET") == 0 &&
         body == NULL && body_length == 0u)) || !valid_path(path) ||
      headers == NULL || (body == NULL && body_length != 0u) ||
      body_length > LC_PRIVATE_HTTP_BODY_MAX || response == NULL ||
      response_capacity < 2u || response_length == NULL) return result;

  if (!append_text(request_header, sizeof(request_header), &request_length,
                   "%s %s HTTP/1.1\r\n", method, path) ||
      !append_text(request_header, sizeof(request_header), &request_length,
                   "Host: %s:%s\r\n", host, port) ||
      !append_text(request_header, sizeof(request_header), &request_length,
                   "%s", "Connection: close\r\n", "") ||
      !append_text(request_header, sizeof(request_header), &request_length,
                   "%s", "User-Agent: living-canvas/1\r\n", "") ||
      !append_length(request_header, sizeof(request_header), &request_length,
                     body_length))
    {
      wipe_sensitive(request_header, sizeof(request_header));
      return result;
    }

  for (;;)
    {
      size_t name_length;
      if (header_count >= LC_PRIVATE_HTTP_HEADER_COUNT_MAX)
        {
          wipe_sensitive(request_header, sizeof(request_header));
          return result;
        }
      if (headers[header_count].name == NULL) break;
      if (headers[header_count].value == NULL ||
          !valid_header_name(headers[header_count].name, &name_length) ||
          !valid_header_value(headers[header_count].value) ||
          !append_text(request_header, sizeof(request_header), &request_length,
                       "%s: %s\r\n", headers[header_count].name,
                       headers[header_count].value))
        {
          wipe_sensitive(request_header, sizeof(request_header));
          return result;
        }
      header_count++;
    }
  if (!append_text(request_header, sizeof(request_header), &request_length,
                   "%s", "\r\n", ""))
    {
      wipe_sensitive(request_header, sizeof(request_header));
      return result;
    }

  response_header = malloc(LC_PRIVATE_HTTP_HEADER_MAX);
  if (response_header == NULL)
    {
      wipe_sensitive(request_header, sizeof(request_header));
      return LC_PRIVATE_HTTP_READ;
    }
  handle = ops->open(context, host, numeric_port);
  if (handle < 0)
    {
      free(response_header);
      wipe_sensitive(request_header, sizeof(request_header));
      return LC_PRIVATE_HTTP_CONNECT;
    }
  result = send_all(ops, context, handle, request_header, request_length);
  if (result == 0 && body_length != 0u)
    result = send_all(ops, context, handle, body, body_length);
  if (result != 0) goto done;

  while (header_end == SIZE_MAX && header_used < LC_PRIVATE_HTTP_HEADER_MAX)
    {
      int amount = ops->receive(context, handle,
        response_header + header_used,
        LC_PRIVATE_HTTP_HEADER_MAX - header_used);
      if (amount <= 0 ||
          (size_t)amount > LC_PRIVATE_HTTP_HEADER_MAX - header_used)
        {
          result = LC_PRIVATE_HTTP_READ;
          goto done;
        }
      header_used += (size_t)amount;
      header_end = delimiter(response_header, header_used, "\r\n\r\n", 4u);
    }
  if (header_end == SIZE_MAX)
    {
      result = LC_PRIVATE_HTTP_PROTOCOL;
      goto done;
    }
  header_end += 4u;
  result = parse_response_header(response_header, header_end, &status,
                                 &content_length);
  if (result != 0) goto done;
  if (content_length >= response_capacity)
    {
      result = LC_PRIVATE_HTTP_OVERFLOW;
      goto done;
    }
  if (header_used - header_end > content_length)
    {
      result = LC_PRIVATE_HTTP_PROTOCOL;
      goto done;
    }
  copied = header_used - header_end;
  memcpy(response_bytes, response_header + header_end, copied);
  while (copied < content_length)
    {
      int amount = ops->receive(context, handle, response_bytes + copied,
                                content_length - copied);
      if (amount <= 0 || (size_t)amount > content_length - copied)
        {
          result = LC_PRIVATE_HTTP_READ;
          goto done;
        }
      copied += (size_t)amount;
    }
  {
    unsigned char extra;
    int amount = ops->receive(context, handle, &extra, 1u);
    if (amount > 0)
      {
        result = LC_PRIVATE_HTTP_PROTOCOL;
        goto done;
      }
    if (amount < 0)
      {
        result = LC_PRIVATE_HTTP_READ;
        goto done;
      }
  }
  response_bytes[copied] = 0u;
  *response_length = copied;
  result = status;

done:
  ops->close(context, handle);
  free(response_header);
  wipe_sensitive(request_header, sizeof(request_header));
  if (result < 100)
    {
      response_bytes[0] = 0u;
      *response_length = 0u;
    }
  return result;
}

int lc_private_http_request_with_ops(
  const lc_private_http_ops_t *ops, void *context,
  const char *host, const char *port, const char *method, const char *path,
  const lc_private_http_header_t *headers,
  const void *body, size_t body_length,
  void *response, size_t response_capacity, size_t *response_length)
{
  return lc_http_framed_request_with_ops(
    ops, context, true, host, port, method, path, headers, body, body_length,
    response, response_capacity, response_length);
}

static bool deadline_start(socket_context_t *context)
{
  if (context == NULL ||
      clock_gettime(CLOCK_MONOTONIC, &context->deadline) != 0) return false;
  context->deadline.tv_sec += LC_PRIVATE_HTTP_TOTAL_TIMEOUT_SECONDS;
  context->deadline_ready = true;
  return true;
}

static bool deadline_remaining(const socket_context_t *context,
                               struct timeval *remaining)
{
  struct timespec now;
  time_t seconds;
  long nanoseconds;

  if (context == NULL || remaining == NULL || !context->deadline_ready ||
      clock_gettime(CLOCK_MONOTONIC, &now) != 0) return false;
  seconds = context->deadline.tv_sec - now.tv_sec;
  nanoseconds = context->deadline.tv_nsec - now.tv_nsec;
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

static int socket_ready(socket_context_t *context, int fd, bool writable)
{
  for (;;)
    {
      fd_set read_set;
      fd_set write_set;
      struct timeval remaining;
      int selected;

      if (!deadline_remaining(context, &remaining)) return -1;
      FD_ZERO(&read_set);
      FD_ZERO(&write_set);
      if (writable) FD_SET(fd, &write_set);
      else FD_SET(fd, &read_set);
      selected = select(fd + 1, writable ? NULL : &read_set,
                        writable ? &write_set : NULL, NULL, &remaining);
      if (selected < 0 && errno == EINTR) continue;
      if (selected <= 0) return -1;
      return FD_ISSET(fd, writable ? &write_set : &read_set) ? 0 : -1;
    }
}

static intptr_t socket_open(void *raw_context, const char *host,
                            unsigned int port)
{
  socket_context_t *context = raw_context;
  struct sockaddr_in address;
  struct timeval connect_timeout = {LC_PRIVATE_HTTP_CONNECT_TIMEOUT_SECONDS, 0};
  fd_set writable;
  socklen_t error_length;
  int socket_error = 0;
  int flags;
  int connected;
  int fd;
  if (!deadline_start(context)) return -1;
  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_port = htons((uint16_t)port);
  if (inet_pton(AF_INET, host, &address.sin_addr) != 1) return -1;
  fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;
  flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0)
    {
      close(fd);
      return -1;
    }
  connected = connect(fd, (const struct sockaddr *)&address, sizeof(address));
  if (connected != 0)
    {
      if (errno != EINPROGRESS && errno != EAGAIN)
        {
          close(fd);
          return -1;
        }
      FD_ZERO(&writable);
      FD_SET(fd, &writable);
      connected = select(fd + 1, NULL, &writable, NULL, &connect_timeout);
      error_length = sizeof(socket_error);
      if (connected <= 0 || !FD_ISSET(fd, &writable) ||
          getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error,
                     &error_length) != 0 || socket_error != 0)
        {
          close(fd);
          return -1;
        }
    }
  /* Keep the socket nonblocking. Every subsequent send/receive waits against
   * one CLOCK_MONOTONIC request deadline, so a slow trickle cannot reset a
   * per-syscall timeout indefinitely. */
  return (intptr_t)fd;
}

static int socket_send(void *raw_context, intptr_t handle,
                       const void *bytes, size_t length)
{
  socket_context_t *context = raw_context;
  ssize_t amount;
  if (socket_ready(context, (int)handle, true) != 0) return -1;
#ifdef MSG_NOSIGNAL
  amount = send((int)handle, bytes, length, MSG_NOSIGNAL);
#else
  amount = send((int)handle, bytes, length, 0);
#endif
  return amount > 0 && (size_t)amount <= length ? (int)amount : -1;
}

static int socket_receive(void *raw_context, intptr_t handle,
                          void *bytes, size_t capacity)
{
  socket_context_t *context = raw_context;
  ssize_t amount;
  if (socket_ready(context, (int)handle, false) != 0) return -1;
  amount = recv((int)handle, bytes, capacity, 0);
  return amount >= 0 && (size_t)amount <= capacity ? (int)amount : -1;
}

static void socket_close(void *context, intptr_t handle)
{
  (void)context;
  (void)close((int)handle);
}

int lc_private_http_request(
  const char *host, const char *port, const char *method, const char *path,
  const lc_private_http_header_t *headers,
  const void *body, size_t body_length,
  void *response, size_t response_capacity, size_t *response_length)
{
  static const lc_private_http_ops_t ops = {
    socket_open, socket_send, socket_receive, socket_close
  };
  socket_context_t context;
  memset(&context, 0, sizeof(context));
  return lc_private_http_request_with_ops(&ops, &context, host, port, method, path,
    headers, body, body_length, response, response_capacity, response_length);
}
