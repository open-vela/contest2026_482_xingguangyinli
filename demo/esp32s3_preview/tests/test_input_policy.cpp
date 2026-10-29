#include <cassert>

#include "living_canvas_input_policy.h"

static void configured_checkin_accepts_only_touch() {
  assert(livingCanvasInputAction(true, false, true) ==
         LivingCanvasInputAction::None);
  assert(livingCanvasInputAction(true, true, false) ==
         LivingCanvasInputAction::Touch);
  assert(livingCanvasInputAction(true, true, true) ==
         LivingCanvasInputAction::Touch);
}

static void preview_mode_keeps_the_button_fallback() {
  assert(livingCanvasInputAction(false, false, true) ==
         LivingCanvasInputAction::PreviewButton);
  assert(livingCanvasInputAction(false, true, false) ==
         LivingCanvasInputAction::Touch);
  assert(livingCanvasInputAction(false, false, false) ==
         LivingCanvasInputAction::None);
}

int main() {
  configured_checkin_accepts_only_touch();
  preview_mode_keeps_the_button_fallback();
  return 0;
}
