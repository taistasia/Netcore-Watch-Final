// netcore_apps.cpp
// NETCORE app registry + minimal placeholder apps
// Restores required globals (mode/runningApp/apps/APP_COUNT/appsTick)
// and keeps the ANIM demo progress bar dirty-redraw fix.

#include "netcore_apps.h"
#include "netcore_buttons.h"
#include "netcore_ui.h"
#include "netcore_settings.h"
#include "netcore_sd.h"
#include "svc_anim.h"
#include "motion_constants.h"
#include "svc_notify.h"
#include "svc_statusbar.h"
#include "svc_haptics.h"
#include "svc_wifi.h"
#include "svc_tasks.h"
#include "svc_perf.h"
#include "netcore_notes.h"
#include "netcore_theme.h"
#include "netcore_ducky.h"
#include "svc_portscan.h"
#include "svc_air.h"
#include "svc_tempir.h"
#include "svc_rfid.h"

#include "svc_keyboard.h"
#include "svc_ble.h"

#include <Arduino.h>

// ─────────────────────────────────────────────────────────────────────────────
// Global app framework state (required by netcore_ui.cpp + sketch.ino)
// ─────────────────────────────────────────────────────────────────────────────

Mode mode = MODE_MENU;
int  runningApp = -1;

// ─────────────────────────────────────────────────────────────────────────────
// Tiny system log (fixed storage, no heap)
// ─────────────────────────────────────────────────────────────────────────────

static const uint8_t  SYSLOG_MAX = 24;
static const uint8_t  SYSLOG_LEN = 48;
static char           s_syslog[SYSLOG_MAX][SYSLOG_LEN];
static uint8_t        s_syslogHead = 0;

void sysLogPush(const char* msg) {
  if (!msg) return;
  // Copy into ring buffer (truncate)
  char* dst = s_syslog[s_syslogHead];
  uint8_t i = 0;
  for (; i < SYSLOG_LEN - 1 && msg[i]; i++) dst[i] = msg[i];
  dst[i] = '\0';
  s_syslogHead = (uint8_t)((s_syslogHead + 1) % SYSLOG_MAX);
}

void diagUpdateStall(uint32_t /*loopStartMs*/) {
  // svc_perf owns stall tracking now; keep API stable.
}

// ─────────────────────────────────────────────────────────────────────────────
// Shared helpers
// ─────────────────────────────────────────────────────────────────────────────

static void appChromeEnter(const char* title, const char* sub, const char* footer) {
  tft.fillScreen(COL_BG());
  drawStatusBarFrame();
  drawStatusFieldsForce();
  drawTitleBar(title, sub);
  fillBody();
  drawFooter(footer);
}

static void appPrintBody(int x, int y, const char* s, uint16_t fg, uint16_t bg) {
  tft.setTextSize(1);
  tft.setTextColor(fg, bg);
  tft.setCursor(x, y);
  tft.print(s);
}

// ─────────────────────────────────────────────────────────────────────────────
// APP 0: ANIM DEMO — progress bar dirty redraw
// ─────────────────────────────────────────────────────────────────────────────

static const int PB_X = 30;
static const int PB_Y = 170;
static const int PB_W = 180;
static const int PB_H = 12;
static const int PB_BORDER = 1;

static int s_prevFillW = -1; // -1 forces first interior paint

static int computeFillWidthQ16(int32_t qProgress) {
  if (qProgress < 0) qProgress = 0;
  if (qProgress > Q16_ONE) qProgress = Q16_ONE;

  const int innerW = PB_W - (PB_BORDER * 2);
  int32_t w = (int32_t)(((int64_t)innerW * (int64_t)qProgress) >> 16);
  if (w < 0) w = 0;
  if (w > innerW) w = innerW;
  return (int)w;
}

static void animDemoRenderProgress() {
  const int32_t q = animGetQ(AT_DEMO, AP_PROGRESS);
  const int newFillW = computeFillWidthQ16(q);

  const int innerX = PB_X + PB_BORDER;
  const int innerY = PB_Y + PB_BORDER;
  const int innerH = PB_H - (PB_BORDER * 2);
  const int innerW = PB_W - (PB_BORDER * 2);

  // Border: tiny; acceptable each frame
  tft.drawRect(PB_X, PB_Y, PB_W, PB_H, COL_HILITE());

  // First draw
  if (s_prevFillW < 0) {
    tft.fillRect(innerX, innerY, innerW, innerH, COL_BG());
    if (newFillW > 0) tft.fillRect(innerX, innerY, newFillW, innerH, COL_HILITE());
    s_prevFillW = newFillW;
    return;
  }

  if (newFillW == s_prevFillW) return;

  const int minW = (newFillW < s_prevFillW) ? newFillW : s_prevFillW;
  const int maxW = (newFillW > s_prevFillW) ? newFillW : s_prevFillW;
  const int deltaW = maxW - minW;
  if (deltaW <= 0) { s_prevFillW = newFillW; return; }

  const int deltaX = innerX + minW;
  if (newFillW < s_prevFillW) {
    // shrink → clear
    tft.fillRect(deltaX, innerY, deltaW, innerH, COL_BG());
  } else {
    // grow → fill
    tft.fillRect(deltaX, innerY, deltaW, innerH, COL_HILITE());
  }

  s_prevFillW = newFillW;
}

static void appAnimEnter() {
  appChromeEnter("ANIM", "DEMO", "BACK menu");

  appPrintBody(12, BODY_Y + 18, "Deterministic tweens (Q16.16)", COL_FG(), COL_BG());
  appPrintBody(12, BODY_Y + 32, "Progress bar uses dirty redraw", COL_DIM(), COL_BG());

  // Reset cached draw state
  s_prevFillW = -1;

  // Reset + start tween
  animCancelTarget(AT_DEMO);
  animSetQ(AT_DEMO, AP_PROGRESS, 0);
  animTween(AT_DEMO, AP_PROGRESS, 0, Q16_ONE, MOTION_DUR_DEMO, MOTION_EASE_BREATHE, (ANIM_F_LOOP | ANIM_F_PINGPONG | ANIM_F_REPLACE));

  // Draw once immediately
  animDemoRenderProgress();
}

