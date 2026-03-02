// ─────────────────────────────────────────────────────────────────────────────
// svc_rfid.cpp  —  RFID Scanner Service (MFRC522 / Demo)
//
// Architecture:
//   Unarmed  : tick() exits in < 1 µs — zero SPI activity.
//   Armed    : polls MFRC522 every RFID_POLL_ARMED_MS; a card-present check
//              costs ~2–5 ms of SPI when no card is near.
//   New UID  : stored as ASCII hex string, hash stored for cooldown tracking,
//              notification + haptic fired.
//   Same UID : suppressed for RFID_SAME_UID_COOLDOWN_MS (3 s).
//
// MFRC522 SPI note (real hardware):
//   SPI.begin(36, 37, 35) is called in sketch.ino before rfidSvcInit().
//   rfidSvcInit() (real mode) calls SPI.begin() again to ensure correct pins
//   are active, then mfrc522.PCD_Init(). This is safe on ESP32-Arduino.
// ─────────────────────────────────────────────────────────────────────────────
#include "svc_rfid.h"

#if RFID_ENABLED   // ── entire module gated ───────────────────────────────────

#include <Arduino.h>
#include "svc_notify.h"   // notifySvcPost
#include "svc_haptics.h"  // hapticsBuzz

// Real hardware: conditionally pull in MFRC522 library
#if !RFID_DEMO_MODE
#include <SPI.h>
#include <MFRC522.h>
static MFRC522 _mfrc(RFID_CS_PIN, RFID_RST_PIN);
#endif

// ─────────────────────────────────────────────────────────────────────────────
// State
// ─────────────────────────────────────────────────────────────────────────────
static bool     _sensorOk    = false;
static bool     _armed       = false;
static uint32_t _lastPollMs  = 0;
static uint32_t _scanCount   = 0;

// Last UID as ASCII string "04 A1 B2 C3" — empty if none yet
static char     _lastUidStr[RFID_UID_STR_LEN] = { 0 };

// Anti-spam: hash of UID bytes + timestamp of last notification
static uint32_t _lastNotifHash = 0;
static uint32_t _lastNotifMs   = 0;

// ─────────────────────────────────────────────────────────────────────────────
// UID utilities
// ─────────────────────────────────────────────────────────────────────────────

// djb2 hash over raw bytes — fast, zero malloc
static uint32_t _hashBytes(const uint8_t* data, int len) {
  uint32_t h = 5381;
  for (int i = 0; i < len; i++)
    h = ((h << 5) + h) ^ data[i];
  return h;
}

// Format bytes as "04 A1 B2 C3 D4" into dst
static void _formatUid(const uint8_t* bytes, int len, char* dst, int dstLen) {
  int pos = 0;
  for (int i = 0; i < len && pos + 3 < dstLen; i++) {
    if (i > 0) dst[pos++] = ' ';
    const char* hex = "0123456789ABCDEF";
    dst[pos++] = hex[(bytes[i] >> 4) & 0x0F];
    dst[pos++] = hex[ bytes[i]       & 0x0F];
  }
  dst[pos] = '\0';
}

// ─────────────────────────────────────────────────────────────────────────────
// Common scan handler — called with raw UID bytes after a real or demo read
// ─────────────────────────────────────────────────────────────────────────────
static void _handleUid(const uint8_t* bytes, int len) {
  uint32_t h   = _hashBytes(bytes, len);
  uint32_t now = millis();

  // Always format and store
  _formatUid(bytes, len, _lastUidStr, sizeof(_lastUidStr));
  _scanCount++;

  // Anti-spam: same UID within cooldown → suppress notification
  bool sameUid      = (h == _lastNotifHash);
  bool inCooldown   = (now - _lastNotifMs < RFID_SAME_UID_COOLDOWN_MS);
  if (sameUid && inCooldown) return;

  _lastNotifHash = h;
  _lastNotifMs   = now;

  // Build notification body "TAG 04 A1 B2 C3"
  char body[RFID_UID_STR_LEN + 6];
  snprintf(body, sizeof(body), "TAG %s", _lastUidStr);
  notifySvcPost(NOTIFY_INFO, "RFID", body, 2500);
  hapticsBuzz(40);   // short tap — distinct from HAPTIC_CLICK pattern
}

// ─────────────────────────────────────────────────────────────────────────────
// Demo mode — 5 synthetic UIDs cycling pseudo-randomly when armed
// ─────────────────────────────────────────────────────────────────────────────
#if RFID_DEMO_MODE

