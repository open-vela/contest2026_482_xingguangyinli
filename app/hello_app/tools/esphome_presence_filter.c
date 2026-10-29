#include "lc_esphome_presence.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct
{
  size_t reports;
  bool output_failed;
} output_t;

static void emit(void *context, bool detected)
{
  output_t *output = context;

  if (fputs(detected ? "{\"detected\":true}\n" :
                      "{\"detected\":false}\n", stdout) == EOF ||
      fflush(stdout) != 0)
    {
      output->output_failed = true;
      return;
    }
  ++output->reports;
}

int main(int argc, char **argv)
{
  lc_esphome_presence_t parser;
  output_t output = {0};
  uint8_t chunk[128];

  (void)argv;
  if (argc != 1)
    {
      fputs("stdin-only presence filter takes no arguments\n", stderr);
      return 64;
    }

  lc_esphome_presence_init(&parser);
  while (!output.output_failed)
    {
      size_t length = fread(chunk, 1u, sizeof(chunk), stdin);

      if (length != 0u)
        {
          (void)lc_esphome_presence_feed(&parser, chunk, length, emit,
                                         &output);
        }
      if (length != sizeof(chunk))
        {
          if (ferror(stdin))
            {
              fputs("presence input unavailable\n", stderr);
              return 1;
            }
          break;
        }
    }

  lc_esphome_presence_init(&parser);
  if (output.output_failed)
    {
      fputs("presence output unavailable\n", stderr);
      return 1;
    }
  if (output.reports == 0u)
    {
      fputs("no validated presence report\n", stderr);
      return 2;
    }
  return 0;
}
