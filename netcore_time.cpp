#include "netcore_time.h"
#include <WiFi.h>
#include "svc_notify.h"     // notifySvcPost on NTP sync
#include "svc_haptics.h"    // haptic on NTP sync

// ─────────────────────────────────────────────────────────────────────────────
// Internal constants  (not exposed; callers use the API only)
// ─────────────────────────────────────────────────────────────────────────────
#define _NTP_SERVER_1     "pool.ntp.org"
#define _NTP_SERVER_2     "time.nist.gov"
#define _TZ_EASTERN       "EST5EDT,M3.2.0/2,M11.1.0/2"
#define _POLL_MS          500UL      // how often to check RTC during WAIT
#define _SYNC_TIMEOUT_MS  15000UL    // give up waiting → RETRY
#define _SYNC_RETRY_MS    30000UL    // retry interval when RETRY / IDLE

// ─────────────────────────────────────────────────────────────────────────────
// State
// ─────────────────────────────────────────────────────────────────────────────
static NtpState  _state        = NTP_IDLE;
static uint32_t  _lastSyncEpoch = 0;
static uint32_t  _syncStartMs  = 0;
static uint32_t  _lastPollMs   = 0;
static uint32_t  _lastRetryMs  = 0;

// ── Mock clock ─────────────────────────────────────────────────────────────────
// Starts at 12:00:00 and ticks every real second.
// Seeded from RTC the first time NTP syncs — after that it shows real time
// even if WiFi drops (since ESP32 RTC keeps running).
static int      _mHour   = 12;
static int      _mMin    = 0;
static int      _mSec    = 0;
static uint32_t _lastSecMs = 0;

static void _mockAdvance() {
  // Catch-up loop: if loop() stalled for 3s, credit all 3 seconds correctly.
  while (millis() - _lastSecMs >= 1000) {
    _lastSecMs += 1000;
    if (++_mSec  >= 60) { _mSec  = 0; ++_mMin;  }
    if (  _mMin  >= 60) { _mMin  = 0; _mHour = (_mHour + 1) % 24; }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

void timeSvcInit() {
  // Set TZ env var + tzset() so that mktime(), localtime_r(), and any direct
  // calls to getLocalTime() all use Eastern time.
  // We also use configTzTime() (not configTime) in timeSvcTriggerSync() which
  // prevents the SNTP stack from overwriting TZ with "UTC0".
  setenv("TZ", _TZ_EASTERN, 1);
  tzset();
  _lastSecMs = millis();
  _state     = NTP_IDLE;
}

void timeSvcTriggerSync() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (_state == NTP_SYNCED)          return;   // already good

  // ── WHY configTzTime() and NOT configTime() ────────────────────────────────
  // configTime(gmtOffset, dstOffset, server) internally calls
  //   setenv("TZ", "UTC0", 1)
  // which OVERWRITES any TZ env var we set in timeSvcInit(), reverting to UTC.
  // Result: getLocalTime() returns UTC, and the displayed hour is wrong.
  //
  // configTzTime(tz_posix_str, server1, server2) is the correct ESP32 API:
  // it passes the POSIX TZ string directly to the SNTP stack and does NOT
  // clobber the TZ env var. Time reported by getLocalTime() is then Eastern.
  configTzTime(_TZ_EASTERN, _NTP_SERVER_1, _NTP_SERVER_2);

  _state       = NTP_START;
  _syncStartMs = millis();
  _lastPollMs  = millis();
}

void timeSvcTick() {
  _mockAdvance();

  // ── Auto-trigger / retry when WiFi is available ───────────────────────────
  if (_state == NTP_IDLE || _state == NTP_RETRY) {
    if (WiFi.status() == WL_CONNECTED) {
      bool due = (_state == NTP_IDLE) ||
                 (millis() - _lastRetryMs > _SYNC_RETRY_MS);
      if (due) {
        timeSvcTriggerSync();
        _lastRetryMs = millis();
      }
    }
    return;
  }

  // ── START → WAIT (configTime just issued; give it one tick before polling) ─
  if (_state == NTP_START) {
    _state = NTP_WAIT;
    return;
  }

  // ── WAIT — poll RTC until valid or timed out ──────────────────────────────
  if (_state == NTP_WAIT) {
    if (millis() - _lastPollMs < _POLL_MS) return;
    _lastPollMs = millis();

    struct tm t;
    // timeout = 0: returns immediately with whatever the RTC has.
    // Default timeout (no argument) is 5000 ms — the source of the
    // original 5-second watchface freeze when SNTP was unsynced.
    if (getLocalTime(&t, 0) && t.tm_year > 100) {
      _state        = NTP_SYNCED;
      _lastSyncEpoch = (uint32_t)mktime(&t);
      // Seed mock clock so status bar shows correct time immediately.
      _mHour     = t.tm_hour;
      _mMin      = t.tm_min;
      _mSec      = t.tm_sec;
      _lastSecMs = millis();
      notifySvcPost(NOTIFY_OK, "NTP", "Time synced", 2000);
      hapticsPattern(HAPTIC_SUCCESS);
      return;
    }

    // Timed out → back to RETRY; will re-attempt after _SYNC_RETRY_MS
    if (millis() - _syncStartMs > _SYNC_TIMEOUT_MS) {
      _state       = NTP_RETRY;
      _lastRetryMs = millis();
    }
  }
}

bool timeSvcGetLocal(struct tm* t) {
  if (_state == NTP_SYNCED) {
    // timeout = 0 — never blocks
    if (getLocalTime(t, 0) && t->tm_year > 100) return true;
    // RTC lost unexpectedly — fall through to mock
  }
  // Mock fallback: provide HH:MM:SS only; date fields ZEROED.
  // Callers MUST display "--- -- --- ----" for date when this returns false.
  memset(t, 0, sizeof(*t));
  t->tm_hour = _mHour;
  t->tm_min  = _mMin;
  t->tm_sec  = _mSec;
  return false;
}

bool     timeSvcIsReady()  { return _state == NTP_SYNCED; }
NtpState timeSvcState()    { return _state; }
int      timeSvcMockHour() { return _mHour; }
int      timeSvcMockMin()  { return _mMin;  }
int      timeSvcMockSec()  { return _mSec;  }

uint32_t timeSvcSyncAge() {
  if (_lastSyncEpoch == 0) return 0;
  struct tm now;
  if (getLocalTime(&now, 0) && now.tm_year > 100) {
    uint32_t nowEpoch = (uint32_t)mktime(&now);
    return (nowEpoch > _lastSyncEpoch) ? (nowEpoch - _lastSyncEpoch) : 0;
  }
  // RTC unavailable — can't determine age accurately; report 0.
  return 0;
}

bool timeSvcIsStale() {
  if (_state != NTP_SYNCED) return false;
  return timeSvcSyncAge() > TIME_STALE_SEC;
}