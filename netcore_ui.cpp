#include "netcore_ui.h"
#include "netcore_apps.h"
#include "netcore_buttons.h"
#include "netcore_settings.h"
#include "netcore_time.h"    // single source of truth for clock
#include "svc_notify.h"      // notification service
#include "svc_statusbar.h"   // unified status bar service
#include "svc_anim.h"        // deterministic tweens
#include "motion_constants.h" // centralised motion language
#include "svc_perf.h"        // stall guard (optional visuals)

// ─────────────────────────────────────────────────────────────────────────────
// Clock state — removed from this module; now owned by netcore_time.
//
// getClockHour/Min/Sec() delegate to timeSvcMockHour/Min/Sec() so any
// existing callers continue to work without change.
//
// clockTick() is a no-op: timeSvcTick() in the main loop advances the clock.
// statusTick() just checks whether the minute changed and redraws the bar.
// ─────────────────────────────────────────────────────────────────────────────

int menuSel    = 0;
int menuScroll = 0;

// Battery placeholder (not yet wired to real ADC)
static int mockBat = 82;

int getClockHour() { return timeSvcMockHour(); }
int getClockMin()  { return timeSvcMockMin();  }
int getClockSec()  { return timeSvcMockSec();  }

// ── Menu geometry// ── Menu geometry ─────────────────────────────────────────────────────────────
static const int MENU_X       = 14;
static const int MENU_Y       = BODY_Y + 8;
static const int ROW_H        = 28;
static const int ICON_S       = 22;
static const int MENU_W       = W - 28;
static const int MENU_VISIBLE = 5;

// CRT scanline drift offset — crawls 1px every ~200ms for subtle CRT feel
static int scanlineDrift() {
  return (int)((millis() / 200) % 5);  // 0..4 px slow drift
}

static void drawScanlines(int y0, int y1, int step) {
  if (!fxScanlines) return;
  int drift = scanlineDrift();
  for (int y = y0 + drift; y < y1; y += step)
    tft.drawFastHLine(0, y, W, COL_DARK());
}

// ── Chrome ───────────────────────────────────────────────────────────────────

// drawStatusBarFrame() → see svc_statusbar.h (inline bridge to statusBarForceRedraw)

// drawStatusFieldsForce() → see svc_statusbar.h (inline bridge to statusBarInvalidate)

// clockTick() — no-op.  timeSvcTick() called in sketch.ino loop() handles
// clock advancement.  Kept in API so existing call sites compile unchanged.
void clockTick() {
  // intentionally empty — clock is owned by netcore_time / timeSvcTick()
}

// statusTick() — redraws the active-mode status bar only when the minute changes.
// Must NOT be called in watchface mode (watchface draws its own header).
void statusTick() {
  // Delegate to unified status bar service.
  // Service does its own dirty check — this call is cheap when nothing changed.
  statusBarTick();
}

void drawTitleBar(const char* title, const char* sub) {
  tft.fillRect(0, STATUS_H, W, TITLE_H, COL_BG());
  // Double rule — industrial CRT aesthetic
  tft.drawFastHLine(0, STATUS_H, W, COL_DARK());
  tft.drawFastHLine(0, STATUS_H + TITLE_H - 1, W, COL_DARK());
  // Bracketed title: [TITLE]
  tft.setCursor(8, STATUS_H + 8);
  tft.setTextSize(2);
  tft.setTextColor(COL_DARK(), COL_BG());
  tft.print('[');
  tft.setTextColor(COL_FG(), COL_BG());
  tft.print(title);
  tft.setTextColor(COL_DARK(), COL_BG());
  tft.print(']');
  // Sub-label right-aligned with dimmed brackets
  tft.setTextSize(1);
  int sw = (int)strlen(sub) * 6 + 12; // +12 for brackets
  tft.setCursor(W - 8 - sw, STATUS_H + 12);
  tft.setTextColor(COL_DARK(), COL_BG());
  tft.print('<');
  tft.setTextColor(COL_DIM(), COL_BG());
  tft.print(sub);
  tft.setTextColor(COL_DARK(), COL_BG());
  tft.print('>');
}

void drawFooter(const char* hint) {
  if (notifySvcIsActive()) return;   // banner takes priority
  tft.fillRect(0, H - FOOTER_H, W, FOOTER_H, COL_BG());
  tft.drawFastHLine(0, H - FOOTER_H, W, COL_DARK());
  tft.setTextSize(1);
  // Industrial prompt prefix
  tft.setTextColor(COL_DARK(), COL_BG());
  tft.setCursor(6, H - FOOTER_H + 8);
  tft.print("> ");
  tft.setTextColor(COL_DIM(), COL_BG());
  tft.print(hint);
}

