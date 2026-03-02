// ─────────────────────────────────────────────────────────────────────────────
// svc_notify.cpp  —  Notification Manager (single definition unit)
//
// Rule: This is the ONLY .cpp that defines notifySvcPost / notifySvcTick.
//       netcore_ui.cpp must NOT also define notifyPost/notifTick — those
//       have been removed from netcore_ui.cpp and are now inline shims in
//       svc_notify.h that forward here.
// ─────────────────────────────────────────────────────────────────────────────
#include "svc_notify.h"
#include "netcore_config.h"   // tft, COL_* macros (via theme), W, H, FOOTER_H

// svc_statusbar.h is included for statusBarInvalidate() — banner overlay
// temporarily "covers" the footer, and on dismiss the status bar must know
// to redraw that region cleanly.
// Forward-declare only to avoid circular header dependency:
void statusBarInvalidateFooter();   // defined in svc_statusbar.cpp

// ── Ring buffer ───────────────────────────────────────────────────────────────
static NotifyEntry _ring[NOTIFY_RING_SIZE];
static int         _head      = 0;   // next write position
static int         _count     = 0;

// ── Banner state ─────────────────────────────────────────────────────────────
static bool     _bannerActive = false;
static uint32_t _bannerStartMs = 0;
static uint32_t _bannerTtl    = 0;
static char     _bannerTitle[NOTIFY_MSG_LEN] = "";
static char     _bannerBody[NOTIFY_MSG_LEN]  = "";
static NotifyType _bannerType = NOTIFY_INFO;

// ── Colour helpers ────────────────────────────────────────────────────────────
// Colours depend on netcore_theme — resolved at runtime.
extern Adafruit_ILI9341 tft;
// We include theme header for COL_* access.
#include "netcore_theme.h"

static uint16_t _bannerBg(NotifyType t) {
  switch (t) {
    case NOTIFY_OK:    return 0x07E0;  // pure green (readable on any theme)
    case NOTIFY_WARN:  return 0xFFE0;  // yellow
    case NOTIFY_ERROR: return 0xF800;  // red
    default:           return COL_HILITE();
  }
}
static uint16_t _bannerFg(NotifyType t) {
  // All banner types except INFO use black text for legibility
  return (t == NOTIFY_INFO) ? COL_BG() : 0x0000;
}

// ── Banner draw / undraw ──────────────────────────────────────────────────────

static void _drawBanner() {
  int y = H - FOOTER_H;
  uint16_t bg = _bannerBg(_bannerType);
  uint16_t fg = _bannerFg(_bannerType);
  tft.fillRect(0, y, W, FOOTER_H, bg);

  tft.setTextSize(1);
  tft.setTextColor(fg, bg);

  // If we have a non-empty title, prefix it
  char display[NOTIFY_MSG_LEN * 2 + 4];
  if (_bannerTitle[0] != '\0' &&
      strncmp(_bannerTitle, _bannerBody, NOTIFY_MSG_LEN) != 0) {
    snprintf(display, sizeof(display), "%s: %s", _bannerTitle, _bannerBody);
  } else {
    strncpy(display, _bannerBody, sizeof(display) - 1);
    display[sizeof(display) - 1] = '\0';
  }

  // Truncate to fit on screen
  int maxChars = (W - 12) / 6;
  if ((int)strlen(display) > maxChars) display[maxChars] = '\0';

  int tw = (int)strlen(display) * 6;
  tft.setCursor((W - tw) / 2, y + 8);
  tft.print(display);
}

static void _clearBanner() {
  // Clear footer region; status bar will redraw the footer line on next tick
  tft.fillRect(0, H - FOOTER_H, W, FOOTER_H, COL_BG());
  tft.drawFastHLine(0, H - FOOTER_H, W, COL_DARK());
  // Tell status bar service that footer region is dirty
  statusBarInvalidateFooter();
}

// ── Public API ────────────────────────────────────────────────────────────────

void notifySvcInit() {
  _head  = 0;
  _count = 0;
  _bannerActive = false;
}

void notifySvcPost(NotifyType type, const char* title, const char* body, uint32_t ttlMs) {
  // Write to ring buffer
  NotifyEntry& e = _ring[_head];
  e.type  = type;
  e.ttlMs = ttlMs;
  strncpy(e.title, title ? title : "", NOTIFY_MSG_LEN - 1);
  e.title[NOTIFY_MSG_LEN - 1] = '\0';
  strncpy(e.body,  body  ? body  : "", NOTIFY_MSG_LEN - 1);
  e.body[NOTIFY_MSG_LEN - 1] = '\0';

  _head = (_head + 1) % NOTIFY_RING_SIZE;
  if (_count < NOTIFY_RING_SIZE) _count++;

  // Activate / replace active banner
  _bannerType    = type;
  strncpy(_bannerTitle, e.title, NOTIFY_MSG_LEN - 1);
  _bannerTitle[NOTIFY_MSG_LEN - 1] = '\0';
  strncpy(_bannerBody,  e.body,  NOTIFY_MSG_LEN - 1);
  _bannerBody[NOTIFY_MSG_LEN - 1]  = '\0';
  _bannerTtl     = ttlMs;
  _bannerStartMs = millis();
  _bannerActive  = true;
  _drawBanner();
}

void notifySvcPostSimple(const char* msg) {
  notifySvcPost(NOTIFY_INFO, "", msg, NOTIFY_TTL_DEFAULT);
}

void notifySvcTick() {
  if (!_bannerActive) return;
  if (millis() - _bannerStartMs >= _bannerTtl) {
    _bannerActive = false;
    _clearBanner();
  }
}

bool notifySvcIsActive()   { return _bannerActive; }
int  notifySvcCount()      { return _count; }

const NotifyEntry* notifySvcGet(int i) {
  if (i < 0 || i >= _count) return nullptr;
  int idx = ((_head - 1 - i) + NOTIFY_RING_SIZE) % NOTIFY_RING_SIZE;
  return &_ring[idx];
}
