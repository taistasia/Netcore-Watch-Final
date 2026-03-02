#include "netcore_watchface.h"
#include "netcore_ui.h"
#include "netcore_settings.h"
#include "netcore_sd.h"
#include "netcore_time.h"
#include "netcore_ducky.h"   // DUCKY_DRY_RUN macro for watchface status pill
// NOTE: <WiFi.h> and "time.h" are no longer included directly here.
//       All NTP/RTC access goes through netcore_time (timeSvcGetLocal etc.).
//       WiFi state is read via WiFi.h which is pulled in by netcore_time.h
//       transitively through the time service translation unit.
#include <WiFi.h>   // for WiFi.status() / WiFi.SSID() in pill renderer

// ─── Layout ──────────────────────────────────────────────────────────────────
static const int WF_BRACE_X = 20, WF_BRACE_Y = 64,
                 WF_BRACE_W = W - 40, WF_BRACE_H = 80;
static const int WF_TIME_Y  = 78;
static const int WF_SEC_Y   = 90;
static const int WF_DATE_Y  = 118;
static const int WF_STAT_Y  = 162;
static const int WF_SYNC_Y  = 192;
static const int WF_WAKE_Y  = 214;

static const char* MONTH_NAMES[] = {
  "JAN","FEB","MAR","APR","MAY","JUN","JUL","AUG","SEP","OCT","NOV","DEC"};
static const char* DAY_NAMES[] = {"SUN","MON","TUE","WED","THU","FRI","SAT"};

// ─── Dirty-check state ───────────────────────────────────────────────────────
static int  wfLastHour = -1, wfLastMin = -1, wfLastSec = -1, wfLastDay = -1;
static bool wfColonOn  = true;

// NTP state change detection (so we redraw on sync/desync events)
static NtpState wfLastNtpState = NTP_IDLE;

// ─── Drawing helpers ──────────────────────────────────────────────────────────
static void drawWfBrackets(uint16_t color) {
  int x = WF_BRACE_X, y = WF_BRACE_Y, w = WF_BRACE_W, h = WF_BRACE_H;
  const int arm = 18;
  tft.drawFastHLine(x,           y,           arm, color);
  tft.drawFastVLine(x,           y,           arm, color);
  tft.drawFastHLine(x + w - arm, y,           arm, color);
  tft.drawFastVLine(x + w - 1,   y,           arm, color);
  tft.drawFastHLine(x,           y + h - 1,   arm, color);
  tft.drawFastVLine(x,           y + h - arm, arm, color);
  tft.drawFastHLine(x + w - arm, y + h - 1,   arm, color);
  tft.drawFastVLine(x + w - 1,   y + h - arm, arm, color);
}

static void drawStatPill(int x, int y, int w, const char* label,
                         const char* value, bool lit) {
  tft.fillRect(x, y, w, 14, COL_BG());
  tft.drawRect(x, y, w, 14, lit ? COL_DIM() : COL_DARK());
  tft.setTextSize(1);
  tft.setTextColor(COL_DARK(), COL_BG());
  tft.setCursor(x + 3, y + 3); tft.print(label);
  if (value) {
    tft.setTextColor(lit ? COL_FG() : COL_DARK(), COL_BG());
    tft.setCursor(x + w - (int)strlen(value)*6 - 3, y + 3);
    tft.print(value);
  }
}

// ─── Section renderers ────────────────────────────────────────────────────────

static void wfDrawStatusStrip() {
  tft.fillRect(0, 0, W, 20, COL_BG());
  tft.drawFastHLine(0, 20, W, COL_DARK());
  tft.setTextSize(1);
  tft.setTextColor(COL_DIM(), COL_BG());
  tft.setCursor(6, 6); tft.print("NETCORE");
  tft.setCursor(W - 8*6 - 4, 6); tft.print("WATCH  ");

  // NTP status indicator with stale detection
  const char* syncStr;
  uint16_t    syncCol;
  NtpState ns = timeSvcState();
  if (ns == NTP_SYNCED) {
    if (timeSvcIsStale()) { syncStr = "OLD"; syncCol = COL_DARK(); }
    else                  { syncStr = "NTP"; syncCol = COL_FG(); }
  } else if (ns == NTP_WAIT || ns == NTP_START) {
    syncStr = "..."; syncCol = COL_DIM();
  } else {
    syncStr = "---"; syncCol = COL_DARK();
  }
  tft.setTextColor(syncCol, COL_BG());
  tft.setCursor(W - 3*6 - 4, 6); tft.print(syncStr);
}

