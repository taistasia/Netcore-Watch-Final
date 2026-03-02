// ─────────────────────────────────────────────────────────────────────────────
// svc_air.cpp  —  Air Quality Service (SCD40 / Demo mode)
//
// SCD40 I2C protocol notes:
//   Address    : 0x62
//   Start meas : cmd 0x21B1  — no response; starts 5s periodic cycle
//   Data ready : cmd 0xE4B8  → 3 bytes (word + CRC); bit 11 of word = ready
//   Read meas  : cmd 0xEC05  → 9 bytes (CO2 word+CRC, temp word+CRC, hum word+CRC)
//   CRC-8      : poly 0x31, init 0xFF
//
// All I2C operations complete in microseconds on 100 kHz bus — well within
// a single loop() cycle. No blocking; state machine drives reads lazily.
// ─────────────────────────────────────────────────────────────────────────────
#include "svc_air.h"
#include <Arduino.h>
#include <Wire.h>
#include "svc_notify.h"   // notifySvcPost
#include "svc_haptics.h"  // hapticsPattern

// ─────────────────────────────────────────────────────────────────────────────
// Internal constants
// ─────────────────────────────────────────────────────────────────────────────
#define SCD40_ADDR         0x62
#define SCD40_CMD_START    0x21B1
#define SCD40_CMD_READY    0xE4B8
#define SCD40_CMD_READ     0xEC05
#define SCD40_CMD_STOP     0x3F86

// ─────────────────────────────────────────────────────────────────────────────
// State
// ─────────────────────────────────────────────────────────────────────────────
enum AirState {
  AIR_STATE_MISSING  = 0,   // sensor not found / I2C error
  AIR_STATE_WARMUP   = 1,   // sensor found, waiting for first measurement
  AIR_STATE_POLLING  = 2,   // normal operation: poll every AIR_POLL_MS
};

static AirState   _state        = AIR_STATE_MISSING;
static uint32_t   _stateMs      = 0;    // millis() of last state entry
static uint32_t   _lastPollMs   = 0;    // millis() of last data-ready check
static bool       _hasData      = false;

static uint16_t   _co2          = 0;    // ppm
static int16_t    _tempC_x10    = 0;    // °C × 10
static uint16_t   _hum_x10      = 0;    // % × 10

// ── Alert state ───────────────────────────────────────────────────────────────
static AirAlertLevel _alertLevel      = AIR_ALERT_NONE;
static uint32_t      _warnLastAlertMs = 0;   // millis() of last WARN notification
static uint32_t      _critLastAlertMs = 0;   // millis() of last CRIT notification
#define COOLDOWN_MS  (AIR_ALERT_COOLDOWN_S * 1000UL)

// ─────────────────────────────────────────────────────────────────────────────
// CRC-8 helper (Sensirion polynomial 0x31, init 0xFF)
// ─────────────────────────────────────────────────────────────────────────────
static uint8_t _crc8(uint8_t a, uint8_t b) {
  uint8_t crc = 0xFF;
  uint8_t data[2] = { a, b };
  for (int i = 0; i < 2; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++)
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
  }
  return crc;
}

// ─────────────────────────────────────────────────────────────────────────────
// I2C helpers
// ─────────────────────────────────────────────────────────────────────────────
static bool _sendCmd(uint16_t cmd) {
  Wire.beginTransmission(SCD40_ADDR);
  Wire.write((uint8_t)(cmd >> 8));
  Wire.write((uint8_t)(cmd & 0xFF));
  return (Wire.endTransmission() == 0);
}

