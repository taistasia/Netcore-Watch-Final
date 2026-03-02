#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// svc_tempir.h  —  IR Temperature Sensor Service (MLX90614)
//
// Hardware  : Melexis MLX90614 on shared I2C bus (address 0x5A).
//             Read via raw Wire (SMBus-compatible) — no external library.
//             Wire must be initialised before tempIrInit() is called.
//             If svc_air is present and real-mode, Wire is already up.
//             If svc_air is in demo mode, tempIrInit() calls Wire.begin().
//
// Compile flags (set in this header) ─────────────────────────────────────────
//   TEMPIR_ENABLED   1 = module active (default)
//                    0 = everything no-ops; UI shows "SENSOR NOT FOUND"
//
//   TEMPIR_DEMO_MODE 1 = synthetic temp ramp; no hardware needed (default)
//                    0 = real MLX90614 over I2C
//
// API ─────────────────────────────────────────────────────────────────────────
//   tempIrInit()                 — call once in setup()
//   tempIrTick()                 — call every loop(); non-blocking
//   tempIrSetAppOpen(bool)       — controls poll rate (250 ms vs 2 s)
//
//   tempIrSensorOk()             — false if sensor missing or ENABLED=0
//   tempIrHasData()              — true after first valid read
//   tempIrObjectC()              — object temperature °C  (0.0 if no data)
//   tempIrAmbientC()             — ambient temperature °C (0.0 if no data)
//   tempIrGetSummary(buf, len)   — "OBJ 32.1C  AMB 24.0C"
//
//   tempIrRequestBurst()         — start 5-sample averaging burst
//   tempIrBurstActive()          — true while burst in progress
//   tempIrBurstResult()          — averaged result once burst done (0 if busy)
//
// Alert ───────────────────────────────────────────────────────────────────────
//   Object temp > TEMPIR_THRESH_WARN (60 °C) → NOTIFY_WARN "TEMP" once,
//   then cooldown TEMPIR_ALERT_COOLDOWN_S (120 s) before repeat.
//   Recovery below TEMPIR_THRESH_HYST (50 °C) → NOTIFY_OK + short buzz.
//
// Poll rates ──────────────────────────────────────────────────────────────────
//   App open  : 250 ms   (tempIrSetAppOpen(true))
//   Background: 2000 ms  (tempIrSetAppOpen(false), default)
// ─────────────────────────────────────────────────────────────────────────────
#include <stdint.h>

// ── Compile flags ─────────────────────────────────────────────────────────────
#define TEMPIR_ENABLED    1   // 0 = full no-op (safe stub)
#define TEMPIR_DEMO_MODE  1   // 1 = synthetic ramp; 0 = real MLX90614

// ── I2C (shares pins with svc_air) ───────────────────────────────────────────
#define TEMPIR_I2C_SDA    8
#define TEMPIR_I2C_SCL    9

// ── Timing ────────────────────────────────────────────────────────────────────
#define TEMPIR_POLL_FAST_MS    250UL    // poll when app is open
#define TEMPIR_POLL_SLOW_MS   2000UL   // poll in background (alert check only)

// ── Alert thresholds ─────────────────────────────────────────────────────────
#define TEMPIR_THRESH_WARN       60.0f  // °C — triggers WARN notification
#define TEMPIR_THRESH_HYST       50.0f  // °C — recovery hysteresis
#define TEMPIR_ALERT_COOLDOWN_S  120UL  // minimum seconds between repeat alerts

// ── Burst ─────────────────────────────────────────────────────────────────────
#define TEMPIR_BURST_SAMPLES     5      // number of samples to average
#define TEMPIR_BURST_INTERVAL_MS 250UL  // ms between burst samples (non-blocking)

// ── Lifecycle ─────────────────────────────────────────────────────────────────
void tempIrInit();
void tempIrTick();
void tempIrSetAppOpen(bool open);  // call from app_enter / app_exit

// ── Queries ───────────────────────────────────────────────────────────────────
bool  tempIrSensorOk();
bool  tempIrHasData();
float tempIrObjectC();
float tempIrAmbientC();
void  tempIrGetSummary(char* out, int outLen);

// ── Burst sampling ────────────────────────────────────────────────────────────
void  tempIrRequestBurst();          // kick off a 5-sample burst
bool  tempIrBurstActive();           // true while burst running
float tempIrBurstResult();           // averaged result; 0.0f if not ready
