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
static void appSystemEnter() {
  appChromeEnter("SYSTEM", "STATUS", "BACK menu");
}

// Alien-style bars for SYSTEM/SENS pages (row-level redraw)
static void drawBarRow(int row, const char* label, float value, float vmin, float vmax, uint16_t col) {
  char txt[32];
  snprintf(txt, sizeof(txt), "%s", label);
  drawLine(row, false, txt);

  int x0 = 120;
  int y0 = BODY_Y + row*LINE_H;
  int w  = 240 - x0 - 10;
  int h  = LINE_H;

  tft.fillRect(x0, y0, w, h, COL_DARK());

  float t = 0.0f;
  if (vmax > vmin) t = (value - vmin) / (vmax - vmin);
  if (t < 0) t = 0;
  if (t > 1) t = 1;
  int bw = (int)(t * (float)w);
  if (bw > 0) tft.fillRect(x0, y0, bw, h, col);

  char vbuf[20];
  if (vmax >= 1000) snprintf(vbuf, sizeof(vbuf), "%.0f", value);
  else snprintf(vbuf, sizeof(vbuf), "%.1f", value);
  tft.setTextSize(1);
  tft.setTextColor(COL_FG(), COL_BG());
  tft.setCursor(x0 + 2, y0 + 2);
  tft.print(vbuf);
}

static void appSystemTick() {
  // Pages: 0=PERF, 1=SENS, 2=RFID/NFC
  static int s_page = 0;
  static int s_prevPage = -1;
  static uint32_t last = 0;

  if (Buttons::up::consume())   { if (s_page > 0) s_page--; }
  if (Buttons::down::consume()) { if (s_page < 2) s_page++; }

  uint32_t now = millis();
  bool timeDue = (now - last) >= 500;
  bool pageChanged = (s_page != s_prevPage);

  if (!timeDue && !pageChanged) return;
  if (timeDue) last = now;

  // Clear all rows on page change to avoid leftovers.
  if (pageChanged) {
    for (int i = 0; i < 6; i++) drawLine(i, false, "");
    s_prevPage = s_page;
  }

  if (s_page == 0) {
    // PERF (Alien bars)
    drawBarRow(0, "CPU%", (float)perfGetCpuPercent(), 0, 100, COL_HILITE());
    drawBarRow(1, "FPS",  (float)perfGetFps(),        0, 60,  COL_DIM());
    drawBarRow(2, "DRAW", (float)perfGetDrawCount(),  0, 400, COL_DIM());

    const char* ip = wifiSvcGetIP(); if (!ip) ip = "";
    char l3[48];
    snprintf(l3, sizeof(l3), "IP:%s", ip);
    drawLine(3, false, l3);

    char l4[48];
    snprintf(l4, sizeof(l4), "SD:%s Payloads:%d", sdPresent ? "Y" : "N", (int)payloadCount);
    drawLine(4, false, l4);

    drawLine(5, false, "UP/DN: Pages");
  } else if (s_page == 1) {
    // SENSORS (threshold bands)
    const float TEMP_WARN_F = 95.0f;
    const float TEMP_BAD_F  = 110.0f;
    const float AIR_SAFE_MAX = 1000.0f;
    const float AIR_WARN_MAX = 1500.0f;

    float objF = 0.0f;
    float ambF = 0.0f;
    if (tempIrHasData()) {
      objF = tempIrObjectC() * 9.0f/5.0f + 32.0f;
      ambF = tempIrAmbientC() * 9.0f/5.0f + 32.0f;
    }
    uint16_t tcol = COL_OK();
    if (objF >= TEMP_BAD_F) tcol = COL_BAD();
    else if (objF >= TEMP_WARN_F) tcol = COL_WARN();
    drawBarRow(0, "IR OBJ F", objF, 60, 140, tcol);
    drawBarRow(1, "IR AMB F", ambF, 40, 110, COL_DIM());

    float co2 = (float)airSvcCO2ppm();
    uint16_t acol = COL_OK();
    if (co2 >= AIR_WARN_MAX) acol = COL_BAD();
    else if (co2 >= AIR_SAFE_MAX) acol = COL_WARN();
    drawBarRow(2, "CO2 PPM", co2, 400, 2000, acol);

    char l3[48];
    snprintf(l3, sizeof(l3), "AIR LVL:%d", (int)airSvcAlertLevel());
    drawLine(3, false, l3);
    drawLine(4, false, "UP/DN: Pages");
    drawLine(5, false, "");
  } else {
    // RFID / NFC
    if (Buttons::select.consume()) {
      rfidSvcSetArmed(!rfidSvcIsArmed());
      notifySvcPost(NOTIFY_INFO, "RFID", rfidSvcIsArmed() ? "ARMED" : "OFF", 800);
    }

    char l0[48];
    snprintf(l0, sizeof(l0), "RFID:%s  Reads:%lu", rfidSvcIsArmed() ? "ARM" : "OFF", (unsigned long)rfidSvcScanCount());
    drawLine(0, false, l0);

    char uid[RFID_UID_STR_LEN];
    uid[0] = '\0';
    (void)rfidSvcGetLastUid(uid, (int)sizeof(uid));
    char l1[48];
    snprintf(l1, sizeof(l1), "UID:%s", uid[0] ? uid : "(none)");
    drawLine(1, false, l1);

    drawLine(2, false, "NFC: spec-only");
    drawLine(3, false, "(no svc_nfc.cpp yet)");
    drawLine(4, false, "SEL: toggle RFID");
    drawLine(5, false, "UP/DN: Pages");
  }
}

static void appSystemExit() {}

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