static void appAnimTick() {
  // Only redraw when progress changes (animDemoRenderProgress handles no-op fast)
  animDemoRenderProgress();
}

static void appAnimExit() {
  animCancelTarget(AT_DEMO);
}

// ─────────────────────────────────────────────────────────────────────────────
// “In-progress” apps — now wired to real backends (Wokwi-real)
//   Goal: prove services are real without adding complex UI/UX yet.
//   Rules: no blocking, no heap churn, dirty-line redraw only.

#include "svc_wifi.h"
#include "svc_tasks.h"
#include "svc_portscan.h"   // (backend owned by taskSvc)
#include "netcore_notes.h"
#include "netcore_ducky.h"
#include "netcore_sd.h"
#include "netcore_settings.h"
#include "svc_input.h"
#include "svc_notify.h"

static const int LIST_X       = 12;
static const int LIST_Y       = BODY_Y + 18;
static const int LINE_H       = 12;
static const int VISIBLE_ROWS = 6;
static const int LIST_W       = 240 - (LIST_X * 2);

static void drawLine(int row, bool sel, const char* s) {
  int y = LIST_Y + row * LINE_H;
  tft.fillRect(LIST_X, y, LIST_W, LINE_H, COL_BG());
  tft.setTextSize(1);
  if (sel) {
    tft.fillRect(LIST_X, y, LIST_W, LINE_H, COL_HILITE());
    tft.setTextColor(COL_BG(), COL_HILITE());
    tft.setCursor(LIST_X + 2, y);
    tft.print("> " );
  } else {
    tft.setTextColor(COL_FG(), COL_BG());
    tft.setCursor(LIST_X, y);
    tft.print("  " );
  }
  if (s) tft.print(s);
}

// ── WIFI ────────────────────────────────────────────────────────────────────
static int s_wifiSel=0, s_wifiTop=0;
static int s_wifiPrevSel=-1, s_wifiPrevTop=-1, s_wifiPrevCount=-1;
static int s_wifiPrevScanning=-1;
static WifiSvcState s_wifiPrevState = WSVC_OFF;

static bool s_wifiSavedView = false;
static char s_wifiSelSsid[WIFI_SVC_SSID_LEN] = {0};
static char s_wifiPass[64] = {0};

static void wifiRenderAll() {
  char st[64];
  const char* stateStr = "OFF";
  switch (wifiSvcGetState()) {
    case WSVC_IDLE:        stateStr = "IDLE"; break;
    case WSVC_SCANNING:    stateStr = "SCANNING"; break;
    case WSVC_CONNECTING:  stateStr = "CONNECTING"; break;
    case WSVC_CONNECTED:   stateStr = "CONNECTED"; break;
    case WSVC_FAILED:      stateStr = "FAILED"; break;
    case WSVC_RETRY_WAIT:  stateStr = "RETRY"; break;
    default: break;
  }

  if (!s_wifiSavedView) {
    snprintf(st, sizeof(st), "%s  %d nets", stateStr, wifiSvcScanCount());
  } else {
    snprintf(st, sizeof(st), "%s  SAVED", stateStr);
  }
  drawLine(0, (s_wifiSel == -1), st);

  if (!s_wifiSavedView) {
    int count = wifiSvcScanCount();
    for (int r = 0; r < VISIBLE_ROWS - 1; r++) {
      int idx = s_wifiTop + r;
      if (idx < 0 || idx >= count) {
        drawLine(r + 1, false, "");
        continue;
      }
      const WifiSvcNet* net = wifiSvcScanResult(idx);
      char row[48];
      if (net) {
        const char* lock = (net->auth == WIFI_AUTH_OPEN) ? " " : "*";
        snprintf(row, sizeof(row), "%s%s (%lddBm)", lock, net->ssid, (long)net->rssi);
      } else {
        snprintf(row, sizeof(row), "<?>");
      }
      drawLine(r + 1, idx == s_wifiSel, row);
    }
  } else {
    if (wifiSvcHasSavedCreds()) {
      char l1[48];
      snprintf(l1, sizeof(l1), "SSID: %s", wifiSvcGetSavedSSID());
      drawLine(1, false, l1);
      drawLine(2, (s_wifiSel == 0), "Connect saved");
      drawLine(3, (s_wifiSel == 1), "Forget saved");
    } else {
      drawLine(1, false, "SSID: (none)");
      drawLine(2, false, "Select secured net");
      drawLine(3, false, "to enter pass");
    }
    drawLine(4, false, "SEL on header: back");
    drawLine(5, false, "");
    drawLine(6, false, "");
    drawLine(7, false, "");
  }

  s_wifiPrevSel = s_wifiSel;
  s_wifiPrevTop = s_wifiTop;
  s_wifiPrevCount = wifiSvcScanCount();
  s_wifiPrevScanning = wifiSvcIsScanning() ? 1 : 0;
  s_wifiPrevState = wifiSvcGetState();
}

static void appWifiEnter() {
  appChromeEnter("WIFI", "SCAN", "BACK menu");
  s_wifiSel = 0; s_wifiTop = 0;
  s_wifiPrevSel = -1; s_wifiPrevTop = -1; s_wifiPrevCount = -1; s_wifiPrevScanning = -1;
  s_wifiSavedView = false;
  s_wifiSelSsid[0] = 0;
  s_wifiPass[0] = 0;

  if (!taskIsRunning()) {
    taskRun(TASK_WIFI_SCAN, nullptr);
  } else if (!wifiSvcIsScanning()) {
    wifiSvcStartScan();
  }

  wifiRenderAll();
}

