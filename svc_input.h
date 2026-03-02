#pragma once
// ═════════════════════════════════════════════════════════════════════════════
// svc_input.h  —  Enhanced Input: Hold Detection + Rotary Acceleration
//
// PURPOSE
//   Adds premium rotary feel and hold-button behavior on top of the existing
//   BtnLatched + Buttons namespace in netcore_buttons.h.
//
//   This is a SEPARATE header — it does NOT modify netcore_buttons.h.
//   Include this AFTER netcore_buttons.h where advanced input is needed.
//
// ── HOLD DETECTION ───────────────────────────────────────────────────────────
//   inputHoldTick()          — call once per loop(); must be called BEFORE checking holds
//   inputHoldSelect()        — true once when SELECT held ≥ INPUT_HOLD_MS
//   inputHoldBack()          — true once when BACK held ≥ INPUT_HOLD_MS
//
//   inputIsSelectDown()      — raw: SELECT currently pressed (not just latched)
//   inputIsBackDown()        — raw: BACK currently pressed
//
// ── ROTARY ACCELERATION ──────────────────────────────────────────────────────
//   inputRotaryUp(maxStep)   — consume up steps with acceleration; returns 0–maxStep
//   inputRotaryDown(maxStep) — consume down steps with acceleration; returns 0–maxStep
//
//   Acceleration: if rotary events fire faster than INPUT_ACCEL_FAST_MS apart,
//   each step counts as INPUT_ACCEL_MULTIPLIER steps (capped at maxStep).
//
// ── GLOBAL HOLD ACTIONS ──────────────────────────────────────────────────────
//   These are the platform-wide conventions:
//   Hold BACK  (≥500ms)  →  go to HOME (menu root) from anywhere
//   Hold SELECT (≥500ms) →  open Quick Menu (brightness / WiFi / HID)
//
//   Apps that want to implement these must check:
//     if (inputHoldBack())   { goHome(); return; }
//     if (inputHoldSelect()) { openQuickMenu(); return; }
//   at the TOP of their tick() function, before any other input handling.
// ═════════════════════════════════════════════════════════════════════════════
#include "netcore_buttons.h"
#include <stdint.h>

// ── Timing constants ─────────────────────────────────────────────────────────
#define INPUT_HOLD_MS              500UL   // ms to register a hold press
#define INPUT_ACCEL_FAST_MS        80UL    // if step interval < this, accelerate
#define INPUT_ACCEL_MULTIPLIER     4       // fast spin = 4× step count
#define INPUT_ACCEL_MAX_STEP       5       // hard cap on steps per poll

// ── Lifecycle ─────────────────────────────────────────────────────────────────
void inputSvcInit();
void inputSvcTick();   // call from loop() before any input checks; updates hold state

// ── Hold detection ────────────────────────────────────────────────────────────
bool inputHoldSelect();  // true once per hold event; resets after consume
bool inputHoldBack();    // true once per hold event; resets after consume
bool inputIsSelectDown();
bool inputIsBackDown();

// ── Accelerated rotary ────────────────────────────────────────────────────────
// Returns number of steps to advance (1 to maxStep).
// Consumes the encoder event — don't also call Buttons::up::consume() for the same step.
// Returns 0 if no event pending.
int inputRotaryUp(int maxStep   = INPUT_ACCEL_MAX_STEP);
int inputRotaryDown(int maxStep = INPUT_ACCEL_MAX_STEP);
