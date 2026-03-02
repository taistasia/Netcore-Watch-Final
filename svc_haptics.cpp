// ─────────────────────────────────────────────────────────────────────────────
// svc_haptics.cpp  —  Non-Blocking Haptic / Vibration Service
//
// State machine: IDLE → ON → (PAUSE → ON)* → IDLE
// All transitions driven by millis(); zero blocking calls.
// ─────────────────────────────────────────────────────────────────────────────
#include "svc_haptics.h"
#include <Arduino.h>

// ── Configuration ─────────────────────────────────────────────────────────────
static int _pin = -1;   // < 0 means disabled (no-op mode)

// ── Pattern table ─────────────────────────────────────────────────────────────
// Each pattern is a sequence of (onMs, offMs) step pairs.
// offMs == 0 on the final step means "stop after this buzz".
// Maximum 4 steps per pattern (covers all current IDs).

struct HapticStep {
  uint16_t onMs;   // how long motor is ON
  uint16_t offMs;  // how long motor is OFF before next step (0 = end)
};

// Pattern table indexed by HAPTIC_* IDs
static const HapticStep _patterns[][4] = {
  // HAPTIC_CLICK  (0)
  { { 30,  0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } },
  // HAPTIC_SUCCESS (1)
  { { 100, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } },
  // HAPTIC_WARN   (2) — double pulse
  { { 50, 40 }, { 50, 0 }, { 0, 0 }, { 0, 0 } },
  // HAPTIC_ERROR  (3)
  { { 200, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } },
};
static const int _PATTERN_COUNT  = 4;
static const int _MAX_STEPS      = 4;

// ── Runtime state ─────────────────────────────────────────────────────────────
enum HapState { HAP_IDLE, HAP_ON, HAP_PAUSE };

static HapState  _hapState    = HAP_IDLE;
static uint32_t  _phaseEndMs  = 0;    // millis() when current on/off phase ends
static int       _patStep     = 0;    // which step within current pattern
static int       _patId       = -1;   // active pattern (-1 = simple buzz)

// Steps remaining from a simple hapticsBuzz() call
// If _patId == -1 and _hapState != IDLE, we're doing a plain single buzz.

// ── Internal helpers ──────────────────────────────────────────────────────────

static inline void _motorOn()  { if (_pin >= 0) digitalWrite(_pin, HIGH); }
static inline void _motorOff() { if (_pin >= 0) digitalWrite(_pin, LOW);  }

static void _startStep(uint16_t onMs) {
  _motorOn();
  _hapState   = HAP_ON;
  _phaseEndMs = millis() + onMs;
}

// ── Public API ────────────────────────────────────────────────────────────────

void hapticsInit(int pin) {
  _pin = pin;
  if (pin >= 0) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }
  _hapState = HAP_IDLE;
  _patStep  = 0;
  _patId    = -1;
}

void hapticsBuzz(uint16_t ms) {
  if (_pin < 0 || ms == 0) return;
  // Override any active pattern immediately
  _patId   = -1;
  _patStep = 0;
  _startStep(ms);
}

void hapticsPattern(uint8_t patternId) {
  if (_pin < 0) return;
  if (patternId >= _PATTERN_COUNT) return;
  _patId   = (int)patternId;
  _patStep = 0;
  _startStep(_patterns[patternId][0].onMs);
}

void hapticsTick() {
  if (_pin < 0 || _hapState == HAP_IDLE) return;

  uint32_t now = millis();
  if (now < _phaseEndMs) return;   // phase still running

  // ── Phase just expired ────────────────────────────────────────────────────
  if (_hapState == HAP_ON) {
    _motorOff();

    if (_patId < 0) {
      // Simple buzz — done
      _hapState = HAP_IDLE;
      return;
    }

    // Pattern: check if there's a pause before next step
    uint16_t offMs = _patterns[_patId][_patStep].offMs;
    if (offMs == 0) {
      // No pause → pattern complete
      _hapState = HAP_IDLE;
      return;
    }
    // Pause before next step
    _hapState   = HAP_PAUSE;
    _phaseEndMs = now + offMs;

  } else if (_hapState == HAP_PAUSE) {
    // Pause done → advance to next step
    _patStep++;
    if (_patStep >= _MAX_STEPS || _patterns[_patId][_patStep].onMs == 0) {
      // No more steps
      _hapState = HAP_IDLE;
      return;
    }
    _startStep(_patterns[_patId][_patStep].onMs);
  }
}
