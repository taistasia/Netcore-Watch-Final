// svc_tempir.cpp
#include "svc_tempir.h"
#include <Arduino.h>
#include <Wire.h>
#include "svc_notify.h"
#include "svc_haptics.h"

#if TEMPIR_ENABLED

// MLX90614 default I2C address
#define MLX_ADDR 0x5A

// MLX registers
#define MLX_REG_AMB 0x06
#define MLX_REG_OBJ 0x07

// Alert cooldown (ms)
#define COOLDOWN_MS 15000UL

static bool     _sensorOk     = false;
static bool     _hasData      = false;
static int16_t  _objC_x10     = 0;   // °C × 10
static int16_t  _ambC_x10     = 0;   // °C × 10
static bool     _alertActive  = false;
static uint32_t _lastAlertMs  = 0;

static bool     _appOpen      = false;  // hint from UI/app (set via tempIrSetAppOpen)

// Burst state
static bool     _burstActive   = false;
static int      _burstCount    = 0;
static int32_t  _burstSum_x10  = 0;   // sum of °C×10 samples
static uint32_t _burstNextMs   = 0;
static int16_t  _burstResult_x10 = 0;

// ─────────────────────────────────────────────────────────────────────────────
// I2C raw read (MLX90614) — non-blocking enough for tick cadence; no heap.
// ─────────────────────────────────────────────────────────────────────────────
static bool _readReg(uint8_t reg, float* outC) {
  // SMBus read word: reg -> 3 bytes: low, high, PEC. We ignore PEC for now.
  Wire.beginTransmission(MLX_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;

  if (Wire.requestFrom((int)MLX_ADDR, 3) != 3) return false;
  uint8_t lo = Wire.read();
  uint8_t hi = Wire.read();
  (void)Wire.read(); // PEC ignored

  uint16_t raw = ((uint16_t)hi << 8) | lo;
  // Convert per datasheet: Temp = (raw * 0.02) - 273.15
  // Keep float conversion here (not in tick loops); values are cached.
  float tK = (float)raw * 0.02f;
  float tC = tK - 273.15f;
  *outC = tC;
  return true;
}

static bool _doRead() {
  float obj = 0.0f, amb = 0.0f;
  if (!_readReg(MLX_REG_OBJ, &obj)) return false;
  if (!_readReg(MLX_REG_AMB, &amb)) return false;
  // Convert to °C×10 (integer) to keep hot path float-free elsewhere.
  _objC_x10 = (int16_t)(obj * 10.0f);
  _ambC_x10 = (int16_t)(amb * 10.0f);
  _hasData = true;
  return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Demo generator (Wokwi) — deterministic and integer-only in tick.
// ─────────────────────────────────────────────────────────────────────────────
#if TEMPIR_DEMO_MODE

// Deterministic mock generator (integer stepping, no floats in tick).
// Object temp drifts slowly between 22.0°C and 75.0°C with occasional small steps.
// Ambient stays ~24.0°C with tiny jitter.

static int16_t  _demoObj_x10   = 220;  // 22.0°C
static int16_t  _demoAmb_x10   = 240;  // 24.0°C
static int8_t   _demoDir       = 1;    // +1 rising, -1 falling
static uint32_t _demoPollMs    = 0;
static uint16_t _lfsr          = 0xACE1u;

static inline uint16_t _lfsrNext() {
  // 16-bit Galois LFSR
  uint16_t l = _lfsr;
  uint16_t lsb = l & 1u;
  l >>= 1;
  if (lsb) l ^= 0xB400u;
  _lfsr = l;
  return l;
}

static void _demoTick() {
  uint32_t now = millis();
  uint32_t interval = _appOpen ? TEMPIR_POLL_FAST_MS : TEMPIR_POLL_SLOW_MS;
  if (now - _demoPollMs < interval) return;
  _demoPollMs = now;

  // Base drift: 0.2°C per tick (x10 = 2)
  _demoObj_x10 += (int16_t)(_demoDir * 2);

  // Occasional step (every ~16 ticks): +/- 0.5°C
  if (((_lfsrNext() >> 4) & 0x0F) == 0) {
    int8_t step = (int8_t)((_lfsrNext() & 1u) ? 5 : -5);
    _demoObj_x10 += step;
  }

  // Bounds: 22.0°C..75.0°C
  if (_demoObj_x10 >= 750) { _demoObj_x10 = 750; _demoDir = -1; }
  if (_demoObj_x10 <= 220) { _demoObj_x10 = 220; _demoDir =  1; }

  // Ambient tiny jitter: +/-0.1°C occasionally
  if (((_lfsrNext() >> 8) & 0x1F) == 0) {
    _demoAmb_x10 += (int16_t)((_lfsrNext() & 1u) ? 1 : -1);
    if (_demoAmb_x10 < 235) _demoAmb_x10 = 235;
    if (_demoAmb_x10 > 245) _demoAmb_x10 = 245;
  }

  _objC_x10 = _demoObj_x10;
  _ambC_x10 = _demoAmb_x10;
  _hasData  = true;
}

#endif  // TEMPIR_DEMO_MODE

// ─────────────────────────────────────────────────────────────────────────────
// Alerts
// ─────────────────────────────────────────────────────────────────────────────
static void _checkAlert() {
  uint32_t now = millis();

  if (_objC_x10 >= TEMPIR_THRESH_WARN_X10) {
    bool firstTime  = !_alertActive;
    bool cooldownOk = (now - _lastAlertMs >= COOLDOWN_MS);

    if (firstTime || cooldownOk) {
      char msg[32];
      int whole = (int)(_objC_x10 / 10);
      int frac  = (int)(_objC_x10 % 10);
      if (frac < 0) frac = -frac;
      snprintf(msg, sizeof(msg), "OBJ %d.%dC", whole, frac);
      notifySvcPost(NOTIFY_WARN, "TEMP WARN", msg, 4000);
      hapticsPattern(HAPTIC_WARN);
      _lastAlertMs  = now;
      _alertActive  = true;
    }
  } else if (_objC_x10 < TEMPIR_THRESH_HYST_X10 && _alertActive) {
    // Recovery
    notifySvcPost(NOTIFY_OK, "TEMP", "Object temp normal", 3000);
    hapticsBuzz(50);
    _alertActive = false;
    _lastAlertMs = 0;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Burst sampling (non-blocking)
// ─────────────────────────────────────────────────────────────────────────────
static void _burstTick() {
  if (!_burstActive) return;

  uint32_t now = millis();
  if (now < _burstNextMs) return;

  // Take a sample
#if TEMPIR_DEMO_MODE
  // Demo: just add current reading
  _burstSum_x10 += _objC_x10;
  _burstCount++;
#else
  float tmp = 0.0f;
  if (_readReg(MLX_REG_OBJ, &tmp)) {
    _burstSum_x10 += (int32_t)(tmp * 10.0f);
    _burstCount++;
  }
#endif

  _burstNextMs = now + TEMPIR_BURST_SPACING_MS;

  if (_burstCount >= TEMPIR_BURST_SAMPLES) {
    _burstResult_x10 = (_burstCount > 0) ? (int16_t)(_burstSum_x10 / (int32_t)_burstCount) : 0;
    _objC_x10        = _burstResult_x10;
    _hasData      = true;
    _burstActive  = false;
    _burstCount   = 0;
    _burstSum_x10 = 0;
    hapticsPattern(HAPTIC_SUCCESS);
    _checkAlert();
  }
}

void tempIrInit() {
#if TEMPIR_DEMO_MODE
  _sensorOk    = true;   // demo always "ok"
  _demoObj_x10  = 220;
  _demoAmb_x10  = 240;
  _demoDir      = 1;
  _demoPollMs   = millis() - TEMPIR_POLL_FAST_MS;
  Serial.println("tempIr: SIM MODE (deterministic mock IR temp)");
#else
  Wire.begin(8, 9);
  Wire.setClock(100000);

  // Quick probe read
  float tmp = 0.0f;
  _sensorOk = _readReg(MLX_REG_OBJ, &tmp);
  Serial.println(_sensorOk ? "tempIr: MLX90614 OK" : "tempIr: MLX90614 MISSING");
#endif
}

void tempIrTick() {
#if TEMPIR_DEMO_MODE
  _demoTick();
  if (_hasData) _checkAlert();
  _burstTick();
#else
  if (!_sensorOk) return;

  // simple polling; no blocking loops
  static uint32_t lastPollMs = 0;
  uint32_t now = millis();
  uint32_t interval = _appOpen ? TEMPIR_POLL_FAST_MS : TEMPIR_POLL_SLOW_MS;
  if (now - lastPollMs >= interval) {
    lastPollMs = now;
    if (_doRead()) _checkAlert();
  }
  _burstTick();
#endif
}

bool  tempIrSensorOk()  { return _sensorOk;    }
bool  tempIrHasData()   { return _hasData;     }
float tempIrObjectC()   { return (float)_objC_x10 * 0.1f; }
float tempIrAmbientC()  { return (float)_ambC_x10 * 0.1f; }

void tempIrFormat(char* out, int outLen) {
  if (!out || outLen <= 0) return;
  if (!_hasData) {
    snprintf(out, outLen, "IR: no data");
    return;
  }
  // Fixed-point print: avoids floats in snprintf on some toolchains
  int objWh = (int)(_objC_x10 / 10);
  int objFr = (int)(_objC_x10 % 10);
  if (objFr < 0) objFr = -objFr;
  int ambWh = (int)(_ambC_x10 / 10);
  int ambFr = (int)(_ambC_x10 % 10);
  if (ambFr < 0) ambFr = -ambFr;
  snprintf(out, outLen, "OBJ %d.%dC  AMB %d.%dC",
           objWh, objFr, ambWh, ambFr);
}

void tempIrRequestBurst() {
  if (!_sensorOk || _burstActive) return;
  _burstCount  = 0;
  _burstSum_x10  = 0;
  _burstResult_x10 = 0;
  _burstActive = true;
  _burstNextMs = millis();   // first sample immediately
}

bool  tempIrBurstActive() { return _burstActive; }
float tempIrBurstResult() { return _burstActive ? 0.0f : ((float)_burstResult_x10 * 0.1f); }

#else

// ─────────────────────────────────────────────────────────────────────────────
// Disabled stubs
// ─────────────────────────────────────────────────────────────────────────────
void tempIrInit() {}
void tempIrTick() {}

bool  tempIrSensorOk()  { return false; }
bool  tempIrHasData()   { return false; }
float tempIrObjectC()   { return 0.0f; }
float tempIrAmbientC()  { return 0.0f; }

void tempIrFormat(char* out, int outLen) {
  if (!out || outLen <= 0) return;
  snprintf(out, outLen, "IR: disabled");
}

void  tempIrRequestBurst() {}
bool  tempIrBurstActive() { return false; }
float tempIrBurstResult() { return 0.0f; }

#endif  // TEMPIR_ENABLED