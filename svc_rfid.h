// svc_rfid.h
#pragma once
#include <stdint.h>
#include <stdbool.h>

// ─────────────────────────────────────────────────────────────────────────────
// svc_rfid  —  RFID scanning service (MFRC522)
//   - tick-driven, non-blocking
//   - optional demo mode (synthetic UIDs) for simulation bring-up
//
// API:
//   rfidSvcInit()
//   rfidSvcTick()
//   rfidSvcArm(bool enable)        // start/stop polling
//   rfidSvcHasTag()                // true if a new tag was seen since last consume
//   rfidSvcConsume(uidBuf, len)    // copies UID and clears "new tag" flag
//
// Build switches:
//   RFID_ENABLED    0 = compile to stubs (safe no-op)
//   RFID_DEMO_MODE  1 = synthetic UIDs; no hardware needed
//                   0 = real MFRC522 over SPI (default for Wokwi MFRC522 part)
//
// NETCORE rules:
//   - no heap in tick
//   - no Strings in tick
//   - no delays in tick
// ─────────────────────────────────────────────────────────────────────────────

// Wokwi note: use the "wokwi-mfrc522" part and keep RFID_DEMO_MODE 0.

#define RFID_ENABLED    1
#define RFID_DEMO_MODE  0   // 0 = real MFRC522 (Wokwi part); 1 = synthetic UIDs

// UID max length per ISO14443A is typically 10 bytes, MFRC522 supports 4/7/10.
#define RFID_UID_MAX    10

void rfidSvcInit();
void rfidSvcTick();

void rfidSvcArm(bool enable);

bool rfidSvcHasTag();
uint8_t rfidSvcUidLen();
void rfidSvcConsume(uint8_t* outUid, uint8_t* outLen);

// Convenience: format to hex into caller buffer (no heap).
// out must be at least (RFID_UID_MAX*2 + 1) bytes.
void rfidSvcUidHex(char* out, int outLen);