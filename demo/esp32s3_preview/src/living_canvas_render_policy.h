#pragma once

#include <cstdint>

#include "living_canvas_cat_ui.h"

enum class LivingCanvasCatExpression : std::uint8_t {
  Awake,
  Blink,
  Working,
  Happy,
  Concerned,
};

struct LivingCanvasCatRenderPlan {
  LivingCanvasCatExpression expression;
  const char *headline;
  const char *detail;
  std::uint8_t pulseStep;
  bool showAction;
  bool networkReady;
};

LivingCanvasCatRenderPlan livingCanvasCatRenderPlan(
    LivingCanvasCatUiState state, bool configured, bool networkReady,
    std::uint32_t nowMs);
