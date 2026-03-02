# NETCORE — Wokwi Sanity Checklist (Phase 0 Baseline)

Use this after every patch or feature addition to verify nothing regressed.

---

## Quick Boot Check

| Step | Expected | Red flag |
|------|----------|----------|
| Power on | Boot animation, then main menu | Crash / reset loop in Serial Monitor |
| Status bar | `NETCORE  HH:MM  ----  --  ---  DRY` | Blank or corrupted strip |
| Idle 20 s | Watchface appears | Hang, white screen |
| Any button | Returns to menu | No response |

---

## 1  Menu Navigation — no fillScreen on keypress

Open the menu and rotate the encoder rapidly up and down.

**Expected:** Row highlight swaps in-place (two `drawMenuRowBase` calls).  
**Violation:** Visible full-screen flash / wipe on every rotation step = `fillScreen` leaking into navigation.

**How menu nav works (correct):**
```
if (menuScroll changed) → renderMenuFull()   // ← full redraw allowed
else                    → drawMenuRowBase(old, false) + drawMenuRowBase(new, true)
```

---

## 2  Status Bar — dirty-only redraw

The status bar (`svc_statusbar.cpp`) must **not** redraw every loop().

**How to verify:**
1. Open `SYS TOOLS → DIAGNOSTICS`
2. Watch `LOOP:` — should read **~25–35 fps, stall <20 ms** with no user activity
3. If fps is ~5–10 and stall is high: status bar is redrawing unconditionally

**Trigger a legitimate redraw:** Open `WIFI → SCAN`, let it finish → status bar
`WiFi` indicator may update.  That single redraw is correct.

**What must NOT trigger a redraw:** encoder rotation, app navigation, idle loop,
watchface tick (it owns its own 20-px strip separately).

**Rule enforced in code:**
```cpp
// svc_statusbar.cpp
void statusBarTick() {
    if (_dirty || _stateChanged()) _draw();   // only then
}
```

---

## 3  WiFi App — scan is async, no stutter

1. `WIFI` → `SCAN NETWORKS`
2. `SCANNING...` dots animate (non-blocking poll of `WiFi.scanComplete()`)
3. List appears; fps should not drop to 0 during scan

**Violation:** Screen freezes for 2–4 s = `WiFi.scanNetworks()` (blocking) called instead of `WiFi.scanNetworks(true)`.

**Correct path in code:** `wifiSvcStartScan()` → `WiFi.scanNetworks(/*async=*/true)` in `svc_wifi.cpp`.

---

## 4  Password Entry — partial redraws only

Select a secured network → CONNECT MENU → ENTER PASSWORD.

| Action | What should redraw |
|--------|--------------------|
| Rotate encoder | Password field + big-char picker only (`_renderPwdField()`) |
| Click (append char) | Password field + big-char picker only |
| Scroll to `[ SHOW ]` + click | Password field only |
| HOLD-SELECT | Transition to CONNECTING screen |

**Violation:** Any visible full-screen wipe/flicker on every character rotation.

---

## 5  Diagnostics screen

`SYS TOOLS → DIAGNOSTICS` — verify live perf values (now sourced from `svc_perf`):

| Field | Expected in Wokwi (no real hardware) |
|-------|--------------------------------------|
| `HEAP:` | ~200–350 KB free, stable |
| `LOOP:` | `25.0 fps  stall 2ms` (approx) |
| `UPTIME:` | Counting up |
| `WiFi:` | `IDLE` |
| `NTP:` | `IDLE` |
| `SD:` | `NONE` |
| `HID:` | `DRY RUN` |
| `BLE:` | `--- (future)` |

`LOOP:` reads from `perfGetFps()` / `perfGetMaxStallMs()` (svc_perf) — not a dead local variable.

---

## 6  Watchface — dirty-region updates only

Enter watchface (idle 20 s or press BACK from menu).

| What changes | What must redraw |
|--------------|------------------|
| Seconds tick (if seconds ON) | Seconds digits + colon only |
| Minute changes | HH:MM block only |
| NTP syncs | Status strip + date + sync hint |
| WiFi connects | Status pill region only |
| Bracket pulse (every 1.5 s) | Bracket lines only |

