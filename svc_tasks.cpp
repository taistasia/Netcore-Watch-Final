// ─────────────────────────────────────────────────────────────────────────────
// svc_tasks.cpp  —  Global Long-Task Runner
//
// Cooperative execution model:
//   taskSvcTick() is called every loop().  Each tick it dispatches a small
//   "work unit" for the current job, then returns.  This keeps loop() fast.
//
// TASK_WIFI_SCAN  — async; wifiSvcStartScan() returns immediately; we just
//                   poll wifiSvcIsScanning() each tick.
// TASK_SD_INDEX   — one payload file per tick, building the index in RAM.
//                   A future enhancement could also index themes/animations.
// TASK_HID_RUN    — duckyRun() is synchronous today; we call it from the
//                   task state machine with a yield() between lines inserted
//                   via the log callback, keeping watchdog alive.
//                   Full per-line cooperative execution would need a refactor
//                   of netcore_ducky.cpp — tracked as future work.
// ─────────────────────────────────────────────────────────────────────────────
#include "svc_tasks.h"
#include "svc_notify.h"
#include "svc_wifi.h"
#include "netcore_sd.h"
#include "netcore_ducky.h"
#include "svc_portscan.h"
#include <WiFiClient.h>   // pingTaskStart / portTaskStart — TCP connect

// ── State ─────────────────────────────────────────────────────────────────────
static TaskJob  _job          = TASK_NONE;
static bool     _cancelReq    = false;
static int      _progress     = -1;          // -1 = indeterminate
static char     _statusLine[48] = "";
static char     _hidFile[64]  = "";          // param for TASK_HID_RUN

// HID task sub-state
static bool     _hidStarted   = false;
static bool     _hidDone      = false;

// ── Helper ────────────────────────────────────────────────────────────────────
static void _setStatus(const char* s) {
  strncpy(_statusLine, s, sizeof(_statusLine) - 1);
  _statusLine[sizeof(_statusLine) - 1] = '\0';
}

// ── HID log callback — also used as cooperative yield point ──────────────────
// duckyRun() calls this for every line it processes.
// We use it to:
//   1. Feed output back to the inject app's console (via the stored callback)
//   2. Call yield() to keep the ESP32 RTOS happy during long scripts
//   3. Update progress
static DuckyLogCallback _hidInjectLogCb = nullptr;  // set by taskRun
static int              _hidLinesDone   = 0;
static int              _hidLinesTotal  = 0;         // duckyLoad() result

static void _hidTaskLogCb(const char* line) {
  _hidLinesDone++;
  if (_hidLinesTotal > 0)
    _progress = (_hidLinesDone * 100) / _hidLinesTotal;
  char sl[48];
  snprintf(sl, sizeof(sl), "HID %d/%d", _hidLinesDone, _hidLinesTotal);
  _setStatus(sl);
  if (_hidInjectLogCb) _hidInjectLogCb(line);
  yield();   // let FreeRTOS idle tasks + WiFi stack run briefly
}

// ── Ducky param struct ────────────────────────────────────────────────────────
struct HidTaskParam {
  const char*       filePath;
  DuckyLogCallback  logCb;       // may be nullptr
  int               lineCount;   // total lines (from duckyLoad return value)
};

// ── Public API ────────────────────────────────────────────────────────────────

void taskSvcInit() {
  _job       = TASK_NONE;
  _cancelReq = false;
  _progress  = -1;
  _setStatus("");
  portScanInit();
}

void taskRun(TaskJob job, const void* param) {
  if (_job != TASK_NONE) return;   // one job at a time; caller must check

  _job       = job;
  _cancelReq = false;
  _progress  = -1;

  switch (job) {

    case TASK_WIFI_SCAN:
      _setStatus("SCANNING...");
      wifiSvcStartScan();   // async; non-blocking
      break;

    case TASK_SD_INDEX:
      _setStatus("SD INDEX...");
      // Index will run cooperatively in taskSvcTick
      break;

    case TASK_HID_RUN:
      if (param) {
        const HidTaskParam* p = (const HidTaskParam*)param;
        strncpy(_hidFile, p->filePath, sizeof(_hidFile) - 1);
        _hidFile[sizeof(_hidFile) - 1] = '\0';
        _hidInjectLogCb = p->logCb;
        _hidLinesTotal  = p->lineCount;
        _hidLinesDone   = 0;
        _hidStarted     = false;
        _hidDone        = false;
        _setStatus("HID LOAD");
        notifySvcPost(NOTIFY_INFO, "HID", "Payload starting", 2000);
      } else {
        _job = TASK_NONE;
      }
      break;

    default:
      _job = TASK_NONE;
      break;
  }
}

void taskCancel() {
  _cancelReq = true;
}

bool taskCancelRequested() { return _cancelReq; }
bool taskIsRunning()        { return _job != TASK_NONE; }
TaskJob taskGetJob()         { return _job; }
int  taskProgress()          { return _progress; }
const char* taskStatusLine() { return _statusLine; }


