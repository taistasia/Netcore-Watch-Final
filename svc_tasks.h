#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// svc_tasks.h  —  Global Long-Task Runner
//
// Purpose  : All heavy / potentially-blocking jobs go through this queue.
//            The runner calls their work function cooperatively from loop(),
//            so the UI never freezes and back-button cancel always works.
//
// Supported jobs ──────────────────────────────────────────────────────────────
//   TASK_WIFI_SCAN     — async WiFi scan (via wifiSvcStartScan)
//   TASK_SD_INDEX      — SD payload indexing
//   TASK_HID_RUN       — HID payload execution (cooperative line-by-line)
//
// Public API ──────────────────────────────────────────────────────────────────
//   taskSvcInit()
//   taskSvcTick()          — call every loop(); dispatches one work unit
//
//   taskRun(job, param)    — enqueue job; param is job-specific (file path etc)
//   taskCancel()           — request cancel; job checks taskCancelRequested()
//   taskIsRunning()
//   taskGetJob()           — current job type
//   taskProgress()         — 0–100, or -1 if indeterminate
//   taskStatusLine()       — short human-readable status string
//
//   taskCancelRequested()  — polled by long jobs to exit cooperatively
//
// Param convention (passed as void* cast to/from these types) ─────────────────
//   TASK_WIFI_SCAN : no param (nullptr)
//   TASK_SD_INDEX  : no param (indexes the current cart)
//   TASK_HID_RUN   : const char* → file path on SD
// ─────────────────────────────────────────────────────────────────────────────
#include "netcore_config.h"
#include <stdint.h>

// ── Job type ──────────────────────────────────────────────────────────────────
enum TaskJob {
  TASK_NONE           = 0,
  TASK_WIFI_SCAN      = 1,
  TASK_SD_INDEX       = 2,
  TASK_HID_RUN        = 3,
  TASK_PING_SEQUENCE  = 4,   // non-blocking TCP-connect ping, one attempt per tick
  TASK_PORT_SCAN      = 5,   // non-blocking TCP port scan, one port per tick
};

// ── Lifecycle ─────────────────────────────────────────────────────────────────
void taskSvcInit();
void taskSvcTick();

// ── Control ───────────────────────────────────────────────────────────────────
void    taskRun(TaskJob job, const void* param);
void    taskCancel();

// ── Query ─────────────────────────────────────────────────────────────────────
bool        taskIsRunning();
TaskJob     taskGetJob();
int         taskProgress();         // 0–100; -1 = indeterminate
const char* taskStatusLine();       // "SCANNING..." / "SD INDEX: 3/12" etc.

// ── Cooperative helper (polled inside long-running steps) ─────────────────────
bool taskCancelRequested();

// ──────────────────────────────────────────────────────────────────────────────
// TASK_PING_SEQUENCE -- non-blocking TCP-connect ping
// ──────────────────────────────────────────────────────────────────────────────
// Strategy: one WiFiClient::connect(host, port, TIMEOUT_MS) per tick.
// The call blocks for at most PING_CONNECT_TIMEOUT_MS ms per attempt, then
// returns control to loop(). Between attempts the UI stays fully responsive.
// PING_TASK_MAX caps the packet count to avoid unbounded loops.
#define PING_TASK_MAX 8

struct PingTaskState {
  char  host[32];
  int   targetPort;
  int   total;          // total attempts requested
  int   sent;           // attempts completed so far
  int   recv;           // successful connects
  long  lastRttMs;      // last RTT ms; -1 = timeout
  long  totalMs;        // cumulative RTT of successes
  long  minMs;          // best RTT seen
  long  maxMs;          // worst RTT seen
  bool  done;           // sequence finished (or cancelled)
  bool  cancelled;
};

void                  pingTaskStart(const char* host, int port, int count);
const PingTaskState*  pingTaskGetState();

// ──────────────────────────────────────────────────────────────────────────────
// TASK_PORT_SCAN -- non-blocking TCP port scan
// ──────────────────────────────────────────────────────────────────────────────
// One port per tick. PORT_CONNECT_TIMEOUT_MS bounds max stall per tick.
// All results stored in PortTaskState.ports[]; apps render via portTaskGetState().
#define PORT_TASK_MAX 8

struct PortScanEntry {
  int    port;
  char   label[6];   // "FTP  " padded to 5 + null
  int8_t result;     // -1=pending  0=closed  1=open
};

struct PortTaskState {
  char          host[32];
  int           total;
  int           idx;        // ports scanned so far (== next index to scan)
  int           openCount;
  bool          done;
  bool          cancelled;
  PortScanEntry ports[PORT_TASK_MAX];
};

void                  portTaskStart(const char*   host,
                                    const int*    portNums,
                                    const char* const* portLabels,
                                    int           count);
const PortTaskState*  portTaskGetState();