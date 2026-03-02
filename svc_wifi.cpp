// ─────────────────────────────────────────────────────────────────────────────
// svc_wifi.cpp  —  WiFi Service implementation
//
// Non-blocking connect/scan/retry FSM.
// Only file allowed to call WiFi.begin() / WiFi.disconnect().
// ─────────────────────────────────────────────────────────────────────────────
#include "svc_wifi.h"
#include "netcore_time.h"   // timeSvcTriggerSync() on connect
#include "netcore_ui.h"     // notifyPost() on state change
#include "svc_haptics.h"    // non-blocking haptic feedback

// ── Timeouts ──────────────────────────────────────────────────────────────────
#define CONNECT_TIMEOUT_MS  15000UL   // give up connecting after 15s
#define RETRY_INTERVAL_MS   15000UL   // auto-retry every 15s when disconnected
#define FAILED_HOLD_MS       3000UL   // stay in FAILED state for 3s before IDLE

// ── State ─────────────────────────────────────────────────────────────────────
static WifiSvcState  _state       = WSVC_IDLE;
static uint32_t      _stateMs     = 0;   // millis() when we entered current state
static WifiFailReason _failReason = WFAIL_NONE;  // captured at failure; read by UI

// ── Saved credentials (runtime copy; NVS is ground truth) ────────────────────
static char _savedSsid[WIFI_SVC_SSID_LEN] = "";
static char _savedPass[WIFI_SVC_PASS_LEN] = "";
static bool _autoConnect = true;

// ── Scan results ─────────────────────────────────────────────────────────────
static WifiSvcNet _nets[WIFI_SVC_MAX_NETS];
static int        _netCount   = 0;
static bool       _scanPending = false;   // scan was requested during CONNECTING

// ── Connection info cache ─────────────────────────────────────────────────────
static char _ip[16]   = "0.0.0.0";
static char _gw[16]   = "0.0.0.0";
static char _dns[16]  = "0.0.0.0";
static char _ssidBuf[WIFI_SVC_SSID_LEN] = "";

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

static void _setState(WifiSvcState s) {
  _state   = s;
  _stateMs = millis();
}

static void _cacheConnectionInfo() {
  strncpy(_ssidBuf, WiFi.SSID().c_str(), WIFI_SVC_SSID_LEN - 1);
  _ssidBuf[WIFI_SVC_SSID_LEN - 1] = '\0';

  IPAddress ip  = WiFi.localIP();
  IPAddress gw  = WiFi.gatewayIP();
  IPAddress dns = WiFi.dnsIP();
  snprintf(_ip,  sizeof(_ip),  "%d.%d.%d.%d", ip[0],  ip[1],  ip[2],  ip[3]);
  snprintf(_gw,  sizeof(_gw),  "%d.%d.%d.%d", gw[0],  gw[1],  gw[2],  gw[3]);
  snprintf(_dns, sizeof(_dns), "%d.%d.%d.%d", dns[0], dns[1], dns[2], dns[3]);
}

static void _saveCreds(const char* ssid, const char* pass) {
  strncpy(_savedSsid, ssid, WIFI_SVC_SSID_LEN - 1);
  _savedSsid[WIFI_SVC_SSID_LEN - 1] = '\0';
  strncpy(_savedPass, pass, WIFI_SVC_PASS_LEN - 1);
  _savedPass[WIFI_SVC_PASS_LEN - 1] = '\0';

  prefs.putString(KEY_WIFI_SSID, _savedSsid);
  prefs.putString(KEY_WIFI_PASS, _savedPass);
}

// Insertion-sort scan results by RSSI descending (strongest first)
static void _sortNets() {
  for (int i = 1; i < _netCount; i++) {
    WifiSvcNet key = _nets[i];
    int j = i - 1;
    while (j >= 0 && _nets[j].rssi < key.rssi) {
      _nets[j + 1] = _nets[j];
      j--;
    }
    _nets[j + 1] = key;
  }
}

// Collect results from a completed scan into _nets[]
static void _collectScanResults() {
  int n = (int)WiFi.scanComplete();
  if (n < 0) n = 0;
  if (n > WIFI_SVC_MAX_NETS) n = WIFI_SVC_MAX_NETS;
  _netCount = 0;
  for (int i = 0; i < n; i++) {
    // M1: no Arduino String in hot path — write directly to fixed-size buf
    WiFi.SSID(i).toCharArray(_nets[i].ssid, WIFI_SVC_SSID_LEN);
    _nets[i].ssid[WIFI_SVC_SSID_LEN - 1] = '\0';
    bool hidden = (_nets[i].ssid[0] == '\0');
    WiFi.BSSIDstr(i).toCharArray(_nets[i].bssid, 18);
    _nets[i].bssid[17] = '\0';
    _nets[i].rssi    = WiFi.RSSI(i);
    _nets[i].channel = WiFi.channel(i);
    _nets[i].auth    = (uint8_t)WiFi.encryptionType(i);
    _nets[i].hidden  = hidden;
  }
  _netCount = n;
  WiFi.scanDelete();
  _sortNets();
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

void wifiSvcInit() {
  // Read persisted settings
  if (prefs.isKey(KEY_WIFI_SSID)) {
    prefs.getString(KEY_WIFI_SSID, _savedSsid, sizeof(_savedSsid));
    prefs.getString(KEY_WIFI_PASS, _savedPass, sizeof(_savedPass));
  }
  _autoConnect = (prefs.getUChar(KEY_WIFI_AUTO, 1) != 0);

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);   // we manage reconnect ourselves

  if (_autoConnect && _savedSsid[0] != '\0') {
    WiFi.begin(_savedSsid, _savedPass);
    _setState(WSVC_CONNECTING);
  } else {
    _setState(WSVC_IDLE);
  }
}