void fillBody() {
  tft.fillRect(0, BODY_Y, W, H - BODY_Y - FOOTER_H, COL_BG());
}

// ── Icons ────────────────────────────────────────────────────────────────────

static void drawIcon(uint8_t idx, int x, int y, bool sel) {
  uint16_t frame = sel ? COL_BG()     : COL_DARK();
  uint16_t fill  = sel ? COL_HILITE() : COL_BG();
  uint16_t ink   = sel ? COL_BG()     : COL_DIM();

  tft.drawRect(x, y, ICON_S, ICON_S, frame);
  tft.fillRect(x + 1, y + 1, ICON_S - 2, ICON_S - 2, fill);

  int ix = x + 3, iy = y + 3, iw = ICON_S - 6, ih = ICON_S - 6;
  auto px = [&](int dx) { return ix + dx; };
  auto py = [&](int dy) { return iy + dy; };

  switch (idx) {
    case 0: {  // PING — signal bars
      int base = py(ih - 1);
      tft.drawFastVLine(px(1),  base - 3, 3, ink);
      tft.drawFastVLine(px(4),  base - 6, 6, ink);
      tft.drawFastVLine(px(7),  base - 9, 9, ink);
      tft.drawFastVLine(px(10), base - 4, 4, ink);
      tft.drawFastHLine(px(0),  base, iw, ink);
    } break;
    case 1: {  // WIFI — arcs
      int cx = px(iw / 2), cy = py(ih - 4);
      tft.drawCircle(cx, cy, 3, ink);
      tft.drawCircle(cx, cy, 6, ink);
      tft.drawCircle(cx, cy, 9, ink);
      int maskH = (y + ICON_S - 1) - cy;
      if (maskH > 0) tft.fillRect(x + 1, cy + 1, ICON_S - 2, maskH - 1, fill);
      tft.fillCircle(cx, cy, 2, ink);
    } break;
    case 2: {  // PORT — server rack
      tft.drawRect(px(1), py(3), iw - 2, ih - 6, ink);
      tft.drawFastVLine(px(4),  py(5), ih - 10, ink);
      tft.drawFastVLine(px(7),  py(5), ih - 10, ink);
      tft.drawFastVLine(px(10), py(5), ih - 10, ink);
    } break;
    case 3: {  // NOTES — notepad
      tft.drawRect(px(0), py(0), iw, ih, ink);
      tft.fillRect(px(0), py(0), iw, 3, ink);
      tft.drawFastHLine(px(2), py(5),  iw - 4, ink);
      tft.drawFastHLine(px(2), py(8),  iw - 4, ink);
      tft.drawFastHLine(px(2), py(11), iw - 7, ink);
    } break;
    case 4: {  // CARTRIDGE — isometric
      int dx = 4, dy = 3;
      int fx = px(0), fy = py(dy), fw = iw - dx, fh = ih - dy;
      tft.drawRect(fx, fy, fw, fh, ink);
      tft.drawLine(fx, fy, px(dx), py(0), ink);
      tft.drawFastHLine(px(dx), py(0), fw, ink);
      tft.drawLine(px(dx) + fw, py(0), fx + fw, fy, ink);
      tft.drawFastVLine(px(dx) + fw - 1, py(0), fh, ink);
      tft.drawLine(px(dx) + fw - 1, py(0) + fh - 1, fx + fw - 1, fy + fh - 1, ink);
      tft.drawFastVLine(fx + 2, fy + fh - 5, 4, ink);
      tft.drawFastVLine(fx + 5, fy + fh - 5, 4, ink);
      tft.drawFastVLine(fx + 8, fy + fh - 5, 4, ink);
      tft.drawFastHLine(fx + 1, fy + 3, fw - 2, ink);
    } break;
    case 5: {  // INJECT — lightning bolt
      tft.drawLine(px(8), py(0), px(4), py(6), ink);
      tft.drawLine(px(4), py(6), px(7), py(6), ink);
      tft.drawLine(px(7), py(6), px(3), py(ih), ink);
      tft.drawLine(px(8), py(0), px(9), py(0), ink);
      tft.drawLine(px(3), py(ih), px(4), py(ih), ink);
    } break;
    case 6: {  // SETTINGS — gear
      int cx = px(iw / 2), cy = py(ih / 2);
      tft.drawCircle(cx, cy, 5, ink);
      tft.fillRect(cx - 2, cy - 2, 5, 5, fill);
      tft.drawCircle(cx, cy, 2, ink);
      tft.fillRect(cx - 1, cy - 8, 3, 4, ink);
      tft.fillRect(cx - 1, cy + 5, 3, 4, ink);
      tft.fillRect(cx - 8, cy - 1, 4, 3, ink);
      tft.fillRect(cx + 5, cy - 1, 4, 3, ink);
    } break;
    case 7: {  // SYSTEM TOOLS — chip/IC
      int cx = px(iw / 2), cy = py(ih / 2);
      tft.drawRect(px(3), py(2), iw - 6, ih - 4, ink);
      for (int p = 0; p < 3; p++) {
        int py2 = py(3 + p * 4);
        tft.drawFastHLine(px(0),        py2, 3, ink);
        tft.drawFastHLine(px(iw - 3),   py2, 3, ink);
      }
      tft.fillRect(cx - 1, cy - 1, 3, 3, ink);
    } break;
    default: {
      tft.setTextSize(1);
      tft.setTextColor(ink, fill);
      tft.setCursor(px(5), py(4));
      tft.print(idx + 1);
    } break;
  }
}