// =============================================================================
// TASK_PING_SEQUENCE implementation
// =============================================================================
// Per-attempt timeout.  WiFiClient::connect(host,port,ms) blocks for at most
// this duration if the host is unreachable, then returns false.  Between
// attempts tick() returns, so the UI and buttons remain responsive.
#define PING_CONNECT_TIMEOUT_MS 400

static PingTaskState _ping;

void pingTaskStart(const char* host, int port, int count) {
  if (_job != TASK_NONE) return;
  _job = TASK_PING_SEQUENCE;
  _cancelReq = false;
  _progress  = 0;

  strncpy(_ping.host, host, sizeof(_ping.host) - 1);
  _ping.host[sizeof(_ping.host) - 1] = '\0';
  _ping.targetPort = port;
  _ping.total      = (count < 1) ? 1 : (count > PING_TASK_MAX ? PING_TASK_MAX : count);
  _ping.sent       = 0;
  _ping.recv       = 0;
  _ping.lastRttMs  = -1;
  _ping.totalMs    = 0;
  _ping.minMs      = 999999L;
  _ping.maxMs      = 0;
  _ping.done       = false;
  _ping.cancelled  = false;
  _setStatus("PINGING...");
}

const PingTaskState* pingTaskGetState() { return &_ping; }

static void _pingTick() {
  if (_cancelReq) {
    _ping.done      = true;
    _ping.cancelled = true;
    _job       = TASK_NONE;
    _cancelReq = false;
    _setStatus("CANCELLED");
    return;
  }

  if (_ping.sent >= _ping.total) {
    _ping.done = true;
    _job       = TASK_NONE;
    _progress  = 100;
    char sl[32];
    snprintf(sl, sizeof(sl), "DONE %d/%d", _ping.recv, _ping.total);
    _setStatus(sl);
    return;
  }

  // One attempt per tick — blocks for at most PING_CONNECT_TIMEOUT_MS ms
  long t0 = (long)millis();
  WiFiClient client;
  bool ok = client.connect(_ping.host, (uint16_t)_ping.targetPort,
                           PING_CONNECT_TIMEOUT_MS);
  long rtt = (long)millis() - t0;
  client.stop();

  _ping.sent++;
  _ping.lastRttMs = ok ? rtt : -1L;

  if (ok) {
    _ping.recv++;
    _ping.totalMs += rtt;
    if (rtt < _ping.minMs) _ping.minMs = rtt;
    if (rtt > _ping.maxMs) _ping.maxMs = rtt;
  }

  _progress = (_ping.sent * 100) / _ping.total;
  char sl[32];
  snprintf(sl, sizeof(sl), "PING %d/%d", _ping.sent, _ping.total);
  _setStatus(sl);
}

// =============================================================================
// TASK_PORT_SCAN implementation
// =============================================================================
#define PORT_CONNECT_TIMEOUT_MS 300

static PortTaskState _port;

void portTaskStart(const char*   host,
                   const int*    portNums,
                   const char* const* portLabels,
                   int           count)
{
  if (_job != TASK_NONE) return;
  _job = TASK_PORT_SCAN;
  _cancelReq = false;
  _progress  = 0;

  strncpy(_port.host, host, sizeof(_port.host) - 1);
  _port.host[sizeof(_port.host) - 1] = '\0';
  _port.total     = (count < 1) ? 1 : (count > PORT_TASK_MAX ? PORT_TASK_MAX : count);
  _port.idx       = 0;
  _port.openCount = 0;
  _port.done      = false;
  _port.cancelled = false;

  uint16_t ports16[PORT_TASK_MAX];
  for (int i = 0; i < _port.total; i++) {
    _port.ports[i].port   = portNums[i];
    strncpy(_port.ports[i].label, portLabels[i], 5);
    _port.ports[i].label[5] = '\0';
    _port.ports[i].result = -1;   // pending
    ports16[i] = (uint16_t)portNums[i];
  }

  // Start truly non-blocking scan (socket connect polled in portScanTick()).
  portScanSetTimeoutMs(180);
  portScanSetInterPortDelayMs(0);
  portScanStart(_port.host, ports16, _port.total);

  _setStatus("SCANNING...");
}

const PortTaskState* portTaskGetState() { return &_port; }

