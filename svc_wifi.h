#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// svc_wifi.h  —  WiFi Service
//
// Purpose  : Single owner of all WiFi connection state.
//            Non-blocking state machine for connect/scan/retry.
//            No other module calls WiFi.begin() directly.
//
// Public API ──────────────────────────────────────────────────────────────────
//   wifiSvcInit()            — call once in setup()
//   wifiSvcTick()            — call every loop(); handles FSM + retry
//
//   wifiSvcIsConnected()     — quick test
//   wifiSvcGetState()        — full WifiSvcState enum for UI
//
//   wifiSvcConnect(ssid,pw)  — start non-blocking connect
//   wifiSvcForget()          — clear NVS creds + disconnect
//   wifiSvcDisconnect()      — disconnect (keeps saved creds)
//   wifiSvcStartScan()       — start async scan; results via ScanResult()
//
//   wifiSvcGetSSID()         — "" if not connected
//   wifiSvcGetRSSI()         — 0 if not connected
//   wifiSvcGetIP()           — "0.0.0.0" if not connected
//   wifiSvcGetGateway()
//   wifiSvcGetDNS()
//
//   wifiSvcIsScanning()      — true while async scan in flight
//   wifiSvcScanCount()       — 0 while scanning / not done
//   wifiSvcScanResult(i)     — ptr to cached result; nullptr if OOB
//
//   wifiSvcAutoConnect()         — current NVS setting
//   wifiSvcSetAutoConnect(bool)  — write to NVS immediately
//
// Ownership ───────────────────────────────────────────────────────────────────
//   Owns: WiFi FSM, scan results, NVS creds (KEY_WIFI_SSID/PASS/AUTO).
//   Triggers: timeSvcTriggerSync() on connect; notifyPost() on state change.
//   Does NOT own the TFT or input — only the WiFi radio.
// ─────────────────────────────────────────────────────────────────────────────
#include "netcore_config.h"
#include <WiFi.h>

// ── Max scan results buffered in RAM ─────────────────────────────────────────
#define WIFI_SVC_MAX_NETS  20
#define WIFI_SVC_SSID_LEN  33   // 32 + null
#define WIFI_SVC_PASS_LEN  64   // 63 + null

// ── Service state ─────────────────────────────────────────────────────────────
enum WifiSvcState {
  WSVC_OFF         = 0,  // WiFi radio disabled (future use)
  WSVC_IDLE        = 1,  // on, not trying to connect
  WSVC_SCANNING    = 2,  // async scan in progress
  WSVC_CONNECTING  = 3,  // WiFi.begin called; polling status
  WSVC_CONNECTED   = 4,  // WL_CONNECTED
  WSVC_FAILED      = 5,  // last connect attempt failed
  WSVC_RETRY_WAIT  = 6,  // waiting 15s before auto-retry
};

// ── Failure reason (captured at moment of failure; survives FSM transition) ─
enum WifiFailReason {
  WFAIL_NONE      = 0,
  WFAIL_AP_NOT_FOUND,   // WL_NO_SSID_AVAIL
  WFAIL_AUTH_FAILED,    // WL_CONNECT_FAILED
  WFAIL_TIMEOUT,        // connect timed out
};

// ── Cached scan result ────────────────────────────────────────────────────────
struct WifiSvcNet {
  char    ssid[WIFI_SVC_SSID_LEN];
  char    bssid[18];
  int32_t rssi;
  int32_t channel;
  uint8_t auth;
  bool    hidden;
};

// ── Lifecycle ─────────────────────────────────────────────────────────────────
void wifiSvcInit();
void wifiSvcTick();

// ── State ─────────────────────────────────────────────────────────────────────
WifiSvcState wifiSvcGetState();
bool         wifiSvcIsConnected();
bool         wifiSvcIsScanning();
bool         wifiSvcIsConnecting();

// ── Connection info (valid only when CONNECTED) ───────────────────────────────
const char* wifiSvcGetSSID();
int32_t     wifiSvcGetRSSI();
const char* wifiSvcGetIP();
const char* wifiSvcGetGateway();
const char* wifiSvcGetDNS();

// ── Saved creds helpers (UI) ───────────────────────────────────────────────
bool         wifiSvcHasSavedCreds();
const char*  wifiSvcGetSavedSSID();
bool         wifiSvcHasCredsFor(const char* ssid);
void         wifiSvcConnectSaved();

// ── Actions ───────────────────────────────────────────────────────────────────
void wifiSvcConnect(const char* ssid, const char* pass);
WifiFailReason wifiSvcGetFailReason();  // last failure cause; WFAIL_NONE if none
void wifiSvcForget();       // clear NVS creds, go to IDLE
void wifiSvcDisconnect();   // disconnect but keep saved creds
void wifiSvcStartScan();    // async; check wifiSvcIsScanning()

// ── Scan results (populated when scan completes) ──────────────────────────────
int                   wifiSvcScanCount();
const WifiSvcNet*     wifiSvcScanResult(int i);  // nullptr if out of range

// ── Settings (persisted to NVS immediately on write) ─────────────────────────
bool wifiSvcAutoConnect();
void wifiSvcSetAutoConnect(bool on);
