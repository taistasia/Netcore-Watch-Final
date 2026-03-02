// svc_anim.cpp
#include "svc_anim.h"
#include <Arduino.h>
#include <math.h>

// Fixed pool sizing
#ifndef ANIM_MAX_TWEENS
#define ANIM_MAX_TWEENS 24
#endif

// Property store sizing (small + predictable)
#ifndef ANIM_MAX_TARGETS
#define ANIM_MAX_TARGETS 8
#endif

#ifndef ANIM_MAX_PROPS
#define ANIM_MAX_PROPS  16
#endif

struct Tween {
  uint8_t  active;
  uint8_t  target;
  uint8_t  prop;
  uint8_t  ease;
  uint8_t  flags;
  int32_t  fromQ;
  int32_t  toQ;
  uint32_t startMs;
  uint32_t durMs;
};

static Tween   g_tw[ANIM_MAX_TWEENS];
static int32_t g_prop[ANIM_MAX_TARGETS][ANIM_MAX_PROPS];

static uint32_t g_lastTickUs = 0;
static uint32_t g_budgetSkips = 0;

static inline uint32_t _clampU32(uint32_t v, uint32_t lo, uint32_t hi) {
  return (v < lo) ? lo : (v > hi) ? hi : v;
}

static inline int32_t _lerpQ16(int32_t a, int32_t b, uint32_t tQ16) {
  // a + (b-a)*t
  int64_t d = (int64_t)(b - a);
  int64_t r = (int64_t)a + ((d * (int64_t)tQ16) >> 16);
  return (int32_t)r;
}

static uint32_t _ease(uint8_t ease, uint32_t tQ16) {
  // tQ16 in [0..65536]
  switch ((AnimEase)ease) {
    default:
    case EASE_LINEAR: return tQ16;

    case EASE_IN_QUAD: {
      // t^2
      uint64_t t = tQ16;
      return (uint32_t)((t * t) >> 16);
    }
    case EASE_OUT_QUAD: {
      // 1 - (1-t)^2
      uint32_t inv = 65536u - tQ16;
      uint64_t x = inv;
      uint32_t inv2 = (uint32_t)((x * x) >> 16);
      return 65536u - inv2;
    }
    case EASE_INOUT_QUAD: {
      // piecewise
      if (tQ16 < 32768u) {
        uint64_t t = (uint64_t)tQ16 * 2u;
        return (uint32_t)((t * t) >> 17); // /2
      } else {
        uint32_t u = (tQ16 - 32768u) * 2u;
        uint32_t out = _ease(EASE_OUT_QUAD, u);
        return 32768u + (out >> 1);
      }
    }

    case EASE_IN_CUBIC: {
      uint64_t t = tQ16;
      return (uint32_t)((t * t * t) >> 32);
    }
    case EASE_OUT_CUBIC: {
      // 1 - (1-t)^3
      uint32_t inv = 65536u - tQ16;
      uint64_t x = inv;
      uint32_t inv3 = (uint32_t)((x * x * x) >> 32);
      return 65536u - inv3;
    }
    case EASE_INOUT_CUBIC: {
      if (tQ16 < 32768u) {
        uint32_t u = tQ16 * 2u;
        uint32_t e = _ease(EASE_IN_CUBIC, u);
        return e >> 1;
      } else {
        uint32_t u = (tQ16 - 32768u) * 2u;
        uint32_t e = _ease(EASE_OUT_CUBIC, u);
        return 32768u + (e >> 1);
      }
    }

    case EASE_OUT_EXPO: {
      // float approx confined here
      // outExpo: 1 - 2^(-10 t)
      float t = (float)tQ16 / 65536.0f;
      float y = (t >= 1.0f) ? 1.0f : (1.0f - powf(2.0f, -10.0f * t));
      uint32_t q = (uint32_t)(y * 65536.0f);
      return _clampU32(q, 0u, 65536u);
    }

    case EASE_INOUT_SINE: {
      // float approx confined here
      // inOutSine: -(cos(pi t)-1)/2
      float t = (float)tQ16 / 65536.0f;
      float y = (1.0f - cosf((float)M_PI * t)) * 0.5f;
      uint32_t q = (uint32_t)(y * 65536.0f);
      return _clampU32(q, 0u, 65536u);
    }
  }
}

static inline bool _validIdx(uint8_t t, uint8_t p) {
  return (t < ANIM_MAX_TARGETS) && (p < ANIM_MAX_PROPS);
}

void animSvcInit() {
  for (int i = 0; i < ANIM_MAX_TWEENS; i++) g_tw[i].active = 0;
  for (int t = 0; t < ANIM_MAX_TARGETS; t++)
    for (int p = 0; p < ANIM_MAX_PROPS; p++)
      g_prop[t][p] = 0;

  g_lastTickUs = 0;
  g_budgetSkips = 0;
}

