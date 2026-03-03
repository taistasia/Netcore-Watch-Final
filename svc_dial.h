// svc_dial.h
#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// svc_dial.h  —  Segmented radial dial engine (direct draw, dirty-rect safe)
//
// Requirements (NETCORE):
// - Direct draw only (Adafruit_ILI9341)
// - No heap usage
// - No float math in tick/hot path
// - Geometry precomputed in dialConfig()
// - Delta-only redraw: only segments that changed are redrawn
// - Theme colors only (COL_*)
//
// API expected by netcore_apps.cpp:
//   dialInit();
//   dialConfig(id, cx, cy, r, sweepDeg, startDeg, thickness, gapDeg, segs);
//   dialInvalidate(id);
//   dialDraw(id, v10, vMin10, vMax10, warn10, crit10);
//
// Value convention:
//   v10, vMin10, vMax10, warn10, crit10 are fixed-point in "x10" units.
// ─────────────────────────────────────────────────────────────────────────────

#include <stdint.h>

void dialInit();

// Precompute geometry for a dial.
// Angles are degrees.
// sweepDeg : total arc covered by segments (e.g. 210)
// startDeg : start angle for the arc (e.g. -30). 0° is +X (right), 90° is down.
// thickness: segment line thickness in pixels (drawn as parallel lines)
// gapDeg   : angular gap between segments (degrees)
// segs     : number of segments
void dialConfig(uint8_t id,
                int16_t cx, int16_t cy,
                uint8_t r,
                uint16_t sweepDeg,
                int16_t startDeg,
                uint8_t thickness,
                uint8_t gapDeg,
                uint8_t segs);

// Forces next draw to repaint all segments for that dial.
void dialInvalidate(uint8_t id);

// Draw dial segments for the given value.
// Delta redraw only: only segments that changed since last call are touched.
void dialDraw(uint8_t id,
              int32_t v10,
              int32_t vMin10,
              int32_t vMax10,
              int32_t warn10,
              int32_t crit10);