#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// svc_haptics.h  —  Non-Blocking Haptic / Vibration Service
//
// Purpose  : Drive a vibration motor or piezo buzzer via a single GPIO pin.
//            All timing is millis()-based — no delay() in any path.
//            If hapticsInit() is called with pin < 0, everything is a no-op,
//            so the service compiles and runs safely without hardware.
//
// Usage ───────────────────────────────────────────────────────────────────────
//   hapticsInit(PIN_HAPTIC)   — call once in setup(); pin < 0 = disabled
//   hapticsTick()             — call every loop(); drives the state machine
//   hapticsBuzz(ms)           — single buzz for ms milliseconds
//   hapticsPattern(id)        — multi-step pattern (see IDs below)
//
// Pattern IDs ─────────────────────────────────────────────────────────────────
//   HAPTIC_CLICK    — 30 ms single tap        (button confirm)
//   HAPTIC_SUCCESS  — 100 ms solid            (WiFi connect, NTP sync)
//   HAPTIC_WARN     — 50–40–50 ms double tap  (WiFi lost, warning banner)
//   HAPTIC_ERROR    — 200 ms solid            (connect failed, error banner)
//
// Rules ───────────────────────────────────────────────────────────────────────
//   - Never calls delay()
//   - hapticsBuzz / hapticsPattern are fire-and-forget; safe from any context
//   - A new call while active immediately supersedes the current pattern
//   - hapticsTick() must be called every loop(); ~1 ms budget max
// ─────────────────────────────────────────────────────────────────────────────
#include <stdint.h>

// ── Pattern IDs ───────────────────────────────────────────────────────────────
#define HAPTIC_CLICK    0   // 30 ms — light tap, button/nav confirm
#define HAPTIC_SUCCESS  1   // 100 ms — connect OK, NTP synced
#define HAPTIC_WARN     2   // 50-40-50 ms double pulse — warning / disconnect
#define HAPTIC_ERROR    3   // 200 ms — connect failed, error banner

// ── Lifecycle ─────────────────────────────────────────────────────────────────
void hapticsInit(int pin);   // pin < 0 disables service (no-op mode)
void hapticsTick();          // call every loop(); never blocks

// ── Actions (fire-and-forget; safe to call from any module) ──────────────────
void hapticsBuzz(uint16_t ms);        // arbitrary duration single buzz
void hapticsPattern(uint8_t patternId);  // predefined multi-step pattern
