// ui_frame.h
// Lightweight industrial frame / panel chrome (dirty-rect friendly)

#pragma once

#include <stdint.h>

// Panel chrome styles
enum {
  STYLE_PRIMARY   = 0,
  STYLE_SECONDARY = 1,
  STYLE_ALERT     = 2,
};

struct UiPanel {
  int16_t x, y, w, h;
  uint8_t style;
  bool    dirty;
};

// Initializes internal registry. Safe to call multiple times.
void uiFrameInit();

// Register a panel pointer (no ownership transfer). Max fixed capacity.
void uiFrameRegister(UiPanel* p);

// Mark panel dirty (forces chrome redraw on next uiFrameTick).
void uiFrameMarkDirty(UiPanel* p);

// Draw panel chrome immediately (does not clear inner content).
void uiFrameDrawPanel(UiPanel* p);

// Draw chrome for any registered dirty panels, then clears dirty flags.
void uiFrameTick();
