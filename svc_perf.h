#pragma once
// ═════════════════════════════════════════════════════════════════════════════
// svc_perf.h  —  NETCORE Performance Rules + Perf Guard
//
// PURPOSE
//   Single authoritative location for all performance contracts.
//   Every constant, rule comment, and perf-guard API lives here.
//   If you want to change a frame cap, change it HERE — nowhere else.
//
// ── HOW TO USE ───────────────────────────────────────────────────────────────
//   #include "svc_perf.h"
//
//   // In tick/render functions:
//   if (perfShouldSkipOptionals()) { /* skip scanlines, glow, etc */ }
//   perfDrawBegin();   // call before any tft draw
//   ...draw...
//   perfDrawEnd();     // call after draw completes
//
//   // In loop():
//   uint32_t loopStart = perfLoopBegin();
//   ... all work ...
//   perfLoopEnd(loopStart);
//
// ── DIAGNOSTICS ACCESS ───────────────────────────────────────────────────────
//   perfGetMaxStallMs()  — worst loop stall in last window (ms)
//   perfGetFps()         — estimated frames/sec (draw calls per second)
//   perfGetDrawCount()   — total draw calls since boot
//   perfGetLoopCount()   — total loop() calls since boot
//   perfGetCpuPercent()  — rough CPU busy estimate (0–100)
// ═════════════════════════════════════════════════════════════════════════════
#include "netcore_config.h"
#include <stdint.h>

// ─────────────────────────────────────────────────────────────────────────────
// SECTION A: HARD RENDER RULES
// These are enforced by convention + code review. Violating them is a bug.
// ─────────────────────────────────────────────────────────────────────────────

// ── Rule R1: Full-screen redraws ──────────────────────────────────────────────
// fillScreen() / tft.fillScreen() is ONLY allowed inside:
//   - An app's enter() function
//   - A top-level mode switch (MODE_MENU → MODE_APP)
//   - watchfaceEnter() / watchfaceExit()
// VIOLATION: calling fillScreen() inside tick(), statusBarTick(), or any
// recurring render function is a regression. Use dirty-rect fill instead.

// ── Rule R2: Watchface frame caps ─────────────────────────────────────────────
// Watchface default (no seconds): redraw ONLY when minute changes.
// Watchface with seconds ON: max PERF_WF_SECS_FPS frames per second.
// The time digit rect is the ONLY area that changes; status bar and brackets
// are redrawn separately only on their own dirty events.
#define PERF_WF_SECS_FPS       5       // max fps in seconds-on mode
#define PERF_WF_SECS_FRAME_MS  (1000UL / PERF_WF_SECS_FPS)   // 200ms

// ── Rule R3: App mode frame cap ───────────────────────────────────────────────
// Apps that do continuous animation must gate behind this check.
// Apps that only draw on user input (list navigation) are exempt.
#define PERF_APP_MAX_FPS       20
#define PERF_APP_FRAME_MS      (1000UL / PERF_APP_MAX_FPS)    // 50ms

// ── Rule R4: Status bar dirty-only ────────────────────────────────────────────
// statusBarTick() MUST check _stateChanged() or _dirty before drawing.
// No timer-based redraw. statusBarInvalidate() is the only trigger.

// ── Rule R5: No drawing into status/header area ───────────────────────────────
// Any tft draw call with y < STATUS_H is forbidden outside svc_statusbar.cpp.
// EXCEPTION: tft.fillScreen() is already banned by R1 for other reasons.
#define PERF_STATUS_H_GUARD    STATUS_H   // y < this is OFF LIMITS to non-statusbar code

// ─────────────────────────────────────────────────────────────────────────────
// SECTION B: HARD BLOCKING RULES
// ─────────────────────────────────────────────────────────────────────────────

// ── Rule B1: No delay() in tick paths ─────────────────────────────────────────
// delay(0) or yield() are allowed (let FreeRTOS breathe).
// delay(N) for N > 0 is ONLY allowed in:
//   - setup() (one-time hardware init)
//   - wifiSvcConnect() (one 50ms radio reset — documented exception)
//   - runBootScreen() (boot animation)
//   - HID USB init (delay(1000) on first USB arm)
// RULE: if a delay() appears in a function called from loop() and is not in
// the exceptions above, it is a regression.

