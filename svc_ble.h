#pragma once
// ═════════════════════════════════════════════════════════════════════════════
// svc_ble.h  —  BLE Phone Companion V1 Specification
//
// STATUS: SPEC + STUB ONLY — no BLE stack is active yet.
//         All functions are no-ops that return safe defaults.
//         Wire up the ESP32 BLE stack when ready; change nothing else.
//
// ── HOW BLE FITS INTO THE ARCHITECTURE ──────────────────────────────────────
//   bleSvcInit()    ← called from setup() when BLE_ENABLED = 1
//   bleSvcTick()    ← called from loop() every iteration
//   publishEvent()  ← used to fire EVT_BLE_CONNECTED / EVT_BLE_NOTIF_RX etc.
//   SystemSnapshot  ← bleConnected field is the authoritative BLE status
//
// ── SECURITY DEFAULTS ────────────────────────────────────────────────────────
//   BLE is OFF by default. User must enable via Settings.
//   WiFi provisioning via BLE is OFF by default + requires on-device confirm.
//   All characteristics require pairing/bonding (BLE_SECURITY_MODE_1_LEVEL_3).
//
// ── GATT SERVICE LAYOUT ──────────────────────────────────────────────────────
//
//   Service: NETCORE_COMPANION
//   UUID:    00001nc0-0000-1000-8000-00805f9b34fb
//            (nc0 = NetCore v0; increment major on breaking change)
//
//   Characteristics:
//   ┌──────────────────┬──────────────────────────────────────────────────────┐
//   │ Name             │ UUID (short)  Props           Notes                  │
//   ├──────────────────┼──────────────────────────────────────────────────────┤
//   │ NOTIF_RX         │ 0xNC01   WRITE_NO_RESP     Phone → Watch push notif  │
//   │ STATE_REQ        │ 0xNC02   WRITE             Phone requests snapshot    │
//   │ STATE_RESP       │ 0xNC03   NOTIFY+READ       Watch sends SystemSnapshot │
//   │ CMD_TX           │ 0xNC04   WRITE             Phone sends commands       │
//   │ GPS_RELAY        │ 0xNC05   WRITE             Phone sends GPS coords     │
//   │ WIFI_PROVISION   │ 0xNC06   WRITE (disabled)  SSID+pass provisioning    │
//   └──────────────────┴──────────────────────────────────────────────────────┘
//
// ── PACKET FORMAT ────────────────────────────────────────────────────────────
//   All packets use this 8-byte header + variable payload:
//
//   Offset  Size  Field
//   0       1     packet_type  (BlePacketType enum below)
//   1       1     flags        (bit 0=has_body, bit 1=requires_ack, bits 2-7 reserved)
//   2       4     timestamp    (Unix epoch, uint32_t little-endian; 0 if unknown)
//   6       2     body_len     (uint16_t little-endian; 0 if no body)
//   8+      N     body         (body_len bytes; format per packet_type)
//
//   Max packet size: 512 bytes (fits in one BLE MTU with room for header).
//   For notifications: body = null-terminated title (up to 32 bytes) +
//                              null-terminated message (up to 128 bytes).
//
// ── COMMANDS (CMD_TX body field) ─────────────────────────────────────────────
//   NOTIF_PUSH         = 0x01   Phone pushes a notification to the watch
//   STATE_REQ          = 0x02   Phone requests a STATE_RESP update
//   GPS_RELAY          = 0x10   Phone sends GPS fix (future)
//   WIFI_PROVISION_REQ = 0x20   Phone wants to provision WiFi (disabled by default)
//   WIFI_PROVISION_ACK = 0x21   Watch user confirmed WiFi provisioning
//   PING               = 0xF0   Keepalive ping
//   PONG               = 0xF1   Keepalive response
//
// ═════════════════════════════════════════════════════════════════════════════
#include "netcore_config.h"
#include <stdint.h>

// ── Build-time enable/disable ─────────────────────────────────────────────────
// Set to 1 in netcore_config.h or build flags to activate BLE stack.
#ifndef BLE_ENABLED
#  define BLE_ENABLED 0   // OFF by default
#endif

// ── Packet type enum ──────────────────────────────────────────────────────────
enum BlePacketType : uint8_t {
  BLE_PKT_NOTIF_PUSH         = 0x01,
  BLE_PKT_STATE_REQ          = 0x02,
  BLE_PKT_STATE_RESP         = 0x03,
  BLE_PKT_GPS_RELAY          = 0x10,
  BLE_PKT_WIFI_PROVISION_REQ = 0x20,
  BLE_PKT_WIFI_PROVISION_ACK = 0x21,
  BLE_PKT_PING               = 0xF0,
  BLE_PKT_PONG               = 0xF1,
};

// ── Notification payload (body of BLE_PKT_NOTIF_PUSH) ─────────────────────────
#define BLE_NOTIF_TITLE_LEN  32
#define BLE_NOTIF_BODY_LEN   128
struct BleNotifPayload {
  uint8_t notify_type;                      // maps to NotifyType enum
  char    title[BLE_NOTIF_TITLE_LEN];
  char    body[BLE_NOTIF_BODY_LEN];
};

// ── GPS payload (body of BLE_PKT_GPS_RELAY) ───────────────────────────────────
struct BleGpsPayload {
  int32_t  lat_deg7;    // latitude  * 1e7 (integer, avoids float)
  int32_t  lon_deg7;    // longitude * 1e7
  int32_t  alt_mm;      // altitude in mm
  uint16_t hdop_10;     // HDOP * 10
  uint8_t  fix_type;    // 0=none 1=2D 2=3D
};

// ── Public API (all are no-ops when BLE_ENABLED=0) ────────────────────────────
void bleSvcInit();
void bleSvcTick();

bool bleSvcIsConnected();
bool bleSvcIsEnabled();
void bleSvcSetEnabled(bool on);     // persists to NVS; fires EVT_BLE_CONNECTED/DISCONNECTED

// WiFi provisioning (gated behind separate NVS flag + on-device confirm)
bool bleSvcWifiProvisionEnabled();
void bleSvcSetWifiProvisionEnabled(bool on);

// Compile-safe WiFi provision bridge.
// When BLE is truly implemented later, bleSvcTick() should push provisioned
// creds into an internal fixed buffer and this function will pop them.
bool bleWifiProvisionAvailable();
bool bleWifiProvisionPop(char* ssidOut, int ssidMax, char* passOut, int passMax, bool* connectNow);

// ── Implementation: stub (safe no-ops) ────────────────────────────────────────
// All functions are defined inline here when BLE_ENABLED=0.
// When BLE_ENABLED=1, provide a full svc_ble.cpp and remove these inlines.
#if !BLE_ENABLED
inline void bleSvcInit()                        {}
inline void bleSvcTick()                        {}
inline bool bleSvcIsConnected()                 { return false; }
inline bool bleSvcIsEnabled()                   { return false; }
inline void bleSvcSetEnabled(bool /*on*/)       {}
inline bool bleSvcWifiProvisionEnabled()        { return false; }
inline void bleSvcSetWifiProvisionEnabled(bool) {}
inline bool bleWifiProvisionAvailable()         { return false; }
inline bool bleWifiProvisionPop(char*, int, char*, int, bool*) { return false; }
#endif