// ── Menu ─────────────────────────────────────────────────────────────────────

void drawMenuRowBase(int i, bool sel) {
  int visIdx = i - menuScroll;
  if (visIdx < 0 || visIdx >= MENU_VISIBLE) return;
  int x = MENU_X, y = MENU_Y + visIdx * ROW_H, w = MENU_W, h = ROW_H - 2;

  if (sel) {
    tft.fillRect(x, y, w, h, COL_HILITE());
    tft.setTextColor(COL_BG(), COL_HILITE());
  } else {
    tft.fillRect(x, y, w, h, COL_BG());
    tft.setTextColor(COL_FG(), COL_BG());
    tft.drawFastHLine(x, y + h - 1, w, COL_DARK());
  }

  drawIcon((uint8_t)i, x + 8, y + 2, sel);

  tft.setTextSize(2);
  tft.setCursor(x + 8 + ICON_S + 10, y + 4);
  tft.print(apps[i].name);

  tft.setTextSize(1);
  tft.setTextColor(sel ? COL_BG() : COL_DIM(), sel ? COL_HILITE() : COL_BG());
  int sw = (int)strlen(apps[i].sub) * 6;
  tft.setCursor(x + w - 10 - sw, y + 9);
  tft.print(apps[i].sub);
}

void renderMenuFull() {
  tft.fillScreen(COL_BG());
  drawStatusBarFrame();
  drawStatusFieldsForce();
  drawTitleBar("NETCORE", "READY");
  fillBody();
  drawScanlines(BODY_Y + 2, H - FOOTER_H - 2, 5);

  for (int i = menuScroll; i < menuScroll + MENU_VISIBLE && i < APP_COUNT; i++)
    drawMenuRowBase(i, i == menuSel);

  if (APP_COUNT > MENU_VISIBLE) {
    tft.setTextSize(1);
    tft.setTextColor(COL_DIM(), COL_BG());
    if (menuScroll > 0) {
      tft.setCursor(W - 14, MENU_Y + 2); tft.print("^");
    }
    if (menuScroll + MENU_VISIBLE < APP_COUNT) {
      tft.setCursor(W - 14, MENU_Y + MENU_VISIBLE * ROW_H - 12); tft.print("v");
    }
  }

  drawFooter("ROTATE move  CLICK open  BACK idle");
}

// ── Hover FX — corner bracket breathing ──────────────────────────────────────

