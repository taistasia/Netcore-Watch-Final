# NETCORE Phase 5 — RFID Scanner (MFRC522) — Wokwi Test Checklist

---

## Hardware & Compile Notes

Wokwi has no MFRC522 part. Two compile flags in `svc_rfid.h` handle this safely:

| Flag | Default | Effect |
|------|---------|--------|
| `RFID_ENABLED` | `1` | Module active |
| `RFID_DEMO_MODE` | `1` | Synthetic UIDs — no hardware needed (Wokwi) |

**Demo behavior:** When armed, the service cycles through 5 hard-coded 4-byte UIDs
at pseudo-random intervals (2–8 s) to simulate real tag presentations:

```
04 A1 B2 C3   DE AD BE EF   12 34 56 78   CA FE BA BE   FF 00 11 22
```

**Real hardware path:** Set `RFID_DEMO_MODE 0`, install library `MFRC522` by
miguelbalboa from Arduino Library Manager. Wire MFRC522:

| MFRC522 | ESP32-S3 GPIO | Notes |
|---------|--------------|-------|
| SDA/CS  | 14           | RFID_CS_PIN |
| RST     | 15           | RFID_RST_PIN |
| SCK     | 36           | Shares TFT FSPI bus |
| MOSI    | 35           | Shares TFT FSPI bus |
| MISO    | 37           | Shares TFT FSPI bus |
| 3.3V    | 3V3          | |
| GND     | GND          | |

SPI.begin() is already called in sketch.ino for the TFT — the MFRC522
just needs its own CS pin. The Adafruit library and MFRC522 library coexist
safely on the same FSPI bus.

**Fully disable:** Set `RFID_ENABLED 0` — all functions become empty stubs,
no library needed, app shows "RFID DISABLED".

In Wokwi, a green LED on GPIO 14 represents the RFID CS line. It lights
during init and active SPI transactions (real mode only).

---

## Test 1 — App appears in menu

1. Boot → main menu
2. **Expected:** `RFID  SCAN` is the 4th entry (after PING, AIR, TEMP)
3. Navigate → SELECT
4. **Expected:** Full screen renders once — no further full redraws
5. Title bar: `RFID` / `SCAN`
6. Footer: `SELECT: arm/disarm   BACK: menu   HOLD-BACK: home`

---

## Test 2 — Initial state (unarmed)

1. Open RFID app
2. **Expected:**
   - Badge: `  DISARMED  ` (dark background)
   - UID field: `--- no tag scanned ---` (dim)
   - SCANS row: `SCANS: 0    STATUS: DEMO`
   - Recent tags section: empty
3. `rfidSvcTick()` exits immediately — zero SPI activity (`if (!_armed) return`)

---

## Test 3 — Arm / disarm toggle

1. Press **SELECT**
2. **Expected:**
   - Badge instantly changes to `  [ ARMED ]  ` (accent/green colour)
   - UID field changes to `--- waiting for tag ---`
   - Demo UIDs begin appearing after ~1.5 s
3. Press **SELECT** again
4. **Expected:** Badge returns to `  DISARMED  `
5. Service stops polling immediately — no residual SPI calls
6. **No full screen redraw on toggle** — only badge area (24 px strip)

---

## Test 4 — First tag scan (demo)

After arming, demo fires first UID within ~1.5 s.

1. **Expected:**
   - UID field updates to `04 A1 B2 C3` (textSize 2, centred, accent)
   - SCANS counter increments to `1`
   - Recent tags section: `1: 04 A1 B2 C3` (newest = row 1, bold)
   - Notification banner: `RFID: TAG 04 A1 B2 C3` (INFO blue, 2.5 s)
   - HAPTIC LED: short 40 ms buzz
2. **No full screen redraw** — only UID field + stats + recent rows

---

## Test 5 — Anti-spam: same UID cooldown (3 s)

1. Arm scanner
2. Note timestamp of first `04 A1 B2 C3` notification
3. Demo will present the same UID again within its cycle
4. **Expected:** Second notification for same UID suppressed if < 3 s elapsed
5. **Expected:** Notification fires again after 3 s cooldown if UID repeats

Verify in Serial:
```
rfidSvc: DEMO scan 04 A1 B2 C3  → notification fires
rfidSvc: DEMO scan 04 A1 B2 C3  → suppressed (< 3000 ms)
rfidSvc: DEMO scan 04 A1 B2 C3  → fires (≥ 3000 ms elapsed)
```

---

## Test 6 — Different UID always notifies immediately

1. Arm scanner
2. Demo cycles through 5 UIDs in sequence
3. **Expected:** Every unique UID fires a notification immediately, regardless
   of timing, because different hash → different cooldown tracking
4. Scan count increments on every scan (including suppressed repeats)

---

## Test 7 — Recent tags ring (max 4)

1. Arm and let 5+ unique UIDs scan
2. **Expected:**
   - Only the 4 most recent UIDs shown
   - Row 1 = newest (full brightness `COL_FG`)
   - Rows 2–4 = older (dimmer `COL_DIM`)
   - 5th UID evicts the oldest entry silently
3. **Duplicates not pushed** — same UID as newest already stored is skipped

---

## Test 8 — Background scanning while on menu

1. Arm scanner, then press BACK → return to menu
2. **Expected:**
   - `rfidSvcSetArmed(false)` called on BACK — scanner disarms
   - Menu renders once; no residual RFID draws
