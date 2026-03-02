// ─────────────────────────────────────────────────────────────────────────────
// svc_tempir.cpp  —  IR Temperature Sensor Service (MLX90614 / Demo)
//
// MLX90614 I2C protocol (SMBus-compatible):
//   Address  : 0x5A  (factory default)
//   RAM 0x06 : T_ambient   (16-bit, LSB first, + PEC byte)
//   RAM 0x07 : T_object1   (16-bit, LSB first, + PEC byte)
//   Raw→°C   : tempK = (raw & 0x7FFF) * 0.02f;  tempC = tempK - 273.15f;
//   Error    : bit 15 of raw = 1 → discard
//
// Wire read sequence:
//   beginTransmission(0x5A)  write(reg)  endTransmission(false)
//   requestFrom(0x5A, 3)     read LSB, MSB, PEC (PEC discarded here)
//
// PEC (CRC-8 / SMBus) is discarded in this implementation — the error bit
// and range check provide sufficient integrity for a thermometer use case.
// ─────────────────────────────────────────────────────────────────────────────
#include "svc_tempir.h"

#if TEMPIR_ENABLED  // ─── entire module wrapped ────────────────────────────────

#include <Arduino.h>
#include <Wire.h>
#include "svc_notify.h"   // notifySvcPost
#include "svc_haptics.h"  // hapticsBuzz / hapticsPattern

// ─────────────────────────────────────────────────────────────────────────────
// Internal constants
// ─────────────────────────────────────────────────────────────────────────────
#define MLX_ADDR     0x5A
#define MLX_REG_AMB  0x06
#define MLX_REG_OBJ  0x07

#define COOLDOWN_MS  (TEMPIR_ALERT_COOLDOWN_S * 1000UL)

// Sanity bounds — discard readings outside physical plausibility
#define TEMP_MIN_C   -40.0f
#define TEMP_MAX_C   380.0f

// ─────────────────────────────────────────────────────────────────────────────
// State
// ─────────────────────────────────────────────────────────────────────────────
static bool     _sensorOk     = false;
static bool     _hasData      = false;
static bool     _appOpen      = false;

static float    _objC         = 0.0f;
static float    _ambC         = 0.0f;

static uint32_t _lastPollMs   = 0;

// ── Alert ─────────────────────────────────────────────────────────────────────
static bool     _alertActive  = false;   // true while above HYST
static uint32_t _lastAlertMs  = 0;

// ── Burst ─────────────────────────────────────────────────────────────────────
static bool     _burstActive   = false;
static int      _burstCount    = 0;
static float    _burstSum      = 0.0f;
static uint32_t _burstNextMs   = 0;
static float    _burstResult   = 0.0f;

// ─────────────────────────────────────────────────────────────────────────────
// I2C read helper (real sensor only)
// ─────────────────────────────────────────────────────────────────────────────
#if !TEMPIR_DEMO_MODE