void wifiSvcTick() {
  switch (_state) {

    // ── IDLE — nothing to do; watch for external WiFi events ────────────────
    case WSVC_IDLE:
      // If WiFi connects externally (shouldn't happen since autoReconnect=false)
      if (WiFi.status() == WL_CONNECTED) {
        _cacheConnectionInfo();
        _setState(WSVC_CONNECTED);
        timeSvcTriggerSync();
        notifySvcPost(NOTIFY_OK, "WiFi", "Connected", 2500);
        hapticsPattern(HAPTIC_SUCCESS);
      }
      break;

    // ── SCANNING — poll async scan ───────────────────────────────────────────
    case WSVC_SCANNING: {
      int16_t res = WiFi.scanComplete();
      if (res == WIFI_SCAN_RUNNING) break;   // still going
      // Done (or failed)
      _collectScanResults();
      // Return to idle/connected as appropriate
      if (WiFi.status() == WL_CONNECTED) _setState(WSVC_CONNECTED);
      else                               _setState(WSVC_IDLE);
      // If a connect was queued during scan, start it
      if (_scanPending) {
        _scanPending = false;
        if (_savedSsid[0] != '\0') {
          WiFi.begin(_savedSsid, _savedPass);
          _setState(WSVC_CONNECTING);
        }
      }
      break;
    }

    // ── CONNECTING — poll until connected or timed out ───────────────────────
    case WSVC_CONNECTING: {
      wl_status_t ws = WiFi.status();
      if (ws == WL_CONNECTED) {
        _cacheConnectionInfo();
        // Save creds of whatever we just connected with (in case it was new)
        // M1: no Arduino String in hot path
        char connSsidBuf[WIFI_SVC_SSID_LEN];
        WiFi.SSID().toCharArray(connSsidBuf, WIFI_SVC_SSID_LEN);
        if (connSsidBuf[0] != '\0')
          _saveCreds(connSsidBuf, _savedPass);
        _setState(WSVC_CONNECTED);
        timeSvcTriggerSync();
        notifySvcPost(NOTIFY_OK, "WiFi", "Connected", 2500);
        hapticsPattern(HAPTIC_SUCCESS);
        break;
      }
      if (ws == WL_NO_SSID_AVAIL) {
        _failReason = WFAIL_AP_NOT_FOUND;
        _setState(WSVC_FAILED);
        notifySvcPost(NOTIFY_ERROR, "WiFi", "AP not found", 3000);
        hapticsPattern(HAPTIC_ERROR);
        break;
      }
      if (ws == WL_CONNECT_FAILED) {
        _failReason = WFAIL_AUTH_FAILED;
        _setState(WSVC_FAILED);
        notifySvcPost(NOTIFY_ERROR, "WiFi", "Auth failed", 3000);
        hapticsPattern(HAPTIC_ERROR);
        break;
      }
      // Timeout
      if (millis() - _stateMs > CONNECT_TIMEOUT_MS) {
        _failReason = WFAIL_TIMEOUT;
        WiFi.disconnect(true);
        _setState(WSVC_FAILED);
        notifySvcPost(NOTIFY_WARN, "WiFi", "Connect timeout", 3000);
        hapticsPattern(HAPTIC_WARN);
      }
      break;
    }

    // ── CONNECTED — monitor for drops ────────────────────────────────────────
    case WSVC_CONNECTED:
      if (WiFi.status() != WL_CONNECTED) {
        _setState(WSVC_RETRY_WAIT);
        notifySvcPost(NOTIFY_WARN, "WiFi", "Connection lost", 3000);
        hapticsPattern(HAPTIC_WARN);
        memset(_ip,  0, sizeof(_ip));
        memset(_gw,  0, sizeof(_gw));
        memset(_dns, 0, sizeof(_dns));
      }
      break;

    // ── FAILED — brief pause, then IDLE ──────────────────────────────────────
    case WSVC_FAILED:
      if (millis() - _stateMs > FAILED_HOLD_MS) {
        _setState(WSVC_IDLE);
      }
      break;

    // ── RETRY_WAIT — auto-reconnect after interval ───────────────────────────
    case WSVC_RETRY_WAIT:
      if (_autoConnect && _savedSsid[0] != '\0') {
        if (millis() - _stateMs > RETRY_INTERVAL_MS) {
          WiFi.begin(_savedSsid, _savedPass);
          _setState(WSVC_CONNECTING);
        }
      } else {
        // No auto-connect, just go idle
        if (millis() - _stateMs > FAILED_HOLD_MS)
          _setState(WSVC_IDLE);
      }
      break;

    case WSVC_OFF:
    default:
      break;
  }
}

