#pragma once

enum class LivingCanvasInputAction {
  None,
  Touch,
  PreviewButton,
};

// Check-in displays accept only an actual touchscreen edge. The physical BOOT
// button remains available solely as a convenience for an unconfigured UI
// preview and can never stand in for a cat's explicit touch.
LivingCanvasInputAction livingCanvasInputAction(bool checkinMode,
                                                bool touchEdge,
                                                bool buttonEdge);