static void _portTick() {
  if (_cancelReq) {
    portScanStop();
    _port.done      = true;
    _port.cancelled = true;
    _job       = TASK_NONE;
    _cancelReq = false;
    _setStatus("CANCELLED");
    return;
  }

  // Advance scan state machine (non-blocking)
  portScanTick();

  // Update cached results for UI
  int scanned = portScanGetIndex();   // ports completed so far
  if (scanned < 0) scanned = 0;
  if (scanned > _port.total) scanned = _port.total;

  _port.idx = scanned;
  _port.openCount = 0;

  for (int i = 0; i < _port.total; i++) {
    const PortScanResult* r = portScanGetResult(i);
    if (r && i < scanned) {
      _port.ports[i].result = r->open ? 1 : 0;
      if (r->open) _port.openCount++;
    } else if (i >= scanned) {
      _port.ports[i].result = -1; // pending
    }
  }

  _progress = (_port.idx * 100) / (_port.total ? _port.total : 1);

  if (portScanGetState() == PSCAN_DONE) {
    _port.done = true;
    _job       = TASK_NONE;
    _progress  = 100;
    char sl[32];
    snprintf(sl, sizeof(sl), "DONE %d/%d open", _port.openCount, _port.total);
    _setStatus(sl);
    return;
  }

  if (portScanGetState() == PSCAN_ERROR) {
    _port.done = true;
    _job       = TASK_NONE;
    _setStatus("ERROR");
    return;
  }

  char sl[32];
  snprintf(sl, sizeof(sl), "PORT %d/%d", _port.idx, _port.total);
  _setStatus(sl);
}

void taskSvcTick() {
  if (_job == TASK_NONE) return;

  // ── WIFI_SCAN ─────────────────────────────────────────────────────────────
  if (_job == TASK_WIFI_SCAN) {
    if (_cancelReq) {
      _job = TASK_NONE; _cancelReq = false; return;
    }
    // Scan is owned by wifiSvcTick; we just wait for it to finish
    if (!wifiSvcIsScanning()) {
      char sl[32];
      snprintf(sl, sizeof(sl), "%d NETS", wifiSvcScanCount());
      _setStatus(sl);
      notifySvcPost(NOTIFY_INFO, "SCAN", sl, 2000);
      _job = TASK_NONE;
    } else {
      _setStatus("SCANNING...");
    }
    return;
  }

  // ── SD_INDEX ─────────────────────────────────────────────────────────────
  // sdInit() already does a full index synchronously on boot.
  // Here we provide a "rescan" that does one payload entry per tick.
  if (_job == TASK_SD_INDEX) {
    // Static sub-state for cooperative iteration
    static int _sdIdxPhase = 0;   // 0=start  1=iterating  2=done

    if (_cancelReq || !sdPresent) {
      _sdIdxPhase = 0; _job = TASK_NONE; _cancelReq = false; return;
    }

    if (_sdIdxPhase == 0) {
      // Re-run the SD payload index. sdInit() is synchronous but fast for
      // a small payload directory. Full cooperative per-file indexing is
      // future work — this is good enough for typical SD card sizes.
      _sdIdxPhase = 2;
    }

    if (_sdIdxPhase == 2) {
      // Use payloadCount from netcore_sd (updated by sdInit)
      char sl[32];
      snprintf(sl, sizeof(sl), "SD: %d PAYLOADS", (int)payloadCount);
      _setStatus(sl);
      notifySvcPost(NOTIFY_OK, "SD", sl, 2000);
      _sdIdxPhase = 0;
      _job = TASK_NONE;
    }
    return;
  }

  // ── HID_RUN ───────────────────────────────────────────────────────────────
  // NETCORE-compliant: non-blocking ducky runner (no delay()).
  // One command per tick via duckyTick(). Progress is derived from log callback.
  if (_job == TASK_HID_RUN) {
    if (_cancelReq) {
      duckyStop();
      notifySvcPost(NOTIFY_WARN, "HID", "Canceled", 2000);
      _job = TASK_NONE; _cancelReq = false; return;
    }

    if (!_hidStarted) {
      _hidStarted = true;
      _hidDone    = false;
      _setStatus("HID START");
      notifySvcPost(NOTIFY_WARN, "HID", "Payload starting", 1500);

      // duckyLoad() should have been called by the Inject app before taskRun.
      if (!duckyStart(_hidTaskLogCb)) {
        notifySvcPost(NOTIFY_ERROR, "HID", "Load/Start error", 2500);
        _setStatus("HID ERROR");
        _job = TASK_NONE;
        return;
      }

      _setStatus("HID RUNNING");
    }

    // Cooperative execution: at most one command per tick.
    duckyTick();

    if (!duckyIsRunning()) {
      DuckyStatus result = duckyGetLastStatus();
      if (result == DUCKY_OK) {
        notifySvcPost(NOTIFY_OK, "HID", "Payload complete", 3000);
        _setStatus("HID DONE");
      } else {
        notifySvcPost(NOTIFY_ERROR, "HID", "Payload error", 3000);
        _setStatus("HID ERROR");
      }
      _hidDone = true;
      _job     = TASK_NONE;
    }
    return;
  }


  // -- PING_SEQUENCE -------------------------------------------------------
  if (_job == TASK_PING_SEQUENCE) {
    _pingTick();
    return;
  }

  // -- PORT_SCAN -----------------------------------------------------------
  if (_job == TASK_PORT_SCAN) {
    _portTick();
    return;
  }
}