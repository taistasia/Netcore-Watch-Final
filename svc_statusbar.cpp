// ─────────────────────────────────────────────────────────────────────────────
// svc_statusbar.cpp  —  Unified Status Bar
//
// Owns the top STATUS_H (20px) strip.
// Dirty-redraws only when state changes or statusBarInvalidate() is called.
// Pulls state from: time service, WiFi service, SD globals, HID define.
// ─────────────────────────────────────────────────────────────────────────────
#include "svc_statusbar.h"
#include "netcore_theme.h"
#include "netcore_time.h"     // timeSvcMockHour/Min, timeSvcState, timeSvcIsStale
#include "svc_wifi.h"         // wifiSvcGetState, wifiSvcGetSSID
#include "netcore_sd.h"       // sdPresent, cartLoaded
#include "netcore_ducky.h"    // DUCKY_DRY_RUN

extern Adafruit_ILI9341 tft;

// ── Internal state snapshot for dirty detection ───────────────────────────────
static int        _lastHour   = -1;
static int        _lastMin    = -1;
static bool       _lastWifi   = false;
static bool       _lastSd     = false;
static NtpState   _lastNtp    = NTP_IDLE;
static bool       _lastStale  = false;
static bool       _dirty      = true;    // start dirty so first tick draws
static bool       _footerDirty = false;

// ── Draw ──────────────────────────────────────────────────────────────────────

static void _draw() {
  // ── Background + separator ───────────────────────────────────────────────
  tft.fillRect(0, 0, W, STATUS_H, COL_BG());
  tft.drawFastHLine(0, STATUS_H - 1, W, COL_DARK());

  tft.setTextSize(1);

  // ── Left: "NETCORE" brand ─────────────────────────────────────────────────
  tft.setTextColor(COL_DIM(), COL_BG());
  tft.setCursor(6, 6);
  tft.print("NETCORE");

  // ── Time HH:MM ────────────────────────────────────────────────────────────
  struct tm t;
  timeSvcGetLocal(&t);
  char timeBuf[6];
  snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", t.tm_hour, t.tm_min);
  // centre the time
  int tx = (W - 5*6) / 2;
  tft.setTextColor(timeSvcIsReady() ? COL_FG() : COL_DIM(), COL_BG());
  tft.setCursor(tx, 6);
  tft.print(timeBuf);

  // ── Right side: icons (WiFi, SD, NTP, HID) — 4 × 6px label columns ───────
  // Layout right-to-left: [HID][NTP][SD][WiFi]   spaced 6px each
  // Each indicator is 3–4 chars at textSize 1.

  // Right edge reference
  int rx = W - 4;

  // HID indicator
#if DUCKY_DRY_RUN
  const char* hidStr = "DRY";
  uint16_t hidCol = COL_DARK();
#else
  const char* hidStr = "HID";
  uint16_t hidCol = COL_FG();
#endif
  int hidW = (int)strlen(hidStr) * 6;
  rx -= hidW;
  tft.setTextColor(hidCol, COL_BG());
  tft.setCursor(rx, 6);
  tft.print(hidStr);
  rx -= 4;

  // SD indicator
  const char* sdStr = sdPresent ? "SD" : "--";
  uint16_t sdCol = sdPresent ? (cartLoaded ? COL_FG() : COL_DIM()) : COL_DARK();
  int sdW = (int)strlen(sdStr) * 6;
  rx -= sdW;
  tft.setTextColor(sdCol, COL_BG());
  tft.setCursor(rx, 6);
  tft.print(sdStr);
  rx -= 4;

  // NTP indicator
  NtpState ntp = timeSvcState();
  const char* ntpStr;
  uint16_t ntpCol;
  if (ntp == NTP_SYNCED) {
    if (timeSvcIsStale()) { ntpStr = "OLD"; ntpCol = COL_DARK(); }
    else                  { ntpStr = "NTP"; ntpCol = COL_FG(); }
  } else if (ntp == NTP_WAIT || ntp == NTP_START) {
    ntpStr = "..."; ntpCol = COL_DIM();
  } else {
    ntpStr = "---"; ntpCol = COL_DARK();
  }
  int ntpW = (int)strlen(ntpStr) * 6;
  rx -= ntpW;
  tft.setTextColor(ntpCol, COL_BG());
  tft.setCursor(rx, 6);
  tft.print(ntpStr);
  rx -= 4;

  // WiFi indicator
  WifiSvcState ws = wifiSvcGetState();
  const char* wfStr;
  uint16_t wfCol;
  switch (ws) {
    case WSVC_CONNECTED:   wfStr = "WiFi"; wfCol = COL_FG();   break;
    case WSVC_CONNECTING:
    case WSVC_SCANNING:    wfStr = "...";  wfCol = COL_DIM();  break;
    case WSVC_RETRY_WAIT:  wfStr = "RTY";  wfCol = COL_DARK(); break;
    case WSVC_FAILED:      wfStr = "ERR";  wfCol = 0xF800;     break;
    default:               wfStr = "----"; wfCol = COL_DARK(); break;
  }
  int wfW = (int)strlen(wfStr) * 6;
  rx -= wfW;
  tft.setTextColor(wfCol, COL_BG());
  tft.setCursor(rx, 6);
  tft.print(wfStr);

  // ── Update snapshot ───────────────────────────────────────────────────────
  _lastHour  = t.tm_hour;
  _lastMin   = t.tm_min;
  _lastWifi  = (ws == WSVC_CONNECTED);
  _lastSd    = sdPresent;
  _lastNtp   = ntp;
  _lastStale = timeSvcIsStale();
  _dirty     = false;
}

static bool _stateChanged() {
  struct tm t; timeSvcGetLocal(&t);
  return (t.tm_hour   != _lastHour  ||
          t.tm_min    != _lastMin   ||
          (wifiSvcGetState() == WSVC_CONNECTED) != _lastWifi ||
          sdPresent        != _lastSd    ||
          timeSvcState()   != _lastNtp   ||
          timeSvcIsStale() != _lastStale);
}

// ── Public API ────────────────────────────────────────────────────────────────

void statusBarInit() {
  _dirty = true;
}

void statusBarTick() {
  if (_dirty || _stateChanged()) {
    _draw();
  }
}

void statusBarInvalidate() {
  _dirty = true;
}

void statusBarInvalidateFooter() {
  // Called by notification service when banner dismisses.
  // The current screen's app is responsible for its own footer;
  // we just note that any cached footer state is gone.
  // For now this is a no-op at the status bar level — apps redraw their
  // own footer on next interaction.  We set _dirty so the next statusBarTick
  // also redraws the status strip cleanly.
  _dirty = true;
}

void statusBarForceRedraw() {
  _draw();
}
