#pragma once
// ═════════════════════════════════════════════════════════════════════════════
// svc_events.h  —  Event Bus + System State Snapshot
//
// PURPOSE
//   Decouple services: instead of svc_wifi.cpp calling notifyPost() directly,
//   it publishEvent(EVT_WIFI_CONNECTED). Any subscriber sees it next tick.
//
//   The SystemSnapshot struct is a read-only aggregate of all service states.
//   Diagnostics, status bar, and notifications all read from this one place
//   instead of calling into each individual service.
//
// DESIGN CONSTRAINTS
//   - Zero dynamic allocation: fixed-size queue, fixed subscriber array.
//   - All events are uint8_t values — fit in one byte.
//   - Queue is a ring buffer (PERF_EVENT_QUEUE_DEPTH deep).
//   - Subscribers are function pointers, registered once at boot.
//   - publishEvent() is safe to call from any service tick().
//   - eventBusTick() drains the queue and calls subscribers.
//   - eventBusTick() must be called from loop() before other service ticks
//     so that events from the previous tick are dispatched before new ones fire.
//
// SYSTEM STATE SNAPSHOT
//   sysSnapshotUpdate() is called by eventBusTick(); it reads all service
//   state and writes into the static SystemSnapshot struct.
//   All other modules call sysSnapshotGet() for a const pointer.
//   The snapshot is NOT updated mid-frame to ensure consistency.
// ═════════════════════════════════════════════════════════════════════════════
#include "netcore_config.h"
#include "svc_perf.h"    // PERF_EVENT_QUEUE_DEPTH
#include <stdint.h>

// ─────────────────────────────────────────────────────────────────────────────
// EVENTS
// ─────────────────────────────────────────────────────────────────────────────
enum NetcoreEvent : uint8_t {
  EVT_NONE             = 0,

  // WiFi
  EVT_WIFI_CONNECTED   = 1,
  EVT_WIFI_DISCONNECTED= 2,
  EVT_WIFI_SCAN_DONE   = 3,
  EVT_WIFI_FAILED      = 4,

  // NTP / time
  EVT_NTP_SYNCED       = 10,
  EVT_NTP_STALE        = 11,
  EVT_NTP_RETRY        = 12,

  // SD
  EVT_SD_INSERTED      = 20,
  EVT_SD_REMOVED       = 21,
  EVT_SD_INDEX_DONE    = 22,

  // HID
  EVT_HID_ARMED        = 30,
  EVT_HID_DISARMED     = 31,
  EVT_HID_START        = 32,
  EVT_HID_DONE         = 33,
  EVT_HID_CANCEL       = 34,
  EVT_HID_ERROR        = 35,

  // UI lifecycle
  EVT_UI_IDLE          = 40,    // entered watchface/sleep
  EVT_UI_WAKE          = 41,    // woke from watchface

  // BLE (future — placeholder; subscribing to these is safe, they won't fire yet)
  EVT_BLE_CONNECTED    = 50,
  EVT_BLE_DISCONNECTED = 51,
  EVT_BLE_NOTIF_RX     = 52,    // phone pushed a notification

  _EVT_COUNT           = 64     // sentinel; do not exceed 255
};

// ─────────────────────────────────────────────────────────────────────────────
// SYSTEM STATE SNAPSHOT
// ─────────────────────────────────────────────────────────────────────────────
// Immutable (to callers) aggregate of all runtime state.
// Updated once per loop() by eventBusTick() → sysSnapshotUpdate().
// Read via sysSnapshotGet() — never write to the returned pointer.

#define SNAP_SSID_LEN   33
#define SNAP_IP_LEN     16

struct SystemSnapshot {
  // ── Time / NTP ────────────────────────────────────────────────────────────
  bool     timeValid;         // true when RTC has NTP-sourced time
  uint32_t lastSyncEpoch;     // Unix timestamp of last successful NTP sync
  uint32_t syncAgeSec;        // seconds since last sync (0 if never)
  bool     isStale;           // time valid but syncAgeSec > TIME_STALE_SEC

  // ── WiFi ──────────────────────────────────────────────────────────────────
  uint8_t  wifiState;         // WifiSvcState enum value (cast to uint8_t)
  char     ssid[SNAP_SSID_LEN];
  int32_t  rssi;
  char     ip[SNAP_IP_LEN];
  char     gateway[SNAP_IP_LEN];
  char     dns[SNAP_IP_LEN];

  // ── SD ────────────────────────────────────────────────────────────────────
  bool     sdPresent;
  bool     sdIndexed;         // true after first full index since insert
  int      payloadCount;
  int      themeCount;        // loaded from SD (not built-in themes)
  int      animCount;         // animation files on SD

  // ── HID ───────────────────────────────────────────────────────────────────
  bool     hidArmed;          // DUCKY_DRY_RUN == false && USB ready
  bool     hidRunning;        // payload currently executing

  // ── BLE (placeholder — fields exist but stay false/0 until BLE is wired) ─
  bool     bleConnected;
  // future: bleDeviceName, bleRssi, etc.

  // ── Battery (placeholder) ─────────────────────────────────────────────────
  uint8_t  batteryPct;        // 0–100; 255 = unknown
  bool     charging;

  // ── Perf ─────────────────────────────────────────────────────────────────
  uint32_t maxStallMs;
  float    fps;
  uint32_t heapFreeKb;
};

// ─────────────────────────────────────────────────────────────────────────────
// EVENT BUS API
// ─────────────────────────────────────────────────────────────────────────────

// Max subscribers (function pointers stored in RAM)
#define EVT_MAX_SUBSCRIBERS  8

typedef void (*EventCallback)(NetcoreEvent evt);

// Lifecycle
void eventBusInit();
void eventBusTick();     // drain queue + dispatch + update snapshot; call first in loop()

// Publish (safe to call from any tick path; enqueues, does not dispatch immediately)
void publishEvent(NetcoreEvent evt);

// Subscribe (call from setup() or module init; not from tick paths)
// Returns true on success, false if subscriber table is full.
bool eventBusSubscribe(EventCallback cb);

// Direct snapshot access (never returns nullptr; always valid after eventBusTick)
const SystemSnapshot* sysSnapshotGet();

// Internal: called by eventBusTick to refresh snapshot from live service state.
// Not for external use; declared here to be callable from svc_events.cpp.
void sysSnapshotUpdate();
