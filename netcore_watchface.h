#pragma once
#include "netcore_config.h"
#include "netcore_theme.h"

// ─── Watchface / Idle manager ─────────────────────────────────────────────────
// State machine lives in sketch.ino:
//   SYS_ACTIVE    — normal operation
//   SYS_WATCHFACE — full-screen clock; any input wakes to menu
//   SYS_SLEEP     — display blanked (future)

void watchfaceEnter();
void watchfaceTick();
void watchfaceExit();

// Call after a successful WiFi connection to trigger opportunistic NTP sync
void watchfaceNtpSync();
