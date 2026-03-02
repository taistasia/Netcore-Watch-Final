# NETCORE Phase 3 — Air Quality (SCD40) — Wokwi Test Checklist

---

## Hardware Notes

**Wokwi doesn't have an SCD40 part.** That's fine — `svc_air.cpp` ships with
`AIR_DEMO_MODE 1` (the default), which runs a synthetic CO₂ wave internally:

```
400 ppm → 2600 ppm → 400 ppm  (rises 25 ppm every 5 s)
Temp:  21.5 °C  (fixed)
Humid: 45.6 %   (fixed)
```

This wave crosses both thresholds (WARN 1200 → CRIT 2000) and recovers,
giving full alert coverage without hardware.

**For real hardware:** set `AIR_DEMO_MODE 0` in `svc_air.h`, wire SCD40 to
pins 8 (SDA) and 9 (SCL). No external library required — the service drives
the SCD40 directly over Wire using raw I2C commands.

---

## Test 1 — App appears in menu

1. Boot → main menu
2. **Expected:** `AIR  CO2` is the second entry (after PING)
3. Navigate to it → SELECT
4. **Expected:** "AIR QUAL" screen with full-screen draw on enter
5. **No fillScreen after this point**

---

## Test 2 — Initial warmup state

1. Open AIR app within first 5 s of boot
2. **Expected:** Grey badge shows `WARMING UP...`
3. Fields below are empty (no data yet)
4. After ~5 s: badge changes to `  NORMAL  ` (green/accent)
5. Fields populate with CO2, TEMP, HUMID, SUMM

> In demo mode warmup is skipped; first reading appears almost immediately.

---

## Test 3 — Field update rate

1. Open AIR app and watch CO2 field
2. CO2 demo value changes every 5 s
3. **Expected:** field updates within 1 s of the value change (rate-limited to 1 Hz check)
4. **Verify:** No full-screen redraw between updates — only the value area flickers
5. Check `LOOP:` fps in DIAGNOSTICS (open in separate session) stays ≥ 25 fps

---

## Test 4 — WARN threshold crossing (CO2 ≥ 1200 ppm)

Demo wave hits 1200 ppm at ~(1200-400)/25 = **32 steps × 5 s ≈ 2.7 min** after boot.

1. Let simulation run until CO2 reaches ~1200 ppm
2. **Expected:**
   - Badge changes from `  NORMAL  ` to `  WARNING  ` (yellow)
   - CO2 field value turns yellow
   - Notification banner: `AIR WARN: CO2 XXXX ppm` (yellow, 3.5 s)
   - HAPTIC LED: double pulse (50ms ON → 40ms OFF → 50ms ON) `HAPTIC_WARN`
3. Badge only redraws once per level transition — not on every CO2 change

---

## Test 5 — CRIT threshold crossing (CO2 ≥ 2000 ppm)

Demo wave hits 2000 ppm at ~(2000-400)/25 = **64 steps × 5 s ≈ 5.3 min**

1. **Expected:**
   - Badge changes to `  CRITICAL  ` (red)
   - CO2 field turns red
   - Notification banner: `AIR CRIT: CO2 2000 ppm` (red, 4 s)
   - HAPTIC LED: long 200 ms solid buzz `HAPTIC_ERROR`
2. No WARN notification fires when entering CRIT (already above WARN; only CRIT fires)

---

## Test 6 — Cooldown anti-spam (120 s)

**Critical test — validates non-spammy behavior.**

1. Stay at WARN level (CO2 between 1200–2000)
2. Note the timestamp of first WARN notification
3. Wait 2 minutes (120 s)
4. **Expected:** A second WARN notification fires after 120 s cooldown IF CO2 is still ≥ WARN
5. **Must NOT fire** at t+30s, t+60s, t+90s
6. Repeat for CRIT: second CRIT notification must not fire until 120 s after the first

To verify in Serial:
```
[airSvc] WARN alert fired  → timestamp A
[airSvc] WARN alert fired  → timestamp A + ≥120s   ← correct
[airSvc] WARN alert fired  → timestamp A + 30s     ← BUG (should not happen)
```

---

## Test 7 — Recovery to NORMAL (CO2 drops below 900 ppm)

Demo wave descends back through 900 ppm (hysteresis threshold) at ~
(2600-900)/25 = **68 steps × 5 s ≈ 5.7 min after peak**.

1. **Expected:**
   - Badge changes back to `  NORMAL  ` (green)
   - Notification banner: `AIR: Back to normal` (green, 3 s)
   - HAPTIC LED: short 50 ms buzz `hapticsBuzz(50)`
2. **Must NOT fire if CO2 is between 900–1200 (hysteresis dead-band)**
   - Badge stays at WARN level; no spurious recovery notification
3. After recovery: cooldown timers reset (next WARN will fire immediately)

---

## Test 8 — Hysteresis dead-band (900–1200 ppm)

This tests that the alert level is sticky:

