// svc_dial.cpp
// svc_dial.cpp  —  Segmented radial dial engine (direct draw)

#include "svc_dial.h"

#include "netcore_config.h" // extern tft
#include "netcore_theme.h"  // COL_*()

#include <Arduino.h>

// ─────────────────────────────────────────────────────────────────────────────
// Trig lookup (Q1.15) for degrees 0..90.
// Avoid floats entirely.
// sin(deg) ~= table[deg] / 32767
// cos(deg) = sin(90-deg)
// Table generated offline; size=91.
// ─────────────────────────────────────────────────────────────────────────────
static const int16_t SIN_Q15_0_90[91] = {
  0,   572,  1144,  1715,  2286,  2856,  3425,  3993,  4560,  5126,
  5690, 6252, 6813, 7372, 7929, 8483, 9035, 9583, 10128, 10669,
  11207, 11741, 12270, 12795, 13315, 13830, 14340, 14845, 15345, 15839,
  16327, 16810, 17286, 17756, 18220, 18677, 19128, 19572, 20009, 20439,
  20862, 21277, 21685, 22086, 22479, 22864, 23241, 23610, 23971, 24324,
  24668, 25004, 25331, 25650, 25960, 26261, 26553, 26836, 27110, 27375,
  27631, 27878, 28115, 28343, 28562, 28771, 28971, 29162, 29343, 29515,
  29677, 29830, 29973, 30106, 30230, 30344, 30448, 30543, 30628, 30703,
  30768, 30823, 30869, 30904, 30930, 30946, 30952, 30948, 30935, 30911,
  30878
};

static inline int16_t _sinQ15(int16_t deg) {
  // Normalize to 0..359
  while (deg < 0) deg += 360;
  while (deg >= 360) deg -= 360;

  // Quadrants
  if (deg <= 90) {
    return SIN_Q15_0_90[deg];
  } else if (deg <= 180) {
    return SIN_Q15_0_90[180 - deg];
  } else if (deg <= 270) {
    return (int16_t)-SIN_Q15_0_90[deg - 180];
  } else {
    return (int16_t)-SIN_Q15_0_90[360 - deg];
  }
}

static inline int16_t _cosQ15(int16_t deg) {
  return _sinQ15((int16_t)(90 - deg));
}

static inline int16_t _mulQ15(int16_t aQ15, int16_t b) {
  // (aQ15 * b) / 32767 with rounding
  int32_t v = (int32_t)aQ15 * (int32_t)b;
  v += 16384;
  v /= 32767;
  return (int16_t)v;
}

// ─────────────────────────────────────────────────────────────────────────────
// Dial state
// ─────────────────────────────────────────────────────────────────────────────

#define DIAL_MAX        6
#define DIAL_MAX_SEGS  40

struct DialGeom {
  bool     configured;
  int16_t  cx, cy;
  uint8_t  r;
  uint8_t  thickness;
  uint8_t  segs;

  // Precomputed mid-angle per segment (deg)
  int16_t  angDeg[DIAL_MAX_SEGS];

  // Cached last "active segment count" to allow delta redraw
  int16_t  prevActive;
};

static DialGeom g_d[DIAL_MAX];

void dialInit() {
  for (int i = 0; i < DIAL_MAX; i++) {
    g_d[i].configured = false;
    g_d[i].prevActive = -1;
    g_d[i].segs       = 0;
  }
}

void dialConfig(uint8_t id,
                int16_t cx, int16_t cy,
                uint8_t r,
                uint16_t sweepDeg,
                int16_t startDeg,
                uint8_t thickness,
                uint8_t gapDeg,
                uint8_t segs) {
  if (id >= DIAL_MAX) return;
  if (segs == 0) return;
  if (segs > DIAL_MAX_SEGS) segs = DIAL_MAX_SEGS;

  DialGeom& d = g_d[id];
  d.cx        = cx;
  d.cy        = cy;
  d.r         = r;
  d.thickness = (thickness == 0) ? 1 : thickness;
  d.segs      = segs;
  d.prevActive = -1;

  // Compute segment mid-angles across sweep.
  // Each segment occupies stepDeg; we leave gapDeg between segments.
  // Keep integer arithmetic.
  int32_t usableSweep = (int32_t)sweepDeg - (int32_t)gapDeg * (int32_t)(segs - 1);
  if (usableSweep < (int32_t)segs) usableSweep = (int32_t)segs; // minimum
  int32_t stepDeg = usableSweep / (int32_t)segs;
  if (stepDeg < 1) stepDeg = 1;

  int32_t ang = startDeg;
  for (uint8_t i = 0; i < segs; i++) {
    // Midpoint angle within the segment chunk
    d.angDeg[i] = (int16_t)(ang + (stepDeg / 2));
    ang += stepDeg + gapDeg;
  }

  d.configured = true;
}