static void appWifiTick() {
  // Keyboard overlay (password entry)
  if (kbActive()) {
    kbTick();
    if (kbFinished()) {
      wifiSvcConnect(s_wifiSelSsid, s_wifiPass);
      notifySvcPost(NOTIFY_INFO, "WIFI", "Connect...", 1200);
      wifiRenderAll();
    } else if (kbCancelled()) {
      notifySvcPost(NOTIFY_WARN, "WIFI", "Cancelled", 1000);
      wifiRenderAll();
    }
    return;
  }

  // BLE provision hook (compile-safe; usually false in Wokwi)
  {
    char ssid[WIFI_SVC_SSID_LEN] = {0};
    char pass[64] = {0};
    bool connectNow = false;
    if (bleWifiProvisionPop(ssid, (int)sizeof(ssid), pass, (int)sizeof(pass), &connectNow)) {
      if (connectNow) wifiSvcConnect(ssid, pass);
      notifySvcPost(NOTIFY_INFO, "BLE", "WiFi creds rx", 1200);
      wifiRenderAll();
      return;
    }
  }

  // Rescan on long select
  if (inputHoldSelect()) {
    if (!taskIsRunning()) taskRun(TASK_WIFI_SCAN, nullptr);
    notifySvcPost(NOTIFY_INFO, "WIFI", "Rescan", 1200);
  }

  int count = wifiSvcScanCount();
  if (count < 0) count = 0;
  int maxSel = s_wifiSavedView ? 1 : (count - 1);

  // Allow header selection (-1) to toggle saved view
  if (Buttons::up::consume()) {
    if (s_wifiSel > -1) s_wifiSel--;
  }
  if (Buttons::down::consume()) {
    if (s_wifiSel < maxSel) s_wifiSel++;
  }

  // Scroll window (scan view only)
  if (!s_wifiSavedView) {
    int win = VISIBLE_ROWS-1;
    if (s_wifiSel < s_wifiTop) s_wifiTop = s_wifiSel;
    if (s_wifiSel >= s_wifiTop + win) s_wifiTop = s_wifiSel - (win-1);
    if (s_wifiTop < 0) s_wifiTop = 0;
    if (s_wifiSel == -1) s_wifiTop = 0;
  }

  if (Buttons::select.consume()) {
    // Header toggles view
    if (s_wifiSel == -1) {
      s_wifiSavedView = !s_wifiSavedView;
      s_wifiSel = s_wifiSavedView ? 0 : 0;
      s_wifiTop = 0;
      wifiRenderAll();
      return;
    }

    if (s_wifiSavedView) {
      if (!wifiSvcHasSavedCreds()) {
        notifySvcPost(NOTIFY_WARN, "WIFI", "NO SAVED", 1200);
      } else if (s_wifiSel == 0) {
        wifiSvcConnectSaved();
        notifySvcPost(NOTIFY_INFO, "WIFI", "Connect saved", 1500);
      } else if (s_wifiSel == 1) {
        wifiSvcForget();
        notifySvcPost(NOTIFY_WARN, "WIFI", "Forgot", 1200);
      }
      wifiRenderAll();
      return;
    }

    // Scan view
    if (count > 0 && s_wifiSel >= 0) {
      const WifiSvcNet* net = wifiSvcScanResult(s_wifiSel);
      const char* selSsid = net ? net->ssid : nullptr;

      // If currently connected to this SSID, toggle disconnect.
      if (wifiSvcIsConnected() && selSsid && strcmp(selSsid, wifiSvcGetSSID()) == 0) {
        wifiSvcDisconnect();
        notifySvcPost(NOTIFY_WARN, "WIFI", "Disconnect", 1200);
      } else if (selSsid && wifiSvcHasCredsFor(selSsid)) {
        // Single saved network: connects only for saved SSID.
        wifiSvcConnectSaved();
        notifySvcPost(NOTIFY_INFO, "WIFI", "Connect saved", 1500);
      } else if (selSsid && net && net->auth == WIFI_AUTH_OPEN) {
        wifiSvcConnect(selSsid, "");
        notifySvcPost(NOTIFY_INFO, "WIFI", "Connect open", 1500);
      } else if (selSsid && net) {
        // Secured, missing creds -> keyboard
        strncpy(s_wifiSelSsid, selSsid, sizeof(s_wifiSelSsid)-1);
        s_wifiSelSsid[sizeof(s_wifiSelSsid)-1] = '\0';
        s_wifiPass[0] = '\0';
        kbStart("WIFI PASS", s_wifiPass, (uint8_t)sizeof(s_wifiPass), true);
        notifySvcPost(NOTIFY_INFO, "WIFI", "Enter pass", 900);
        return;
      }
    }
  }

  if (s_wifiSel != s_wifiPrevSel || s_wifiTop != s_wifiPrevTop ||
      wifiSvcScanCount() != s_wifiPrevCount ||
      (wifiSvcIsScanning()?1:0) != s_wifiPrevScanning ||
      wifiSvcGetState() != s_wifiPrevState) {
    wifiRenderAll();
  }
}

static void appWifiExit() {}

// ── PORT ────────────────────────────────────────────────────────────────────
static char s_portHost[32] = "192.168.1.1";
static int  s_portPrevProg = -999;
static char s_portPrevStatus[48] = "";
static int  s_portPrevIdx = -1;

static const int PORTS[] = { 22, 80, 443, 445, 3389, 5900, 8000, 8443 };
static const char* PORT_LBL[] = { "SSH", "HTTP", "HTTPS", "SMB", "RDP", "VNC", "8000", "8443" };