1. When CO2 is rising and crosses 1200 → WARN fires ✓
2. CO2 rises further, then drops back to 1100 (between 900–1200)
3. **Expected:** Alert level stays at WARN (not yet recovered)
4. No "back to normal" notification
5. Recovery only fires when CO2 drops below 900

---

## Test 9 — SENSOR NOT FOUND (real hardware, no sensor)

> Test only when `AIR_DEMO_MODE 0` and no SCD40 connected.

1. Set `#define AIR_DEMO_MODE 0` in `svc_air.h`
2. Boot without SCD40 wired
3. **Expected:**
   - Badge shows `SENSOR NOT FOUND` (grey)
   - No fields populated
   - No alerts fire
   - No crash, no I2C bus storm
4. `airSvcSensorOk()` returns false
5. `airSvcTick()` returns immediately after `_state == AIR_STATE_MISSING`

---

## Test 10 — No UI stall during sensor tick

1. Open DIAGNOSTICS (`SYS TOOLS → DIAGNOSTICS`)
2. Note `LOOP:` fps (baseline ~25–30 fps)
3. In a different run, open AIR app and monitor fps

| Condition | Expected fps |
|-----------|-------------|
| No air app open | 25–35 fps |
| AIR app open, between polls | 25–35 fps |
| AIR app open, at 5 s poll boundary (demo) | ≥ 20 fps (demo = pure memory op) |
| AIR app open, at 5 s poll boundary (real HW) | ≥ 15 fps (2ms I2C, rare) |

Demo mode: `airSvcTick()` at poll time = 3 integer ops + `_checkAlerts()` ≈ 5 µs.

---

## Test 11 — Service runs in background (app not open)

1. Boot, do NOT open AIR app
2. Let demo CO2 wave rise to WARN level
3. **Expected:**
   - Notification banner appears in footer while on the main menu
   - `HAPTIC_WARN` fires
   - Main menu is NOT redrawn (banner uses dirty footer draw, not full screen)
4. Open AIR app: badge correctly shows WARN state
5. Close AIR app: alerting continues silently in background

---

## Test 12 — BACK navigation

1. AIR app open
2. Press BACK
3. **Expected:** Returns to main menu (`renderMenuFull()` called once)
4. AIR service keeps running (next alert will still fire from menu)

---

## Screen Layout Reference

```
┌──────────────────────────────────────┐  ← y=0
│  STATUS BAR (20px)                   │
├──────────────────────────────────────┤  ← y=20
│  TITLE: AIR QUAL          CO2       │
├──────────────────────────────────────┤  ← y=54 (BODY_Y)
│  ┌──────────────────────────────┐    │  ← +6 = BADGE
│  │     [  NORMAL  / WARN / CRIT ]    │  ← 28px badge
│  └──────────────────────────────┘    │
│ ───────────────────────────────────  │  ← +36 divider
│  CO2:   650 ppm                      │  ← +46  field 0
│  TEMP:  21.5 C  /  70 F             │  ← +64  field 1
│  HUMID: 45.6 %                       │  ← +82  field 2
│  SUMM:  CO2 650ppm  45%  70F         │  ← +100 field 3
│ ───────────────────────────────────  │  ← +118 divider
│  WARN >1200ppm  CRIT >2000ppm  120s  │  ← +122 threshold ref
├──────────────────────────────────────┤  ← y=216 (H-FOOTER_H)
│  BACK: menu                          │  ← FOOTER (24px)
└──────────────────────────────────────┘  ← y=240
```

---

## Files Changed Summary

| File | Type | Description |
|------|------|-------------|
| `svc_air.h` | **NEW** | Air quality service API + thresholds + demo mode flag |
| `svc_air.cpp` | **NEW** | SCD40 I2C FSM, demo wave, alert cooldown, recovery |
| `netcore_apps.cpp` | Modified | `#include svc_air.h + svc_haptics.h`, full AIR app (enter/tick/exit), added to app table |
| `netcore_config.h` | Modified | `#define PIN_I2C_SDA 8` / `PIN_I2C_SCL 9` |
| `sketch.ino` | Modified | `#include svc_air.h`, `airSvcInit()` in setup, `airSvcTick()` in loop |
| `libraries.txt` | Modified | Wire dependency note (built-in, no install needed) |

---

## delayMicroseconds Note

`svc_air.cpp` uses `delayMicroseconds(1000)` twice in the real-sensor I2C path.
This is:
- **Only compiled when `AIR_DEMO_MODE 0`** — default build uses demo mode
- Required by SCD40 spec: 1 ms after sending `get_data_ready_status` / `read_measurement` before reading
- Called at most once per 5 s poll cycle = 2 ms every 5 000 ms = 0.04% blocking
- The stall budget is 250 ms (`RENDER_STALL_MS`); 2 ms is imperceptible

For a fully non-blocking production implementation, split the 5 s poll into a
2-step sub-state: `SEND_CMD` → wait 1 ms → `READ_RESULT`. This adds ~20 lines
but is only needed if 2 ms stalls are unacceptable (they won't be visible).
