#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// svc_statusbar.h  —  Unified Status Bar Service
//
// Purpose  : Single owner of the STATUS_H (top 20px) strip.
//            Apps call statusBarInvalidate() when they want a redraw.
//            statusBarTick() does dirty-only redraw — never every frame.
//            No app or module should call drawStatusBarFrame() or
//            drawStatusFieldsForce() directly after this service exists.
//
// Public API ──────────────────────────────────────────────────────────────────
//   statusBarInit()          — call once in setup(), after services init
//   statusBarTick()          — call every loop() while in active mode
//   statusBarInvalidate()    — mark dirty; will redraw on next tick
//   statusBarInvalidateFooter() — footer region dirty (used by notify dismiss)
//
//   statusBarForceRedraw()   — for enter() calls that need it immediately
//                              (replaces drawStatusBarFrame+ForceFields combo)
//
// Legacy bridge (drawStatusBarFrame / drawStatusFieldsForce) ──────────────────
//   These names are still used in ~28 call sites in netcore_apps.cpp.
//   They are now defined as inline wrappers that call statusBarForceRedraw()
//   and statusBarInvalidate(), so all existing call sites compile unchanged.
// ─────────────────────────────────────────────────────────────────────────────
#include "netcore_config.h"

// ── Lifecycle ─────────────────────────────────────────────────────────────────
void statusBarInit();
void statusBarTick();

// ── Control ───────────────────────────────────────────────────────────────────
void statusBarInvalidate();           // request redraw next tick
void statusBarInvalidateFooter();     // footer dirty (post-banner dismiss)
void statusBarForceRedraw();          // draw immediately (for enter() calls)

// ── Legacy bridge — keep all existing call sites compiling ───────────────────
// drawStatusBarFrame() + drawStatusFieldsForce() used to live in netcore_ui.cpp.
// They now forward here so the 28 callers in netcore_apps.cpp don't need to change.
inline void drawStatusBarFrame()    { statusBarForceRedraw(); }
inline void drawStatusFieldsForce() { statusBarInvalidate(); }