// Hard-coded demo UIDs (realistic Mifare Classic 4-byte UIDs)
static const uint8_t _DEMO_UIDS[][4] = {
  { 0x04, 0xA1, 0xB2, 0xC3 },
  { 0xDE, 0xAD, 0xBE, 0xEF },
  { 0x12, 0x34, 0x56, 0x78 },
  { 0xCA, 0xFE, 0xBA, 0xBE },
  { 0xFF, 0x00, 0x11, 0x22 },
};
static const int _DEMO_UID_COUNT = 5;

static uint32_t _demoNextMs   = 0;
static int      _demoUidIdx   = 0;
static uint32_t _demoInterval = 3000UL;   // ms between demo scans

static void _demoTick() {
  uint32_t now = millis();
  if (now < _demoNextMs) return;

  // Vary interval: 2 s → 8 s cycling to simulate real-world tag presentation
  static const uint32_t _intervals[] = { 2000, 4000, 3000, 7000, 2500, 5000, 8000 };
  static int _iIdx = 0;
  _demoInterval = _intervals[_iIdx++ % 7];
  _demoNextMs   = now + _demoInterval;

  // Advance through UIDs
  const uint8_t* uid = _DEMO_UIDS[_demoUidIdx % _DEMO_UID_COUNT];
  _demoUidIdx = (_demoUidIdx + 1) % _DEMO_UID_COUNT;
  _handleUid(uid, 4);
}

#endif  // RFID_DEMO_MODE

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

void rfidSvcInit() {
#if RFID_DEMO_MODE
  _sensorOk    = true;
  _demoNextMs  = millis() + 2000;   // first demo scan ~2 s after arm
  Serial.println("rfidSvc: DEMO MODE");
  return;
#else
  // Re-assert SPI pins (safe on ESP32; SPI already begin()'d in sketch.ino)
  SPI.begin(36, 37, 35);   // SCK, MISO, MOSI — same as TFT
  _mfrc.PCD_Init(RFID_CS_PIN, RFID_RST_PIN);

  // Self-test: read firmware version register
  byte ver = _mfrc.PCD_ReadRegister(MFRC522::VersionReg);
  if (ver == 0x00 || ver == 0xFF) {
    Serial.println("rfidSvc: MFRC522 NOT found");
    _sensorOk = false;
  } else {
    Serial.print("rfidSvc: MFRC522 v"); Serial.println(ver, HEX);
    _sensorOk = true;
  }
#endif
}

void rfidSvcTick() {
  if (!_armed) return;   // ← unarmed: exits here, < 1 µs

#if RFID_DEMO_MODE
  _demoTick();
  return;
#else
  if (!_sensorOk) return;

  uint32_t now = millis();
  if (now - _lastPollMs < RFID_POLL_ARMED_MS) return;
  _lastPollMs = now;

  // PICC_IsNewCardPresent(): ~2–5 ms SPI when no card present
  if (!_mfrc.PICC_IsNewCardPresent()) return;
  if (!_mfrc.PICC_ReadCardSerial())   return;

  _handleUid(_mfrc.uid.uidByte, _mfrc.uid.size);

  _mfrc.PICC_HaltA();
  _mfrc.PCD_StopCrypto1();
#endif
}

void rfidSvcSetArmed(bool armed) {
  if (armed == _armed) return;
  _armed = armed;
#if RFID_DEMO_MODE
  if (armed) _demoNextMs = millis() + 1500;  // first demo scan ~1.5 s after arming
#endif
}

bool     rfidSvcIsArmed()                             { return _armed;       }
bool     rfidSvcSensorOk()                            { return _sensorOk;    }
uint32_t rfidSvcScanCount()                           { return _scanCount;   }

bool rfidSvcGetLastUid(char* out, int outLen) {
  if (!out || outLen < 2) return false;
  if (_lastUidStr[0] == '\0') { out[0] = '\0'; return false; }
  strncpy(out, _lastUidStr, outLen - 1);
  out[outLen - 1] = '\0';
  return true;
}

// ─────────────────────────────────────────────────────────────────────────────
#else  // RFID_ENABLED == 0  ─── compile-safe stubs ────────────────────────────
// ─────────────────────────────────────────────────────────────────────────────

void     rfidSvcInit()                   {}
void     rfidSvcTick()                   {}
void     rfidSvcSetArmed(bool)           {}
bool     rfidSvcIsArmed()                { return false; }
bool     rfidSvcSensorOk()              { return false; }
uint32_t rfidSvcScanCount()              { return 0; }
bool     rfidSvcGetLastUid(char* o, int l) {
  if (o && l > 0) o[0] = '\0';
  return false;
}

#endif  // RFID_ENABLED
