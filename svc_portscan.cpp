// ─────────────────────────────────────────────────────────────
// svc_portscan.cpp — Non-blocking TCP port probe (LAN tools)
// ─────────────────────────────────────────────────────────────
#include "svc_portscan.h"

#if ARDUINO_ARCH_ESP32
  #include <lwip/sockets.h>
  #include <lwip/inet.h>
  #include <fcntl.h>
  #include <errno.h>
#else
  // Non-ESP32 builds: compile stub
#endif

static PortScanState s_state = PSCAN_IDLE;

static char s_ip[PORTSCAN_IP_LEN] = "0.0.0.0";
static uint16_t s_ports[PORTSCAN_MAX_PORTS];
static int s_total = 0;
static int s_idx   = 0;

static PortScanResult s_results[PORTSCAN_MAX_PORTS];
static int s_openCount = 0;

static uint16_t s_timeoutMs = 180;
static uint16_t s_interDelayMs = 0;

#if ARDUINO_ARCH_ESP32
static int      s_sock = -1;
static uint32_t s_phaseStartMs = 0;
static uint32_t s_nextStartMs  = 0;
static bool     s_connectIssued = false;
static sockaddr_in s_addr;
#endif

static void closeSock() {
#if ARDUINO_ARCH_ESP32
  if (s_sock >= 0) {
    lwip_close(s_sock);
    s_sock = -1;
  }
  s_connectIssued = false;
#endif
}

static void resetState() {
  closeSock();
  s_state = PSCAN_IDLE;
  s_ip[0] = 0;
  s_total = 0;
  s_idx = 0;
  s_openCount = 0;
  for (int i = 0; i < PORTSCAN_MAX_PORTS; i++) {
    s_results[i].port = 0;
    s_results[i].open = 0;
  }
}

void portScanInit() {
  resetState();
}

void portScanSetTimeoutMs(uint16_t ms) { s_timeoutMs = ms ? ms : 1; }
void portScanSetInterPortDelayMs(uint16_t ms) { s_interDelayMs = ms; }

PortScanState portScanGetState() { return s_state; }
const char* portScanGetTargetIP() { return s_ip[0] ? s_ip : "0.0.0.0"; }
int portScanGetTotalPorts() { return s_total; }
int portScanGetIndex() { return s_idx; }
int portScanGetOpenCount() { return s_openCount; }
int portScanGetResultCount() { return (s_state == PSCAN_DONE) ? s_total : 0; }
const PortScanResult* portScanGetResult(int i) {
  if (i < 0 || i >= s_total) return nullptr;
  return &s_results[i];
}

void portScanStop() {
  if (s_state == PSCAN_RUNNING) closeSock();
  s_state = PSCAN_IDLE;
}

static bool startOnePort() {
#if !ARDUINO_ARCH_ESP32
  return false;
#else
  if (s_idx >= s_total) return false;

  closeSock();

  s_sock = lwip_socket(AF_INET, SOCK_STREAM, 0);
  if (s_sock < 0) {
    s_state = PSCAN_ERROR;
    return false;
  }

  // non-blocking
  int flags = fcntl(s_sock, F_GETFL, 0);
  fcntl(s_sock, F_SETFL, flags | O_NONBLOCK);

  memset(&s_addr, 0, sizeof(s_addr));
  s_addr.sin_family = AF_INET;
  s_addr.sin_port = htons(s_ports[s_idx]);

  if (!inet_aton(s_ip, &s_addr.sin_addr)) {
    s_state = PSCAN_ERROR;
    closeSock();
    return false;
  }

  s_phaseStartMs = millis();
  s_connectIssued = true;

  int rc = lwip_connect(s_sock, (sockaddr*)&s_addr, sizeof(s_addr));
  if (rc == 0) {
    // Immediate success (rare)
    return true;
  }

  // EINPROGRESS expected
  return false;
#endif
}

static void finishPort(bool isOpen) {
  s_results[s_idx].port = s_ports[s_idx];
  s_results[s_idx].open = isOpen ? 1 : 0;
  if (isOpen) s_openCount++;

  s_idx++;
#if ARDUINO_ARCH_ESP32
  closeSock();
  s_nextStartMs = millis() + (uint32_t)s_interDelayMs;
#endif

  if (s_idx >= s_total) {
    s_state = PSCAN_DONE;
  }
}

bool portScanStart(const char* ip, const uint16_t* ports, int portCount) {
  if (!ip || !ports) return false;
  if (portCount <= 0) return false;
  if (portCount > PORTSCAN_MAX_PORTS) portCount = PORTSCAN_MAX_PORTS;

  resetState();

  // copy ip
  int i = 0;
  for (; i < PORTSCAN_IP_LEN - 1 && ip[i]; i++) s_ip[i] = ip[i];
  s_ip[i] = '\0';

  // copy ports
  s_total = portCount;
  for (int p = 0; p < portCount; p++) {
    s_ports[p] = ports[p];
    s_results[p].port = ports[p];
    s_results[p].open = 0;
  }

  s_state = PSCAN_RUNNING;
  s_idx = 0;
  s_openCount = 0;
#if ARDUINO_ARCH_ESP32
  s_nextStartMs = millis();
#endif
  return true;
}

void portScanTick() {
  if (s_state != PSCAN_RUNNING) return;

#if !ARDUINO_ARCH_ESP32
  s_state = PSCAN_ERROR;
  return;
#else
  if (s_idx >= s_total) {
    s_state = PSCAN_DONE;
    return;
  }

  // inter-port delay gate
  if ((int32_t)(millis() - s_nextStartMs) < 0) return;

  if (!s_connectIssued) {
    // Start next port
    bool immediate = startOnePort();
    if (immediate) {
      finishPort(true);
    }
    return;
  }

  // Poll connect completion via getsockopt(SO_ERROR)
  int err = 0;
  socklen_t len = sizeof(err);
  int rc = getsockopt(s_sock, SOL_SOCKET, SO_ERROR, &err, &len);
  if (rc == 0 && err == 0) {
    finishPort(true);
    return;
  }

  // timeout?
  uint32_t elapsed = (uint32_t)(millis() - s_phaseStartMs);
  if (elapsed >= s_timeoutMs) {
    finishPort(false);
    return;
  }

  // still in progress; nothing else to do this tick
#endif
}