3. Alternatively: arm, navigate away, armed = false confirmed

> Design decision: BACK and HOLD-BACK both disarm. If you want persistent
> background arming, remove `rfidSvcSetArmed(false)` from exit handlers.

---

## Test 9 — HOLD-BACK → home

1. Open RFID app, arm scanner
2. **Hold BACK** for ≥ 500 ms (`INPUT_HOLD_MS`)
3. **Expected:**
   - `inputHoldBack()` consumed
   - Scanner disarms
   - Main menu rendered (`renderMenuFull()`)
4. Short BACK press (< 500 ms) → normal BACK → menu (also disarms)
5. **No full screen intermediate flicker** between hold detection and menu render

---

## Test 10 — No full screen redraw during normal operation

Observe screen carefully during armed scanning:

| Event | What redraws |
|-------|-------------|
| App opened | Full screen (enter only) |
| Arm toggled | Badge strip only (24 px) |
| New UID scanned | UID field + stats row + 4 recent rows |
| Same UID (suppressed) | Nothing — no visual update |
| BACK pressed | Full screen (menu) |

**Must NOT see:** Full white/black flash during badge toggle or scan.

---

## Test 11 — Performance: unarmed overhead

1. Open DIAGNOSTICS app → note `LOOP:` fps
2. Navigate to RFID app, leave **disarmed**
3. **Expected:** fps unchanged — `rfidSvcTick()` exits at `if (!_armed) return`
4. Arm the scanner
5. **Expected:** fps drops by < 2 fps (100 ms poll = 1% of loop budget)

---

## Test 12 — RFID_ENABLED 0 (full disable)

1. Set `#define RFID_ENABLED 0` in `svc_rfid.h`
2. Rebuild — **no MFRC522 library required**
3. **Expected:** Compiles cleanly, zero warnings
4. RFID app opens, badge shows `RFID DISABLED`
5. SELECT does nothing (arm never activates)
6. No SPI traffic, no I/O, no notifications

---

## Test 13 — RFID_DEMO_MODE 0, no hardware (real mode stub)

1. Set `RFID_DEMO_MODE 0`, library installed, but no hardware connected
2. Boot
3. **Expected:**
   - `rfidSvcInit()` probes firmware version register → reads `0x00` or `0xFF`
   - Serial: `rfidSvc: MFRC522 NOT found`
   - `rfidSvcSensorOk()` returns false
   - App badge shows `SENSOR NOT FOUND`
   - `rfidSvcTick()` returns immediately — no SPI storm
4. No crash, no hang

---

## Screen Layout Reference

```
┌──────────────────────────────────────┐  y=0
│ STATUS BAR (20px)                    │
├──────────────────────────────────────┤  y=20
│ TITLE: RFID                   SCAN  │
├──────────────────────────────────────┤  y=54 (BODY_Y)
│ ┌──────────────────────────────┐     │  +6  → badge
│ │      [ ARMED ] / DISARMED   │     │  24px badge
│ └──────────────────────────────┘     │
│ ──────────────────────────────────── │  +34 divider
│ LAST TAG:                            │  +42 label
│                                      │
│       04 A1 B2 C3             ← textSize 2, centered
│                                      │  +56..+78
│ ──────────────────────────────────── │  +82 divider
│ SCANS: 4    STATUS: DEMO             │  +90
│ ──────────────────────────────────── │  +104 divider
│ RECENT TAGS:                         │  +112
│ 1: 04 A1 B2 C3  ← newest, bright    │  +126
│ 2: CA FE BA BE                       │  +140
│ 3: DE AD BE EF                       │  +154
│ 4: 12 34 56 78                       │  +168
├──────────────────────────────────────┤  y=216
│ SELECT: arm/disarm  BACK: menu       │  FOOTER
└──────────────────────────────────────┘  y=240
```

---

## Files Changed Summary

| File | Type | Description |
|------|------|-------------|
| `svc_rfid.h` | **NEW** | Service API, compile flags, pin defines, timing constants |
| `svc_rfid.cpp` | **NEW** | Demo mode (5 UIDs), real MFRC522 SPI FSM, djb2 anti-spam hash, RFID_ENABLED=0 stubs |
| `netcore_apps.cpp` | Modified | `#include svc_rfid.h`, full RFID app with badge/UID/stats/recent-ring, armed toggle, hold-back home |
| `netcore_config.h` | Modified | `#define RFID_CS_PIN 14`, `RFID_RST_PIN 15` |
| `sketch.ino` | Modified | `#include svc_rfid.h`, `rfidSvcInit()` (#11 in setup), `rfidSvcTick()` in loop |
| `diagram.json` | Modified | Green LED on GPIO 14 labelled RFID-CS |
| `libraries.txt` | Modified | MFRC522 library note (only needed when RFID_DEMO_MODE 0) |

---

## Anti-Spam Design Note

Same-UID cooldown uses a **djb2 hash** of the raw UID bytes — not a string
compare. This keeps the hot path allocation-free (no `strcmp`, no `String`).

The hash is stored alongside the last-notification timestamp:

```
New scan → compute hash
  same hash AND (now - lastNotifMs) < 3000  →  suppress (count still increments)
  different hash OR cooldown expired         →  notify + buzz + update hash/timer
```

Different UIDs always bypass cooldown regardless of timing. This means
rapidly presenting two different tags both get notified immediately.