// Read n_words of (word + CRC) from sensor. Returns false if CRC mismatch.
static bool _readWords(uint16_t* out, int n_words) {
  int bytes = n_words * 3;
  if (Wire.requestFrom((int)SCD40_ADDR, bytes) != bytes) return false;
  for (int i = 0; i < n_words; i++) {
    uint8_t hi  = Wire.read();
    uint8_t lo  = Wire.read();
    uint8_t crc = Wire.read();
    if (crc != _crc8(hi, lo)) return false;
    out[i] = ((uint16_t)hi << 8) | lo;
  }
  return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Alert state machine — called after each new reading
// ─────────────────────────────────────────────────────────────────────────────
static void _checkAlerts() {
  uint32_t now = millis();
  AirAlertLevel prev = _alertLevel;

  // ── Determine new level ───────────────────────────────────────────────────
  if (_co2 >= AIR_THRESH_CRIT) {
    _alertLevel = AIR_ALERT_CRIT;
  } else if (_co2 >= AIR_THRESH_WARN) {
    _alertLevel = AIR_ALERT_WARN;
  } else if (_co2 < AIR_THRESH_HYST) {
    _alertLevel = AIR_ALERT_NONE;
  }
  // else: between HYST and WARN — keep current level (hysteresis dead-band)

  // ── Notify on CRIT ────────────────────────────────────────────────────────
  if (_alertLevel == AIR_ALERT_CRIT) {
    bool firstTime  = (prev != AIR_ALERT_CRIT);
    bool cooldownOk = (now - _critLastAlertMs >= COOLDOWN_MS);
    if (firstTime || cooldownOk) {
      char msg[32];
      snprintf(msg, sizeof(msg), "CO2 %u ppm", (unsigned)_co2);
      notifySvcPost(NOTIFY_ERROR, "AIR CRIT", msg, 4000);
      hapticsPattern(HAPTIC_ERROR);
      _critLastAlertMs = now;
    }
  }

  // ── Notify on WARN ────────────────────────────────────────────────────────
  if (_alertLevel == AIR_ALERT_WARN) {
    bool firstTime  = (prev != AIR_ALERT_WARN);
    bool cooldownOk = (now - _warnLastAlertMs >= COOLDOWN_MS);
    if (firstTime || cooldownOk) {
      char msg[32];
      snprintf(msg, sizeof(msg), "CO2 %u ppm", (unsigned)_co2);
      notifySvcPost(NOTIFY_WARN, "AIR WARN", msg, 3500);
      hapticsPattern(HAPTIC_WARN);
      _warnLastAlertMs = now;
    }
  }

  // ── Notify on recovery ────────────────────────────────────────────────────
  if (prev != AIR_ALERT_NONE && _alertLevel == AIR_ALERT_NONE) {
    notifySvcPost(NOTIFY_OK, "AIR", "Back to normal", 3000);
    hapticsBuzz(50);
    _warnLastAlertMs = 0;
    _critLastAlertMs = 0;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Demo mode — synthetic CO₂ wave, no hardware needed
// ─────────────────────────────────────────────────────────────────────────────
#if AIR_DEMO_MODE

static uint32_t _demoMs      = 0;
static uint32_t _demoPollMs  = 0;
// CO2 profile: linear sweep from 400 to 2600 and back, period ~240 s
// At 5 s steps: 2600-400 = 2200 range, 2200/20 = 110 steps up, same down → ~220 steps × 5s = 1100s
// Faster for testing: use 25 ppm/step → 88 steps up = 440s per cycle. Good.
static int16_t _demoDir = 1;    // +1 rising, -1 falling
static uint16_t _demoCO2 = 400;

static void _demoTick() {
  uint32_t now = millis();
  if (now - _demoPollMs < AIR_POLL_MS) return;
  _demoPollMs = now;

  _demoCO2 += (uint16_t)((int16_t)25 * _demoDir);
  if (_demoCO2 >= 2600) { _demoCO2 = 2600; _demoDir = -1; }
  if (_demoCO2 <= 380)  { _demoCO2 = 400;  _demoDir =  1; }

  _co2       = _demoCO2;
  _tempC_x10 = 215;   // fixed 21.5 °C
  _hum_x10   = 456;   // fixed 45.6 %
  _hasData   = true;
  _checkAlerts();
}

#endif  // AIR_DEMO_MODE

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

void airSvcInit() {
#if AIR_DEMO_MODE
  _state   = AIR_STATE_POLLING;   // demo: skip warmup, start immediately
  _stateMs = millis();
  _demoDir = 1;
  _demoCO2 = 400;
  _demoPollMs = millis() - AIR_POLL_MS;   // trigger first reading soon
  Serial.println("airSvc: DEMO MODE (synthetic CO2 wave)");
  return;
#endif

  Wire.begin(AIR_I2C_SDA, AIR_I2C_SCL);
  Wire.setClock(100000);   // 100 kHz — SCD40 spec

  // Stop any previous periodic measurement (safe even if none running)
  _sendCmd(SCD40_CMD_STOP);
  delay(500);   // called once in setup() — blocking is acceptable here

  // Start fresh periodic measurement
  if (!_sendCmd(SCD40_CMD_START)) {
    Serial.println("airSvc: SCD40 not found — running in MISSING mode");
    _state = AIR_STATE_MISSING;
    return;
  }

  Serial.println("airSvc: SCD40 found — warming up");
  _state   = AIR_STATE_WARMUP;
  _stateMs = millis();
}

void airSvcTick() {
#if AIR_DEMO_MODE
  _demoTick();
  return;
#endif

  if (_state == AIR_STATE_MISSING) return;

  uint32_t now = millis();

  // ── WARMUP: wait AIR_WARMUP_MS before first poll ─────────────────────────
  if (_state == AIR_STATE_WARMUP) {
    if (now - _stateMs < AIR_WARMUP_MS) return;
    _state      = AIR_STATE_POLLING;
    _lastPollMs = now - AIR_POLL_MS;   // trigger immediately
  }

  // ── POLLING: check data-ready flag every AIR_POLL_MS ─────────────────────
  if (now - _lastPollMs < AIR_POLL_MS) return;
  _lastPollMs = now;

  // Send data-ready command
  if (!_sendCmd(SCD40_CMD_READY)) {
    Serial.println("airSvc: I2C error — sensor lost");
    _state = AIR_STATE_MISSING;
    return;
  }
  delayMicroseconds(1000);   // SCD40 needs 1 ms after cmd before response

  uint16_t ready_word;
  if (!_readWords(&ready_word, 1)) return;     // CRC error — try next cycle
  if (!(ready_word & 0x07FF)) return;          // bit 11 = 0 → not ready yet

  // Data is ready — read measurement
  if (!_sendCmd(SCD40_CMD_READ)) return;
  delayMicroseconds(1000);

  uint16_t words[3];
  if (!_readWords(words, 3)) return;   // CRC failure

  uint16_t raw_co2  = words[0];
  uint16_t raw_temp = words[1];
  uint16_t raw_hum  = words[2];

  // Convert (SCD40 datasheet formulas)
  _co2       = raw_co2;
  // tempC = -45 + 175 * raw / 65535  → ×10 for fixed-point
  _tempC_x10 = (int16_t)(-450 + (int32_t)1750 * raw_temp / 65535);
  // hum   = 100 * raw / 65535        → ×10
  _hum_x10   = (uint16_t)((uint32_t)1000 * raw_hum / 65535);

  _hasData = true;
  _checkAlerts();
}

// ── State accessors ───────────────────────────────────────────────────────────
bool          airSvcSensorOk()      { return _state != AIR_STATE_MISSING; }
bool          airSvcHasData()       { return _hasData; }
uint16_t      airSvcCO2ppm()        { return _co2;       }
int16_t       airSvcTempC_x10()     { return _tempC_x10; }
uint16_t      airSvcHumidity_x10()  { return _hum_x10;  }
AirAlertLevel airSvcAlertLevel()    { return _alertLevel;}

void airSvcGetSummary(char* out, int outLen) {
  if (!_hasData) {
    snprintf(out, outLen, airSvcSensorOk() ? "CO2 ---ppm (warmup)" : "SENSOR NOT FOUND");
    return;
  }
  int tempF = (int)(_tempC_x10 * 9 / 50) + 32;     // °C×10 → °F integer
  int hum   = (int)(_hum_x10 / 10);
  snprintf(out, outLen, "CO2 %uppm  %d%%  %dF", (unsigned)_co2, hum, tempF);
}
