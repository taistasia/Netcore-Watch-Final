#pragma once
#include "netcore_config.h"

// ─── BtnLatched ───────────────────────────────────────────────────────────────
// Debounced edge-triggered latch. consume() returns true once per press.
struct BtnLatched {
  uint8_t  pin          = 0;
  bool     lastStable   = true;
  bool     latched      = false;
  uint32_t lastChangeMs = 0;

  void begin(uint8_t p);
  void poll(uint16_t debounceMs);
  bool consume();
};

// ─── Buttons namespace ────────────────────────────────────────────────────────
// up/down come from the rotary encoder (interrupt-driven, zero CPU polling cost)
// select comes from the encoder push switch (SW pin, debounced)
// back comes from the single back button (debounced)
namespace Buttons {
  extern BtnLatched select;
  extern BtnLatched back;

  // Encoder direction events — set by ISR, consumed same as button latches
  extern volatile bool encUp;
  extern volatile bool encDown;

  void begin();
  void poll();  // only polls select + back; encoder is interrupt-driven

  // Wrappers so sketch.ino can use the same consume() pattern for all inputs
  namespace up   { bool consume(); }
  namespace down { bool consume(); }
}
