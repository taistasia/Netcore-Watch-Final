# NETCORE Phase 4 — IR Temperature Sensor (MLX90614) — Wokwi Test Checklist

---

## Hardware and Compile Notes

**Wokwi has no MLX90614 part.** The module ships with two compile flags in
`svc_tempir.h` that handle this safely:

| Flag | Default | Effect |
|------|---------|--------|
| `TEMPIR_ENABLED` | `1` | Module active; all functions implemented |
| `TEMPIR_DEMO_MODE` | `1` | Synthetic ramp; no I2C hardware needed |

**Demo profile:**
```
Object temp : 22 °C → 75 °C → 22 °C  (rises 1 °C every 250 ms when app open)
Ambient     : fixed 24.0 °C
```
Crosses WARN threshold (60 °C) at ~(60−22) × 250 ms ≈ **9.5 s** after first reading.

**Production path:**
1. Set `TEMPIR_DEMO_MODE 0` in `svc_tempir.h`
2. Wire MLX90614: SDA→GPIO 8, SCL→GPIO 9, 3.3 V, GND
3. No library to install — service drives raw Wire (SMBus read, 3 bytes/reg)

**Fully disabled path** (if sensor not fitted, forever):
- Set `TEMPIR_ENABLED 0`
- Every function becomes a no-op; UI shows "DISABLED"
- Compile time: zero code in hot path

---

## Test 1 — App appears in menu

1. Boot → main menu
2. **Expected:** `TEMP  IR` is the third entry (after PING, AIR)
3. Navigate → SELECT
4. **Expected:** Full screen renders once (`app_tempir_enter`)
5. Title bar: `IR TEMP` / `MLX90614`
6. No fillScreen after this point — only dirty-region updates

---

## Test 2 — Initial state / warming up

1. Open TEMP app immediately after boot (demo mode — data available fast)
2. **Expected:**
   - Badge shows `  NORMAL  ` (green/accent) almost immediately
   - Big object temp rendered at textSize=4, centered
   - Ambient row below: `AMB:  24.0C  /  75F`
   - Burst row: `HOLD-SEL: 5-sample burst avg`
   - Threshold hint: `WARN >60C   COOLDOWN 120s`
3. No field flicker — only value area erased and redrawn

Without sensor and `TEMPIR_DEMO_MODE 0`:
- Badge shows `SENSOR NOT FOUND` (grey)
- All value areas show `---`

---

## Test 3 — Poll rate changes on enter/exit

1. Open DIAGNOSTICS → note `LOOP:` fps
2. Navigate to TEMP app → note fps (should be same or higher, not lower)
3. `tempIrSetAppOpen(true)` called → poll rate → 250 ms (fast)
4. Return to menu → `tempIrSetAppOpen(false)` → poll rate → 2000 ms (slow)
5. **No visible stall at poll boundary** — demo tick is ~5 µs

---

## Test 4 — Dirty redraw (most important)

Watch the screen carefully while temp changes:

| Event | What redraws |
|-------|-------------|
| Object temp changes 0.1 °C | Only big number area (34 px strip) |
| Alert level changes NORMAL→WARN | Only badge area (24 px) |
| Ambient changes | Only AMB row (14 px) |
| Burst state changes | Only burst row (12 px) |
| BACK pressed | Full screen redraw (menu) |
| App first opened | Full screen (enter only) |

**Violation:** Any full-screen flash during normal operation → bug.

---

## Test 5 — WARN threshold crossing (OBJ ≥ 60 °C)

Demo ramp hits 60 °C at ~9.5 s after app open.

1. **Expected:**
   - Badge: `  NORMAL  ` → `  WARNING  ` (yellow)
   - Big temp number turns red
   - Notification banner: `TEMP WARN: OBJ XX.XC` (yellow, 4 s)
   - HAPTIC LED: double pulse `HAPTIC_WARN`
2. Badge only redraws once at threshold cross — not on every tick

---

## Test 6 — Alert cooldown (120 s anti-spam)

1. Stay at WARN level (OBJ ≥ 60 °C)
2. Note timestamp of first WARN notification
3. **Must NOT fire** at t+30s, t+60s, t+90s
4. **Expected:** Second notification at t+120s if still above threshold
5. Demo ramp will eventually descend back through 50 °C (recovery)

---

## Test 7 — Recovery notification (OBJ drops below 50 °C hysteresis)

Demo ramp descends: 75 °C → 60 °C → 50 °C.
Recovery fires only when OBJ drops **below 50 °C** (not at 60 °C).

1. **Expected (crossing 60 °C, descending):** No notification, badge still WARN
2. **Expected (crossing 50 °C):**
   - Badge: `  WARNING  ` → `  NORMAL  ` (green)
   - Notification banner: `TEMP: Object temp normal` (green, 3 s)
   - HAPTIC: short 50 ms buzz
