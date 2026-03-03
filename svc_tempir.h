// svc_tempir.h
#pragma once
#include <stdint.h>
#include "netcore_config.h" // NETCORE_SIM_IR_ENABLE

// ─────────────────────────────────────────────────────────────────────────────
// svc_tempir — IR temperature service (MLX90614) + simulated source for Wokwi
//
// Public API is stable:
//   tempIrInit(), tempIrTick()
//   tempIrSensorOk(), tempIrHasData()
//   tempIrObjectC(), tempIrAmbientC()
//   tempIrBurst* helpers for on-demand burst sampling
//
// NETCORE constraints:
//   - No blocking in tick()
//   - No heap churn in tick()
//   - Fixed buffers only
//
// Build switches:
//   TEMPIR_ENABLED    0 = compile as safe stubs (no-op, returns defaults)
//   TEMPIR_DEMO_MODE  1 = synthetic generator (Wokwi / no sensor)
//                 0 = real MLX90614 over I2C (future hardware)
// ─────────────────────────────────────────────────────────────────────────────

#define TEMPIR_ENABLED    1   // 0 = full no-op (safe stub)

// IR sensor has no Wokwi part. In simulation we use a deterministic, hardware-
// ready mock generator (integer stepping, no floats in tick).
#if NETCORE_SIM_IR_ENABLE
#  define TEMPIR_DEMO_MODE  1
#else
#  define TEMPIR_DEMO_MODE  0
#endif

// Polling cadence (ms)
#define TEMPIR_POLL_FAST_MS   200UL   // app open / sensors tab
#define TEMPIR_POLL_SLOW_MS  1000UL   // background

// Alert thresholds (°C)
#define TEMPIR_THRESH_WARN_X10   600    // °C×10 — triggers WARN notification
#define TEMPIR_THRESH_HYST_X10   500    // °C×10 — recovery hysteresis

// Burst sampling (for higher-stability reading on demand)
#define TEMPIR_BURST_SAMPLES  8
#define TEMPIR_BURST_SPACING_MS  40UL

void tempIrInit();
void tempIrTick();

bool  tempIrSensorOk();
bool  tempIrHasData();
float tempIrObjectC();   // degrees C
float tempIrAmbientC();  // degrees C

void tempIrFormat(char* out, int outLen);

// Burst mode (non-blocking)
void  tempIrRequestBurst();
bool  tempIrBurstActive();
float tempIrBurstResult();  // last completed burst result (C)