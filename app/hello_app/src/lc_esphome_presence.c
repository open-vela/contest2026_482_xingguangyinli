#include "lc_esphome_presence.h"

#include <string.h>

static bool digit(char value)
{
  return value >= '0' && value <= '9';
}

static char ascii_lower(char value)
{
  if (value >= 'A' && value <= 'Z')
    {
      return (char)(value + ('a' - 'A'));
    }
  return value;
}

static bool skip_spaces(const char **cursor, bool required)
{
  const char *start = *cursor;

  while (**cursor == ' ' || **cursor == '\t')
    {
      ++*cursor;
    }

  return !required || *cursor != start;
}

static bool take_literal(const char **cursor, const char *literal)
{
  const char *value = *cursor;

  while (*literal != '\0')
    {
      if (ascii_lower(*value) != ascii_lower(*literal))
        {
          return false;
        }
      ++value;
      ++literal;
    }

  *cursor = value;
  return true;
}

static bool take_timestamp(const char **cursor)
{
  const char *value = *cursor;
  size_t fraction = 0u;

  if (value[0] != '[' || !digit(value[1]) || !digit(value[2]) ||
      value[3] != ':' || !digit(value[4]) || !digit(value[5]) ||
      value[6] != ':' || !digit(value[7]) || !digit(value[8]))
    {
      return false;
    }

  value += 9;
  if (*value == '.')
    {
      ++value;
      while (digit(*value) && fraction < 6u)
        {
          ++value;
          ++fraction;
        }
      if (fraction == 0u || digit(*value))
        {
          return false;
        }
    }

  if (*value != ']')
    {
      return false;
    }

  *cursor = value + 1;
  return true;
}

static bool strip_presentation(const uint8_t *line, size_t length,
                               char *cleaned, size_t capacity)
{
  size_t input = 0u;
  size_t output = 0u;

  while (input < length)
    {
      if (line[input] == 0x1bu)
        {
          size_t controls = 0u;

          if (++input >= length || line[input++] != '[')
            {
              return false;
            }
          while (input < length && controls <= 16u &&
                 (digit((char)line[input]) || line[input] == ';'))
            {
              ++input;
              ++controls;
            }
          if (controls > 16u || input >= length || line[input++] != 'm')
            {
              return false;
            }
          continue;
        }

      if (line[input] == '\0' || output + 1u >= capacity)
        {
          return false;
        }
      cleaned[output++] = (char)line[input++];
    }

  if (output != 0u && cleaned[output - 1u] == '\r')
    {
      --output;
    }
  cleaned[output] = '\0';
  return true;
}

static bool parse_line(const uint8_t *line, size_t length, bool *detected)
{
  char cleaned[LC_ESPHOME_PRESENCE_LINE_MAX + 1u];
  const char *cursor;
  unsigned int count = 0u;
  size_t digits = 0u;
  size_t decimals = 0u;

  if (detected == NULL ||
      !strip_presentation(line, length, cleaned, sizeof(cleaned)))
    {
      return false;
    }

  cursor = cleaned;
  const char *level = cursor;
  if (!take_literal(&level, "[D]") && !take_timestamp(&cursor))
    {
      return false;
    }
  if (!take_literal(&cursor, "[D][sensor:"))
    {
      return false;
    }
  while (digit(*cursor) && digits < 4u)
    {
      ++cursor;
      ++digits;
    }
  if (digits == 0u || digit(*cursor) || !take_literal(&cursor, "]:"))
    {
      return false;
    }
  if (!skip_spaces(&cursor, false) ||
      !take_literal(&cursor, "'Target number':") ||
      !skip_spaces(&cursor, true) ||
      !take_literal(&cursor, "Sending state") ||
      !skip_spaces(&cursor, true))
    {
      return false;
    }

  digits = 0u;
  while (digit(*cursor) && digits < 3u)
    {
      count = count * 10u + (unsigned int)(*cursor - '0');
      ++cursor;
      ++digits;
    }
  if (digits == 0u || digit(*cursor) || count > 255u)
    {
      return false;
    }
  if (*cursor == '.')
    {
      ++cursor;
      while (*cursor == '0' && decimals < 5u)
        {
          ++cursor;
          ++decimals;
        }
      if (decimals == 0u || digit(*cursor))
        {
          return false;
        }
    }
  if (!skip_spaces(&cursor, true) || !take_literal(&cursor, "with") ||
      !skip_spaces(&cursor, true) || *cursor < '0' || *cursor > '5')
    {
      return false;
    }
  ++cursor;
  if (!skip_spaces(&cursor, true) ||
      !take_literal(&cursor, "decimals of accuracy") || *cursor != '\0')
    {
      return false;
    }

  *detected = count > 0u;
  return true;
}

void lc_esphome_presence_init(lc_esphome_presence_t *parser)
{
  if (parser != NULL)
    {
      memset(parser, 0, sizeof(*parser));
    }
}

size_t lc_esphome_presence_feed(
  lc_esphome_presence_t *parser,
  const uint8_t *bytes,
  size_t length,
  lc_esphome_presence_callback_t callback,
  void *context)
{
  size_t reports = 0u;

  if (parser == NULL || callback == NULL ||
      (bytes == NULL && length != 0u))
    {
      return 0u;
    }

  for (size_t index = 0u; index < length; ++index)
    {
      if (bytes[index] == '\n')
        {
          bool detected;

          if (!parser->oversized &&
              parse_line(parser->line, parser->used, &detected))
            {
              callback(context, detected);
              ++reports;
            }
          memset(parser->line, 0, sizeof(parser->line));
          parser->used = 0u;
          parser->oversized = false;
        }
      else if (!parser->oversized)
        {
          if (parser->used == sizeof(parser->line))
            {
              memset(parser->line, 0, sizeof(parser->line));
              parser->used = 0u;
              parser->oversized = true;
            }
          else
            {
              parser->line[parser->used++] = bytes[index];
            }
        }
    }

  return reports;
}
