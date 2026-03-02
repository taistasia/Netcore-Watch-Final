// ─────────────────────────────────────────────────────────────────────────────
// svc_input.cpp  —  Hold Detection + Rotary Acceleration
//
// Reads raw pin state via BtnLatched.lastStable (INPUT_PULLUP: LOW = pressed).
// All timing via millis(); no delay(); no blocking.
// ─────────────────────────────────────────────────────────────────────────────
#include "svc_input.h"

// ── Hold state ────────────────────────────────────────────────────────────────
static uint32_t _selectDownMs  = 0;
static bool     _selectHeld    = false;   // latched true once threshold passed
static bool     _selectFired   = false;   // consume() clears this

static uint32_t _backDownMs    = 0;
static bool     _backHeld      = false;
static bool     _backFired     = false;

// ── Rotary acceleration ───────────────────────────────────────────────────────
static uint32_t _lastUpMs      = 0;
static uint32_t _lastDownMs    = 0;

// ── Internal ──────────────────────────────────────────────────────────────────

static void _trackHold(bool isDown, uint32_t& downMs, bool& held, bool& fired) {
  if (isDown) {
    if (downMs == 0) downMs = millis();
    if (!held && (millis() - downMs >= INPUT_HOLD_MS)) {
      held  = true;
      fired = true;
    }
  } else {
    downMs = 0;
    held   = false;
    // fired stays true until consumed
  }
}

static int _accelSteps(int maxStep, volatile bool& flag, uint32_t& lastMs) {
  noInterrupts();
  bool pending = flag;
  if (pending) flag = false;
  interrupts();
  if (!pending) return 0;

  uint32_t now = millis();
  uint32_t gap = now - lastMs;
  lastMs = now;

  if (gap < INPUT_ACCEL_FAST_MS) {
    int steps = INPUT_ACCEL_MULTIPLIER;
    if (steps > maxStep) steps = maxStep;
    return steps;
  }
  return 1;
}

// ── Public API ────────────────────────────────────────────────────────────────

void inputSvcInit() {
  _selectDownMs = 0; _selectHeld = false; _selectFired = false;
  _backDownMs   = 0; _backHeld   = false; _backFired   = false;
  _lastUpMs     = 0; _lastDownMs  = 0;
}

void inputSvcTick() {
  // lastStable == false (LOW) means button pressed on INPUT_PULLUP
  bool selDown  = !Buttons::select.lastStable;
  bool backDown = !Buttons::back.lastStable;
  _trackHold(selDown,  _selectDownMs, _selectHeld, _selectFired);
  _trackHold(backDown, _backDownMs,   _backHeld,   _backFired);
}

bool inputHoldSelect() {
  if (_selectFired) { _selectFired = false; return true; }
  return false;
}

bool inputHoldBack() {
  if (_backFired) { _backFired = false; return true; }
  return false;
}

bool inputIsSelectDown() { return !Buttons::select.lastStable; }
bool inputIsBackDown()   { return !Buttons::back.lastStable; }

int inputRotaryUp(int maxStep) {
  return _accelSteps(maxStep, Buttons::encUp, _lastUpMs);
}

int inputRotaryDown(int maxStep) {
  return _accelSteps(maxStep, Buttons::encDown, _lastDownMs);
}
