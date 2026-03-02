#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// netcore_time.h — Time Service  (NTP state machine + mock-clock fallback)
//
// Purpose : Single owner of all time-related state.
//           Drives NTP sync as a fully non-blocking state machine.
//           Provides mock clock when WiFi / NTP is unavailable.
//           Exposes sync-age + stale detection for watchface indicators.
//
// Public API ──────────────────────────────────────────────────────────────────
//   timeSvcInit()          — call once in setup(); sets TZ, inits mock clock
//   timeSvcTick()          — call every loop(); clock advance + NTP polling
//   timeSvcTriggerSync()   — kick NTP now (call on WiFi connect); idempotent
//   timeSvcGetLocal(tm*)   — fills *t; returns true when NTP-sourced
//   timeSvcIsReady()       — true when NTP_SYNCED
//   timeSvcState()         — NtpState for UI (status strip, sync hint)
//   timeSvcSyncAge()       — seconds since last NTP sync; 0 if never
//   timeSvcIsStale()       — true when synced but age > TIME_STALE_SEC
//   timeSvcMockHour/Min/Sec() — mock-clock accessors for fallback status bar
//
// Ownership ───────────────────────────────────────────────────────────────────
//   Owns: NTP FSM, mock clock, last-sync epoch.
//   Does NOT own WiFi connection.  Callers call timeSvcTriggerSync() after
//   WiFi connects; timeSvcTick() also auto-triggers when WiFi becomes up.
// ─────────────────────────────────────────────────────────────────────────────
#include "netcore_config.h"
#include "time.h"

// ── NTP state machine enum ────────────────────────────────────────────────────
enum NtpState {
  NTP_IDLE   = 0,   // no sync attempt yet (or WiFi never connected)
  NTP_START  = 1,   // configTime() just called; transition to WAIT next tick
  NTP_WAIT   = 2,   // polling RTC via getLocalTime(0); not yet valid
  NTP_SYNCED = 3,   // RTC has valid NTP-sourced time
  NTP_RETRY  = 4,   // timed out waiting; will retry after SYNC_RETRY_MS
};

// ── Lifecycle ─────────────────────────────────────────────────────────────────
void     timeSvcInit();
void     timeSvcTick();

// ── Sync control ──────────────────────────────────────────────────────────────
void     timeSvcTriggerSync();

// ── Time access ───────────────────────────────────────────────────────────────
// Returns true  → RTC has NTP time; *t is real local time.
// Returns false → NTP not ready; *t has mock HH:MM:SS, date fields ZEROED.
//                 UI must display "--- -- --- ----" for date when false.
bool     timeSvcGetLocal(struct tm* t);

bool     timeSvcIsReady();
NtpState timeSvcState();
uint32_t timeSvcSyncAge();   // seconds since last sync; 0 if never
bool     timeSvcIsStale();   // true if synced but age > TIME_STALE_SEC

// ── Mock clock accessors (status bar uses these when NTP not ready) ───────────
int      timeSvcMockHour();
int      timeSvcMockMin();
int      timeSvcMockSec();
