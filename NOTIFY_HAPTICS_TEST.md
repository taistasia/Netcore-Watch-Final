# NETCORE Phase 2 — Notifications + Haptics — Wokwi Test Checklist

---

## Setup

The **HAPTIC LED** (yellow, labelled "HAPTIC") on pin 47 in `diagram.json`
represents the vibration motor. When a haptic fires, the LED blinks ON then OFF.
In a real build, replace with a vibration motor transistor driver on the same pin.

---

## Test 1 — Boot Banner (SD card)

1. Start simulation
2. SD card is present in diagram → `sdInit()` runs in `setup()`
3. **Expected:** Green banner appears in footer: `SD: Card mounted` (2 s)
4. Banner auto-hides; footer returns to normal
5. **Violation:** Full screen redraw while banner shows → fail

---

## Test 2 — Banner overlay doesn't touch rest of UI

1. Boot → main menu is visible
2. Wait for SD banner to appear
3. **Expected:** Only the 24 px footer strip changes colour; menu rows untouched
4. After 2 s: footer reverts to hint text (`ROTATE move  CLICK open  BACK idle`)
5. **No full-screen redraw at dismiss time**

---

## Test 3 — WiFi connection banner (green / HAPTIC_SUCCESS)

> Requires a WiFi network in `diagram.json`.
> Add: `{"type":"wokwi-wifi","id":"w1","attrs":{"ssid":"TestNet","password":""}}`

1. Enable auto-connect (WiFi app → `AUTO-CONNECT: ON`) with `TestNet` saved
2. Reboot
3. **Expected sequence:**
   - Status bar WiFi indicator: `----` → `...` → `WiFi`
   - Green banner: `WiFi: Connected` (2.5 s)
   - HAPTIC LED flashes ON for 100 ms, then OFF → `HAPTIC_SUCCESS`

---

## Test 4 — WiFi failure banner (red / HAPTIC_ERROR)

1. WiFi app → SCAN → select a **non-existent** network → ENTER PASSWORD → any password
2. HOLD-SELECT to connect
3. Wait ~15 s
4. **Expected:**
   - Connecting screen shows `CONNECTION FAILED` + reason
   - Red/orange banner appears: `WiFi: Auth failed` or `WiFi: Connect timeout`
   - HAPTIC LED flashes 200 ms for `HAPTIC_ERROR`

---

## Test 5 — WiFi lost banner (yellow double-pulse / HAPTIC_WARN)

> Hard to trigger in Wokwi. Manually call `wifiSvcDisconnect()` from Serial,
> or remove the Wokwi WiFi part temporarily.

- **Expected banner:** `WiFi: Connection lost` (3 s, yellow/orange)
- **Expected haptic:** HAPTIC LED: ON 50 ms → OFF 40 ms → ON 50 ms (double tap)

---

## Test 6 — NTP sync banner (green / HAPTIC_SUCCESS)

1. WiFi connects (Test 3)
2. After a few seconds the NTP FSM should sync
3. **Expected:**
   - Clock in status bar updates to real time (or simulated NTP time)
   - Green banner: `NTP: Time synced` (2 s)
   - HAPTIC LED: ON 100 ms → OFF (`HAPTIC_SUCCESS`)
4. NTP banner should only fire **once per boot** (FSM stays in `NTP_SYNCED`)

---

## Test 7 — Ring buffer (last 10 notifications)

> Trigger 10+ notifications by toggling WiFi on/off repeatedly.

In `SYS TOOLS → DIAGNOSTICS` there is no ring buffer viewer yet,
but you can verify via Serial. Add temporary debug print if needed:

```cpp
for (int i = 0; i < notifySvcCount(); i++) {
  const NotifyEntry* e = notifySvcGet(i);
  Serial.printf("[%d] %s: %s\n", i, e->title, e->body);
}
```

**Expected:**
- Count never exceeds 10
- Newest is index 0, oldest is index N-1
- Old entries are silently evicted (ring wraps)

---

## Test 8 — Banner doesn't block input

1. WiFi connecting → connecting screen with dots animation
2. A banner appears (any, e.g. SD mounted)
3. **Expected:** Dot animation continues, input still works (BACK cancels connect)
4. **Violation:** Any visible freeze > 50 ms while banner draws

---

## Test 9 — Banner dirty-only redraw rule

Open `SYS TOOLS → DIAGNOSTICS` and watch `LOOP:` fps:

| Condition | Expected fps |
|-----------|-------------|
| No banner, idle menu | 25–35 fps |
| Banner appearing | Same; should not drop below 20 fps |
| Banner active (no change) | Same; no redraw if banner unchanged |
| Banner dismissing | Single clear, then back to steady fps |

`_drawBanner()` is only called from `notifySvcPost()` (on new post) and `notifySvcTick()` (on dismiss via `_clearBanner()`). It is never called on every loop.

---

## Test 10 — Haptics no-op when pin disabled

1. In `netcore_config.h`, temporarily change `#define PIN_HAPTIC 47` to `#define PIN_HAPTIC -1`
2. Rebuild and run
3. **Expected:** Everything functions normally; no crash; HAPTIC LED absent
4. `hapticsInit(-1)` sets `_pin = -1`; all subsequent `hapticsBuzz`/`hapticsPattern`/`hapticsTick` calls return immediately

---

## Test 11 — hapticsBuzz arbitrary duration

From Serial or a test shim, call `hapticsBuzz(500)`.
- **Expected:** HAPTIC LED on for 500 ms, then off
- No blocking; loop continues normally during the 500 ms

---

## Test 12 — Back-compat shims

Ensure old call sites still compile. Search the codebase for these:

| Old call | Resolves to |
|----------|-------------|
| `notifyPost("msg")` | `notifySvcPostSimple("msg")` |
| `notifTick()` | `notifySvcTick()` |
| `notifIsActive()` | `notifySvcIsActive()` |
| `notifGetCount()` | `notifySvcCount()` |
| `notifGetRecent(i)` | `notifySvcGet(i)->body` |

All are inline in `svc_notify.h` — no linker duplicates.

---

## Performance Budget

| Operation | Max time |
|-----------|----------|
| `notifySvcTick()` when idle | < 1 µs |
| `notifySvcTick()` on dismiss | < 5 ms (one fillRect + line) |
| `notifySvcPost()` | < 10 ms (ring write + one fillRect + text) |
| `hapticsTick()` when idle | < 1 µs |
| `hapticsTick()` on phase change | < 5 µs (one digitalWrite) |

---

## Files Changed Summary

| File | Change type | Description |
|------|-------------|-------------|
| `svc_haptics.h` | **NEW** | Haptics service API — 4 patterns, pin-agnostic |
| `svc_haptics.cpp` | **NEW** | Non-blocking FSM: IDLE→ON→PAUSE→ON→IDLE |
| `svc_notify.h` | Modified | Added `NOTIFY_ERR` alias for spec compatibility |
| `netcore_config.h` | Modified | Added `#define PIN_HAPTIC 47` |
| `sketch.ino` | Modified | `#include svc_haptics.h`, `hapticsInit`, `hapticsTick` in loop |
| `svc_wifi.cpp` | Modified | 6 notifyPost() upgraded to typed notifySvcPost() + haptics |
| `netcore_time.cpp` | Modified | NTP sync fires `NOTIFY_OK` banner + `HAPTIC_SUCCESS` |
| `diagram.json` | Modified | Yellow LED on pin 47 for haptic visualisation |