static void portRenderAll() {
  const char* gw = wifiSvcGetGateway();
  if (gw && gw[0]) {
    strncpy(s_portHost, gw, sizeof(s_portHost)-1);
    s_portHost[sizeof(s_portHost)-1] = '\0';
  }

  char line[64];
  snprintf(line, sizeof(line), "Target: %s", s_portHost);
  drawLine(0, false, line);

  int prog = taskProgress();
  const char* st = taskStatusLine();
  if (!st) st = "";
  snprintf(line, sizeof(line), "%s (%d%%)", st, (prog<0?0:prog));
  drawLine(1, false, line);

  const PortTaskState* ps = portTaskGetState();
  for (int i=0; i<PORT_TASK_MAX && i<VISIBLE_ROWS-2; i++) {
    char row[48];
    const PortScanEntry* e = &ps->ports[i];
    const char* r = (e->result < 0) ? "..." : (e->result ? "OPEN" : "--");
    snprintf(row, sizeof(row), "%s %4d %s", e->label, e->port, r);
    drawLine(i+2, false, row);
  }

  s_portPrevProg = prog;
  strncpy(s_portPrevStatus, st, sizeof(s_portPrevStatus)-1);
  s_portPrevStatus[sizeof(s_portPrevStatus)-1]='\0';
  s_portPrevIdx = ps->idx;
}

static void appPortEnter() {
  appChromeEnter("PORT", "TOOLS", "BACK menu");
  s_portPrevProg = -999; s_portPrevStatus[0]='\0'; s_portPrevIdx=-1;
  portRenderAll();
}

static void appPortTick() {
  if (Buttons::select.consume()) {
    if (!taskIsRunning()) {
      portTaskStart(s_portHost, PORTS, PORT_LBL, 8);
      notifySvcPost(NOTIFY_INFO, "PORT", "Scan", 1200);
    } else if (taskGetJob() == TASK_PORT_SCAN) {
      taskCancel();
      notifySvcPost(NOTIFY_WARN, "PORT", "Cancel", 1200);
    }
  }

  int prog = taskProgress();
  const char* st = taskStatusLine();
  if (!st) st = "";
  const PortTaskState* ps = portTaskGetState();

  if (prog != s_portPrevProg || strcmp(st, s_portPrevStatus) != 0 || ps->idx != s_portPrevIdx) {
    portRenderAll();
  }
}

static void appPortExit() {}

// ── NOTES ───────────────────────────────────────────────────────────────────
static int s_notesScroll = 0;
static int s_notesPrevScroll = -1;
static int s_notesPrevCount  = -1;
static int s_notesTestCounter = 0;

static void notesRenderAll() {
  int count = notesCount();
  if (count < 0) count = 0;
  char hdr[48];
  snprintf(hdr, sizeof(hdr), "%d lines", count);
  drawLine(0, false, hdr);

  int start = count - (VISIBLE_ROWS-1) - s_notesScroll;
  if (start < 0) start = 0;

  for (int r=0; r<VISIBLE_ROWS-1; r++) {
    int idx = start + r;
    const char* line = (idx < count) ? notesLine(idx) : "";
    drawLine(r+1, false, line ? line : "");
  }

  s_notesPrevScroll = s_notesScroll;
  s_notesPrevCount  = count;
}

static void appNotesEnter() {
  appChromeEnter("NOTES", "LOG", "BACK menu");
  s_notesScroll = 0;
  s_notesPrevScroll = -1;
  s_notesPrevCount  = -1;
  notesRenderAll();
}

static void appNotesTick() {
  int count = notesCount();
  int maxScroll = (count > (VISIBLE_ROWS-1)) ? (count - (VISIBLE_ROWS-1)) : 0;

  if (Buttons::up::consume()) { if (s_notesScroll < maxScroll) s_notesScroll++; }
  if (Buttons::down::consume()) { if (s_notesScroll > 0) s_notesScroll--; }

  if (Buttons::select.consume()) {
    char msg[48];
    snprintf(msg, sizeof(msg), "TEST %02d:%02d #%d", getClockHour(), getClockMin(), s_notesTestCounter++);
    notesAppend(msg);
    notifySvcPost(NOTIFY_OK, "NOTES", "Appended", 1200);
  }

  if (inputHoldSelect() && sdPresent) {
    notesSaveToSD();
    notifySvcPost(NOTIFY_OK, "NOTES", "Saved", 1500);
  }

  if (s_notesScroll != s_notesPrevScroll || notesCount() != s_notesPrevCount) {
    notesRenderAll();
  }
}

static void appNotesExit() {}

// ── CART ────────────────────────────────────────────────────────────────────
static int s_cartSel=0, s_cartTop=0;
static int s_cartPrevSel=-1, s_cartPrevTop=-1;
static uint16_t s_cartPrevCount=0xFFFF;
static bool s_cartPrevPresent=false, s_cartPrevLoaded=false;

static void cartRenderAll() {
  char hdr[64];
  snprintf(hdr, sizeof(hdr), "SD:%s CART:%s  %d payloads",
           sdPresent?"Y":"N",
           cartLoaded?"Y":"N",
           (int)payloadCount);
  drawLine(0, false, hdr);

  int count = (int)payloadCount;
  for (int r=0; r<VISIBLE_ROWS-1; r++) {
    int idx = s_cartTop + r;
    if (idx < 0 || idx >= count) { drawLine(r+1, false, ""); continue; }
    drawLine(r+1, idx==s_cartSel, payloadList[idx].label);
  }

  s_cartPrevSel = s_cartSel;
  s_cartPrevTop = s_cartTop;
  s_cartPrevCount = payloadCount;
  s_cartPrevPresent = sdPresent;
  s_cartPrevLoaded = cartLoaded;
}