3. Cooldown timers reset — next WARN will fire immediately

---

## Test 8 — Burst sampling (HOLD-SELECT)

1. TEMP app open, sensor has data
2. **Hold SELECT button for 600 ms**
3. **Expected:**
   - Badge flashes to `  SAMPLING  ` (accent colour) immediately
   - Burst row shows: `SAMPLING...  HOLD-SEL to burst`
   - After ~1.25 s (5 × 250 ms): burst completes
   - HAPTIC: `HAPTIC_SUCCESS` (100 ms)
   - Badge reverts to NORMAL/WARN state
   - Burst row updates: `BURST: XX.XC avg  HOLD-SEL to resample`
4. **No blocking** — loop continues at full speed during burst window
5. Short SELECT press (< 600 ms) does nothing — no accidental trigger

---

## Test 9 — Alert fires during burst

1. Trigger burst while OBJ < 60 °C
2. Burst completes → averaged result ≥ 60 °C
3. **Expected:** WARN notification fires on burst completion (burst updates `_objC`)

---

## Test 10 — BACK navigation / poll rate restore

1. TEMP app open (poll = 250 ms)
2. Press BACK
3. **Expected:**
   - `tempIrSetAppOpen(false)` called immediately → poll drops to 2000 ms
   - Main menu renders once
   - Alerts still fire in background if OBJ stays hot (banner in menu footer)

---

## Test 11 — Service runs in background while on menu

1. Let TEMP demo ramp to WARN while on main menu (not in TEMP app)
2. **Expected:**
   - WARN notification banner appears in footer of menu screen
   - Main menu rows are NOT redrawn (banner uses dirty footer, not full screen)
   - HAPTIC fires
3. Open TEMP app: badge correctly shows WARN state, correct OBJ reading

---

## Test 12 — TEMPIR_ENABLED 0 (full disable)

1. Set `#define TEMPIR_ENABLED 0` in `svc_tempir.h`
2. Rebuild
3. **Expected:** Compiles cleanly (no warnings about undefined functions)
4. TEMP app shows `DISABLED` in summary
5. All tempIr* calls return immediately / return safe defaults
6. No I2C traffic, no alerts, no CPU overhead

---

## Screen Layout Reference

```
┌──────────────────────────────────────┐  y=0
│ STATUS BAR (20px)                    │
├──────────────────────────────────────┤  y=20
│ TITLE: IR TEMP          MLX90614    │
├──────────────────────────────────────┤  y=54 (BODY_Y)
│ ─── divider (below badge) ───────── │  y=88
│     OBJECT TEMP:                     │  y=94
│                                      │
│         ╔══════════════╗             │
│         ║   72.3C      ║  75F       │  y=102 (textSize=4, 32px)
│         ╚══════════════╝             │
│ ─── divider ────────────────────────│  y=154
│ AMB:   24.0C  /  75F               │  y=162
│ ─── divider ────────────────────────│  y=178
│ BURST: 71.8C avg  HOLD-SEL resample │  y=184
│ ─── divider ────────────────────────│  y=198
│ WARN >60C   COOLDOWN 120s           │  y=202
├──────────────────────────────────────┤  y=216
│ BACK: menu   HOLD-SEL: burst sample │  y=216 FOOTER
└──────────────────────────────────────┘  y=240
```

---

## Files Changed Summary

| File | Type | Description |
|------|------|-------------|
| `svc_tempir.h` | **NEW** | Service API, compile flags, thresholds, burst, poll rates |
| `svc_tempir.cpp` | **NEW** | MLX90614 raw I2C FSM, demo ramp, burst state machine, alerts; full no-op stubs when `TEMPIR_ENABLED=0` |
| `netcore_apps.cpp` | Modified | `#include svc_tempir.h`, full TEMP app (enter/tick/exit + helpers), added `"TEMP" "IR"` to app table |
| `sketch.ino` | Modified | `#include svc_tempir.h`, `tempIrInit()` in setup (#10), `tempIrTick()` in loop |
| `libraries.txt` | Modified | MLX90614 note (raw Wire — no install needed) |

---

## Why Raw Wire Instead of a Library

The Adafruit MLX90614 library is solid but brings ~2 KB of flash and a transitive
dependency on Adafruit BusIO. The raw Wire approach here needs 2 functions (`_readReg`
+ `_doRead`) totalling ~30 lines. For a single-register sensor this is the right call:

- Fewer dependencies = fewer surprise version conflicts
- CRC (PEC) not validated — acceptable for a thermometer
- If you later want calibration, emissivity, or EEPROM writes, drop in the
  Adafruit library by changing `_readReg` to `mlx.readObjectTempC()` and
  `mlx.readAmbientTempC()` — the rest of the service stays identical
