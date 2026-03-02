# NETCORE WiFi UX — Wokwi Test Checklist (Phase 1)

> Run these after loading the patched project in Wokwi.
> Wokwi's WiFi simulation supports WPA2 networks — you can add them in `diagram.json`.

---

## Setup: Add test networks to diagram.json

Add to `parts`:
```json
{ "type": "wokwi-wifi", "id": "wifi1", "attrs": { "ssid": "TestOpenNet", "password": "" } },
{ "type": "wokwi-wifi", "id": "wifi2", "attrs": { "ssid": "TestSecureNet", "password": "hunter2" } }
```

---

## Test 1 — Status Screen

1. Boot → main menu appears
2. Rotate to **WIFI** → SELECT
3. **Expected:** WiFi status screen shows `STATE: NOT CONNECTED` (or `CONNECTED` if auto-connect fired)
4. Three action rows visible: `SCAN NETWORKS`, `FORGET NETWORK`, `AUTO-CONNECT: ON/OFF`
5. Rotate → row highlight swaps (no full-screen flash)

---

## Test 2 — Scan Screen

1. From WiFi status, rotate to `SCAN NETWORKS` → SELECT
2. **Expected:** Screen switches to "SCANNING..." with animated dots (no freeze)
3. After ~2s: list appears with columns SSID / AUTH / SIG / dBm
4. `TestOpenNet` shows `OPEN`, 4 bars
5. `TestSecureNet` shows `WPA2`, 4 bars
6. List sorted by RSSI strongest first
7. Rotate → row highlight swaps in-place (no full-screen redraw)

---

## Test 3 — Open Network Connect

1. From scan list, navigate to `TestOpenNet` → SELECT
2. **Expected:** Connect Menu screen with title = SSID
   - Shows SSID, AUTH: OPEN, RSSI + bars
   - One action row: `CONNECT (open)`
   - No `FORGET` row (it's not the saved network)
3. SELECT on `CONNECT (open)`
4. **Expected:** CONNECTING screen with dots, target SSID shown
5. After brief pause: returns to STATUS screen showing `STATE: CONNECTED`
6. Notification banner: "WiFi Connected"
7. BACK → main menu

---

## Test 4 — Secured Network — Wrong Password

1. Open WiFi app → SCAN → navigate to `TestSecureNet` → SELECT
2. Connect Menu shows `ENTER PASSWORD`
3. SELECT → Password entry screen opens
   - Title bar: "TestSecureNet" + "PASS"
   - SSID label shows correctly
   - Password field is empty (or pre-filled if saved)
   - Big char picker shows `A` (first char)
4. Rotate right a few clicks → big char advances
5. SELECT → appends character to password field (shows `*`)
6. Type a wrong password (any chars)
7. **HOLD-SELECT (600ms)** → saves + initiates connect
8. CONNECTING screen appears
9. After ~15s: `CONNECTION FAILED` + `REASON: AUTH FAILED` (or TIMEOUT if Wokwi doesn't support WPA2 auth failure)
10. SELECT or BACK → returns to Connect Menu

---

## Test 5 — Secured Network — Correct Password

1. Open WiFi app → SCAN → `TestSecureNet` → SELECT → `ENTER PASSWORD`
2. Password field may be pre-filled from previous test; delete with BACK keypresses
3. Type `hunter2` character by character:
   - Each SELECT appends char (shown as `*`)
   - Big char picker shows current char
4. **HOLD-SELECT (600ms)** → saves + connects
5. **Expected:** CONNECTING → STATUS showing `STATE: CONNECTED` + IP
6. NTP sync should trigger (NTP status in status bar changes)

---

## Test 6 — SHOW/HIDE Toggle

1. Password entry screen open
2. Rotate until big char picker shows `[ SHOW ]` (scroll right past `?`)
3. SELECT → password field changes from `****` to actual chars
4. Rotate past sentinel → `[ HIDE ]` appears
5. SELECT → masks again

---

## Test 7 — Password Edit: Delete

1. Password entry screen, some chars typed
2. BACK press (short) → deletes last character
3. Repeat until field is empty
4. BACK press on empty field → returns to Connect Menu (no crash)

---

## Test 8 — Cancel Entry with HOLD-BACK

1. Password entry screen, some chars typed
2. **HOLD-BACK (600ms)** → cancels, returns to Connect Menu
3. No NVS write should happen
4. Connect Menu still shows same SSID

---

## Test 9 — Forget Network

1. Must have a saved network (complete Test 5 first)
2. Open WiFi app → SCAN → select the saved network → SELECT
3. Connect Menu should show **two** rows: `ENTER PASSWORD` + `FORGET THIS NETWORK`
4. Rotate to `FORGET THIS NETWORK` → SELECT
5. **Expected:** NVS cleared, returns to STATUS, banner "Network forgotten"
6. Re-scan → connect menu for same network now shows only `ENTER PASSWORD` (no FORGET row)

---

## Test 10 — Auto-Connect on Boot

1. Have `TestSecureNet` saved (complete Test 5 + Test 9 reversed)
2. Press the Wokwi reset button (or power cycle)
3. **Expected:** Boot animation → menu, but WiFi FSM starts CONNECTING in background
4. Status bar WiFi indicator transitions: `----` → `...` → `WiFi`
5. Notification banner: "WiFi Connected"

---

## Test 11 — Auto-Connect Toggle

1. WiFi app → status screen → rotate to `AUTO-CONNECT: ON`
2. SELECT → toggles to `OFF` (partial redraw of that row only)
3. Reboot → WiFi does NOT auto-connect
4. Repeat → toggle back to ON

---

## Performance Checklist (run during any test)

Open `SYS TOOLS → DIAGNOSTICS`:

| Check | Pass condition |
|-------|---------------|
| `LOOP: X fps` | ≥ 20fps during WiFi scan polling |
| Stall during typing | < 30ms per keypress |
| No full-screen flash on rotate in list | Row swap only |
| No full-screen flash on rotate in password entry | Picker area only |
| Status bar during scan | Updates only on state change |

---

## Known Wokwi Limitations

- Wokwi may report `WL_CONNECT_FAILED` for wrong passwords (shows `AUTH FAILED`) ✅
- Wokwi may not simulate `WL_NO_SSID_AVAIL` (shows `TIMEOUT` instead for unknown SSIDs)
- RSSI values in Wokwi are static (e.g. -60 dBm); real hardware varies
