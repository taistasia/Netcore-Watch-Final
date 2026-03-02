# NETCORE Phase 4 — IR Temperature (MLX90614) — Wokwi Test Checklist

---

## Hardware Notes

**Wokwi has no MLX90614 part.**

The default build ships with `TEMPIR_REAL_SENSOR 0` in `svc_tempir.h`.
This runs a **synthetic sine-wave** object temperature inside `_demoAdvance()`:

```
OBJ:  22°C → 75°C → 22°C  (period ~120 s, wall-clock accurate)
AMB:  23.5°C ± 1.2°C  (slow drift)
```

The wave crosses the 60°C alert threshold at ~73 s, recovers below 55°C at ~108 s.
All alerts, burst sampling, and UI state changes are exercisable without hardware.

**For real hardware:**
1. Set `#define TEMPIR_REAL_SENSOR 1` in `svc_tempir.h`
2. Wire MLX90614: SDA→GPIO8, SCL→GPIO9, VCC→3.3V, GND→GND
3. Both SCD40 (AIR app) and MLX90614 (TEMP app) share the same I2C bus — addresses
   never conflict (SCD40=0x62, MLX90614=0x5A)
4. Wire calls `Wire.begin(8,9)` — ESP32 no-ops on second call if already started

---

## Test 1 — TEMP entry in menu

1. Boot → main menu
2. **Expected:** `TEMP  IR` appears as the 3rd entry (PING → AIR → TEMP → WIFI…)
3. Navigate to TEMP → SELECT
4. **Expected:** `IR TEMP / MLX` title bar, full screen draw on enter
5. No `fillScreen` after enter — all subsequent updates are partial

---

## Test 2 — Initial state / sensor found

1. Open TEMP app within first 5 s
2. **Expected (demo mode):**
   - Badge shows `  NORMAL  ` (green) almost immediately
   - Big OBJ temp shows something like `22.0 C`
   - AMB row shows `23.5C  (74F)` or similar
   - Footer: `BACK:menu  HOLD-SEL:burst`
   - Hint row: `HOLD-SEL: 5-sample burst`

---

## Test 3 — Display update rate

1. Watch OBJ temperature change over time
2. **Expected:** Field redraws at ~4 Hz (250 ms) only when value changes ≥ 0.1°C
3. If temperature is stable: no redraw (dirty detection prevents flicker)
4. `LOOP:` fps in DIAGNOSTICS should stay ≥ 25 fps while TEMP app is open
5. `TEMP` label and `AMBIENT:` label do NOT redraw — only value area erases/redraws

---

## Test 4 — Background vs foreground poll rate

1. Open TEMP app → note Serial output frequency (see step 2 below)
2. Close TEMP app → return to menu
3. **Expected behavior:**
   - App open: poll every 500 ms (`TEMPIR_FG_POLL_MS`)
   - App closed: poll every 5000 ms (`TEMPIR_BG_POLL_MS`)
4. To verify in Serial: `tempIr: DEMO MODE (synthetic temp wave)` on boot; no
   per-sample output by default (add a `Serial.printf` to `_doSample()` if needed)
5. `tempIrSetForeground(true)` is called on `app_temp_enter()`, reverted on exit AND
   on back-button press (both call `tempIrSetForeground(false)`)

---

## Test 5 — HOLD-SELECT burst sample

1. Open TEMP app
2. Hold SELECT for ≥ 500 ms (INPUT_HOLD_MS)
3. **Expected:**
   - Hint row changes to `BURST SAMPLE [1/5]`, `[2/5]` … `[5/5]`
   - Each step 200 ms apart (TEMPIR_BURST_INTERVAL_MS)
   - OBJ/AMB readings update visibly during burst
   - After 5th sample: hint row reverts to `HOLD-SEL: 5-sample burst`
   - Total burst duration: ~1 000 ms (5 × 200 ms)
4. No UI freeze during burst — loop continues normally
5. Triggering hold while burst active: `tempIrBurstRequest()` is a no-op (already running)

---

## Test 6 — WARN alert (OBJ ≥ 60°C)

Demo wave reaches 60°C at approximately:

> sin(phase) = (60-22)/53 × 2 - 1 = +0.434 → phase ≈ 0.45 rad
> Time = 0.45/(2π) × 120 s ≈ **8.6 s** after the phase crosses 0

In practice, first alert fires around **8–15 s** after boot (when sine
passes 60°C on the way up from 22°C). Exact timing depends on demo phase start.

**Expected:**
- Badge changes from `  NORMAL  ` to `  TEMP HIGH  ` (red)
- OBJ temp value turns red
- Notification banner: `TEMP HIGH: OBJ 60.Xc` (yellow, 4 s)
- HAPTIC LED: double pulse `HAPTIC_WARN` (50ms → 40ms gap → 50ms)

---

## Test 7 — Alert cooldown (60 s)

