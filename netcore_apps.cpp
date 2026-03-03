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
#include "svc_dial.h"
#include "ui_frame.h"
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
enum CartView : uint8_t { CV_PAYLOADS=0, CV_APPS=1, CV_RUN=2 };

static CartView s_cartView = CV_PAYLOADS;
static int s_cartSel=0, s_cartTop=0;
static int s_cartPrevSel=-1, s_cartPrevTop=-1;
static uint16_t s_cartPrevCount=0xFFFF;
static bool s_cartPrevPresent=false, s_cartPrevLoaded=false;

// Run/intro state (deterministic time-based, no blocking).
static bool s_cartIntro = false;
static uint32_t s_cartIntroStart = 0;
static int s_cartRunApp = -1;
static int s_cartRunLines = 0;
static int s_cartRunScroll = 0;

static void cartResetCache() {
  s_cartPrevSel=-1; s_cartPrevTop=-1; s_cartPrevCount=0xFFFF;
  s_cartPrevPresent=false; s_cartPrevLoaded=false;
}

static void cartStartRun(int appIdx) {
  if (!sdPresent || !cartLoaded) {
    notifySvcPost(NOTIFY_WARN, "SD", "No cartridge", 1500);
    return;
  }
  if (appIdx < 0 || appIdx >= cartInfo.appCount) return;
  const char* file = cartInfo.apps[appIdx].file;
  if (!file || !file[0]) {
    notifySvcPost(NOTIFY_WARN, "CART", "Missing file", 1500);
    return;
  }

  // Load script into the existing SD script line buffer.
  s_cartRunLines = sdRunScript(file, 24);
  s_cartRunScroll = 0;
  s_cartRunApp = appIdx;

  // 1.5s intro animation window.
  s_cartIntro = true;
  s_cartIntroStart = millis();
  s_cartView = CV_RUN;
  cartResetCache();
}

static void cartRenderAll() {
  // Header
  char hdr[64];
  const char* viewStr = (s_cartView==CV_PAYLOADS) ? "PAYLOADS" : (s_cartView==CV_APPS) ? "APPS" : "RUN";
  snprintf(hdr, sizeof(hdr), "SD:%s CART:%s  %s",
           sdPresent?"Y":"N",
           cartLoaded?"Y":"N",
           viewStr);
  drawLine(0, false, hdr);

  // View body
  if (s_cartView == CV_PAYLOADS) {
    int count = (int)payloadCount;
    for (int r=0; r<VISIBLE_ROWS-1; r++) {
      int idx = s_cartTop + r;
      if (idx < 0 || idx >= count) { drawLine(r+1, false, ""); continue; }
      drawLine(r+1, idx==s_cartSel, payloadList[idx].label);
    }
  } else if (s_cartView == CV_APPS) {
    int count = (sdPresent && cartLoaded) ? cartInfo.appCount : 0;
    for (int r=0; r<VISIBLE_ROWS-1; r++) {
      int idx = s_cartTop + r;
      if (idx < 0 || idx >= count) { drawLine(r+1, false, ""); continue; }
      char row[48];
      snprintf(row, sizeof(row), "%s", cartInfo.apps[idx].name);
      drawLine(r+1, idx==s_cartSel, row);
    }
    if (!count) {
      drawLine(1, false, "(no cartridge apps)");
    }
  } else { // CV_RUN
    // Intro progress bar + last lines of script.
    const uint16_t accent = sdCartAccentColor();
    const char* name = (sdPresent && cartLoaded && s_cartRunApp>=0) ? cartInfo.apps[s_cartRunApp].name : "NO CART";

    // Line 1: module name
    char row[48];
    snprintf(row, sizeof(row), "MOD: %s", name);
    drawLine(1, false, row);

    // Line 2: intro bar (drawn as text bar for minimal overdraw)
    uint32_t now = millis();
    uint32_t dt = (uint32_t)(now - s_cartIntroStart);
    if (s_cartIntro && dt < 1500) {
      int p = (int)((dt * 20UL) / 1500UL); // 0..20
      if (p < 0) p = 0; if (p > 20) p = 20;
      char bar[48];
      int k=0;
      bar[k++]='[';
      for (int i=0;i<20;i++) bar[k++] = (i<p) ? '#' : '.';
      bar[k++]=']';
      bar[k]='\0';
      drawLine(2, false, bar);
    } else {
      // completed
      drawLine(2, false, "[####################]");
      s_cartIntro = false;
    }

    // Lines 3..: script output window (last N)
    int lines = sdScriptLineCount();
    int win = (VISIBLE_ROWS-1) - 3;
    if (win < 1) win = 1;
    int start = lines - win - s_cartRunScroll;
    if (start < 0) start = 0;
    for (int r=0; r<win; r++) {
      int li = start + r;
      const char* l = (li < lines) ? sdScriptLine(li) : "";
      drawLine(3+r, false, l);
    }

    // Accent bracket (tiny, contained draw)
    // Draw a small accent marker at top-right of the chrome row.
    tft.fillRect(SCREEN_W-6, 1, 4, 4, accent);
  }

  s_cartPrevSel = s_cartSel;
  s_cartPrevTop = s_cartTop;
  s_cartPrevCount = payloadCount;
  s_cartPrevPresent = sdPresent;
  s_cartPrevLoaded = cartLoaded;
}