void menuFxTick() {
  if (!fxShimmer) return;
  if (mode != MODE_MENU) return;

  // Cap FX update rate (keeps drawing cheap)
  static uint32_t lastFrameMs = 0;
  if (millis() - lastFrameMs < 50) return; // ~20 fps
  lastFrameMs = millis();

  // Restart pulse on selection change + fire select-pop micro-interaction
  static int lastSel = -1;
  if (lastSel != menuSel) {
    lastSel = menuSel;
    animCancel(AT_MENU, AP_PULSE);
    animSetQ(AT_MENU, AP_PULSE, 0);
    animTween(AT_MENU, AP_PULSE, 0, Q16_FROM_I(1), MOTION_DUR_BREATHE, MOTION_EASE_BREATHE,
              ANIM_F_REPLACE | ANIM_F_LOOP | ANIM_F_PINGPONG);

    // Select-pop: one-shot 1→0 fade for highlight flash
    animTween(AT_MICRO, AP_SEL_PULSE, Q16_ONE, 0, MOTION_DUR_FAST, MOTION_EASE_BOUNCE,
              ANIM_F_REPLACE);
  }

  // Ensure cursor tween exists
  if (animGetQ(AT_MENU, AP_CURSOR_X) == 0 && animActiveCount() == 0) {
    // (First boot / after animSvcInit). Start menu FX baseline tweens.
    animTween(AT_MENU, AP_CURSOR_X, 0, Q16_FROM_I(MENU_W - 6), MOTION_DUR_SWEEP, MOTION_EASE_SNAP,
              ANIM_F_REPLACE | ANIM_F_LOOP | ANIM_F_PINGPONG);
    animTween(AT_MENU, AP_PULSE, 0, Q16_FROM_I(1), MOTION_DUR_BREATHE, MOTION_EASE_BREATHE,
              ANIM_F_REPLACE | ANIM_F_LOOP | ANIM_F_PINGPONG);
  }

  int visIdx = menuSel - menuScroll;
  if (visIdx < 0 || visIdx >= MENU_VISIBLE) return;

  int x = MENU_X, y = MENU_Y + visIdx * ROW_H, w = MENU_W, h = ROW_H - 2;

  // Optional micro-jitter + scan-sweep only if perf budget is healthy
  bool perfOk = perfGetMaxStallMs() < 200;

  // Pulse drives bracket brightness
  int32_t pQ = animGetQ(AT_MENU, AP_PULSE);
  // Map 0..1 to three discrete ink levels
  uint16_t bc = (pQ > (int32_t)(0.66f * 65536.0f)) ? COL_FG()
              : (pQ > (int32_t)(0.33f * 65536.0f)) ? COL_DIM()
              : COL_DARK();

  int ARM  = (pQ > (int32_t)(0.6f * 65536.0f)) ? 11 : 7;
  int EARM = 13;

  // Draw corner brackets (terminal hover)
  tft.drawFastHLine(x,           y,           ARM, bc);
  tft.drawFastVLine(x,           y,           ARM, bc);
  tft.drawFastHLine(x + w - ARM, y,           ARM, bc);
  tft.drawFastVLine(x + w - 1,   y,           ARM, bc);
  tft.drawFastHLine(x,           y + h - 1,   ARM, bc);
  tft.drawFastVLine(x,           y + h - ARM, ARM, bc);
  tft.drawFastHLine(x + w - ARM, y + h - 1,   ARM, bc);
  tft.drawFastVLine(x + w - 1,   y + h - ARM, ARM, bc);

  // Select-pop flash: bright inner border that fades on selection change
  if (animIsActive(AT_MICRO, AP_SEL_PULSE)) {
    int32_t sp = animGetQ(AT_MICRO, AP_SEL_PULSE);
    // Only draw when pulse is above threshold (visible)
    if (sp > (int32_t)(0.15f * 65536.0f)) {
      uint16_t popCol = (sp > (int32_t)(0.5f * 65536.0f)) ? COL_FG() : COL_DIM();
      tft.drawRect(x + 1, y + 1, w - 2, h - 2, popCol);
    }
  }

  // Scan-sweep cursor (alien terminal vibe)
  static int lastCursor = -9999;
  int cx = animGetI(AT_MENU, AP_CURSOR_X);
  if (perfOk) {
    // Micro glitch jitter 0/1 px sometimes
    if ((millis() & 0x1FF) < 12) cx += 1;
  }
  cx = x + 2 + cx;
  if (cx < x + 2) cx = x + 2;
  if (cx > x + w - 4) cx = x + w - 4;

  // Dirty: clear old cursor band then draw new
  if (lastCursor != -9999) {
    int ox = lastCursor;
    tft.fillRect(ox, y + 2, 3, h - 4, COL_HILITE());
  }
  // Cursor: bright + dim trailing edge
  tft.fillRect(cx,     y + 2, 2, h - 4, COL_FG());
  tft.fillRect(cx - 2, y + 2, 1, h - 4, COL_DIM());
  lastCursor = cx;
}

// ── App enter/exit slide transitions ──────────────────────────────────────────
// Slide uses body band only (BODY_Y .. H-FOOTER_H). No full-screen redraw.
// State: TRANS_NONE → TRANS_ENTER (slide body in from right)
//        TRANS_EXIT (slide body out to right) → completion callback

enum TransState : uint8_t { TRANS_NONE = 0, TRANS_ENTER, TRANS_EXIT };

static TransState s_transState   = TRANS_NONE;
static int        s_transAppIdx  = -1;
static int        s_transPrevOff = 0;  // previous slide offset for dirty rect
static int        s_transPrevBarX = -999; // previous leading edge bar X

