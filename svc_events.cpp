// ─────────────────────────────────────────────────────────────────────────────
// svc_events.cpp  —  Event Bus + System State Snapshot
//
// No dynamic allocation. Fixed ring queue, fixed subscriber table.
// publishEvent() is safe to call from any tick path (only writes one byte
// + advances head; no callbacks, no TFT ops, no I2C/SPI).
// Dispatch happens in eventBusTick() only — always from loop().
// ─────────────────────────────────────────────────────────────────────────────
#include "svc_events.h"
#include "svc_notify.h"    // notifySvcPost
#include "svc_wifi.h"      // wifiSvcGetState, wifiSvcGetSSID, etc.
#include "netcore_time.h"  // timeSvcIsReady, timeSvcState, timeSvcSyncAge, timeSvcIsStale
#include "netcore_sd.h"    // sdPresent, payloadCount
#include "netcore_ducky.h" // DUCKY_DRY_RUN
#include "svc_perf.h"      // perfGetMaxStallMs, perfGetFps

// ── Event queue ───────────────────────────────────────────────────────────────
static NetcoreEvent _queue[PERF_EVENT_QUEUE_DEPTH];
static uint8_t      _qHead  = 0;    // next write position
static uint8_t      _qTail  = 0;    // next read position
static uint8_t      _qCount = 0;

// ── Subscriber table ──────────────────────────────────────────────────────────
static EventCallback _subs[EVT_MAX_SUBSCRIBERS];
static uint8_t       _subCount = 0;

// ── System snapshot ───────────────────────────────────────────────────────────
static SystemSnapshot _snap;

// ── Default notification handler ─────────────────────────────────────────────
// Fires notifySvcPost() for system events so svc_wifi.cpp doesn't need to.
// After wiring: svc_wifi.cpp should STOP calling notifyPost() directly and
// instead call publishEvent(EVT_WIFI_CONNECTED) etc.
// For now (transition period) both paths fire — notify deduplicates visually
// because a new banner replaces the old one immediately.

static void _defaultNotifHandler(NetcoreEvent evt) {
  switch (evt) {
    case EVT_WIFI_CONNECTED:
      notifySvcPost(NOTIFY_OK,    "WiFi", "Connected",   3000); break;
    case EVT_WIFI_DISCONNECTED:
      notifySvcPost(NOTIFY_WARN,  "WiFi", "Disconnected",3000); break;
    case EVT_WIFI_FAILED:
      notifySvcPost(NOTIFY_ERROR, "WiFi", "Failed",      3000); break;
    case EVT_WIFI_SCAN_DONE: {
      char body[24];
      snprintf(body, sizeof(body), "%d networks found", _snap.wifiState);
      notifySvcPost(NOTIFY_INFO,  "WiFi", body,          2000);
    } break;
    case EVT_NTP_SYNCED:
      notifySvcPost(NOTIFY_OK,    "NTP",  "Clock synced",2000); break;
    case EVT_NTP_STALE:
      notifySvcPost(NOTIFY_WARN,  "NTP",  "Clock stale", 3000); break;
    case EVT_SD_INSERTED:
      notifySvcPost(NOTIFY_INFO,  "SD",   "Card mounted",2000); break;
    case EVT_SD_REMOVED:
      notifySvcPost(NOTIFY_WARN,  "SD",   "Card removed",2000); break;
    case EVT_SD_INDEX_DONE: {
      char body[28];
      snprintf(body, sizeof(body), "%d payloads indexed", _snap.payloadCount);
      notifySvcPost(NOTIFY_OK,    "SD",   body,          2000);
    } break;
    case EVT_HID_START:
      notifySvcPost(NOTIFY_WARN,  "HID",  "Payload running",1500); break;
    case EVT_HID_DONE:
      notifySvcPost(NOTIFY_OK,    "HID",  "Payload complete",3000); break;
    case EVT_HID_CANCEL:
      notifySvcPost(NOTIFY_INFO,  "HID",  "Canceled",    2000); break;
    case EVT_HID_ERROR:
      notifySvcPost(NOTIFY_ERROR, "HID",  "Error",       3000); break;
    case EVT_BLE_CONNECTED:
      notifySvcPost(NOTIFY_INFO,  "BLE",  "Phone linked",2000); break;
    case EVT_BLE_DISCONNECTED:
      notifySvcPost(NOTIFY_INFO,  "BLE",  "Phone unlinked",2000); break;
    default: break;
  }
}

