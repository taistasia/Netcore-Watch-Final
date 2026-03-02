#include "netcore_buttons.h"

// ─── BtnLatched implementation ────────────────────────────────────────────────

void BtnLatched::begin(uint8_t p) {
  pin = p;
  pinMode(pin, INPUT_PULLUP);
  lastStable    = digitalRead(pin);
  latched       = false;
  lastChangeMs  = millis();
}

void BtnLatched::poll(uint16_t debounceMs) {
  bool raw = digitalRead(pin);
  if (raw != lastStable) {
    if ((millis() - lastChangeMs) >= debounceMs) {
      lastStable   = raw;
      lastChangeMs = millis();
      if (raw == LOW) latched = true;
    }
  } else {
    lastChangeMs = millis();
  }
}

bool BtnLatched::consume() {
  if (!latched) return false;
  latched = false;
  return true;
}

// ─── Rotary encoder ───────────────────────────────────────────────────────────
// Interrupt fires on every FALLING edge of CLK.
// At that moment, DT HIGH = CW (down), DT LOW = CCW (up).
// Volatile flags read and cleared by consume() wrappers.

namespace Buttons {
  BtnLatched select;
  BtnLatched back;

  volatile bool encUp   = false;
  volatile bool encDown = false;
}

static void IRAM_ATTR encoderISR() {
  // Read DT immediately — this window is very short, must be fast
  bool dt = digitalRead(PIN_ENC_DT);
  if (dt) {
    Buttons::encDown = true;  // CW  = down
  } else {
    Buttons::encUp   = true;  // CCW = up
  }
}

namespace Buttons {

  void begin() {
    // Encoder CLK — interrupt on falling edge
    pinMode(PIN_ENC_CLK, INPUT_PULLUP);
    pinMode(PIN_ENC_DT,  INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_ENC_CLK), encoderISR, FALLING);

    // Encoder push = SELECT, BACK button
    select.begin(PIN_ENC_SW);
    back.begin(PIN_BACK);
  }

  void poll() {
    // Only debounce-poll the two push buttons.
    // Encoder direction is handled entirely in the ISR — no polling needed.
    select.poll(30);
    back.poll(35);
  }

  // ── Direction consume wrappers ────────────────────────────────────────────
  // Disables interrupts briefly to safely read+clear the volatile flags.
  // Returns true once per encoder step, just like a BtnLatched.

  namespace up {
    bool consume() {
      if (!Buttons::encUp) return false;
      noInterrupts();
      Buttons::encUp = false;
      interrupts();
      return true;
    }
  }

  namespace down {
    bool consume() {
      if (!Buttons::encDown) return false;
      noInterrupts();
      Buttons::encDown = false;
      interrupts();
      return true;
    }
  }
}
