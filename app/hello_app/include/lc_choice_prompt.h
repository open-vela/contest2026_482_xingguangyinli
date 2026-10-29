#ifndef LIVING_CANVAS_LC_CHOICE_PROMPT_H
#define LIVING_CANVAS_LC_CHOICE_PROMPT_H

#include "lc_choice.h"

#include <stdbool.h>
#include <stddef.h>

#define LC_CHOICE_PROMPT_MAX 256u

bool lc_choice_build_prompt(lc_choice_id_t choice,
                            char *prompt,
                            size_t prompt_capacity);

#endif