static bool _readReg(uint8_t reg, float* outC) {
  Wire.beginTransmission(MLX_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;   // NAK — sensor missing

  if (Wire.requestFrom((int)MLX_ADDR, 3) != 3) return false;
  uint8_t lo  = Wire.read();
  uint8_t hi  = Wire.read();
  /* uint8_t pec = */ Wire.read();   // PEC discarded

  uint16_t raw = ((uint16_t)hi << 8) | lo;
  if (raw & 0x8000) return false;    // error flag set

  float tempK = (raw & 0x7FFF) * 0.02f;
  float tempC = tempK - 273.15f;

  if (tempC < TEMP_MIN_C || tempC > TEMP_MAX_C) return false;  // sanity
  *outC = tempC;
  return true;
}

static bool _doRead() {
  float obj = 0.0f, amb = 0.0f;
  if (!_readReg(MLX_REG_OBJ, &obj)) return false;
  if (!_readReg(MLX_REG_AMB, &amb)) return false;
  _objC    = obj;
  _ambC    = amb;
  _hasData = true;
  return true;
}

#endif  // !TEMPIR_DEMO_MODE

// ─────────────────────────────────────────────────────────────────────────────
// Demo mode — synthetic ramp
// ─────────────────────────────────────────────────────────────────────────────
#if TEMPIR_DEMO_MODE

static float    _demoObj     = 22.0f;
static float    _demoDir     = 1.0f;    // +1 rising, -1 falling
static uint32_t _demoPollMs  = 0;

// Ramp: 22 → 75 → 22 °C, step 1°C every 500ms → full cycle ~106 s
// Crosses WARN(60) at ~(60-22)/1 * 0.5s = 19s; CRIT—no CRIT in spec, just WARN
// Ambient fixed at 24.0°C

static void _demoTick() {
  uint32_t now = millis();
  uint32_t interval = _appOpen ? TEMPIR_POLL_FAST_MS : TEMPIR_POLL_SLOW_MS;
  if (now - _demoPollMs < interval) return;
  _demoPollMs = now;

  _demoObj += _demoDir * 1.0f;
  if (_demoObj >= 75.0f) { _demoObj = 75.0f; _demoDir = -1.0f; }
  if (_demoObj <= 22.0f) { _demoObj = 22.0f; _demoDir =  1.0f; }

  _objC    = _demoObj;
  _ambC    = 24.0f;
  _hasData = true;
}

#endif  // TEMPIR_DEMO_MODE

// ─────────────────────────────────────────────────────────────────────────────
// Alert state machine
// ─────────────────────────────────────────────────────────────────────────────
static void _checkAlert() {
  uint32_t now = millis();

  if (_objC >= TEMPIR_THRESH_WARN) {
    bool firstTime  = !_alertActive;
    bool cooldownOk = (now - _lastAlertMs >= COOLDOWN_MS);

    if (firstTime || cooldownOk) {
      char msg[32];
      // Format float without String: split into integer + 1 decimal
      int whole = (int)_objC;
      int frac  = (int)((_objC - (float)whole) * 10.0f);
      snprintf(msg, sizeof(msg), "OBJ %d.%dC", whole, frac);
      notifySvcPost(NOTIFY_WARN, "TEMP WARN", msg, 4000);
      hapticsPattern(HAPTIC_WARN);
      _lastAlertMs  = now;
      _alertActive  = true;
    }
  } else if (_objC < TEMPIR_THRESH_HYST && _alertActive) {
    // Recovery
    notifySvcPost(NOTIFY_OK, "TEMP", "Object temp normal", 3000);
    hapticsBuzz(50);
    _alertActive = false;
    _lastAlertMs = 0;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Burst state machine (non-blocking, shared with regular poll timer)
// ─────────────────────────────────────────────────────────────────────────────
static void _burstTick() {
  if (!_burstActive) return;

  uint32_t now = millis();
  if (now < _burstNextMs) return;
  _burstNextMs = now + TEMPIR_BURST_INTERVAL_MS;

#if TEMPIR_DEMO_MODE
  // Demo: just add current reading
  _burstSum += _objC;
  _burstCount++;
#else
  float tmp = 0.0f;
  if (_readReg(MLX_REG_OBJ, &tmp)) {
    _burstSum += tmp;
    _burstCount++;
  }
#endif

  if (_burstCount >= TEMPIR_BURST_SAMPLES) {
    _burstResult  = (_burstCount > 0) ? (_burstSum / (float)_burstCount) : 0.0f;
    _objC         = _burstResult;   // update live reading too
    _hasData      = true;
    _burstActive  = false;
    _burstCount   = 0;
    _burstSum     = 0.0f;
    hapticsPattern(HAPTIC_SUCCESS);
    _checkAlert();
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

void tempIrInit() {
#if TEMPIR_DEMO_MODE
  _sensorOk    = true;   // demo always "ok"
  _demoObj     = 22.0f;
  _demoDir     = 1.0f;
  _demoPollMs  = millis() - TEMPIR_POLL_FAST_MS;  // trigger first read soon
  Serial.println("tempIr: DEMO MODE (synthetic ramp 2222→75°C)");
#else
  // Real sensor — Wire may already be initialised by svc_air (same pins).
  // Calling Wire.begin() again with the same pins is safe on ESP32.
  Wire.begin(TEMPIR_I2C_SDA, TEMPIR_I2C_SCL);
  Wire.setClock(100000);

  // Probe: try to read ambient register; NAK = sensor absent
  float probe = 0.0f;
  if (_readReg(MLX_REG_AMB, &probe)) {
    _sensorOk = true;
    Serial.println("tempIr: MLX90614 found");
  } else {
    _sensorOk = false;
    Serial.println("tempIr: MLX90614 NOT found — stub mode");
  }
#endif
}

void tempIrTick() {
#if TEMPIR_DEMO_MODE
  _demoTick();
  if (_hasData) _checkAlert();
  _burstTick();
#else
  if (!_sensorOk) return;

  // Burst takes priority over regular poll
  if (_burstActive) {
    _burstTick();
    return;
  }

  uint32_t now      = millis();
  uint32_t interval = _appOpen ? TEMPIR_POLL_FAST_MS : TEMPIR_POLL_SLOW_MS;
  if (now - _lastPollMs < interval) return;
  _lastPollMs = now;

  if (_doRead()) _checkAlert();
#endif
}

void tempIrSetAppOpen(bool open) {
  _appOpen = open;
}

// ── Accessors ─────────────────────────────────────────────────────────────────
bool  tempIrSensorOk()  { return _sensorOk;    }
bool  tempIrHasData()   { return _hasData;     }
float tempIrObjectC()   { return _objC;        }
float tempIrAmbientC()  { return _ambC;        }

void tempIrGetSummary(char* out, int outLen) {
  if (!_sensorOk) {
    snprintf(out, outLen, "SENSOR NOT FOUND");
    return;
  }
  if (!_hasData) {
    snprintf(out, outLen, "OBJ ---C  AMB ---C");
    return;
  }
  // Fixed-point print: avoids floats in snprintf on some toolchains
  int objWh = (int)_objC;
  int objFr = (int)((_objC - (float)objWh) * 10.0f);
  if (objFr < 0) objFr = -objFr;
  int ambWh = (int)_ambC;
  int ambFr = (int)((_ambC - (float)ambWh) * 10.0f);
  if (ambFr < 0) ambFr = -ambFr;
  snprintf(out, outLen, "OBJ %d.%dC  AMB %d.%dC",
           objWh, objFr, ambWh, ambFr);
}

// ── Burst ─────────────────────────────────────────────────────────────────────
void tempIrRequestBurst() {
  if (!_sensorOk || _burstActive) return;
  _burstCount  = 0;
  _burstSum    = 0.0f;
  _burstResult = 0.0f;
  _burstActive = true;
  _burstNextMs = millis();   // first sample immediately
}

bool  tempIrBurstActive() { return _burstActive; }
float tempIrBurstResult() { return _burstActive ? 0.0f : _burstResult; }

// ─────────────────────────────────────────────────────────────────────────────
#else  // TEMPIR_ENABLED == 0  — compile-safe stubs ─────────────────────────────
// ─────────────────────────────────────────────────────────────────────────────

void  tempIrInit()               {}
void  tempIrTick()               {}
void  tempIrSetAppOpen(bool)     {}
bool  tempIrSensorOk()           { return false; }
bool  tempIrHasData()            { return false; }
float tempIrObjectC()            { return 0.0f;  }
float tempIrAmbientC()           { return 0.0f;  }
void  tempIrGetSummary(char* o, int l) { snprintf(o, l, "DISABLED"); }
void  tempIrRequestBurst()       {}
bool  tempIrBurstActive()        { return false; }
float tempIrBurstResult()        { return 0.0f;  }

#endif  // TEMPIR_ENABLED