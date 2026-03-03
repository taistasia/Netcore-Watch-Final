// svc_air.h
#pragma once
#include <stdint.h>

// ── Simulation / Wokwi mode ──────────────────────────────────────────────────
// Default: use MQ2 analog as a CO₂-proxy feed (NETCORE_SIM_AIR_MQ2_ENABLE).
// If NETCORE_SIM_AIR_MQ2_ENABLE is 0, the service can be built for SCD40.
#include "netcore_config.h"  // NETCORE_SIM_AIR_MQ2_ENABLE / PIN_MQ2_ADC

#define AIR_DEMO_MODE  0   // legacy synthetic wave (kept for bring-up only)

// ── I2C wiring (if real sensor used) ──────────────────────────────────────────
#define AIR_I2C_SDA    8
#define AIR_I2C_SCL    9

// ── MQ2 analog pin (Wokwi) ───────────────────────────────────────────────────
#define AIR_MQ2_ADC_PIN  PIN_MQ2_ADC

// ── Thresholds (CO₂ ppm) ─────────────────────────────────────────────────────
#define AIR_SAFE_MAX   1000   // <= safe
#define AIR_WARN_MAX   2000   // > safe and <= warn
// > AIR_WARN_MAX is CRIT

// ── Polling ──────────────────────────────────────────────────────────────────
#define AIR_POLL_MS          5000UL    // SCD40 poll interval (ms)
#define AIR_WARMUP_MS        5500UL    // SCD40 warmup (ms)

// MQ2 sim poll interval — fast enough for UI without spamming ADC
#define AIR_MQ2_POLL_MS       250UL

// ── Public API ───────────────────────────────────────────────────────────────
void airSvcInit();
void airSvcTick();

bool     airSvcSensorOk();
bool     airSvcHasData();
uint16_t airSvcCO2();         // ppm
int16_t  airSvcTempC_x10();   // °C * 10 (optional)
uint16_t airSvcHum_x10();     // %RH * 10 (optional)

bool airSvcIsWarn();
bool airSvcIsCrit();