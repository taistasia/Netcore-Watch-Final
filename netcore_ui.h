#pragma once
#include "netcore_config.h"
#include "netcore_theme.h"
#include "svc_notify.h"    // notifyPost / notifTick shims
#include "svc_statusbar.h" // drawStatusBarFrame / drawStatusFieldsForce shims

// ── Core chrome ───────────────────────────────────────────────────────────────
// drawStatusBarFrame()    →  svc_statusbar.h (inline → statusBarForceRedraw)
// drawStatusFieldsForce() →  svc_statusbar.h (inline → statusBarInvalidate)
void clockTick();   // advance mock clock only, no screen draw (use in watchface mode)
void statusTick();  // advance clock + redraw status bar (use only in active mode)
void drawTitleBar(const char* title, const char* sub);
void drawFooter(const char* hint);
void fillBody();

// ── Menu ─────────────────────────────────────────────────────────────────────
extern int menuSel;
extern int menuScroll;
void renderMenuFull();
void drawMenuRowBase(int i, bool sel);
void menuFxTick();

// ── App launch + transitions ─────────────────────────────────────────────────
void launchApp(int idx);
void exitAppTransition();  // start slide-out; call app.exit() first
bool transitionTick();     // returns true while transition is in progress
bool transitionActive();   // true if slide anim is running

// ── Clock access (for watchface) ─────────────────────────────────────────────
int getClockHour();
int getClockMin();
int getClockSec();   // seconds derived from millis() within current minute

// ── Notification system ───────────────────────────────────────────────────────
// Post a short message; shows as banner in footer area for 3s.
// Notification service — see svc_notify.h for full API + ring buffer.
// notifyPost / notifTick / notifIsActive / notifGetCount / notifGetRecent
// are inline shims in svc_notify.h; include that header for access.