1. Note timestamp when first TEMP HIGH alert fires
2. Temperature stays above 60°C for >60 s
3. **Expected:** Second alert fires at t + ≥60 s, not before
4. **Must NOT fire** every 5 s while above threshold
5. If you open TEMP app during the cooldown window: badge still shows red
   (`TEMP HIGH`) — alert visual is immediate; notification is rate-limited

---

## Test 8 — Recovery (OBJ drops below 55°C)

Demo wave descends through 55°C at ~112 s after boot (returning from peak).

**Expected:**
- Badge changes from `  TEMP HIGH  ` to `  NORMAL  ` (green)
- OBJ temp value colour returns to normal
- Notification banner: `TEMP: Back to normal` (green, 3 s)
- HAPTIC LED: short 50 ms buzz `hapticsBuzz(50)`
- Cooldown timer resets: next crossing of 60°C will fire immediately

---

## Test 9 — Hysteresis dead-band (55–60°C)

1. Temperature descends through 60°C
2. **Expected:** Alert level stays HIGH until it drops below 55°C
3. No "back to normal" notification fires at 58°C or 57°C
4. Recovery fires only at <55°C

---

## Test 10 — SENSOR NOT FOUND (real hardware, no sensor)

> Only testable with `TEMPIR_REAL_SENSOR 1` and no MLX90614 connected.

1. Set `#define TEMPIR_REAL_SENSOR 1`
2. Boot without sensor
3. **Expected:**
   - Badge shows `SENSOR NOT FOUND` (grey)
   - No OBJ/AMB fields populated
   - No alerts fire
   - No crash, no I2C bus storm (NAK handled gracefully)
4. `tempIrSensorOk()` returns false
5. `tempIrTick()` returns early: `if (!_sensorOk) return;`

---

## Test 11 — Background alerting (app closed)

1. Close TEMP app → main menu
2. Let demo wave rise to >60°C
3. **Expected:**
   - Notification banner appears in footer of main menu
   - HAPTIC fires
   - Menu rows NOT redrawn (banner = footer dirty draw only)
4. Open TEMP app: badge correctly shows TEMP HIGH

---

## Test 12 — AIR app co-existence (shared I2C)

> Relevant when `TEMPIR_REAL_SENSOR 1` AND `AIR_DEMO_MODE 0` — both real sensors.

In demo mode (default), Wire is never started — no conflict possible.

In real-hardware mode:
1. Both `airSvcInit()` and `tempIrInit()` call `Wire.begin(8,9)`
2. **Expected:** Second `Wire.begin()` call is a no-op on ESP32 (confirmed in ESP32 Wire.cpp)
3. SCD40 reads and MLX90614 reads interleave without collision (no concurrent I2C)
4. Confirm in DIAGNOSTICS: both CO2 and OBJ temp update normally

---

## Screen Layout Reference

```
┌──────────────────────────────────────┐  ← y=0
│  STATUS BAR (20px)                   │
├──────────────────────────────────────┤  ← y=20
│  IR TEMP                       MLX  │
├──────────────────────────────────────┤  ← y=54 (BODY_Y)
│ ──────────────────────────────────── │  ← +36 divider
│  ┌─────────────────────────────────┐ │  ← +6 badge (28px)
│  │      NORMAL  /  TEMP HIGH       │ │
│  └─────────────────────────────────┘ │
│  OBJECT TEMP                         │  ← +44 label
│         32.1 C                       │  ← +46 (textSize 3, centred)
│  AMBIENT: 23.5C  (74F)               │  ← +96
│ ──────────────────────────────────── │  ← +114 divider
│  HOLD-SEL: 5-sample burst            │  ← +120 (or BURST [N/5])
│  WARN >60C  RECOVER <55C  60s        │  ← +140 threshold ref
├──────────────────────────────────────┤  ← y=216
│  BACK:menu  HOLD-SEL:burst           │  ← FOOTER
└──────────────────────────────────────┘  ← y=240
```

---

## Compile Mode Reference

| Setting | Effect |
|---------|--------|
| `TEMPIR_REAL_SENSOR 0` | Demo mode (default). Synthetic sine wave. No I2C. Sensor "found" immediately. |
| `TEMPIR_REAL_SENSOR 1` | Real MLX90614 on I2C (SDA=8, SCL=9). NAK on init → stub mode (no data). |

---

## Files Changed Summary

| File | Type | Description |
|------|------|-------------|
| `svc_tempir.h` | **NEW** | IR temp service header — API, thresholds, compile flags |
| `svc_tempir.cpp` | **NEW** | MLX90614 SMBus reads + demo wave + alert cooldown + burst FSM |
| `netcore_apps.cpp` | Modified | Added `#include svc_tempir.h + svc_input.h`, full TEMP app (enter/tick/exit), `"TEMP" "IR"` in app table |
| `sketch.ino` | Modified | `#include svc_tempir.h`, `tempIrInit()` in setup (#10), `tempIrTick()` in loop |
| `netcore_config.h` | Modified | Updated I2C bus comment to mention MLX90614 |
| `libraries.txt` | Modified | MLX90614 raw I2C note |
