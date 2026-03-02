# NFC PLAN — NETCORE Phase 6 Specification

## Status: SPEC ONLY — no functional code changes in this phase

---

## 1. What the MFRC522 Can and Cannot Do

### ✅ MFRC522 CAN do (already implemented in Phase 5)

| Capability | Notes |
|---|---|
| **Read UID** | Works reliably on ISO 14443-A (Mifare, most key fobs, access cards) |
| **Mifare Classic 1K/4K basic read** | Possible with default key `FF FF FF FF FF FF` on factory-fresh cards |
| **Card detection / presence sensing** | ~2–5 ms per poll, works fine at 100 ms interval |
| **Write UID sectors (Mifare Classic)** | Possible if auth succeeds and card is not locked |
| **Passive tag scanning** | Works for most common RFID key fobs and cards |

### ❌ MFRC522 CANNOT do

| Limitation | Why it matters |
|---|---|
| **NDEF tag read/write** | NDEF is the NFC Forum standard used by Android/iOS and smart NFC tags. MFRC522 has no NDEF stack. |
| **NFC Forum Type 2/4/5 tags** | NTAG213/215/216 (the "sticker" tags you buy everywhere) need a proper NFC controller stack, not just ISO 14443-A framing. Partial reads work; NDEF payload access does not. |
| **Phone-to-device NFC (HCE, P2P)** | Android HCE and iOS background NFC both require ISO-DEP / APDU command support. MFRC522 cannot act as a card reader for phone-emulated cards in any reliable way. |
| **NFC-B (ISO 14443-B)** | Japanese transit cards, some ID cards, passports. MFRC522 is Type-A only. |
| **NFC-F (FeliCa)** | Used in Japan's Suica/Pasmo transit, Sony's RFID ecosystem. Not supported. |
| **Long range** | MFRC522 is good for ~3–5 cm. PN532 reaches ~7–10 cm. |
| **Background tag reading** | MFRC522 needs active polling (`IsNewCardPresent` every 100 ms). True NFC chips can wake on tag approach via interrupt. |
| **Reader-to-reader (P2P)** | No NFC peer-to-peer capability. |

### 🟡 MFRC522 grey zones (possible but fragile)

| Capability | Reality |
|---|---|
| **NTAG21x UID read** | UID comes through fine. Actual NDEF payload requires implementing Type 2 tag commands manually. Fragile and not worth it on MFRC522. |
| **Mifare Classic sector read** | Works with default key on blank cards. Real-world access cards use custom keys — unreadable without the key. Encryption is broken (Crypto1) but you need the key material. |
| **Cloning key fobs** | MFRC522 + magic cards can clone UID-locked Mifare Classic cards. This is the primary "hacker gadget" use case for MFRC522. |

---

## 2. Current NETCORE RFID Feature (Phase 5 Summary)

What we have today with MFRC522:

- ✅ Scan any ISO 14443-A tag → display UID
- ✅ Anti-spam cooldown (3 s same-UID)
- ✅ Recent tag history (last 4 UIDs)
- ✅ Notification + haptic on new scan
- ✅ Armed/disarmed state (zero overhead when unarmed)
- ✅ Demo mode for Wokwi

This is the right scope for MFRC522. Trying to go further on this chip is diminishing returns.

---

## 3. If You Want Real NFC — Hardware Recommendation

### Option A: PN532 (recommended for this project)

**Best overall choice** for an ESP32-based device targeting Android/iOS compatibility.