bool animIsActive(uint8_t targetId, uint8_t propId) {
  for (int i = 0; i < ANIM_MAX_TWEENS; i++) {
    if (g_tw[i].active && g_tw[i].target == targetId && g_tw[i].prop == propId)
      return true;
  }
  return false;
}

uint16_t animActiveCount() {
  uint16_t c = 0;
  for (int i = 0; i < ANIM_MAX_TWEENS; i++) c += (g_tw[i].active != 0);
  return c;
}

uint32_t animLastTickUs() { return g_lastTickUs; }
uint32_t animBudgetSkips() { return g_budgetSkips; }

void animSetQ(uint8_t targetId, uint8_t propId, int32_t q16) {
  if (!_validIdx(targetId, propId)) return;
  g_prop[targetId][propId] = q16;
}

int32_t animGetQ(uint8_t targetId, uint8_t propId) {
  if (!_validIdx(targetId, propId)) return 0;
  return g_prop[targetId][propId];
}

int animGetI(uint8_t targetId, uint8_t propId) {
  return Q16_TO_I(animGetQ(targetId, propId));
}

void animCancel(uint8_t targetId, uint8_t propId) {
  for (int i = 0; i < ANIM_MAX_TWEENS; i++) {
    if (!g_tw[i].active) continue;
    if (g_tw[i].target == targetId && g_tw[i].prop == propId) {
      g_tw[i].active = 0;
    }
  }
}

void animCancelTarget(uint8_t targetId) {
  for (int i = 0; i < ANIM_MAX_TWEENS; i++) {
    if (!g_tw[i].active) continue;
    if (g_tw[i].target == targetId) g_tw[i].active = 0;
  }
}

static int _allocTweenSlot() {
  for (int i = 0; i < ANIM_MAX_TWEENS; i++) {
    if (!g_tw[i].active) return i;
  }
  return -1;
}

bool animTween(uint8_t targetId, uint8_t propId,
               int32_t fromQ, int32_t toQ,
               uint32_t durationMs,
               uint8_t easing,
               uint8_t flags) {
  if (!_validIdx(targetId, propId)) return false;
  if (durationMs == 0) {
    animSetQ(targetId, propId, toQ);
    animCancel(targetId, propId);
    return true;
  }

  if (flags & ANIM_F_REPLACE) animCancel(targetId, propId);

  int idx = _allocTweenSlot();
  if (idx < 0) return false;

  Tween &tw = g_tw[idx];
  tw.active  = 1;
  tw.target  = targetId;
  tw.prop    = propId;
  tw.ease    = easing;
  tw.flags   = flags;
  tw.fromQ   = fromQ;
  tw.toQ     = toQ;
  tw.startMs = millis();
  tw.durMs   = durationMs;

  // Immediately set starting value (deterministic)
  animSetQ(targetId, propId, fromQ);
  return true;
}

void animSvcTick(uint32_t budgetUs) {
  uint32_t t0 = micros();

  // If nothing active, keep tick cheap
  if (animActiveCount() == 0) {
    g_lastTickUs = (uint32_t)(micros() - t0);
    return;
  }

  uint32_t nowMs = millis();

  for (int i = 0; i < ANIM_MAX_TWEENS; i++) {
    if (!g_tw[i].active) continue;

    // Budget guard (stop processing remaining tweens this loop)
    if ((uint32_t)(micros() - t0) > budgetUs) {
      g_budgetSkips++;
      break;
    }

    Tween &tw = g_tw[i];

    uint32_t elapsed = nowMs - tw.startMs;
    if (elapsed >= tw.durMs) {
      // Finish
      animSetQ(tw.target, tw.prop, tw.toQ);

      bool loop = (tw.flags & ANIM_F_LOOP) != 0;
      bool ping = (tw.flags & ANIM_F_PINGPONG) != 0;

      if (loop) {
        // Restart
        tw.startMs = nowMs;
        if (ping) {
          int32_t tmp = tw.fromQ;
          tw.fromQ = tw.toQ;
          tw.toQ = tmp;
        }
        animSetQ(tw.target, tw.prop, tw.fromQ);
      } else {
        tw.active = 0;
      }
      continue;
    }

    // Progress fraction in Q16.16
    uint32_t tQ16 = (uint32_t)((uint64_t)elapsed * 65536ull / (uint64_t)tw.durMs);
    if (tQ16 > 65536u) tQ16 = 65536u;

    uint32_t eQ16 = _ease(tw.ease, tQ16);
    int32_t v = _lerpQ16(tw.fromQ, tw.toQ, eQ16);
    animSetQ(tw.target, tw.prop, v);
  }

  g_lastTickUs = (uint32_t)(micros() - t0);
}