static void appCartEnter() {
  appChromeEnter("CART", "SD", "BACK menu");
  s_cartSel=0; s_cartTop=0;
  s_cartPrevSel=-1; s_cartPrevTop=-1; s_cartPrevCount=0xFFFF;
  cartRenderAll();
}

static void appCartTick() {
  int count=(int)payloadCount;
  if (Buttons::up::consume()) { if (s_cartSel>0) s_cartSel--; }
  if (Buttons::down::consume()) { if (s_cartSel < count-1) s_cartSel++; }

  int win = VISIBLE_ROWS-1;
  if (s_cartSel < s_cartTop) s_cartTop = s_cartSel;
  if (s_cartSel >= s_cartTop + win) s_cartTop = s_cartSel - (win-1);
  if (s_cartTop < 0) s_cartTop = 0;

  if (Buttons::select.consume()) {
    if (sdPresent) {
      sdScanPayloads();
      sdLoadManifest();
      notifySvcPost(NOTIFY_OK, "SD", "Rescanned", 1500);
    } else {
      notifySvcPost(NOTIFY_WARN, "SD", "No card", 1500);
    }
  }

  if (s_cartSel!=s_cartPrevSel || s_cartTop!=s_cartPrevTop || payloadCount!=s_cartPrevCount ||
      sdPresent!=s_cartPrevPresent || cartLoaded!=s_cartPrevLoaded) {
    cartRenderAll();
  }
}

static void appCartExit() {}

// ── INJECT (DUCKY dry-run in Wokwi) ─────────────────────────────────────────
static int s_injSel=0, s_injTop=0;
static int s_injPrevSel=-1, s_injPrevTop=-1;
static int s_injCount=0;
static int s_injMap[16];
static char s_injLog[3][48];
static int s_injLogDirty=0;

static void injectLogCb(const char* line) {
  for (int i=0;i<2;i++) {
    strncpy(s_injLog[i], s_injLog[i+1], sizeof(s_injLog[i])-1);
    s_injLog[i][sizeof(s_injLog[i])-1]='\0';
  }
  if (!line) line = "";
  strncpy(s_injLog[2], line, sizeof(s_injLog[2])-1);
  s_injLog[2][sizeof(s_injLog[2])-1]='\0';
  s_injLogDirty = 1;
}

static void injectRebuildList() {
  s_injCount = 0;
  for (int i=0; i<(int)payloadCount && s_injCount < (int)(sizeof(s_injMap)/sizeof(s_injMap[0])); i++) {
    if (true /*all payloads are ducky*/) {
      s_injMap[s_injCount++] = i;
    }
  }
  if (s_injSel >= s_injCount) s_injSel = (s_injCount>0) ? (s_injCount-1) : 0;
}

static void injectRenderAll() {
  char hdr[64];
  const char* st = taskIsRunning()?taskStatusLine():"IDLE";
  if (!st) st = "";
  snprintf(hdr, sizeof(hdr), "DUCKY:%d  %s", s_injCount, st);
  drawLine(0, false, hdr);

  int win = VISIBLE_ROWS-3;
  for (int r=0; r<win; r++) {
    int idx = s_injTop + r;
    if (idx < 0 || idx >= s_injCount) { drawLine(r+1, false, ""); continue; }
    int pi = s_injMap[idx];
    drawLine(r+1, idx==s_injSel, payloadList[pi].label);
  }

  drawLine(VISIBLE_ROWS-2, false, s_injLog[1]);
  drawLine(VISIBLE_ROWS-1, false, s_injLog[2]);

  s_injPrevSel=s_injSel; s_injPrevTop=s_injTop;
  s_injLogDirty=0;
}

static void appInjectEnter() {
  appChromeEnter("INJECT", "DUCKY", "BACK menu");
  s_injSel=0; s_injTop=0; s_injPrevSel=-1; s_injPrevTop=-1;
  for (int i=0;i<3;i++) s_injLog[i][0]='\0';
  injectRebuildList();
  injectRenderAll();
}

static void appInjectTick() {
  injectRebuildList();
  int count = s_injCount;

  // Run cooperative ducky engine (non-blocking). No HID in Wokwi; this is DRY RUN.
  if (duckyIsRunning()) {
    duckyTick();
  }

  if (Buttons::up::consume()) { if (s_injSel > 0) s_injSel--; }
  if (Buttons::down::consume()) { if (s_injSel < count - 1) s_injSel++; }

  int win = VISIBLE_ROWS - 3;
  if (s_injSel < s_injTop) s_injTop = s_injSel;
  if (s_injSel >= s_injTop + win) s_injTop = s_injSel - (win - 1);
  if (s_injTop < 0) s_injTop = 0;

  if (Buttons::select.consume()) {
    if (!sdPresent) {
      notifySvcPost(NOTIFY_WARN, "HID", "No SD", 1500);
    } else if (count == 0) {
      notifySvcPost(NOTIFY_WARN, "HID", "No scripts", 1500);
    } else if (!duckyIsRunning()) {
      int pi = s_injMap[s_injSel];
      int lines = duckyLoad(payloadList[pi].filename);
      if (lines <= 0) {
        notifySvcPost(NOTIFY_ERROR, "HID", "Load fail", 2000);
      } else {
        duckyStart(injectLogCb);
        notifySvcPost(NOTIFY_WARN, "HID", "DRY RUN", 1500);
      }
    } else {
      duckyStop();
      notifySvcPost(NOTIFY_WARN, "HID", "Cancel", 1200);
    }
  }

  if (s_injSel != s_injPrevSel || s_injTop != s_injPrevTop || s_injLogDirty) {
    injectRenderAll();
  }
}

static void appInjectExit() {}

// ── SETTINGS ────────────────────────────────────────────────────────────────
static int s_setSel=0;
static int s_setPrevSel=-1;
static int s_setPrevTheme=-1;
static int s_setPrevBright=-1;
static int s_setPrevSound=-1;