// ── Rule B2: getLocalTime() must use timeout=0 in tick paths ──────────────────
// getLocalTime(&t)           ← BANNED in tick paths (blocks up to 5000ms default)
// getLocalTime(&t, 0)        ← REQUIRED (returns immediately with whatever is in RTC)
// getLocalTime(&t, 200)      ← ALLOWED only in one-shot setup paths
// Enforcement: timeSvcGetLocal() always calls getLocalTime(&t, 0) internally.
// Never bypass timeSvcGetLocal() with a raw getLocalTime() call.

// ── Rule B3: No synchronous WiFi scan in tick paths ───────────────────────────
// WiFi.scanNetworks()           ← BANNED in tick/loop paths
// WiFi.scanNetworks(true)       ← ALLOWED (async mode, returns WIFI_SCAN_RUNNING)
// svc_tasks: TASK_WIFI_SCAN     ← preferred entry point
// wifiSvcStartScan()            ← calls WiFi.scanNetworks(true) correctly

// ── Rule B4: No SD directory walk in tick paths ───────────────────────────────
// SD.open() / root.openNext() in loop() ← BANNED
// All SD scanning must go through: taskRun(TASK_SD_INDEX, nullptr)
// sdInit() is called once in setup() only.

// ─────────────────────────────────────────────────────────────────────────────
// SECTION C: MEMORY RULES
// ─────────────────────────────────────────────────────────────────────────────

// ── Rule M1: No Arduino String in hot paths ────────────────────────────────────
// Arduino String allocates from heap and causes fragmentation.
// Use: char buf[N]; snprintf(buf, N, ...);
// WiFi.SSID().c_str() is only called during scan result copy (once, to fixed buf).
// String::length() comparisons are OK; String in local function scope is OK for
// one-off setup calls but not inside tick() functions.

// ── Rule M2: Pre-allocated list arrays ────────────────────────────────────────
// All lists are fixed-size arrays defined at compile time:
#define PERF_MAX_WIFI_NETS     20     // WifiSvcNet _nets[PERF_MAX_WIFI_NETS]
#define PERF_MAX_PAYLOADS      16     // PayloadEntry payloadList[PERF_MAX_PAYLOADS]
#define PERF_MAX_NOTIF         10     // NotifyEntry _ring[PERF_MAX_NOTIF]
#define PERF_MAX_LOG_LINES     32     // sysLogLines[PERF_MAX_LOG_LINES]
#define PERF_MAX_THEMES        8      // theme array
#define PERF_MAX_ANIMS         8      // SD animation cache

// ── Rule M3: Ring buffers for event streams ────────────────────────────────────
// Notification ring:  svc_notify.cpp  — PERF_MAX_NOTIF entries
// System log ring:    netcore_apps.cpp sysLogLines[]
// Event bus queue:    svc_events.cpp  — PERF_EVENT_QUEUE_DEPTH entries
#define PERF_EVENT_QUEUE_DEPTH 16     // max queued unprocessed events

// ─────────────────────────────────────────────────────────────────────────────
// SECTION D: STALL GUARD
// ─────────────────────────────────────────────────────────────────────────────

// If loop() took longer than PERF_STALL_SKIP_MS last frame, skip optional visuals.
// This matches the existing RENDER_STALL_MS constant in netcore_config.h.
// We reuse that value here so there is ONE authoritative threshold.
#define PERF_STALL_SKIP_MS     RENDER_STALL_MS    // 250ms — from netcore_config.h

// Counters reset window
#define PERF_WINDOW_MS         5000UL    // max-stall window resets every 5s
#define PERF_FPS_WINDOW_MS     1000UL    // fps counter resets every 1s

// ─────────────────────────────────────────────────────────────────────────────
// SECTION E: API
// ─────────────────────────────────────────────────────────────────────────────

// Lifecycle (call from sketch.ino)
void perfInit();
uint32_t perfLoopBegin();           // returns millis() at loop start
void     perfLoopEnd(uint32_t t0);  // updates stall counters

// Draw accounting (call from any function that touches the TFT)
void     perfDrawBegin();           // call immediately before tft operations
void     perfDrawEnd();             // call after tft operations complete

// Stall guard (call from any tick function that has optional visual work)
bool     perfShouldSkipOptionals(); // true if last loop stall > PERF_STALL_SKIP_MS

// Diagnostics read-outs
uint32_t perfGetMaxStallMs();    // worst loop stall in current window
float    perfGetFps();           // draw frames per second
uint32_t perfGetDrawCount();     // total draw calls since boot (wraps at 2^32)
uint32_t perfGetLoopCount();     // total loop() calls since boot
uint8_t  perfGetCpuPercent();    // rough CPU busy: (draw_time / loop_time) * 100
