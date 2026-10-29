/* Read MR60BHA2 raw UART packets from explicit stdin; emit boolean-only JSON.
 * No device open, UART settings, commands, radar/XIAO firmware write or network.
 * Factory ESPHome USB logs are NOT raw radar packets: do not assume passthrough.
 */
#include "lc_mr60_presence.h"
#include <stdio.h>

typedef struct
{
  bool observed;
  bool output_failed;
} output_t;

static void emit(void *context, bool detected)
{
  output_t *result = context;
  result->observed = true;
  if (fputs(detected ? "{\"detected\":true}\n" : "{\"detected\":false}\n", stdout) == EOF ||
      fflush(stdout) != 0) result->output_failed = true;
}

int main(int argc, char **argv)
{
  (void)argv;
  if (argc != 1)
    {
      fputs("reads explicit raw packet stdin only; no device arguments\n", stderr);
      return 64;
    }
  if (setvbuf(stdin, NULL, _IONBF, 0) != 0) return 1;
  lc_mr60_presence_t parser;
  lc_mr60_presence_init(&parser);
  output_t result = {false, false};
  uint8_t chunk[64];
  size_t length;
  while ((length = fread(chunk, 1u, sizeof(chunk), stdin)) != 0u)
    {
      (void)lc_mr60_presence_feed(&parser, chunk, length, emit, &result);
      volatile uint8_t *wipe = chunk;
      for (size_t i = 0u; i < sizeof(chunk); ++i) wipe[i] = 0u;
      if (result.output_failed) return 1;
    }
  lc_mr60_presence_init(&parser);
  if (ferror(stdin))
    { fputs("packet input unavailable\n", stderr); return 1; }
  if (!result.observed)
    { fputs("no validated presence report\n", stderr); return 2; }
  return 0;
}