static void wfDrawTime(bool full) {
  struct tm t; timeSvcGetLocal(&t);
  int h = t.tm_hour, m = t.tm_min, s = t.tm_sec;

  // HH:MM — only redraw when hour or minute changes
  if (full || h != wfLastHour || m != wfLastMin) {
    tft.fillRect(WF_BRACE_X + 1, WF_TIME_Y - 2, WF_BRACE_W - 2, 38, COL_BG());
    char buf[6]; snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
    // textSize 4: each char 24px wide x 32px tall → "HH:MM" = 5 × 24 = 120px
    int tx = (W - 120) / 2;
    tft.setTextSize(4); tft.setTextColor(COL_FG(), COL_BG());
    tft.setCursor(tx, WF_TIME_Y); tft.print(buf);
    wfLastHour = h; wfLastMin = m;
    if (!wfShowSeconds) wfColonOn = true;  // solid colon when seconds off
  }

  // Seconds + colon blink — only when wfShowSeconds enabled
  if (wfShowSeconds) {
    if (full || s != wfLastSec) {
      int sx = (W + 120) / 2 + 6;
      tft.fillRect(sx, WF_SEC_Y, 30, 16, COL_BG());
      char sbuf[3]; snprintf(sbuf, sizeof(sbuf), "%02d", s);
      tft.setTextSize(2); tft.setTextColor(COL_DIM(), COL_BG());
      tft.setCursor(sx, WF_SEC_Y); tft.print(sbuf);

      bool colonOn = (s % 2 == 0);
      if (full || colonOn != wfColonOn) {
        wfColonOn = colonOn;
        int colonX = (W - 120) / 2 + 48;
        tft.setTextSize(4);
        tft.setTextColor(colonOn ? COL_FG() : COL_DARK(), COL_BG());
        tft.setCursor(colonX, WF_TIME_Y); tft.print(":");
      }
      wfLastSec = s;
    }
  }
}

static void wfDrawDate() {
  tft.fillRect(WF_BRACE_X + 1, WF_DATE_Y, WF_BRACE_W - 2, 10, COL_BG());
  tft.setTextSize(1); tft.setTextColor(COL_DIM(), COL_BG());

  if (!timeSvcIsReady()) {
    // No hardcoded fallback date — show placeholder until NTP syncs
    const char* msg = "--- -- --- ----";
    int tw = 15 * 6;
    tft.setCursor((W - tw) / 2, WF_DATE_Y); tft.print(msg);
    return;
  }

  struct tm t; timeSvcGetLocal(&t);
  char dbuf[20];
  const char* mon = (t.tm_mon >= 0 && t.tm_mon <= 11) ? MONTH_NAMES[t.tm_mon] : "???";
  const char* dow = (t.tm_wday >= 0 && t.tm_wday <= 6) ? DAY_NAMES[t.tm_wday] : "???";
  snprintf(dbuf, sizeof(dbuf), "%s %02d %s %d", dow, t.tm_mday, mon, t.tm_year + 1900);
  int tw = (int)strlen(dbuf) * 6;
  tft.setCursor((W - tw) / 2, WF_DATE_Y); tft.print(dbuf);
}

