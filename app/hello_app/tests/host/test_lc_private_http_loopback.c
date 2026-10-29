#define _POSIX_C_SOURCE 200809L
#include "lc_private_http.h"

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <ifaddrs.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct
{
  int listener;
  bool request_valid;
} server_t;

static bool find_private_address(char address[INET_ADDRSTRLEN])
{
  struct ifaddrs *interfaces = NULL;
  struct ifaddrs *item;
  if (getifaddrs(&interfaces) != 0) return false;
  for (item = interfaces; item != NULL; item = item->ifa_next)
    if (item->ifa_addr != NULL && item->ifa_addr->sa_family == AF_INET &&
        inet_ntop(AF_INET,
          &((const struct sockaddr_in *)item->ifa_addr)->sin_addr,
          address, INET_ADDRSTRLEN) != NULL &&
        lc_private_http_host_allowed(address))
      {
        freeifaddrs(interfaces);
        return true;
      }
  freeifaddrs(interfaces);
  return false;
}

static bool write_all(int fd, const void *data, size_t length)
{
  const unsigned char *cursor = data;
  while (length != 0u)
    {
      ssize_t amount = send(fd, cursor, length, 0);
      if (amount <= 0) return false;
      cursor += (size_t)amount;
      length -= (size_t)amount;
    }
  return true;
}

static void *serve_once(void *context)
{
  static const char header[] =
    "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nConnection: close\r\n\r\n";
  static const unsigned char body[] = {'R', 0, 'I', 'F', 'F'};
  server_t *server = context;
  unsigned char request[4096];
  size_t received = 0u;
  int client = accept(server->listener, NULL, NULL);
  assert(client >= 0);
  while (received < sizeof(request) - 1u)
    {
      ssize_t amount = recv(client, request + received,
                            sizeof(request) - 1u - received, 0);
      assert(amount > 0);
      received += (size_t)amount;
      request[received] = 0u;
      unsigned char *end = (unsigned char *)strstr((const char *)request,
                                                   "\r\n\r\n");
      if (end != NULL)
        {
          size_t header_length = (size_t)(end + 4u - request);
          if (received >= header_length + 4u)
            {
              server->request_valid =
                strstr((const char *)request,
                  "POST /v1/voice/asr?session_id=meal_0123456789ab HTTP/1.1\r\n") != NULL &&
                strstr((const char *)request,
                  "Authorization: Bearer synthetic-owner-device-token-0000\r\n") != NULL &&
                strstr((const char *)request, "Content-Length: 4\r\n") != NULL &&
                memcmp(request + header_length, "W\0AV", 4u) == 0;
              break;
            }
        }
    }
  assert(write_all(client, header, sizeof(header) - 1u));
  assert(write_all(client, body, sizeof(body)));
  close(client);
  return NULL;
}

int main(void)
{
  static const lc_private_http_header_t headers[] = {
    {"Content-Type", "audio/wav"},
    {"Authorization", "Bearer synthetic-owner-device-token-0000"},
    {NULL, NULL}
  };
  char host[INET_ADDRSTRLEN];
  struct sockaddr_in address;
  socklen_t address_length = sizeof(address);
  char port[6];
  pthread_t thread;
  server_t server = {-1, false};
  unsigned char response[8] = {0xff};
  size_t response_length = 0u;

  if (!find_private_address(host))
    {
      puts("SKIP: no active RFC1918 interface for private HTTP test");
      return 0;
    }
  server.listener = socket(AF_INET, SOCK_STREAM, 0);
  assert(server.listener >= 0);
  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_port = 0;
  assert(inet_pton(AF_INET, host, &address.sin_addr) == 1);
  if (bind(server.listener, (const struct sockaddr *)&address,
           sizeof(address)) != 0 && (errno == EPERM || errno == EACCES))
    {
      close(server.listener);
      puts("SKIP: private TCP bind is blocked by the test sandbox");
      return 0;
    }
  assert(listen(server.listener, 1) == 0);
  assert(getsockname(server.listener, (struct sockaddr *)&address,
                     &address_length) == 0);
  assert(snprintf(port, sizeof(port), "%u", (unsigned int)ntohs(address.sin_port)) > 0);
  assert(pthread_create(&thread, NULL, serve_once, &server) == 0);
  assert(lc_private_http_request(host, port, "POST",
    "/v1/voice/asr?session_id=meal_0123456789ab", headers,
    "W\0AV", 4u, response, sizeof(response), &response_length) == 200);
  assert(pthread_join(thread, NULL) == 0);
  close(server.listener);
  assert(server.request_valid);
  assert(response_length == 5u && memcmp(response, "R\0IFF", 5u) == 0);
  puts("PASS: real private-LAN TCP request with binary body and response");
  return 0;
}