static void appCartEnter() {
  appChromeEnter("CART", "SD", "BACK menu");
  s_cartView = CV_PAYLOADS;
  s_cartSel=0; s_cartTop=0;
  s_cartIntro=false; s_cartRunApp=-1; s_cartRunLines=0; s_cartRunScroll=0;
  cartResetCache();
  cartRenderAll();
}

static void appCartTick() {
  // View switch: SELECT-hold toggles PAYLOADS/APPS when not running.
  if (inputHoldSelect()) {
    if (s_cartView == CV_PAYLOADS) s_cartView = CV_APPS;
    else if (s_cartView == CV_APPS) s_cartView = CV_PAYLOADS;
    cartResetCache();
  }

  // Navigation differs per view.
  int count = 0;
  if (s_cartView == CV_PAYLOADS) count = (int)payloadCount;
  else if (s_cartView == CV_APPS) count = (sdPresent && cartLoaded) ? cartInfo.appCount : 0;
  else count = 1;

  if (Buttons::up::consume()) {
    if (s_cartView == CV_RUN) {
      if (s_cartRunScroll < 50) s_cartRunScroll++;
    } else {
      if (s_cartSel>0) s_cartSel--;
    }
  }
  if (Buttons::down::consume()) {
    if (s_cartView == CV_RUN) {
      if (s_cartRunScroll > 0) s_cartRunScroll--;
    } else {
      if (s_cartSel < count-1) s_cartSel++;
    }
  }

  if (s_cartView != CV_RUN) {
    int win = VISIBLE_ROWS-1;
    if (s_cartSel < s_cartTop) s_cartTop = s_cartSel;
    if (s_cartSel >= s_cartTop + win) s_cartTop = s_cartSel - (win-1);
    if (s_cartTop < 0) s_cartTop = 0;
  }

  if (Buttons::select.consume()) {
    if (s_cartView == CV_PAYLOADS) {
      // RESCAN action in payload view
      if (sdPresent) {
        sdScanPayloads();
        sdLoadManifest();
        notifySvcPost(NOTIFY_OK, "SD", "Rescanned", 1500);
      } else {
        notifySvcPost(NOTIFY_WARN, "SD", "No card", 1500);
      }
    } else if (s_cartView == CV_APPS) {
      // Launch selected cartridge app
      cartStartRun(s_cartSel);
    } else {
      // In RUN view: SELECT returns to APPS
      s_cartView = CV_APPS;
      s_cartRunScroll = 0;
      cartResetCache();
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

// ── SYSTEM (tabbed, no-scroll) ───────────────────────────────────────────────
// Tabs: [SENSORS] [RF] [IDENT] [STATUS]
// Rotary up/down switches tabs. SELECT currently toggles "focus" (reserved).
// NOTE: BACK exits app (framework-level). Focus-exit via BACK is not supported
// without changing appsTick() behavior.
enum SystemTab : uint8_t {
  TAB_SENSORS = 0,
  TAB_RF      = 1,
  TAB_IDENT   = 2,
  TAB_STATUS  = 3,
  TAB_COUNT   = 4
};

static uint8_t s_sysTab = TAB_SENSORS;
static uint8_t s_sysPrevTab = 0xFF;
static bool    s_sysFocus = false;

// Content region (below chrome header)
static const int SYS_X0 = 0;
static const int SYS_Y0 = 24;   // below appChrome header (uses y=0..23)
static const int SYS_W  = 240;
static const int SYS_H  = 296 - SYS_Y0;

// Tab strip (PASS 8 visual discipline)
// 4px grid baseline + consistent padding.
static const int TAB_H  = 20;
static const int TAB_Y  = SYS_Y0;
static const int TAB_X  = 8;
static const int TAB_W  = SYS_W - 16;

// Content starts below tabs
static const int BODY_Y = TAB_Y + TAB_H + 4;
static const int BODY_H = 296 - BODY_Y;

// Dial engine: reusable segmented radial gauges (svc_dial).
static bool s_sysDialsConfigured = false;

// RF tab cache
static uint32_t s_rfPrevCount = 0;
static uint32_t s_rfPrevLastMs = 0;
static char     s_rfPrevUid[RFID_UID_STR_LEN] = {0};

// IDENT cache
static uint32_t s_identPrevUptimeS = 0;
static char     s_identPrevIP[16] = {0};
static char     s_identPrevGW[16] = {0};
static char     s_identPrevSSID[33] = {0};

// STATUS cache
static int      s_statPrevCpu = -1;
static int      s_statPrevFps = -1;
static int      s_statPrevDraw = -1;
static uint32_t s_statPrevHeap = 0;
static bool     s_statPrevSd = false;
static bool     s_statPrevCart = false;
static WifiSvcState s_statPrevWifi = (WifiSvcState)0xFF;

static inline void sysClearBody() {
  tft.fillRect(SYS_X0, SYS_Y0, SYS_W, SYS_H, COL_BG());
}

static void sysDrawTabs() {
  // Tab strip background
  tft.fillRect(SYS_X0, SYS_Y0, SYS_W, TAB_H + 4, COL_BG());

  const char* names[TAB_COUNT] = { "SENSORS", "RF", "IDENT", "STATUS" };
  const int tabW = TAB_W / TAB_COUNT;

  for (int i = 0; i < TAB_COUNT; i++) {
    int x = TAB_X + i * tabW;
    int w = tabW - 2;
    bool active = (i == s_sysTab);

    tft.drawRect(x, TAB_Y, w, TAB_H, active ? COL_HILITE() : COL_DIM());
    if (active) {
      tft.fillRect(x + 1, TAB_Y + 1, w - 2, TAB_H - 2, COL_DARK());
    }
    tft.setTextColor(active ? COL_HILITE() : COL_DIM());
    // Consistent baseline: 4px grid, optical center for 20px tab height.
    tft.setCursor(x + 4, TAB_Y + 7);
    tft.print(names[i]);
  }
}

static void sysDialEnsureConfigured() {
  if (s_sysDialsConfigured) return;
  dialInit();
  // 3 top-row dials. Geometry is precomputed at init time inside svc_dial.
  // PASS 8: 4px grid alignment (panels at x=8,84,160 with w=72)
  // Centers become 44,120,196.
  dialConfig(0,  44,  96, 24, 210, -30, 6, 3, 30);
  dialConfig(1, 120,  96, 24, 210, -30, 6, 3, 30);
  dialConfig(2, 196,  96, 24, 210, -30, 6, 3, 30);
  s_sysDialsConfigured = true;
}

static int32_t s_dialPrevV10[3] = { 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF };

static void sysDialReset() {
  sysDialEnsureConfigured();
  for (uint8_t i = 0; i < 3; i++) dialInvalidate(i);
  s_dialPrevV10[0] = s_dialPrevV10[1] = s_dialPrevV10[2] = 0x7FFFFFFF;
}

static void sysDrawDial(uint8_t dialId, int cx, int cy,
                        int32_t v10, int32_t vMin10, int32_t vMax10,
                        int32_t warn10, int32_t crit10,
                        const char* label, const char* unit) {
  sysDialEnsureConfigured();

  // Segments (delta-only inside svc_dial).
  dialDraw(dialId, v10, vMin10, vMax10, warn10, crit10);

  // Label (static-ish; redraw on tab switches via full renderAll)
  tft.setTextColor(COL_FG());
  tft.setCursor(cx - 28, cy + 42);
  tft.print(label);

  // Numeric: redraw only when value changes (dirty rect is small).
  if (s_dialPrevV10[dialId] != v10) {
    s_dialPrevV10[dialId] = v10;
    tft.fillRect(cx - 34, cy + 52, 68, 12, COL_BG());
    tft.setTextColor(COL_HILITE());
    tft.setCursor(cx - 34, cy + 52);
    char num[16];
    const int32_t whole = v10 / 10;
    const int32_t frac  = v10 < 0 ? ((-v10) % 10) : (v10 % 10);
    if ((!unit || unit[0] == 0) && frac == 0) {
      snprintf(num, sizeof(num), "%ld", (long)whole);
    } else if (unit && unit[0]) {
      snprintf(num, sizeof(num), "%ld.%ld%s", (long)whole, (long)frac, unit);
    } else {
      snprintf(num, sizeof(num), "%ld.%ld", (long)whole, (long)frac);
    }
    tft.print(num);
  }
}

static void sysDrawSegBar(int x, int y, int w, int segs, int active, uint16_t onCol, uint16_t offCol) {
  if (active < 0) active = 0;
  if (active > segs) active = segs;
  int segW = w / segs;
  for (int i = 0; i < segs; i++) {
    int sx = x + i * segW;
    tft.fillRect(sx, y, segW - 1, 6, (i < active) ? onCol : offCol);
  }
}

static void sysRenderSensorsFull() {
  // Industrial frame panels (chrome separate from inner content)
  // PASS 8: enforce 4px grid + consistent gutters.
  static UiPanel pDialObj { 8,  (int16_t)(BODY_Y + 8), 72, 122, STYLE_SECONDARY, true };
  static UiPanel pDialAmb { 84, (int16_t)(BODY_Y + 8), 72, 122, STYLE_SECONDARY, true };
  static UiPanel pDialAir { 160,(int16_t)(BODY_Y + 8), 72, 122, STYLE_SECONDARY, true };
  static UiPanel pBottom  { 8,  (int16_t)(BODY_Y + 136), 224, 40, STYLE_PRIMARY,  true };

  // Registry is reset on SYSTEM enter; always re-register for this view.
  uiFrameRegister(&pDialObj);
  uiFrameRegister(&pDialAmb);
  uiFrameRegister(&pDialAir);
  uiFrameRegister(&pBottom);

  // Clear body and reset dial delta cache.
  tft.fillRect(0, BODY_Y, SYS_W, BODY_H, COL_BG());
  uiFrameMarkDirty(&pDialObj);
  uiFrameMarkDirty(&pDialAmb);
  uiFrameMarkDirty(&pDialAir);
  uiFrameMarkDirty(&pBottom);
  uiFrameTick();
  sysDialReset();

  // Read sensors
  float objC = tempIrObjectC();
  float ambC = tempIrAmbientC();
  float objF = objC * 9.0f / 5.0f + 32.0f;
  float ambF = ambC * 9.0f / 5.0f + 32.0f;

  int co2 = airSvcCO2ppm();
  uint8_t aLvl = airSvcAlertLevel();

  // Top row 3 dials
  const int32_t objF10 = (int32_t)lroundf(objF * 10.0f);
  const int32_t ambF10 = (int32_t)lroundf(ambF * 10.0f);
  const int32_t co210  = (int32_t)co2 * 10;

  // PASS 8: centers match new panel geometry.
  sysDrawDial(0, 44,  96, objF10,  500, 1400,  950, 1100, "IR OBJ", "F");
  sysDrawDial(1, 120, 96, ambF10,  500, 1400,  900, 1050, "IR AMB", "F");
  sysDrawDial(2, 196, 96, co210,  4000, 20000, 8000, 12000, "CO2", "");

  // Bottom indicators: Battery (unknown → show placeholder level), Signal, CPU
  int batt = 6; // TODO: wire real battery when hardware exists
  int sig = 0;
  if (wifiSvcIsConnected()) {
    int rssi = wifiSvcGetRSSI();
    if (rssi > -55) sig = 10;
    else if (rssi > -65) sig = 8;
    else if (rssi > -75) sig = 6;
    else if (rssi > -85) sig = 4;
    else sig = 2;
  }

  int cpu = perfGetCpuPercent();
  int cpuSeg = (cpu * 10) / 100;

  // PASS 8: micro-label discipline (ALL CAPS, 2px off frame edge, 4px grid)
  tft.setTextColor(COL_DARK());
  tft.setCursor(pBottom.x + 2,   pBottom.y + 18); tft.print("BATT");
  tft.setCursor(pBottom.x + 78,  pBottom.y + 18); tft.print("SIG");
  tft.setCursor(pBottom.x + 150, pBottom.y + 18); tft.print("CPU");

  sysDrawSegBar(pBottom.x + 2,   pBottom.y + 30, 72, 10, batt,   COL_HILITE(), COL_DARK());
  sysDrawSegBar(pBottom.x + 78,  pBottom.y + 30, 72, 10, sig,    COL_FG(),     COL_DARK());
  sysDrawSegBar(pBottom.x + 150, pBottom.y + 30, 72, 10, cpuSeg, COL_FG(),     COL_DARK());

  // Air alert light (small)
  uint16_t lvlCol = (aLvl >= 2) ? COL_HILITE() : (aLvl == 1 ? COL_FG() : COL_DIM());
  tft.fillRect(pBottom.x + 212, pBottom.y + 18, 10, 10, lvlCol);
  tft.setTextColor(COL_DARK());
  tft.setCursor(pBottom.x + 202, pBottom.y + 10);
  tft.print("AIR");
}

static void sysRenderRfFull() {
  static UiPanel pRFTop { 10, (int16_t)(BODY_Y + 6), 220, 82, STYLE_PRIMARY, true };
  static UiPanel pRFUid { 10, (int16_t)(BODY_Y + 94), 220, 76, STYLE_SECONDARY, true };
  uiFrameRegister(&pRFTop);
  uiFrameRegister(&pRFUid);

  tft.fillRect(0, BODY_Y, SYS_W, BODY_H, COL_BG());
  uiFrameMarkDirty(&pRFTop);
  uiFrameMarkDirty(&pRFUid);
  uiFrameTick();

  bool armed = rfidSvcIsArmed();
  bool ok = rfidSvcSensorOk();
  uint32_t cnt = rfidSvcScanCount();

  char uid[RFID_UID_STR_LEN];
  uid[0] = '\0';
  rfidSvcGetLastUid(uid, sizeof(uid));
  uint32_t lastMs = rfidSvcLastScanMs();

  tft.setTextColor(COL_HILITE());
  tft.setCursor(10, BODY_Y + 10); tft.print("RFID");
  tft.setTextColor(COL_FG());
  tft.setCursor(10, BODY_Y + 26); tft.print(ok ? "SENSOR OK" : "SENSOR FAIL");
  tft.setCursor(10, BODY_Y + 42); tft.print(armed ? "ARMED" : "DISARMED");

  tft.setTextColor(COL_DIM());
  tft.setCursor(10, BODY_Y + 62); tft.print("UID:");
  tft.setTextColor(COL_FG());
  tft.setCursor(50, BODY_Y + 62); tft.print(uid[0] ? uid : "(none)");

  tft.setTextColor(COL_DIM());
  tft.setCursor(10, BODY_Y + 80); tft.print("COUNT:");
  tft.setTextColor(COL_FG());
  tft.setCursor(60, BODY_Y + 80); tft.print((unsigned long)cnt);

  tft.setTextColor(COL_DIM());
  tft.setCursor(10, BODY_Y + 98); tft.print("LAST:");
  tft.setTextColor(COL_FG());
  if (lastMs == 0) {
    tft.setCursor(60, BODY_Y + 98); tft.print("(n/a)");
  } else {
    uint32_t age = (millis() - lastMs) / 1000;
    char a[24];
    snprintf(a, sizeof(a), "%lus ago", (unsigned long)age);
    tft.setCursor(60, BODY_Y + 98); tft.print(a);
  }

  // NFC is spec-only right now.
  tft.setTextColor(COL_HILITE());
  tft.setCursor(10, BODY_Y + 132); tft.print("NFC");
  tft.setTextColor(COL_DIM());
  tft.setCursor(10, BODY_Y + 148); tft.print("NOT IMPLEMENTED");
}

static void sysRenderIdentFull() {
  static UiPanel pIdent { 10, (int16_t)(BODY_Y + 6), 220, 186, STYLE_SECONDARY, true };
  uiFrameRegister(&pIdent);

  tft.fillRect(0, BODY_Y, SYS_W, BODY_H, COL_BG());
  uiFrameMarkDirty(&pIdent);
  uiFrameTick();

  const char* ssid = wifiSvcGetSSID();
  const char* ip   = wifiSvcGetIP();
  const char* gw   = wifiSvcGetGateway();

  tft.setTextColor(COL_HILITE());
  tft.setCursor(10, BODY_Y + 10); tft.print("IDENT");

  tft.setTextColor(COL_DIM());
  tft.setCursor(10, BODY_Y + 30); tft.print("FW:");
  tft.setTextColor(COL_FG());
  tft.setCursor(50, BODY_Y + 30); tft.print(FW_VERSION);

  tft.setTextColor(COL_DIM());
  tft.setCursor(10, BODY_Y + 46); tft.print("UP:");
  tft.setTextColor(COL_FG());
  tft.setCursor(50, BODY_Y + 46);
  uint32_t upS = millis() / 1000;
  char up[24];
  snprintf(up, sizeof(up), "%lus", (unsigned long)upS);
  tft.print(up);

  tft.setTextColor(COL_DIM());
  tft.setCursor(10, BODY_Y + 62); tft.print("SSID:");
  tft.setTextColor(COL_FG());
  tft.setCursor(50, BODY_Y + 62); tft.print((ssid && ssid[0]) ? ssid : "(none)");

  tft.setTextColor(COL_DIM());
  tft.setCursor(10, BODY_Y + 78); tft.print("IP:");
  tft.setTextColor(COL_FG());
  tft.setCursor(50, BODY_Y + 78); tft.print((ip && ip[0]) ? ip : "(none)");

  tft.setTextColor(COL_DIM());
  tft.setCursor(10, BODY_Y + 94); tft.print("GW:");
  tft.setTextColor(COL_FG());
  tft.setCursor(50, BODY_Y + 94); tft.print((gw && gw[0]) ? gw : "(none)");
}

static void sysRenderStatusFull() {
  static UiPanel pStatA { 10, (int16_t)(BODY_Y + 6), 220, 88, STYLE_PRIMARY, true };
  static UiPanel pStatB { 10, (int16_t)(BODY_Y + 100), 220, 92, STYLE_SECONDARY, true };
  uiFrameRegister(&pStatA);
  uiFrameRegister(&pStatB);

  tft.fillRect(0, BODY_Y, SYS_W, BODY_H, COL_BG());
  uiFrameMarkDirty(&pStatA);
  uiFrameMarkDirty(&pStatB);
  uiFrameTick();

  tft.setTextColor(COL_HILITE());
  tft.setCursor(10, BODY_Y + 10); tft.print("STATUS");

  // Heap
  uint32_t heap = ESP.getFreeHeap();
  tft.setTextColor(COL_DIM());
  tft.setCursor(10, BODY_Y + 30); tft.print("HEAP:");
  tft.setTextColor(COL_FG());
  tft.setCursor(60, BODY_Y + 30);
  char hb[24];
  snprintf(hb, sizeof(hb), "%lu", (unsigned long)heap);
  tft.print(hb);

  // WiFi
  WifiSvcState st = wifiSvcGetState();
  const char* ws = "UNK";
  switch (st) {
    case WSVC_OFF:        ws = "OFF"; break;
    case WSVC_IDLE:       ws = "IDLE"; break;
    case WSVC_SCANNING:   ws = "SCANNING"; break;
    case WSVC_CONNECTING: ws = "CONNECTING"; break;
    case WSVC_CONNECTED:  ws = "CONNECTED"; break;
    case WSVC_FAILED:     ws = "FAILED"; break;
    case WSVC_RETRY_WAIT: ws = "RETRY"; break;
    default: break;
  }

  tft.setTextColor(COL_DIM());
  tft.setCursor(10, BODY_Y + 46); tft.print("WIFI:");
  tft.setTextColor(COL_FG());
  tft.setCursor(60, BODY_Y + 46); tft.print(ws);

  // SD / Cart
  tft.setTextColor(COL_DIM());
  tft.setCursor(10, BODY_Y + 62); tft.print("SD:");
  tft.setTextColor(COL_FG());
  tft.setCursor(60, BODY_Y + 62); tft.print(sdPresent ? "PRESENT" : "NONE");

  tft.setTextColor(COL_DIM());
  tft.setCursor(10, BODY_Y + 78); tft.print("CART:");
  tft.setTextColor(COL_FG());
  tft.setCursor(60, BODY_Y + 78); tft.print(cartLoaded ? "LOADED" : "NONE");

  // BLE (hook exists, feature-gated)
  tft.setTextColor(COL_DIM());
  tft.setCursor(10, BODY_Y + 94); tft.print("BLE:");
  tft.setTextColor(COL_FG());
  tft.setCursor(60, BODY_Y + 94); tft.print(bleWifiProvisionAvailable() ? "ON" : "OFF");

  // Perf
  int cpu = perfGetCpuPercent();
  int fps = perfGetFps();
  int drw = perfGetDrawCount();

  tft.setTextColor(COL_DIM());
  tft.setCursor(10, BODY_Y + 120); tft.print("CPU");
  tft.setCursor(90, BODY_Y + 120); tft.print("FPS");
  tft.setCursor(160, BODY_Y + 120); tft.print("DRAW");

  int cpuSeg = (cpu * 10) / 100;
  int fpsSeg = (fps > 60) ? 10 : (fps * 10) / 60;
  int drwSeg = (drw > 40) ? 10 : (drw * 10) / 40;

  sysDrawSegBar(10, BODY_Y + 132, 60, 10, cpuSeg, COL_FG(), COL_DARK());
  sysDrawSegBar(90, BODY_Y + 132, 60, 10, fpsSeg, COL_FG(), COL_DARK());
  sysDrawSegBar(160,BODY_Y + 132, 60, 10, drwSeg, COL_FG(), COL_DARK());
}

static void appSystemEnter() {
  appChromeEnter("SYSTEM", "TABS", "BACK menu");
  // Reset panel registry for the SYSTEM page so each tab can register its panels.
  uiFrameInit();
  // Keep prior tab, but force full redraw for safety.
  s_sysPrevTab = 0xFF;
  sysDrawTabs();
  // Full render for active tab.
  switch (s_sysTab) {
    case TAB_SENSORS: sysRenderSensorsFull(); break;
    case TAB_RF:      sysRenderRfFull(); break;
    case TAB_IDENT:   sysRenderIdentFull(); break;
    case TAB_STATUS:  sysRenderStatusFull(); break;
    default: break;
  }
}

static void appSystemTick() {
  // Tab switching (no vertical scroll)
  bool tabChanged = false;
  if (!s_sysFocus) {
    if (Buttons::up.consume()) {
      if (s_sysTab > 0) s_sysTab--;
      else s_sysTab = TAB_COUNT - 1;
      tabChanged = true;
    }
    if (Buttons::down.consume()) {
      s_sysTab = (s_sysTab + 1) % TAB_COUNT;
      tabChanged = true;
    }
  }

  if (Buttons::select.consume()) {
    s_sysFocus = !s_sysFocus; // reserved for future per-tab focus, no UI change yet
  }

  if (tabChanged || s_sysTab != s_sysPrevTab) {
    s_sysPrevTab = s_sysTab;
    sysDrawTabs();
    // Clear and full redraw tab content to eliminate stale carryover.
    switch (s_sysTab) {
      case TAB_SENSORS: sysRenderSensorsFull(); break;
      case TAB_RF:      sysRenderRfFull(); break;
      case TAB_IDENT:   sysRenderIdentFull(); break;
      case TAB_STATUS:  sysRenderStatusFull(); break;
      default: break;
    }
    return;
  }

  // Live updates: keep tick-cost low by only updating small fields.
  // For now, only SENSORS dial segments update; other tabs refresh on tab entry.
  if (s_sysTab == TAB_SENSORS) {
    // Update only dial segments + small numeric fields.
    float objF = tempIrObjectC() * 9.0f / 5.0f + 32.0f;
    float ambF = tempIrAmbientC() * 9.0f / 5.0f + 32.0f;
    int   co2  = airSvcCO2ppm();

    const int32_t objF10 = (int32_t)lroundf(objF * 10.0f);
    const int32_t ambF10 = (int32_t)lroundf(ambF * 10.0f);
    const int32_t co210  = (int32_t)co2 * 10;

    sysDrawDial(0, 60,  96, objF10,  500, 1400,  950, 1100, "IR OBJ", "F");
    sysDrawDial(1, 120, 96, ambF10,  500, 1400,  900, 1050, "IR AMB", "F");
    sysDrawDial(2, 180, 96, co210,  4000, 20000, 8000, 12000, "CO2", "");

    // CPU seg bar update (small area) — cheap full redraw of those bars.
    int cpu = perfGetCpuPercent();
    int cpuSeg = (cpu * 10) / 100;
    sysDrawSegBar(164, 226, 60, 10, cpuSeg, COL_FG(), COL_DARK());
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
