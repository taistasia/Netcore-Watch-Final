// svc_air.cpp
#include "svc_air.h"
#include <Arduino.h>

#include "svc_notify.h"   // notifySvcPost
#include "svc_haptics.h"  // hapticsPattern

// Real SCD40 path uses Wire; MQ2 sim path does not.
#if !NETCORE_SIM_AIR_MQ2_ENABLE
#include <Wire.h>
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Internal constants (SCD40 real hardware only)
// ─────────────────────────────────────────────────────────────────────────────
#if !NETCORE_SIM_AIR_MQ2_ENABLE
#define SCD40_ADDR         0x62
#define SCD40_CMD_START    0x21B1
#define SCD40_CMD_READY    0xE4B8
#define SCD40_CMD_READ     0xEC05
#define SCD40_CMD_STOP     0x3F86
#endif

// Alert cooldown to avoid spam
#define COOLDOWN_MS        15000UL

enum AirState {
  AIR_STATE_MISSING  = 0,
  AIR_STATE_WARMUP   = 1,
  AIR_STATE_POLLING  = 2,
  AIR_STATE_SIM_MQ2  = 3,
};

static AirState  _state     = AIR_STATE_MISSING;
static uint32_t  _stateMs   = 0;

static bool      _sensorOk  = false;
static bool      _hasData   = false;

static uint16_t  _co2       = 0;      // ppm
static int16_t   _tempC_x10 = 0;      // °C *10 (optional)
static uint16_t  _hum_x10   = 0;      // %RH *10 (optional)

static bool      _isWarn    = false;
static bool      _isCrit    = false;

static uint32_t  _lastAlertMs = 0;

// CRC-8 for SCD40 word reads (poly 0x31, init 0xFF)
static uint8_t _crc8(uint8_t a, uint8_t b) {
  uint8_t crc = 0xFF;
  uint8_t data[2] = {a, b};
  for (int i = 0; i < 2; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      if (crc & 0x80) crc = (uint8_t)((crc << 1) ^ 0x31);
      else           crc = (uint8_t)(crc << 1);
    }
  }
  return crc;
}

// ─────────────────────────────────────────────────────────────────────────────
// I2C helpers (SCD40 real hardware only)
// ─────────────────────────────────────────────────────────────────────────────
#if !NETCORE_SIM_AIR_MQ2_ENABLE
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
#endif