| Property | Value |
|---|---|
| **Chip** | NXP PN532 |
| **Interface** | I2C or SPI or HSU (UART) — all three supported |
| **Library** | Adafruit PN532 (stable, well-maintained) |
| **NDEF** | ✅ Full NDEF read/write via libnfc-style API |
| **NFC Forum** | ✅ Types 1, 2, 3, 4 — covers 95%+ of real-world tags |
| **Phone HCE** | ✅ Can read Android HCE-emulated cards |
| **iOS tags** | ✅ Can read NFC tags that iOS writes |
| **Mifare Classic** | ✅ Full Crypto1 support |
| **NTAG21x** | ✅ NDEF payload read/write works properly |
| **P2P** | ⚠️ Limited; not commonly used anymore (Beam is deprecated) |
| **I2C address** | 0x24 (fits on our existing I2C bus, pins 8/9) |
| **Wokwi** | ❌ No Wokwi part — needs demo mode like air/tempir |
| **Cost** | ~$3–6 breakout (Adafruit #364, or HiLetGo clones) |

**Why PN532 over alternatives:**
- The Adafruit PN532 library is battle-tested on ESP32
- I2C mode lets it share the existing Wire bus (pins 8/9) — no new SPI CS pin needed
- NDEF support is the single biggest unlock: you can read/write URLs, vCards, WiFi credentials, etc.
- It's the standard recommendation for DIY NFC projects everywhere

### Option B: ST25R3911B (if you need serious range + ISO 15693)

Good if you later want to read warehouse-style UHF-adjacent tags or need >10 cm range. Much heavier library, expensive, overkill for a watch/handheld device.

### Option C: ACR122U USB NFC Reader

Irrelevant for embedded — desktop only.

---

## 4. What PN532 Would Unlock for NETCORE

In order of implementation priority:

### Phase 6A: NDEF Tag Read
- Read a standard NFC sticker (NTAG215 etc.) and display its payload
- URL tags: show the URL on screen
- Text tags: display raw text
- WiFi tags (NFC Forum WiFi Simple Configuration): auto-connect to embedded SSID/password 🔥

### Phase 6B: NDEF Tag Write
- Write a URL to a blank NFC sticker from the device
- Write contact info / vCard
- Write WiFi credentials (provision a network by tapping a sticker)

### Phase 6C: Android HCE Compatibility
- Read an Android phone's HCE-emulated card (Android Pay, custom apps)
- Display AID + APDU response
- Useful for security research, badge emulation testing

### Phase 6D: Mifare Classic Full Read
- Read sector data (not just UID) on cards using default or known keys
- Display sector map: locked / open / data
- Optional: dictionary attack on common default keys

### Phase 6E: Tag Emulation (card present to a reader)
- PN532 can emulate a tag when connected to another reader
- NETCORE could pretend to be an NFC card — interesting for access card cloning research

---

## 5. Migration Path: MFRC522 → PN532

The service layer (`svc_rfid`) is designed to hide hardware specifics. Migration plan:

```
Phase 5 (now):    svc_rfid.cpp  →  MFRC522 over SPI (CS=14)
Phase 6+ (later): svc_rfid.cpp  →  PN532 over I2C (addr 0x24)
                  + svc_nfc.cpp →  NDEF read/write layer (new, additive)
```

### What doesn't change
- `svc_rfid.h` public API: `rfidSvcInit`, `rfidSvcTick`, `rfidSvcGetLastUid`, `rfidSvcScanCount`, etc.
- All apps using `rfidSvcGetLastUid()` — they just see better UIDs
- `netcore_apps.cpp` RFID app UI — same screen, same UX
- `netcore_config.h` — just change `RFID_CS_PIN` to unused if going I2C

### What changes
- `svc_rfid.cpp` implementation swaps from MFRC522 library to Adafruit PN532
- Add `RFID_HARDWARE_PN532` compile flag to choose at build time
- Add `svc_nfc.h / svc_nfc.cpp` for NDEF layer (separate service, additive)
- Add "NFC READ" and "NFC WRITE" sub-screens to the RFID app

### Pin changes
```
Current (MFRC522 SPI):   CS=14, RST=15, shared SCK/MISO/MOSI
Future  (PN532 I2C):     SDA=8, SCL=9  (already wired! same bus as SCD40/MLX90614)
```
Pins 14 and 15 would be freed up.

---

## 6. Why Not Just Use PN532 Now?

Reasons to wait:

1. **MFRC522 is already wired and working** — UID scanning is the most common real-world use case and it works great
2. **Adafruit PN532 library is larger** — adds ~15–20 KB of flash. Not a problem yet, worth being intentional about
3. **NDEF parsing adds complexity** — URLs, vCards, WiFi records all need a parser. That's a proper phase of work
4. **No Wokwi PN532 part** — demo mode still needed, same as MFRC522

Decision point: when you want to **read/write NFC sticker payloads** or **interact with phones**, swap to PN532. Until then, MFRC522 is the right call.

---

## 7. Suggested CART.txt / Future Phase List

```
PHASE 6A  svc_nfc.h stub header + PN532 NDEF read (PN532 hardware)
PHASE 6B  NDEF write from device (write URL/text/WiFi to blank sticker)
PHASE 6C  RFID app: sub-menu for NDEF viewer (URL / text / raw hex)
PHASE 6D  Mifare Classic sector reader (key dictionary, sector map display)
PHASE 6E  Tag emulation / card cloning research mode
```

---

## 8. Compile Flag Reference (current + proposed)

### Current (Phase 5)
```c
// svc_rfid.h
#define RFID_ENABLED    1   // 0 = full no-op stubs
#define RFID_DEMO_MODE  1   // 1 = synthetic UIDs; 0 = real MFRC522
```

### Proposed additions (Phase 6+)
```c
// svc_rfid.h (future)
#define RFID_HARDWARE_MFRC522  1   // current default
#define RFID_HARDWARE_PN532    0   // set to 1 when hardware swapped

// svc_nfc.h (new file, Phase 6A)
#define NFC_ENABLED     0   // 0 = stub; 1 = PN532 NDEF layer active
#define NFC_DEMO_MODE   1   // synthetic NDEF records for Wokwi
```

Only one `RFID_HARDWARE_*` flag should be 1 at a time. Both 0 = error at compile time (add a `#error` guard).