static void settingsRenderAll() {
  char line[64];

  snprintf(line, sizeof(line), "Theme: %d/%d", (int)themeIndex+1, THEME_COUNT);
  drawLine(0, s_setSel==0, line);

  snprintf(line, sizeof(line), "Brightness: %d%%", (int)uiBrightness);
  drawLine(1, s_setSel==1, line);

  snprintf(line, sizeof(line), "FX Sound: %s", fxSound?"ON":"OFF");
  drawLine(2, s_setSel==2, line);

  drawLine(3, false, "SELECT: change");
  drawLine(4, false, "HOLD SELECT: save");
  drawLine(5, false, "");

  s_setPrevSel = s_setSel;
  s_setPrevTheme = (int)themeIndex;
  s_setPrevBright = (int)uiBrightness;
  s_setPrevSound = fxSound?1:0;
}

static void appSettingsEnter() {
  appChromeEnter("SETTINGS", "THEME", "BACK menu");
  s_setSel=0; s_setPrevSel=-1;
  s_setPrevTheme=-1; s_setPrevBright=-1; s_setPrevSound=-1;
  settingsRenderAll();
}

static void appSettingsTick() {
  if (Buttons::up::consume()) { if (s_setSel>0) s_setSel--; }
  if (Buttons::down::consume()) { if (s_setSel<2) s_setSel++; }

  if (Buttons::select.consume()) {
    if (s_setSel==0) {
      int next = (themeIndex+1) % THEME_COUNT;
      setThemeIndex(next, true);
      notifySvcPost(NOTIFY_OK, "THEME", themes[next].name, 1200);
      appChromeEnter("SETTINGS", "THEME", "BACK menu");
    } else if (s_setSel==1) {
      uint8_t b = uiBrightness;
      b = (uint8_t)((b >= 100) ? 0 : (b + 10));
      settingsSetBrightness(b, true);
      notifySvcPost(NOTIFY_INFO, "BRIGHT", "Saved", 800);
    } else if (s_setSel==2) {
      settingsSetFxSound(!fxSound, true);
      notifySvcPost(NOTIFY_INFO, "SOUND", fxSound?"ON":"OFF", 800);
    }
  }

  if (inputHoldSelect()) {
    notifySvcPost(NOTIFY_OK, "SET", "Saved", 1000);
  }

  if (s_setSel!=s_setPrevSel || (int)themeIndex!=s_setPrevTheme || (int)uiBrightness!=s_setPrevBright || (fxSound?1:0)!=s_setPrevSound) {
    settingsRenderAll();
  }
}

static void appSettingsExit() {}

// ── SYSTEM (basic live view) ────────────────────────────────────────────────
static void systemResetState();

static void appSystemEnter() {
  appChromeEnter("SYSTEM", "TABS", "BACK menu");
  systemResetState();
}

enum SystemTab : uint8_t {
  SYS_TAB_SENSORS = 0,
  SYS_TAB_RF,
  SYS_TAB_IDENT,
  SYS_TAB_STATUS,
  SYS_TAB_COUNT
};

static const int SYS_TAB_Y = BODY_Y + 2;
static const int SYS_TAB_H = 14;
static const int SYS_BODY_TOP = SYS_TAB_Y + SYS_TAB_H + 4;
static const int SYS_BODY_H = H - FOOTER_H - SYS_BODY_TOP;
static const int SYS_SEG_THICK = 4;

struct SegDial {
  int16_t cx;
  int16_t cy;
  int16_t minV;
  int16_t maxV;
  int16_t warnV;
  int16_t critV;
  uint8_t segCount;
  int16_t sx0[24];
  int16_t sy0[24];
  int16_t sx1[24];
  int16_t sy1[24];
  int16_t prevActive;
};

static SegDial s_sysDials[3];
static bool s_sysDialGeomInit = false;
static int s_sysTab = 0;
static int s_sysPrevTab = -1;
static bool s_sysFocus = false;
static uint32_t s_sysLastMs = 0;
static uint8_t s_rfBlinkState = 0;

static void systemResetState() {
  s_sysTab = 0;
  s_sysPrevTab = -1;
  s_sysFocus = false;
  s_sysLastMs = 0;
}

