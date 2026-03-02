#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// svc_nfc_spec.h  —  NFC Service SPECIFICATION / STUB  (Phase 6 — NOT YET IMPLEMENTED)
//
// STATUS: THIS FILE IS A SPEC HEADER ONLY.
//         No implementation (.cpp) exists yet.
//         Including this file will NOT compile — it is documentation in C form.
//         It is provided so the intended API is locked in writing before Phase 6A.
//
// Target hardware: NXP PN532 over I2C (addr 0x24, shares pins 8/9 with svc_air/svc_tempir)
// Library:         Adafruit PN532 (Arduino Library Manager: "Adafruit PN532")
//
// Phase readiness:
//   Phase 6A — NDEF read (svc_nfc.cpp implemented, PN532 hardware)
//   Phase 6B — NDEF write
//   Phase 6C — Full app integration
//   Phase 6D — Mifare Classic sector reader
//
// See NFC_PLAN.md for full rationale, hardware comparison, and migration path.
// ─────────────────────────────────────────────────────────────────────────────

// ── Proposed compile flags (future svc_nfc.h) ────────────────────────────────
// #define NFC_ENABLED    0   // 0 = stubs only; 1 = PN532 active
// #define NFC_DEMO_MODE  1   // 1 = synthetic NDEF records for Wokwi

// ── NDEF record types the parser will handle ──────────────────────────────────
// enum NfcNdefType {
//   NDEF_NONE    = 0,
//   NDEF_TEXT    = 1,   // Well-known type "T" — plain text
//   NDEF_URI     = 2,   // Well-known type "U" — URL with prefix byte
//   NDEF_WIFI    = 3,   // MIME type "application/vnd.wfa.wsc" — WiFi config
//   NDEF_VCARD   = 4,   // MIME type "text/vcard"
//   NDEF_UNKNOWN = 5,   // Raw payload; display as hex
// };

// ── Proposed API ─────────────────────────────────────────────────────────────
// void nfcSvcInit();           — probe PN532; set _sensorOk
// void nfcSvcTick();           — poll for tag when armed; non-blocking
// void nfcSvcSetArmed(bool);   — start/stop scanning (same pattern as rfidSvc)
// bool nfcSvcSensorOk();       — false if PN532 absent or NFC_ENABLED=0
// bool nfcSvcHasTag();         — true if tag detected and NDEF read complete
// NfcNdefType nfcSvcNdefType();
// bool nfcSvcGetText(char* out, int outLen);    — populated if NDEF_TEXT
// bool nfcSvcGetUri(char* out, int outLen);     — populated if NDEF_URI
// bool nfcSvcGetRawPayload(uint8_t* buf, int* len, int maxLen); — any type
//
// Write path (Phase 6B):
// bool nfcSvcWriteText(const char* text);  — write NDEF_TEXT to blank tag
// bool nfcSvcWriteUri(const char* uri);    — write NDEF_URI  to blank tag
// bool nfcSvcWriteWifi(const char* ssid, const char* pass); — WiFi record

// ── Dependency plan ───────────────────────────────────────────────────────────
// svc_nfc depends on:
//   <Wire.h>               — already init'd by svc_air (pins 8/9)
//   <Adafruit_PN532.h>     — install from Arduino Library Manager
//   "svc_notify.h"         — notification on NDEF read
//   "svc_haptics.h"        — buzz on tag detection
//   "svc_rfid.h"           — optional: svc_nfc can call rfidSvcSetArmed(false)
//                            to avoid both services polling simultaneously
//
// svc_nfc does NOT depend on:
//   SPI / MFRC522          — PN532 uses I2C; no SPI conflict
//   netcore_apps.cpp       — service is self-contained; app code polls via API

// ── I2C address note ─────────────────────────────────────────────────────────
// PN532 I2C address: 0x24 (fixed by default; can be changed via DIP switches)
// Existing I2C devices on bus: SCD40 @ 0x62, MLX90614 @ 0x5A
// 0x24 does not conflict with either.

// ── PN532 mode selection ──────────────────────────────────────────────────────
// PN532 selects interface via two solder pads (SEL0, SEL1):
//   I2C:  SEL0=HIGH, SEL1=HIGH
//   SPI:  SEL0=LOW,  SEL1=HIGH
//   UART: SEL0=HIGH, SEL1=LOW
// Adafruit breakout has DIP switches. Set to I2C to use pins 8/9.

// ─────────────────────────────────────────────────────────────────────────────
// This file is intentionally not #include'd anywhere in the build.
// When Phase 6A begins, rename to svc_nfc.h, un-comment the API above,
// and implement svc_nfc.cpp.
// ─────────────────────────────────────────────────────────────────────────────
