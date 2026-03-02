// svc_anim.h
#pragma once
#include <stdint.h>

// ─────────────────────────────────────────────────────────────────────────────
// NETCORE deterministic tween engine (fixed pool, Q16.16, no heap churn)
// ─────────────────────────────────────────────────────────────────────────────

// Q16.16 helpers
#define Q16_ONE        ((int32_t)0x00010000)
#define Q16_FROM_I(x)  ((int32_t)((x) << 16))
#define Q16_TO_I(x)    ((int32_t)((x) >> 16))

// Targets used by current NETCORE UI
enum AnimTarget : uint8_t {
  AT_NONE  = 0,
  AT_MENU  = 1,   // menu hover FX (pulse + cursor sweep)
  AT_DEMO  = 2,   // ANIM demo app
  AT_TRANS = 3,   // app enter/exit transition
  AT_MICRO = 4,   // micro-interactions (button pulse, highlight)
};

// Properties used by current NETCORE UI
enum AnimProp : uint8_t {
  AP_NONE      = 0,
  AP_PULSE     = 1,  // 0..1 Q16.16
  AP_CURSOR_X  = 2,  // pixels in Q16.16
  AP_PANEL_X   = 3,  // pixels in Q16.16
  AP_PROGRESS  = 4,  // 0..1 Q16.16
  AP_SLIDE_X   = 5,  // transition slide offset (pixels Q16.16)
  AP_SEL_PULSE = 6,  // button select pulse (0..1 Q16.16)
};

// Easing modes (keep stable IDs)
enum AnimEase : uint8_t {
  EASE_LINEAR = 0,
  EASE_IN_QUAD,
  EASE_OUT_QUAD,
  EASE_INOUT_QUAD,
  EASE_IN_CUBIC,
  EASE_OUT_CUBIC,
  EASE_INOUT_CUBIC,
  EASE_OUT_EXPO,       // float approx (confined to easing)
  EASE_INOUT_SINE,     // float approx (confined to easing)
};

// Flags
enum AnimFlags : uint8_t {
  ANIM_F_NONE     = 0,
  ANIM_F_REPLACE  = 1 << 0, // cancel existing (target+prop) before starting
  ANIM_F_LOOP     = 1 << 1, // restart when complete
  ANIM_F_PINGPONG = 1 << 2, // reverse direction each loop
};

// Init/tick
void     animSvcInit();
void     animSvcTick(uint32_t budgetUs); // budget in microseconds; skips remaining work if over

// Control
bool     animTween(uint8_t targetId, uint8_t propId,
                   int32_t fromQ, int32_t toQ,
                   uint32_t durationMs,
                   uint8_t easing,
                   uint8_t flags);

void     animCancel(uint8_t targetId, uint8_t propId);
void     animCancelTarget(uint8_t targetId);

// State IO (the UI reads these)
void     animSetQ(uint8_t targetId, uint8_t propId, int32_t q16);
int32_t  animGetQ(uint8_t targetId, uint8_t propId);
int      animGetI(uint8_t targetId, uint8_t propId);

// Query
bool     animIsActive(uint8_t targetId, uint8_t propId);

// Telemetry
uint16_t animActiveCount();
uint32_t animLastTickUs();
uint32_t animBudgetSkips();