static void wfDrawStatusPills() {
  tft.fillRect(0, WF_STAT_Y, W, 16, COL_BG());
  int y = WF_STAT_Y;
  bool wifiOk = (WiFi.status() == WL_CONNECTED);
  char wifiLabel[14] = "----";
  if (wifiOk) { strncpy(wifiLabel, WiFi.SSID().c_str(), 8); wifiLabel[8] = '\0'; }
  drawStatPill(10,  y, 96, "WiFi", wifiLabel, wifiOk);
  char cartLabel[12] = "----";
  if (sdPresent && cartLoaded) strncpy(cartLabel, cartInfo.name, 11);
  else if (sdPresent)          strncpy(cartLabel, "SD OK",       11);
  drawStatPill(116, y, 96, "CART", cartLabel, sdPresent);
  drawStatPill(222, y, 88, "USB",
#if DUCKY_DRY_RUN
    "DRY",
#else
    "ARMED",
#endif
    true);
}

static void wfDrawSyncHint() {
  tft.fillRect(0, WF_SYNC_Y, W, 12, COL_BG());
  const char* msg = nullptr;
  static char ageBuf[40];

  NtpState ns = timeSvcState();
  if (ns == NTP_SYNCED) {
    if (timeSvcIsStale()) {
      uint32_t ageMin = timeSvcSyncAge() / 60;
      if (ageMin < 60)
        snprintf(ageBuf, sizeof(ageBuf), "NTP STALE - SYNCED %um AGO", (unsigned)ageMin);
      else
        snprintf(ageBuf, sizeof(ageBuf), "NTP STALE - SYNCED %uh AGO", (unsigned)(ageMin / 60));
      msg = ageBuf;
    }
    // Fresh sync → no hint needed, leave area blank
  } else if (ns == NTP_START || ns == NTP_WAIT) {
    msg = "SYNCING CLOCK...";
  } else {
    msg = "CLOCK NOT SYNCED - CONNECT WiFi";
  }

  if (msg) {
    tft.setTextSize(1); tft.setTextColor(COL_DARK(), COL_BG());
    int tw = (int)strlen(msg) * 6;
    if (tw > W - 8) tw = W - 8;
    tft.setCursor((W - tw) / 2, WF_SYNC_Y + 2); tft.print(msg);
  }
}

static void wfDrawWakeHint() {
  tft.fillRect(0, WF_WAKE_Y, W, 14, COL_BG());
  tft.setTextSize(1); tft.setTextColor(COL_DARK(), COL_BG());
  const char* hint = "ROTATE OR PRESS TO WAKE";
  int tw = (int)strlen(hint) * 6;
  tft.setCursor((W - tw) / 2, WF_WAKE_Y + 3); tft.print(hint);
}

// ─── Pill dirty-tracking ──────────────────────────────────────────────────────
static bool pillLastWifi = false, pillLastSd  = false,
            pillLastCart = false, pillLastNtp = false;
static bool pillDirty = true;

static bool pillStateChanged() {
  bool wifiNow = (WiFi.status() == WL_CONNECTED);
  bool sdNow   = sdPresent, cartNow = cartLoaded;
  bool ntpNow  = timeSvcIsReady();
  if (wifiNow != pillLastWifi || sdNow != pillLastSd ||
      cartNow != pillLastCart || ntpNow != pillLastNtp) {
    pillLastWifi = wifiNow; pillLastSd = sdNow;
    pillLastCart = cartNow; pillLastNtp = ntpNow;
    return true;
  }
  return false;
}

// ─── Performance instrumentation ─────────────────────────────────────────────
#if DEBUG_PERF
static uint32_t perfLoopStart  = 0;
static uint32_t perfMaxStallMs = 0;
static uint32_t perfStallWin   = 0;
static uint32_t perfFrameCount = 0;
static uint32_t perfFpsWin     = 0;
static float    perfFps        = 0.0f;

static void perfFrameBegin() { perfLoopStart = millis(); }

static void perfFrameEnd() {
  uint32_t now     = millis();
  uint32_t elapsed = now - perfLoopStart;
  perfFrameCount++;
  if (elapsed > perfMaxStallMs) perfMaxStallMs = elapsed;
  if (now - perfStallWin > 5000) { perfStallWin = now; perfMaxStallMs = 0; }
  if (now - perfFpsWin   > 1000) {
    perfFps = (float)perfFrameCount * 1000.0f / (float)(now - perfFpsWin);
    perfFrameCount = 0; perfFpsWin = now;
  }
}