// ── Actions ───────────────────────────────────────────────────────────────────

void wifiSvcConnect(const char* ssid, const char* pass) {
  _failReason = WFAIL_NONE;   // clear stale reason before each new attempt
  // Do disconnect to trigger fresh auth.
  WiFi.disconnect(true);
  delay(50);   // allow radio to reset — this is called from user-triggered
               // action (button press), NOT from a tick/loop path
  // Store as pending creds (will be saved to NVS on successful connect)
  strncpy(_savedSsid, ssid, WIFI_SVC_SSID_LEN - 1);
  _savedSsid[WIFI_SVC_SSID_LEN - 1] = '\0';
  strncpy(_savedPass, pass, WIFI_SVC_PASS_LEN - 1);
  _savedPass[WIFI_SVC_PASS_LEN - 1] = '\0';

  WiFi.begin(ssid, pass);
  _setState(WSVC_CONNECTING);
}

void wifiSvcForget() {
  WiFi.disconnect(true);
  prefs.remove(KEY_WIFI_SSID);
  prefs.remove(KEY_WIFI_PASS);
  memset(_savedSsid, 0, sizeof(_savedSsid));
  memset(_savedPass, 0, sizeof(_savedPass));
  _setState(WSVC_IDLE);
}

void wifiSvcDisconnect() {
  WiFi.disconnect(true);
  _setState(WSVC_IDLE);
}

void wifiSvcStartScan() {
  _netCount = 0;
  if (_state == WSVC_CONNECTING) {
    // Can't scan while connecting; queue it
    _scanPending = true;
    return;
  }
  // async=true: returns WIFI_SCAN_RUNNING immediately; no blocking
  WiFi.scanNetworks(/*async=*/true);
  _setState(WSVC_SCANNING);
}

// ── Failure reason ──────────────────────────────────────────────────────────
WifiFailReason wifiSvcGetFailReason() { return _failReason; }

// ── State accessors ───────────────────────────────────────────────────────────

WifiSvcState wifiSvcGetState()     { return _state; }
bool wifiSvcIsConnected()           { return _state == WSVC_CONNECTED; }
bool wifiSvcIsScanning()            { return _state == WSVC_SCANNING; }
bool wifiSvcIsConnecting()          { return _state == WSVC_CONNECTING; }

// ── Connection info ───────────────────────────────────────────────────────────

const char* wifiSvcGetSSID()    { return _ssidBuf; }
int32_t     wifiSvcGetRSSI()    { return _state == WSVC_CONNECTED ? WiFi.RSSI() : 0; }
const char* wifiSvcGetIP()      { return _ip; }
const char* wifiSvcGetGateway() { return _gw; }
const char* wifiSvcGetDNS()     { return _dns; }

// ── Scan results ──────────────────────────────────────────────────────────────

int wifiSvcScanCount() { return _netCount; }
const WifiSvcNet* wifiSvcScanResult(int i) {
  if (i < 0 || i >= _netCount) return nullptr;
  return &_nets[i];
}

// ── Settings ──────────────────────────────────────────────────────────────────

bool wifiSvcAutoConnect() { return _autoConnect; }
void wifiSvcSetAutoConnect(bool on) {
  _autoConnect = on;
  prefs.putUChar(KEY_WIFI_AUTO, on ? 1 : 0);
}


// ─────────────────────────────────────────────────────────────────────────────
// Saved creds helpers (UI)
// ─────────────────────────────────────────────────────────────────────────────

bool wifiSvcHasSavedCreds() {
  return _savedSsid[0] != '\0';
}

const char* wifiSvcGetSavedSSID() {
  return _savedSsid;
}

bool wifiSvcHasCredsFor(const char* ssid) {
  if (!ssid || !ssid[0]) return false;
  if (_savedSsid[0] == '\0') return false;
  // exact match
  for (int i=0; i < WIFI_SVC_SSID_LEN; i++) {
    char a = _savedSsid[i];
    char b = ssid[i];
    if (a != b) return false;
    if (a == '\0') break;
  }
  return true;
}


void wifiSvcConnectSaved() {
  if (_savedSsid[0] == '\0') return;
  // If a scan is running, queue the connect for when scan completes
  if (_state == WSVC_SCANNING) {
    _scanPending = true;
    return;
  }
  WiFi.begin(_savedSsid, _savedPass);
  _setState(WSVC_CONNECTING);
}