// ── Snapshot update ───────────────────────────────────────────────────────────

void sysSnapshotUpdate() {
  // Time
  _snap.timeValid      = timeSvcIsReady();
  _snap.lastSyncEpoch  = 0;   // TODO: expose from netcore_time if needed
  _snap.syncAgeSec     = timeSvcSyncAge();
  _snap.isStale        = timeSvcIsStale();

  // WiFi
  WifiSvcState ws = wifiSvcGetState();
  _snap.wifiState = (uint8_t)ws;
  if (ws == WSVC_CONNECTED) {
    strncpy(_snap.ssid,    wifiSvcGetSSID(),    SNAP_SSID_LEN - 1);
    strncpy(_snap.ip,      wifiSvcGetIP(),      SNAP_IP_LEN  - 1);
    strncpy(_snap.gateway, wifiSvcGetGateway(), SNAP_IP_LEN  - 1);
    strncpy(_snap.dns,     wifiSvcGetDNS(),     SNAP_IP_LEN  - 1);
    _snap.rssi = wifiSvcGetRSSI();
  } else {
    _snap.ssid[0] = '\0';
    _snap.ip[0]   = '\0';
    _snap.rssi    = 0;
  }
  _snap.ssid[SNAP_SSID_LEN - 1]    = '\0';
  _snap.ip[SNAP_IP_LEN - 1]        = '\0';
  _snap.gateway[SNAP_IP_LEN - 1]   = '\0';
  _snap.dns[SNAP_IP_LEN - 1]       = '\0';

  // SD
  _snap.sdPresent    = sdPresent;
  _snap.sdIndexed    = sdPresent;   // TODO: expose indexed flag from svc_tasks
  _snap.payloadCount = payloadCount;
  _snap.themeCount   = 0;           // TODO: count SD-loaded themes when implemented
  _snap.animCount    = 0;           // TODO: count SD animation files

  // HID
#if DUCKY_DRY_RUN
  _snap.hidArmed   = false;
#else
  _snap.hidArmed   = true;
#endif
  _snap.hidRunning = false;         // TODO: expose from svc_tasks/netcore_ducky

  // BLE (placeholder — wired to false until BLE stack is added)
  _snap.bleConnected = false;

  // Battery (placeholder)
  _snap.batteryPct = 255;    // 255 = unknown
  _snap.charging   = false;

  // Perf
  _snap.maxStallMs  = perfGetMaxStallMs();
  _snap.fps         = perfGetFps();
  _snap.heapFreeKb  = (uint32_t)(ESP.getFreeHeap() / 1024);
}

// ── Public API ────────────────────────────────────────────────────────────────

void eventBusInit() {
  _qHead    = 0;
  _qTail    = 0;
  _qCount   = 0;
  _subCount = 0;
  memset(&_snap, 0, sizeof(_snap));
  _snap.batteryPct = 255;

  // Register default notification handler as first subscriber
  eventBusSubscribe(_defaultNotifHandler);
}

bool eventBusSubscribe(EventCallback cb) {
  if (!cb || _subCount >= EVT_MAX_SUBSCRIBERS) return false;
  _subs[_subCount++] = cb;
  return true;
}

void publishEvent(NetcoreEvent evt) {
  if (evt == EVT_NONE) return;
  if (_qCount >= PERF_EVENT_QUEUE_DEPTH) {
    // Queue full — drop oldest (overwrite tail)
    _qTail = (_qTail + 1) % PERF_EVENT_QUEUE_DEPTH;
    _qCount--;
  }
  _queue[_qHead] = evt;
  _qHead = (_qHead + 1) % PERF_EVENT_QUEUE_DEPTH;
  _qCount++;
}

void eventBusTick() {
  // Update snapshot first so subscribers see fresh state when they fire
  sysSnapshotUpdate();

  // Drain queue and dispatch
  while (_qCount > 0) {
    NetcoreEvent evt = _queue[_qTail];
    _qTail  = (_qTail + 1) % PERF_EVENT_QUEUE_DEPTH;
    _qCount--;
    for (uint8_t i = 0; i < _subCount; i++) {
      if (_subs[i]) _subs[i](evt);
    }
  }
}

const SystemSnapshot* sysSnapshotGet() {
  return &_snap;
}