void dialInvalidate(uint8_t id) {
  if (id >= DIAL_MAX) return;
  g_d[id].prevActive = -1;
}

static inline int16_t _mapToSegs(int32_t v10, int32_t vMin10, int32_t vMax10, uint8_t segs) {
  if (segs == 0) return 0;
  if (vMax10 <= vMin10) return 0;
  if (v10 <= vMin10) return 0;
  if (v10 >= vMax10) return segs;
  int32_t num = (v10 - vMin10) * (int32_t)segs;
  int32_t den = (vMax10 - vMin10);
  int32_t s = num / den;
  if (s < 0) s = 0;
  if (s > segs) s = segs;
  return (int16_t)s;
}

static inline uint16_t _segColor(uint8_t segIdx,
                                 int16_t warnSeg,
                                 int16_t critSeg) {
  // NETCORE theme has no amber/red. Use tiers via existing theme colors.
  if (segIdx >= (uint8_t)critSeg) return COL_DIM();
  if (segIdx >= (uint8_t)warnSeg) return COL_FG();
  return COL_HILITE();
}

static inline void _drawSegLine(const DialGeom& d, uint8_t segIdx, uint16_t col) {
  // Segment = short radial tick. Outer radius = r, inner radius = r-7.
  const int16_t ang = d.angDeg[segIdx];
  const int16_t sQ15 = _sinQ15(ang);
  const int16_t cQ15 = _cosQ15(ang);

  const int16_t rOut = d.r;
  const int16_t rIn  = (d.r > 7) ? (int16_t)(d.r - 7) : (int16_t)(d.r / 2);

  const int16_t x0 = (int16_t)(d.cx + _mulQ15(cQ15, rIn));
  const int16_t y0 = (int16_t)(d.cy + _mulQ15(sQ15, rIn));
  const int16_t x1 = (int16_t)(d.cx + _mulQ15(cQ15, rOut));
  const int16_t y1 = (int16_t)(d.cy + _mulQ15(sQ15, rOut));

  // Thickness via offset along the normal (perpendicular) direction.
  // n = (-sin, cos)
  const int16_t nXQ15 = (int16_t)-sQ15;
  const int16_t nYQ15 = cQ15;

  const int8_t half = (int8_t)(d.thickness / 2);
  for (int8_t t = -half; t <= half; t++) {
    int16_t ox = _mulQ15(nXQ15, t);
    int16_t oy = _mulQ15(nYQ15, t);
    tft.drawLine(x0 + ox, y0 + oy, x1 + ox, y1 + oy, col);
  }
}

void dialDraw(uint8_t id,
              int32_t v10,
              int32_t vMin10,
              int32_t vMax10,
              int32_t warn10,
              int32_t crit10) {
  if (id >= DIAL_MAX) return;
  DialGeom& d = g_d[id];
  if (!d.configured || d.segs == 0) return;

  const int16_t active = _mapToSegs(v10, vMin10, vMax10, d.segs);
  const int16_t warnSeg = _mapToSegs(warn10, vMin10, vMax10, d.segs);
  const int16_t critSeg = _mapToSegs(crit10, vMin10, vMax10, d.segs);

  // First draw or forced invalidate: repaint everything (still dial-local)
  if (d.prevActive < 0) {
    for (uint8_t i = 0; i < d.segs; i++) {
      uint16_t col = (i < (uint8_t)active) ? _segColor(i, warnSeg, critSeg) : COL_DARK();
      _drawSegLine(d, i, col);
    }
    d.prevActive = active;
    return;
  }

  if (active == d.prevActive) return; // no change

  // Delta-only redraw.
  if (active > d.prevActive) {
    // Turn ON new segments
    for (int16_t i = d.prevActive; i < active; i++) {
      if (i < 0) continue;
      if (i >= d.segs) break;
      uint16_t col = _segColor((uint8_t)i, warnSeg, critSeg);
      _drawSegLine(d, (uint8_t)i, col);
    }
  } else {
    // Turn OFF segments that are no longer active
    for (int16_t i = active; i < d.prevActive; i++) {
      if (i < 0) continue;
      if (i >= d.segs) break;
      _drawSegLine(d, (uint8_t)i, COL_DARK());
    }
  }

  d.prevActive = active;
}