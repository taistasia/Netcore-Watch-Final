#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// svc_rfid.h  —  RFID Scanner Service (MFRC522 over SPI)
//
// Hardware  : MFRC522 sharing FSPI bus with TFT (SCK=36, MISO=37, MOSI=35).
//             Dedicated CS=GPIO 14, RST=GPIO 15.
//             Requires library:  "MFRC522" by miguelbalboa  (Arduino Library Manager)
//
// Compile flags (set here) ────────────────────────────────────────────────────
//   RFID_ENABLED    1 = module active (default)
//                   0 = everything no-ops; app shows "RFID DISABLED"
//
//   RFID_DEMO_MODE  1 = synthetic UIDs; no hardware needed (default, Wokwi)
//                   0 = real MFRC522 over SPI (requires library + hardware)
//
// Wokwi note: Wokwi has no MFRC522 part. Keep RFID_DEMO_MODE 1 for simulation.
// In demo mode the scanner cycles through 5 hard-coded UIDs when armed.
//
// API ─────────────────────────────────────────────────────────────────────────
//   rfidSvcInit()               — call once in setup()
//   rfidSvcTick()               — call every loop(); ~0µs when unarmed
//   rfidSvcSensorOk()           — false when ENABLED=0 or hardware absent
//   rfidSvcSetArmed(bool)       — arm / disarm scanner
//   rfidSvcIsArmed()            — current armed state
//   rfidSvcGetLastUid(buf,len)  — "04 A1 B2 C3 D4" or "" if none
//   rfidSvcScanCount()          — total unique tag reads since boot
//
// Anti-spam ───────────────────────────────────────────────────────────────────
//   Same UID re-notifies only after RFID_SAME_UID_COOLDOWN_MS (3000 ms).
//   Different UID always notifies immediately.
//
// Poll rates ──────────────────────────────────────────────────────────────────
//   Armed    : RFID_POLL_ARMED_MS   (100 ms)  — active scanning
//   Unarmed  : no I/O at all  (tick exits in < 1 µs)
// ─────────────────────────────────────────────────────────────────────────────
#include <stdint.h>

// ── Compile flags ─────────────────────────────────────────────────────────────
#define RFID_ENABLED    1   // 0 = full no-op
#define RFID_DEMO_MODE  1   // 1 = synthetic UIDs (Wokwi); 0 = real MFRC522

// ── SPI pins ─────────────────────────────────────────────────────────────────
// Shares FSPI bus (SCK=36, MISO=37, MOSI=35) already init'd by TFT in setup()
#define RFID_CS_PIN   14
#define RFID_RST_PIN  15

// ── Timing ────────────────────────────────────────────────────────────────────
#define RFID_POLL_ARMED_MS       100UL   // SPI poll interval when armed
#define RFID_SAME_UID_COOLDOWN_MS 3000UL // suppress re-notify for same UID

// ── UID buffer size (longest UID = 10 bytes = "XX XX XX XX XX XX XX XX XX XX\0") ──
#define RFID_UID_STR_LEN  32

// ── Lifecycle ─────────────────────────────────────────────────────────────────
void rfidSvcInit();
void rfidSvcTick();

// ── Control ───────────────────────────────────────────────────────────────────
void rfidSvcSetArmed(bool armed);
bool rfidSvcIsArmed();

// ── Queries ───────────────────────────────────────────────────────────────────
bool     rfidSvcSensorOk();
bool     rfidSvcGetLastUid(char* out, int outLen);  // false if no scan yet
uint32_t rfidSvcScanCount();
