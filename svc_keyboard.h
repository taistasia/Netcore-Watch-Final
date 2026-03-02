#pragma once
// ═════════════════════════════════════════════════════════════════════════════
// svc_keyboard.h — Rotary Keyboard (deterministic, fixed-buffer, dirty-rect)
//
// Purpose
//   Provide a reusable, non-blocking rotary-driven text input UI.
//   Used first for WiFi password entry.
//
// Rules
//   No heap allocation
//   No blocking loops
//   No full-screen redraw in tick()
//   Uses existing Buttons + svc_input (hold + accel) signals
// ═════════════════════════════════════════════════════════════════════════════

#include <stdint.h>

// Start a keyboard session.
// targetBuffer is written on confirm; maxLen includes the null terminator.
void kbStart(const char* title, char* targetBuffer, uint8_t maxLen, bool maskInput);

// Tick the keyboard state machine and render any dirty regions.
void kbTick();

bool kbActive();
bool kbFinished();
bool kbCancelled();
