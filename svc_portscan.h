#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// svc_portscan.h — Non-blocking TCP port probe (LAN tools)
// Goal: provide "Port tools" backend without UI.
// Constraints: no delay(), no heap churn in hot paths, deterministic tick usage.
// Notes:
//   - Uses non-blocking connect() with a single in-flight socket.
//   - Designed for short lists (<=64 ports) and LAN latency.
//   - Results are cached in fixed arrays for UI to display later.
// ─────────────────────────────────────────────────────────────────────────────
#include "netcore_config.h"
#include <stdint.h>
#include <stdbool.h>

#define PORTSCAN_MAX_PORTS 64
#define PORTSCAN_IP_LEN    16  // "255.255.255.255" + null

enum PortScanState {
  PSCAN_IDLE = 0,
  PSCAN_RUNNING,
  PSCAN_DONE,
  PSCAN_ERROR
};

struct PortScanResult {
  uint16_t port;
  uint8_t  open;   // 1=open, 0=closed/filtered
};

void portScanInit();
void portScanTick();

// Start scanning target IPv4 string (dotted), with a provided port list.
// Ports are copied into an internal fixed buffer.
bool portScanStart(const char* ip, const uint16_t* ports, int portCount);

// Stop current scan immediately (closes socket if needed).
void portScanStop();

// State + data accessors
PortScanState portScanGetState();
const char*   portScanGetTargetIP();
int           portScanGetTotalPorts();
int           portScanGetIndex();          // current port index [0..total]
int           portScanGetOpenCount();
int           portScanGetResultCount();    // equals total when DONE
const PortScanResult* portScanGetResult(int i); // nullptr if OOB

// Tuning (set before start)
void portScanSetTimeoutMs(uint16_t ms);    // default 180
void portScanSetInterPortDelayMs(uint16_t ms); // default 0 (pure tick-driven)