static const int BODY_H = H - BODY_Y - FOOTER_H;

bool transitionActive() { return s_transState != TRANS_NONE; }

static void transStartEnter(int appIdx) {
  s_transState  = TRANS_ENTER;
  s_transAppIdx = appIdx;
  s_transPrevOff = W;  // start fully off-screen right
  s_transPrevBarX = -999;

  // Clear body for slide
  tft.fillRect(0, BODY_Y, W, BODY_H, COL_BG());

  // Tween: slide from W (off-screen right) to 0 (final position)
  animCancelTarget(AT_TRANS);
  animSetQ(AT_TRANS, AP_SLIDE_X, Q16_FROM_I(W));
  animTween(AT_TRANS, AP_SLIDE_X, Q16_FROM_I(W), 0,
            MOTION_DUR_SLIDE, MOTION_EASE_SLIDE, ANIM_F_REPLACE);
}

static void transStartExit() {
  s_transState   = TRANS_EXIT;
  s_transPrevOff = 0;
  s_transPrevBarX = -999;

  // Tween: slide from 0 to W (off-screen right)
  animCancelTarget(AT_TRANS);
  animSetQ(AT_TRANS, AP_SLIDE_X, 0);
  animTween(AT_TRANS, AP_SLIDE_X, 0, Q16_FROM_I(W),
            MOTION_DUR_NORMAL, MOTION_EASE_EXIT, ANIM_F_REPLACE);
}

bool transitionTick() {
  if (s_transState == TRANS_NONE) return false;

  int off = animGetI(AT_TRANS, AP_SLIDE_X);
  if (off < 0) off = 0;
  if (off > W) off = W;

  bool done = !animIsActive(AT_TRANS, AP_SLIDE_X);

  if (s_transState == TRANS_ENTER) {
    // Dirty rect: clear the band between prev and current offset
    if (off != s_transPrevOff) {
      int clearX = off;
      int clearW = (s_transPrevOff > off) ? (s_transPrevOff - off) : 1;
      if (clearX + clearW > W) clearW = W - clearX;
      tft.fillRect(clearX, BODY_Y, clearW, BODY_H, COL_BG());
    }
    s_transPrevOff = off;

    if (done) {
      // Transition complete — let the app draw its full enter screen
      s_transState = TRANS_NONE;
      mode = MODE_APP;
      runningApp = s_transAppIdx;
      apps[s_transAppIdx].enter();
      return false;
    }
    // Draw a sliding "curtain" indicator: vertical bar at leading edge
    int barX = off - 2;
    // Clear previous bar to prevent stripe artifacts
    if (s_transPrevBarX != -999 && s_transPrevBarX >= 0 && s_transPrevBarX < W) {
      tft.fillRect(s_transPrevBarX, BODY_Y, 2, BODY_H, COL_BG());
    }
    if (barX >= 0 && barX < W) {
      tft.fillRect(barX, BODY_Y, 2, BODY_H, COL_HILITE());
    }
    s_transPrevBarX = barX;
    return true;
  }

  if (s_transState == TRANS_EXIT) {
    // Dirty rect: clear the band the content just vacated
    if (off != s_transPrevOff) {
      int clearX = s_transPrevOff;
      int clearW = (off > s_transPrevOff) ? (off - s_transPrevOff) : 1;
      if (clearX < 0) clearX = 0;
      if (clearX + clearW > W) clearW = W - clearX;
      tft.fillRect(clearX, BODY_Y, clearW, BODY_H, COL_BG());
    }
    s_transPrevOff = off;

    if (done) {
      // Exit complete — return to menu
      s_transState = TRANS_NONE;
      runningApp = -1;
      mode = MODE_MENU;
      renderMenuFull();
      return false;
    }
    // Leading edge bar sliding out
    int barX = off - 2;
    // Clear previous bar to prevent stripe artifacts
    if (s_transPrevBarX != -999 && s_transPrevBarX >= 0 && s_transPrevBarX < W) {
      tft.fillRect(s_transPrevBarX, BODY_Y, 2, BODY_H, COL_BG());
    }
    if (barX >= 0 && barX < W) {
      tft.fillRect(barX, BODY_Y, 2, BODY_H, COL_HILITE());
    }
    s_transPrevBarX = barX;
    return true;
  }

  return false;
}

void exitAppTransition() {
  // Start slide-out; caller must have already called app.exit()
  transStartExit();
}

void launchApp(int idx) {
  // Start slide-in transition; app enter() is called when slide completes
  transStartEnter(idx);
}
