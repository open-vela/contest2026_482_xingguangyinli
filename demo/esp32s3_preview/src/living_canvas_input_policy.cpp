#include "living_canvas_input_policy.h"

LivingCanvasInputAction livingCanvasInputAction(bool checkinMode,
                                                bool touchEdge,
                                                bool buttonEdge) {
  if (touchEdge) return LivingCanvasInputAction::Touch;
  if (buttonEdge && !checkinMode) {
    return LivingCanvasInputAction::PreviewButton;
  }
  return LivingCanvasInputAction::None;
}