static int clampI32(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static uint16_t dialZoneColor(const SegDial& d, uint8_t idx) {
  int valueAtSeg = d.minV + (((int)(idx + 1) * (d.maxV - d.minV)) / d.segCount);
  if (valueAtSeg >= d.critV) return COL_BAD();
  if (valueAtSeg >= d.warnV) return COL_WARN();
  return COL_HILITE();
}

static void dialInitGeom(SegDial& d) {
  const int r0 = 18;
  const int r1 = 26;
  const int a0 = -150;
  const int a1 =  150;
  for (uint8_t i = 0; i < d.segCount; i++) {
    int a = a0 + ((a1 - a0) * (int)i) / (int)(d.segCount - 1);
    float ar = (float)a * 0.0174532925f;
    int cs = (int)(cosf(ar) * 1024.0f);
    int sn = (int)(sinf(ar) * 1024.0f);
    d.sx0[i] = d.cx + (int16_t)((r0 * cs) / 1024);
    d.sy0[i] = d.cy + (int16_t)((r0 * sn) / 1024);
    d.sx1[i] = d.cx + (int16_t)((r1 * cs) / 1024);
    d.sy1[i] = d.cy + (int16_t)((r1 * sn) / 1024);
  }
  d.prevActive = -1;
}

static int dialActiveSegments(const SegDial& d, int value) {
  int clamped = clampI32(value, d.minV, d.maxV);
  int span = d.maxV - d.minV;
  if (span <= 0) return 0;
  return clampI32(((clamped - d.minV) * d.segCount) / span, 0, d.segCount);
}

static void dialDrawDelta(SegDial& d, int value) {
  int active = dialActiveSegments(d, value);
  if (active == d.prevActive) return;

  int lo = d.prevActive;
  int hi = active;
  if (lo > hi) { int t = lo; lo = hi; hi = t; }
  if (lo < 0) lo = 0;

  for (int i = lo; i < hi; i++) {
    bool on = (i < active);
    uint16_t col = on ? dialZoneColor(d, (uint8_t)i) : COL_DARK();
    tft.drawLine(d.sx0[i], d.sy0[i], d.sx1[i], d.sy1[i], col);
    tft.drawLine(d.sx0[i], d.sy0[i] + 1, d.sx1[i], d.sy1[i] + 1, col);
  }
  d.prevActive = active;
}

static void systemDrawTabs() {
  static const char* TABS[SYS_TAB_COUNT] = { "SENSORS", "RF", "IDENT", "STATUS" };
  static const int TAB_W[SYS_TAB_COUNT]  = { 70, 40, 56, 62 };

  int x = 8;
  for (int i = 0; i < SYS_TAB_COUNT; i++) {
    bool active = (i == s_sysTab);
    uint16_t fg = active ? COL_BG() : COL_FG();
    uint16_t bg = active ? COL_HILITE() : COL_BG();
    tft.fillRect(x, SYS_TAB_Y, TAB_W[i], SYS_TAB_H, bg);
    tft.drawRect(x, SYS_TAB_Y, TAB_W[i], SYS_TAB_H, COL_DARK());
    tft.setTextSize(1);
    tft.setTextColor(fg, bg);
    tft.setCursor(x + 4, SYS_TAB_Y + 3);
    tft.print(TABS[i]);
    x += TAB_W[i] + 4;
  }
}

static void systemClearBody() {
  tft.fillRect(0, SYS_BODY_TOP, W, SYS_BODY_H, COL_BG());
}

static void systemInitDials() {
  s_sysDials[0] = { 56, SYS_BODY_TOP + 46, 50, 140, 95, 110, 20, {0}, {0}, {0}, {0}, -1 };
  s_sysDials[1] = { 160, SYS_BODY_TOP + 46, 40, 110, 85, 100, 20, {0}, {0}, {0}, {0}, -1 };
  s_sysDials[2] = { 264, SYS_BODY_TOP + 46, 400, 2000, 1000, 1500, 20, {0}, {0}, {0}, {0}, -1 };
  for (int i = 0; i < 3; i++) dialInitGeom(s_sysDials[i]);
  s_sysDialGeomInit = true;
}

static void systemDrawSensorStatic() {
  tft.setTextSize(1);
  tft.setTextColor(COL_DIM(), COL_BG());
  tft.setCursor(26, SYS_BODY_TOP + 8); tft.print("IR OBJ");
  tft.setCursor(132, SYS_BODY_TOP + 8); tft.print("IR AMB");
  tft.setCursor(238, SYS_BODY_TOP + 8); tft.print("AIR");
  tft.drawFastHLine(8, SYS_BODY_TOP + 84, W - 16, COL_DARK());
}

static void systemDrawHSeg(int x, int y, int segs, int active, uint16_t col) {
  const int sw = 8;
  const int sh = SYS_SEG_THICK;
  for (int i = 0; i < segs; i++) {
    uint16_t c = (i < active) ? col : COL_DARK();
    tft.fillRect(x + i * (sw + 2), y, sw, sh, c);
  }
}

static void appSystemTick() {
  if (!s_sysDialGeomInit) systemInitDials();

  if (!s_sysFocus) {
    if (Buttons::up::consume() && s_sysTab > 0) s_sysTab--;
    if (Buttons::down::consume() && s_sysTab < (SYS_TAB_COUNT - 1)) s_sysTab++;
    if (Buttons::select.consume()) s_sysFocus = true;
  } else if (Buttons::back.consume()) {
    s_sysFocus = false;
  }

  uint32_t now = millis();
  bool tabChanged = (s_sysTab != s_sysPrevTab);
  bool due = (now - s_sysLastMs) >= 250;
  if (!tabChanged && !due) return;
  if (due) s_sysLastMs = now;

  if (tabChanged) {
    systemDrawTabs();
    systemClearBody();
    for (int i = 0; i < 3; i++) s_sysDials[i].prevActive = -1;
    s_sysPrevTab = s_sysTab;
  }

  if (s_sysTab == SYS_TAB_SENSORS) {
    if (tabChanged) systemDrawSensorStatic();
    int objF = 0;
    int ambF = 0;
    if (tempIrHasData()) {
      objF = (int)((tempIrObjectC() * 9.0f / 5.0f) + 32.0f);
      ambF = (int)((tempIrAmbientC() * 9.0f / 5.0f) + 32.0f);
    }
    int co2 = (int)airSvcCO2ppm();
    dialDrawDelta(s_sysDials[0], objF);
    dialDrawDelta(s_sysDials[1], ambF);
    dialDrawDelta(s_sysDials[2], co2);

    systemDrawHSeg(16,  SYS_BODY_TOP + 96, 10, clampI32((int)((millis() / 1000UL) % 100) / 10, 0, 10), COL_HILITE());
    systemDrawHSeg(120, SYS_BODY_TOP + 96, 10, wifiSvcIsConnected() ? 9 : 2, COL_DIM());
    systemDrawHSeg(224, SYS_BODY_TOP + 96, 10, clampI32((int)perfGetCpuPercent() / 10, 0, 10), COL_WARN());
  } else if (s_sysTab == SYS_TAB_RF) {
    if (Buttons::select.consume()) {
      rfidSvcSetArmed(!rfidSvcIsArmed());
    }
    if ((now / 400) != ((now - 250) / 400)) s_rfBlinkState ^= 1;
    tft.drawRect(12, SYS_BODY_TOP + 6, 144, 48, COL_DARK());
    tft.drawRect(164, SYS_BODY_TOP + 6, 144, 48, COL_DARK());
    tft.fillRect(20, SYS_BODY_TOP + 16, 8, 8, rfidSvcIsArmed() && s_rfBlinkState ? COL_WARN() : COL_DARK());
    tft.fillRect(172, SYS_BODY_TOP + 16, 8, 8, COL_DARK());
    tft.setTextSize(1);
    tft.setTextColor(COL_FG(), COL_BG());
    tft.setCursor(32, SYS_BODY_TOP + 16); tft.print("RFID");
    tft.setCursor(184, SYS_BODY_TOP + 16); tft.print("NFC");
    char uid[RFID_UID_STR_LEN]; uid[0] = '\0';
    (void)rfidSvcGetLastUid(uid, (int)sizeof(uid));
    tft.fillRect(12, SYS_BODY_TOP + 64, W - 24, 28, COL_BG());
    tft.setCursor(16, SYS_BODY_TOP + 68); tft.print("UID:"); tft.print(uid[0] ? uid : "(none)");
    tft.setCursor(16, SYS_BODY_TOP + 80); tft.print("Reads:"); tft.print((unsigned long)rfidSvcScanCount());
  } else if (s_sysTab == SYS_TAB_IDENT) {
    const char* ip = wifiSvcGetIP(); if (!ip) ip = "-";
    tft.drawRect(12, SYS_BODY_TOP + 6, W - 24, 90, COL_DARK());
    tft.drawFastVLine(110, SYS_BODY_TOP + 6, 90, COL_DARK());
    tft.setTextSize(1);
    tft.setTextColor(COL_DIM(), COL_BG());
    tft.setCursor(18, SYS_BODY_TOP + 14); tft.print("FW");
    tft.setCursor(18, SYS_BODY_TOP + 28); tft.print("UPTIME");
    tft.setCursor(18, SYS_BODY_TOP + 42); tft.print("MAC");
    tft.setCursor(18, SYS_BODY_TOP + 56); tft.print("IP");
    tft.setCursor(18, SYS_BODY_TOP + 70); tft.print("GW");
    tft.setTextColor(COL_FG(), COL_BG());
    tft.setCursor(118, SYS_BODY_TOP + 14); tft.print(FW_VERSION);
    tft.setCursor(118, SYS_BODY_TOP + 28); tft.print((unsigned long)(millis() / 1000UL)); tft.print("s");
    tft.setCursor(118, SYS_BODY_TOP + 42); tft.print(WiFi.macAddress());
    tft.setCursor(118, SYS_BODY_TOP + 56); tft.print(ip);
    tft.setCursor(118, SYS_BODY_TOP + 70); tft.print(WiFi.gatewayIP());
  } else {
    systemDrawHSeg(20, SYS_BODY_TOP + 18, 12, clampI32((int)(ESP.getFreeHeap() / 12000), 0, 12), COL_HILITE());
    systemDrawHSeg(20, SYS_BODY_TOP + 38, 12, wifiSvcIsConnected() ? 12 : 3, COL_WARN());
    systemDrawHSeg(20, SYS_BODY_TOP + 58, 12, sdPresent ? 12 : 0, COL_DIM());
    systemDrawHSeg(20, SYS_BODY_TOP + 78, 12, bleSvcIsEnabled() ? 12 : 0, COL_BAD());
    tft.setTextSize(1);
    tft.setTextColor(COL_DIM(), COL_BG());
    tft.setCursor(140, SYS_BODY_TOP + 18); tft.print("HEAP");
    tft.setCursor(140, SYS_BODY_TOP + 38); tft.print("WIFI");
    tft.setCursor(140, SYS_BODY_TOP + 58); tft.print("SD");
    tft.setCursor(140, SYS_BODY_TOP + 78); tft.print("BLE");
  }
}

static void appSystemExit() {
  s_sysFocus = false;
}

App apps[] = {
  { "ANIM",     "DEMO",     appAnimEnter,     appAnimTick,     appAnimExit },
  { "WIFI",     "SCAN",     appWifiEnter,     appWifiTick,     appWifiExit },
  { "PORT",     "TOOLS",    appPortEnter,     appPortTick,     appPortExit },
  { "NOTES",    "LOG",      appNotesEnter,    appNotesTick,    appNotesExit },
  { "CART",     "SD",       appCartEnter,     appCartTick,     appCartExit },
  { "INJECT",   "DUCKY",    appInjectEnter,   appInjectTick,   appInjectExit },
  { "SETTINGS", "THEME",    appSettingsEnter, appSettingsTick, appSettingsExit },
  { "SYSTEM",   "STATUS",   appSystemEnter,   appSystemTick,   appSystemExit },
};

const int APP_COUNT = (int)(sizeof(apps) / sizeof(apps[0]));

// ─────────────────────────────────────────────────────────────────────────────
// App tick dispatcher
// ─────────────────────────────────────────────────────────────────────────────

void appsTick() {
  // During transition, only tick the transition (no app input)
  if (transitionActive()) {
    transitionTick();
    return;
  }

  if (runningApp < 0 || runningApp >= APP_COUNT) return;

  // BACK: normally exits app. If the rotary keyboard is active, BACK is reserved
  // for the keyboard (cancel/confirm) and must not exit the app.
  if (!kbActive() && Buttons::back.consume()) {
    if (fxSound) hapticsPattern(HAPTIC_CLICK);
    if (apps[runningApp].exit) apps[runningApp].exit();
    exitAppTransition();
    return;
  }

  // Delegate per-app
  if (apps[runningApp].tick) apps[runningApp].tick();
}