**Violation:** Full-screen wipe every second.

Frame cap is enforced:
- Seconds ON: `RENDER_FRAME_MS_SECS` (200 ms = 5 fps max)
- Seconds OFF: `RENDER_FRAME_MS_MIN` (1000 ms = 1 fps max)

---

## 7  What must NOT stutter under any circumstances

| Scenario | Max acceptable loop stall |
|----------|--------------------------|
| Encoder rotation in menu | < 30 ms |
| Encoder rotation in password entry | < 30 ms |
| Status bar minute update | < 20 ms |
| WiFi scan running in background | < 5 ms per loop |
| NTP sync happening | 0 ms (fully async) |
| SD card present, not scanning | 0 ms |

Stall > 100 ms turns the `LOOP:` value red in Diagnostics.

---

## 8  Known Accepted Blocking Exceptions

These are intentional and are **not bugs**:

| Location | Exception | Justification |
|----------|-----------|---------------|
| `runPingSequence()` | `delay(400)` between pings | User-triggered tool; blocks until done by design |
| `runPortCheck()` | `delay(100)` between probes | User-triggered tool; same rationale |
| `runCartIntro()` | Many `delay()` calls | Called only from `app_cart_enter()`, never from tick |
| `netcore_boot.cpp` | All `delay()` calls | Boot animation; `loop()` not yet running |
| `netcore_ducky.cpp` | `delay()` in key events | USB HID hardware requirement |
| `svc_wifi.cpp:wifiSvcConnect()` | `delay(50)` | One-shot radio reset; occurs on user action, not in poll loop |

---

## 9  Files Changed — Phase 0 Patch

| File | Change summary |
|------|---------------|
| `svc_wifi.cpp` | **Removed `Arduino String`** from `_collectScanResults()` and `WSVC_CONNECTING`. Used `.toCharArray()` direct writes. |
| `sketch.ino` | Added `#include svc_perf.h` + `svc_events.h`. Wired `perfInit()`, `eventBusInit()` in setup(). Added `eventBusTick()` as first service tick. Replaced `diagUpdateStall()` with `perfLoopEnd()`. |
| `netcore_apps.cpp` | Added `#include svc_perf.h`. `diagUpdateStall()` is now a no-op shim. `renderDiagnostics()` reads from `perfGetFps()` / `perfGetMaxStallMs()`. |
| `netcore_apps.h` | Updated `diagUpdateStall` comment to reflect shim status. |
| `SANITY.md` | **New.** This file. |

---

## 10  Audit Findings — Nothing to Fix (Confirmed Clean)

| Area | Finding | Status |
|------|---------|--------|
| `notifyPost` / `notifySvcPost` | One definition in `svc_notify.cpp`; shim in header | ✅ Clean |
| `drawStatusBarFrame` / `drawStatusFieldsForce` | Inline shims in `svc_statusbar.h` → real functions in `.cpp` | ✅ Clean |
| `timeSvcInit` / `timeSvcTick` | One definition each in `netcore_time.cpp` | ✅ Clean |
| `wifiSvcInit` / `wifiSvcTick` | One definition each in `svc_wifi.cpp` | ✅ Clean |
| `statusBarInit` / `statusBarTick` | One definition each in `svc_statusbar.cpp` | ✅ Clean |
| `fillScreen` in tick paths | None found — all calls in `enter()` or `watchfaceEnter()` | ✅ Clean |
| `WiFi.scanNetworks` | Async-only: `WiFi.scanNetworks(true)` in `wifiSvcStartScan()` | ✅ Clean |
| `getLocalTime` timeout | All calls use `getLocalTime(&t, 0)` | ✅ Clean |
| SD directory walk in tick | `sdExScan()` is SELECT-triggered, not auto-tick | ✅ Clean |
| `delay()` in auto-tick paths | None — all delays are in user-triggered one-shot functions | ✅ Clean |
