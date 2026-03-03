// ui_frame.cpp

#include "ui_frame.h"

#include "netcore_theme.h"
#include "netcore_config.h" // extern tft

// Fixed registry (no heap)
static UiPanel* s_panels[16];
static uint8_t  s_panelCount = 0;

static inline uint16_t _colBorder(uint8_t style) {
  switch (style) {
    case STYLE_ALERT:     return COL_HILITE();
    case STYLE_SECONDARY: return COL_DIM();
    default:              return COL_FG();
  }
}

static inline uint16_t _colCorner(uint8_t style) {
  switch (style) {
    case STYLE_ALERT:     return COL_HILITE();
    case STYLE_SECONDARY: return COL_FG();
    default:              return COL_HILITE();
  }
}

static void _drawBracketCorners(const UiPanel* p, uint16_t col) {
  // L-shaped corners, 4px legs. Keep inside panel bounds.
  const int16_t x0 = p->x;
  const int16_t y0 = p->y;
  const int16_t x1 = (int16_t)(p->x + p->w - 1);
  const int16_t y1 = (int16_t)(p->y + p->h - 1);
  const int16_t L  = 5;

  // Top-left
  tft.drawFastHLine(x0, y0, L, col);
  tft.drawFastVLine(x0, y0, L, col);

  // Top-right
  tft.drawFastHLine(x1 - (L - 1), y0, L, col);
  tft.drawFastVLine(x1, y0, L, col);

  // Bottom-left
  tft.drawFastHLine(x0, y1, L, col);
  tft.drawFastVLine(x0, y1 - (L - 1), L, col);

  // Bottom-right
  tft.drawFastHLine(x1 - (L - 1), y1, L, col);
  tft.drawFastVLine(x1, y1 - (L - 1), L, col);
}

static void _drawInteriorGrid(const UiPanel* p, uint16_t col) {
  // Sparse 6px grid. Very cheap and fully deterministic.
  const int16_t x0 = (int16_t)(p->x + 2);
  const int16_t y0 = (int16_t)(p->y + 2);
  const int16_t x1 = (int16_t)(p->x + p->w - 3);
  const int16_t y1 = (int16_t)(p->y + p->h - 3);

  for (int16_t x = x0; x <= x1; x += 12) {
    tft.drawFastVLine(x, y0, (int16_t)(y1 - y0 + 1), col);
  }
  for (int16_t y = y0; y <= y1; y += 12) {
    tft.drawFastHLine(x0, y, (int16_t)(x1 - x0 + 1), col);
  }
}

void uiFrameInit() {
  s_panelCount = 0;
}

void uiFrameRegister(UiPanel* p) {
  if (!p) return;
  // Avoid duplicates (linear scan; small N)
  for (uint8_t i = 0; i < s_panelCount; i++) {
    if (s_panels[i] == p) return;
  }
  if (s_panelCount >= (uint8_t)(sizeof(s_panels) / sizeof(s_panels[0]))) return;
  s_panels[s_panelCount++] = p;
}

void uiFrameMarkDirty(UiPanel* p) {
  if (!p) return;
  p->dirty = true;
}

void uiFrameDrawPanel(UiPanel* p) {
  if (!p) return;
  if (p->w <= 2 || p->h <= 2) return;

  const uint16_t border = _colBorder(p->style);
  const uint16_t corner = _colCorner(p->style);

  // 1px industrial border
  tft.drawRect(p->x, p->y, p->w, p->h, border);
  // Corner brackets (Nostromo-ish)
  _drawBracketCorners(p, corner);

  // Optional sparse interior grid (secondary only)
  if (p->style == STYLE_SECONDARY) {
    _drawInteriorGrid(p, COL_DARK());
  }
}

void uiFrameTick() {
  for (uint8_t i = 0; i < s_panelCount; i++) {
    UiPanel* p = s_panels[i];
    if (!p) continue;
    if (!p->dirty) continue;
    uiFrameDrawPanel(p);
    p->dirty = false;
  }
}
