#include "lc_product_transport.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define TEST_TOKEN "lc-owner-device-token-0123456789abcdef"

static void test_https_backend_configuration(void)
{
  lc_product_transport_t transport;

  assert(lc_product_transport_init(
    &transport, "https://canvas.example.com/api/"));
  assert(transport.configured);
  assert(strcmp(transport.host, "canvas.example.com") == 0);
  assert(strcmp(transport.port, "443") == 0);
  assert(strcmp(transport.base_path, "/api") == 0);
  assert(strcmp(transport.origin, "https://canvas.example.com/api") == 0);
  assert(!transport.private_http);

  assert(lc_product_transport_init(
    &transport, "https://canvas.example.com:8443"));
  assert(strcmp(transport.port, "8443") == 0);
}

static void test_private_lan_http_backend_configuration(void)
{
  lc_product_transport_t transport;

  assert(lc_product_transport_init(
    &transport, "http://192.168.8.20:8765/api/"));
  assert(transport.configured);
  assert(transport.private_http);
  assert(strcmp(transport.host, "192.168.8.20") == 0);
  assert(strcmp(transport.port, "8765") == 0);
  assert(strcmp(transport.base_path, "/api") == 0);
  assert(strcmp(transport.origin, "http://192.168.8.20:8765/api") == 0);

  assert(lc_product_transport_init(
    &transport, "http://172.20.10.1:8765"));
  assert(transport.private_http);
  assert(lc_product_transport_init(
    &transport, "http://10.42.0.1:8765"));
  assert(transport.private_http);
}

static void test_rejects_unsafe_or_ambiguous_backend_urls(void)
{
  lc_product_transport_t transport;

  assert(!lc_product_transport_init(&transport, "http://canvas.example.com"));
  assert(!lc_product_transport_init(&transport, "http://8.8.8.8:8765"));
  assert(!lc_product_transport_init(&transport, "http://127.0.0.1:8765"));
  assert(!lc_product_transport_init(&transport, "http://192.168.8.20"));
  assert(!lc_product_transport_init(&transport, "http://192.168.8.20:80"));
  assert(!lc_product_transport_init(&transport, "http://192.168.008.20:8765"));
  assert(!lc_product_transport_init(&transport, "https://"));
  assert(!lc_product_transport_init(&transport,
                                    "https://user@canvas.example.com"));
  assert(!lc_product_transport_init(&transport,
                                    "https://canvas.example.com/api?q=1"));
  assert(!lc_product_transport_init(&transport,
                                    "https://canvas.example.com/api\r\nInjected:1"));
  assert(!lc_product_transport_init(&transport,
                                    "https://canvas.example.com/api path"));
}

static void test_authenticated_configuration_rejects_secret_shaped_or_unsafe_tokens(void)
{
  lc_product_transport_t transport;

  assert(lc_product_transport_init_authenticated(
    &transport, "https://canvas.example.com/api", TEST_TOKEN));
  assert(transport.configured);
  assert(transport.authorized);
  assert(strcmp(transport.authorization, "Bearer " TEST_TOKEN) == 0);

  assert(!lc_product_transport_init_authenticated(
    &transport, "https://canvas.example.com/api", "short"));
  assert(!lc_product_transport_init_authenticated(
    &transport, "https://canvas.example.com/api",
    "s" "k-test-not-a-real-provider-secret-123456"));
  assert(!lc_product_transport_init_authenticated(
    &transport, "https://canvas.example.com/api",
    "owner-device-token-without-product-prefix"));
  assert(!lc_product_transport_init_authenticated(
    &transport, "https://canvas.example.com/api",
    "owner device token with a space 123456789"));
  assert(!lc_product_transport_init_authenticated(
    &transport, "https://canvas.example.com/api",
    "owner-device-token-with-newline-123456\n"));

  assert(lc_product_transport_init_authenticated(
    &transport, "https://canvas.example.com/api", TEST_TOKEN));
  lc_product_transport_clear(&transport);
  for (size_t index = 0u; index < sizeof(transport); index++)
    {
      assert(((const unsigned char *)&transport)[index] == 0u);
    }
}

static void test_compact_handoff_must_match_exact_session(void)
{
  assert(lc_product_compact_path_matches_session(
    "/c/0123456789ab", "meal_0123456789ab"));
  assert(!lc_product_compact_path_matches_session(
    "/c/ffffffffffff", "meal_0123456789ab"));
  assert(!lc_product_compact_path_matches_session(
    "//evil.example/c/0123456789ab", "meal_0123456789ab"));
  assert(!lc_product_compact_path_matches_session(
    "/c/0123456789ab?x=1", "meal_0123456789ab"));
  assert(!lc_product_compact_path_matches_session(
    "/c/0123456789AB", "meal_0123456789ab"));
  assert(!lc_product_compact_path_matches_session(NULL,
                                                   "meal_0123456789ab"));
}

int main(void)
{
  test_https_backend_configuration();
  test_private_lan_http_backend_configuration();
  test_rejects_unsafe_or_ambiguous_backend_urls();
  test_authenticated_configuration_rejects_secret_shaped_or_unsafe_tokens();
  test_compact_handoff_must_match_exact_session();
  puts("PASS: Living Canvas product transport configuration");
  return 0;
}
