#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// svc_air.h  —  Air Quality Service (SCD40 CO₂ / Temp / Humidity)
//
// Hardware  : Sensirion SCD40 on I2C (address 0x62).
//             Uses raw Wire commands — no external SCD4x library required.
//             If sensor not found on init, service runs in MISSING mode
//             (all reads return 0, airSvcSensorOk() = false) — compiles and
//             runs safely with no hardware attached.
//
// Demo mode : Define AIR_DEMO_MODE 1 below (default = 1) for Wokwi.
//             Feeds a synthetic CO₂ sine-wave that crosses both thresholds
//             so all alerts can be exercised without real hardware.
//             Set to 0 for production / real sensor builds.
//
// API ─────────────────────────────────────────────────────────────────────────
//   airSvcInit()              — call once in setup(); starts I2C + SCD40
//   airSvcTick()              — call every loop(); non-blocking poll
//
//   airSvcSensorOk()          — false until first successful read
//   airSvcHasData()           — true once first measurement arrived
//   airSvcCO2ppm()            — last CO₂ in ppm  (0 if no data)
//   airSvcTempC_x10()         — temperature × 10 °C  (e.g. 215 = 21.5 °C)
//   airSvcHumidity_x10()      — relative humidity × 10 % (e.g. 456 = 45.6 %)
//   airSvcGetSummary(buf,len) — fills "CO2 650ppm  45%  72F"
//   airSvcAlertLevel()        — AIR_ALERT_NONE / WARN / CRIT
//
// Thresholds ──────────────────────────────────────────────────────────────────
//   AIR_THRESH_WARN  1200 ppm  → NOTIFY_WARN + HAPTIC_WARN
//   AIR_THRESH_CRIT  2000 ppm  → NOTIFY_ERROR + HAPTIC_ERROR
//   AIR_THRESH_HYST   900 ppm  → recovery hysteresis (back-to-normal only below this)
//   AIR_ALERT_COOLDOWN_S  120  → minimum seconds between same-level alerts
//
// Timing ──────────────────────────────────────────────────────────────────────
//   SCD40 measurement cycle : 5 000 ms (sensor requirement — cannot be shortened)
//   Service poll interval   : AIR_POLL_MS = 5 000 ms (checks data-ready flag)
//   No delay() anywhere.
// ─────────────────────────────────────────────────────────────────────────────
#include <stdint.h>

// ── Demo / Wokwi mode ─────────────────────────────────────────────────────────
#define AIR_DEMO_MODE  1   // 1 = synthetic CO₂ wave (no hardware needed)
                           // 0 = real SCD40 over I2C

// ── I2C pins ──────────────────────────────────────────────────────────────────
#define AIR_I2C_SDA    8
#define AIR_I2C_SCL    9

// ── Timing ────────────────────────────────────────────────────────────────────
#define AIR_POLL_MS          5000UL    // poll interval (ms) — matches SCD40 cycle
#define AIR_WARMUP_MS        5500UL    // time after start before first read is valid
#define AIR_ALERT_COOLDOWN_S  120UL    // minimum seconds between repeat alerts

// ── Thresholds ────────────────────────────────────────────────────────────────
#define AIR_THRESH_WARN  1200   // ppm — WARN level
#define AIR_THRESH_CRIT  2000   // ppm — CRITICAL level
#define AIR_THRESH_HYST   900   // ppm — recovery hysteresis (must drop below this)

// ── Alert level enum ─────────────────────────────────────────────────────────
enum AirAlertLevel {
  AIR_ALERT_NONE = 0,
  AIR_ALERT_WARN = 1,
  AIR_ALERT_CRIT = 2,
};

// ── Lifecycle ─────────────────────────────────────────────────────────────────
void airSvcInit();
void airSvcTick();

// ── State queries ─────────────────────────────────────────────────────────────
bool         airSvcSensorOk();      // true if sensor responded on init
bool         airSvcHasData();       // true after first valid measurement
uint16_t     airSvcCO2ppm();        // ppm  (0 if no data)
int16_t      airSvcTempC_x10();     // °C × 10  (e.g. 215 = 21.5 °C)
uint16_t     airSvcHumidity_x10();  // % × 10   (e.g. 456 = 45.6 %)
AirAlertLevel airSvcAlertLevel();   // current alert level

// ── Summary string ────────────────────────────────────────────────────────────
// Fills: "CO2 650ppm  45%  72F"  (or "CO2 ---ppm" if no data)
void airSvcGetSummary(char* out, int outLen);