static void wfDrawPerfOverlay() {
  char buf[40];
  snprintf(buf, sizeof(buf), "FPS:%.1f STL:%lums", perfFps, (unsigned long)perfMaxStallMs);
  tft.fillRect(0, 1, W, 12, COL_BG());
  tft.setTextSize(1); tft.setTextColor(COL_HILITE(), COL_BG());
  tft.setCursor(6, 2); tft.print(buf);
}
#endif  // DEBUG_PERF

// ─── Public API ───────────────────────────────────────────────────────────────

void watchfaceEnter() {
  wfLastHour = -1; wfLastMin = -1; wfLastSec  = -1;
  wfLastDay  = -1; wfColonOn = true;
  pillDirty  = true;
  wfLastNtpState = timeSvcState();

  // Kick NTP via time service (idempotent; no-op if already synced/in-progress)
  timeSvcTriggerSync();

  tft.fillScreen(COL_BG());
  wfDrawStatusStrip();
  drawWfBrackets(COL_DIM());
  wfDrawTime(true);
  wfDrawDate();
  wfDrawStatusPills();
  wfDrawSyncHint();
  wfDrawWakeHint();
}

void watchfaceTick() {
#if DEBUG_PERF
  perfFrameBegin();
#endif

  // ── Rate limiter: hard cap using centralised RENDER_ constants ─────────────
  static uint32_t lastFrameMs = 0;
  const uint32_t frameGap = (wfShowSeconds && !wfLowPower)
                              ? RENDER_FRAME_MS_SECS
                              : RENDER_FRAME_MS_MIN;
  const uint32_t now = millis();
  if (now - lastFrameMs < frameGap) return;

  // ── Stall detector: skip optional visuals if prev frame was late ───────────
  const uint32_t stallMs = now - lastFrameMs;
  const bool     stalled = (stallMs > RENDER_STALL_MS && lastFrameMs != 0);
  lastFrameMs = now;

  // ── NTP state-change detection — triggers UI updates ──────────────────────
  NtpState curNtp = timeSvcState();
  if (curNtp != wfLastNtpState) {
    wfLastNtpState = curNtp;
    wfDrawStatusStrip();
    wfDrawDate();
    wfDrawSyncHint();
    pillDirty  = true;
    // Force time region redraw so real time appears immediately after sync
    wfLastHour = -1; wfLastMin = -1; wfLastSec = -1; wfLastDay = -1;
  }

  // ── Time display ──────────────────────────────────────────────────────────
  wfDrawTime(false);

  // ── Midnight / day-change detection ──────────────────────────────────────
  if (timeSvcIsReady()) {
    struct tm t; timeSvcGetLocal(&t);
    if (t.tm_mday != wfLastDay) {
      wfLastDay = t.tm_mday;
      wfDrawDate();
    }
  }

  // ── Status pills (dirty only) ─────────────────────────────────────────────
  if (!stalled && (pillDirty || pillStateChanged())) {
    wfDrawStatusPills();
    pillDirty = false;
  }

  // ── Stale / sync-hint refresh (once per minute is plenty) ─────────────────
  static uint32_t lastHintMs = 0;
  if (millis() - lastHintMs > 60000UL) {
    lastHintMs = millis();
    wfDrawSyncHint();
    wfDrawStatusStrip();   // stale indicator may change
  }

  // ── Bracket pulse (skip if stalled to avoid extra SPI load) ──────────────
  static uint32_t lastBraceMs = 0;
  static uint8_t  bracePhase  = 0;
  if (!stalled && millis() - lastBraceMs > 1500) {
    lastBraceMs = millis();
    bracePhase  = (bracePhase + 1) & 1;
    drawWfBrackets(bracePhase ? COL_FG() : COL_DIM());
  }

#if DEBUG_PERF
  perfFrameEnd();
  wfDrawPerfOverlay();
#endif
}

void watchfaceExit() { /* caller handles full redraw */ }

// Called by external modules after a WiFi connect event.
// Delegates to time service (idempotent).
void watchfaceNtpSync() {
  timeSvcTriggerSync();
}