static void _checkAlerts() {
  bool warn = (_co2 > AIR_SAFE_MAX);
  bool crit = (_co2 > AIR_WARN_MAX);

  _isWarn = warn;
  _isCrit = crit;

  uint32_t now = millis();
  bool cooldownOk = (now - _lastAlertMs >= COOLDOWN_MS);

  if (crit && cooldownOk) {
    notifySvcPost(NOTIFY_CRIT, "AIR CRIT", "CO2 high", 5000);
    hapticsPattern(HAPTIC_CRIT);
    _lastAlertMs = now;
  } else if (warn && cooldownOk) {
    notifySvcPost(NOTIFY_WARN, "AIR WARN", "CO2 elevated", 4000);
    hapticsPattern(HAPTIC_WARN);
    _lastAlertMs = now;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// MQ2 SIM MODE (Wokwi) — analog CO2-proxy feed
//   - No floats
//   - No I2C
//   - Fixed-point/integer-friendly mapping
//   - Slow IIR smoothing
//
// Mapping strategy:
//   raw ADC (0..4095) -> proxy ppm (400..~5000)
//   ppm = 400 + raw * 4600 / 4095
// ─────────────────────────────────────────────────────────────────────────────
#if NETCORE_SIM_AIR_MQ2_ENABLE

static uint32_t _mq2PollMs = 0;
static uint16_t _mq2Filt   = 0;   // 0..4095
static bool     _mq2Init   = false;

static uint16_t _mapAdcToPpm(uint16_t adc) {
  // 400..5000 (approx)
  uint32_t ppm = 400UL + ((uint32_t)adc * 4600UL) / 4095UL;
  if (ppm > 5000UL) ppm = 5000UL;
  return (uint16_t)ppm;
}

static void _mq2Tick() {
  uint32_t now = millis();
  if (now - _mq2PollMs < AIR_MQ2_POLL_MS) return;
  _mq2PollMs = now;

  uint16_t raw = (uint16_t)analogRead(AIR_MQ2_ADC_PIN); // 0..4095
  if (!_mq2Init) {
    _mq2Init = true;
    _mq2Filt = raw;
  } else {
    // IIR: 7/8 old + 1/8 new (integer)
    _mq2Filt = (uint16_t)(((uint32_t)_mq2Filt * 7UL + (uint32_t)raw) / 8UL);
  }

  _co2       = _mapAdcToPpm(_mq2Filt);
  _tempC_x10 = 0;
  _hum_x10   = 0;
  _hasData   = true;
  _checkAlerts();
}

#endif  // NETCORE_SIM_AIR_MQ2_ENABLE

// ─────────────────────────────────────────────────────────────────────────────
// Legacy DEMO MODE — synthetic wave (kept for bring-up only)
// ─────────────────────────────────────────────────────────────────────────────
#if AIR_DEMO_MODE

static uint32_t _demoPollMs = 0;
static int      _demoDir    = 1;
static uint16_t _demoCO2    = 400;

static void _demoTick() {
  uint32_t now = millis();
  if (now - _demoPollMs < AIR_POLL_MS) return;
  _demoPollMs = now;

  // simple saw wave: 400 → 2400 → 400
  if (_demoDir > 0) {
    _demoCO2 += 50;
    if (_demoCO2 >= 2400) { _demoCO2 = 2400; _demoDir = -1; }
  } else {
    if (_demoCO2 > 450) _demoCO2 -= 50;
    else { _demoCO2 = 400; _demoDir = 1; }
  }

  _co2       = _demoCO2;
  _tempC_x10 = 0;
  _hum_x10   = 0;
  _hasData   = true;
  _checkAlerts();
}

#endif  // AIR_DEMO_MODE

void airSvcInit() {

#if NETCORE_SIM_AIR_MQ2_ENABLE
  pinMode(AIR_MQ2_ADC_PIN, INPUT);
  analogReadResolution(12);
  _mq2Init   = false;
  _mq2PollMs = millis() - AIR_MQ2_POLL_MS;
  _state     = AIR_STATE_SIM_MQ2;
  _stateMs   = millis();
  Serial.println("airSvc: SIM MQ2 MODE (analog CO2 proxy)");
  return;
#endif

#if AIR_DEMO_MODE
  _state   = AIR_STATE_POLLING;
  _stateMs = millis();
  _demoDir = 1;
  _demoCO2 = 400;
  _demoPollMs = millis() - AIR_POLL_MS;
  Serial.println("airSvc: DEMO MODE (synthetic CO2 wave)");
  return;
#endif

  // Real sensor mode (SCD40) — only when NETCORE_SIM_AIR_MQ2_ENABLE == 0
#if !NETCORE_SIM_AIR_MQ2_ENABLE
  Wire.begin(AIR_I2C_SDA, AIR_I2C_SCL);
  Wire.setClock(100000);

  // Stop any previous periodic measurement (safe even if none running)
  _sendCmd(SCD40_CMD_STOP);
  delay(500);   // setup() only

  if (!_sendCmd(SCD40_CMD_START)) {
    Serial.println("airSvc: SCD40 not found — running in MISSING mode");
    _state = AIR_STATE_MISSING;
    return;
  }

  Serial.println("airSvc: SCD40 found — warming up");
  _state   = AIR_STATE_WARMUP;
  _stateMs = millis();
#endif
}

void airSvcTick() {

#if NETCORE_SIM_AIR_MQ2_ENABLE
  _mq2Tick();
  return;
#endif

#if AIR_DEMO_MODE
  _demoTick();
  return;
#endif

#if NETCORE_SIM_AIR_MQ2_ENABLE
  // (handled above)
  return;
#else
  // SCD40 real sensor mode
  if (_state == AIR_STATE_MISSING) return;

  uint32_t now = millis();

  if (_state == AIR_STATE_WARMUP) {
    if (now - _stateMs >= AIR_WARMUP_MS) {
      _state = AIR_STATE_POLLING;
      _stateMs = now - AIR_POLL_MS; // trigger first read immediately
    }
    return;
  }

  // Poll cycle
  if (now - _stateMs < AIR_POLL_MS) return;
  _stateMs = now;

  // Data ready?
  if (!_sendCmd(SCD40_CMD_READY)) {
    Serial.println("airSvc: I2C error — sensor lost");
    _state = AIR_STATE_MISSING;
    return;
  }
  delayMicroseconds(1000);   // SCD40 response latency

  uint16_t ready = 0;
  if (!_readWords(&ready, 1)) return;
  if ((ready & 0x07FF) == 0) return; // not ready yet

  // Read measurement
  if (!_sendCmd(SCD40_CMD_READ)) return;
  delayMicroseconds(1000);

  uint16_t w[3] = {0,0,0};
  if (!_readWords(w, 3)) return;

  // Convert per datasheet
  uint16_t co2 = w[0];
  uint16_t tRaw = w[1];
  uint16_t hRaw = w[2];

  // Temperature °C = -45 + 175 * (tRaw / 65535)
  // Humidity %RH   = 100 * (hRaw / 65535)
  // Keep fixed-point x10 integer conversions.
  int32_t t_x10 = -450 + (1750L * (int32_t)tRaw) / 65535L;
  uint32_t h_x10 = (1000UL * (uint32_t)hRaw) / 65535UL;

  _co2       = co2;
  _tempC_x10 = (int16_t)t_x10;
  _hum_x10   = (uint16_t)h_x10;

  _hasData = true;
  _checkAlerts();

#endif // NETCORE_SIM_AIR_MQ2_ENABLE
}

bool     airSvcSensorOk()   { return _sensorOk; }
bool     airSvcHasData()    { return _hasData;  }
uint16_t airSvcCO2()        { return _co2;      }
int16_t  airSvcTempC_x10()  { return _tempC_x10;}
uint16_t airSvcHum_x10()    { return _hum_x10;  }

bool airSvcIsWarn() { return _isWarn; }
bool airSvcIsCrit() { return _isCrit; }