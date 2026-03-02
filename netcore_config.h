#pragma once
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <Preferences.h>

#define FW_VERSION "1.2.0"

// ── Display pins (FSPI) ───────────────────────────────────────────────────────
#define TFT_CS   38
#define TFT_DC   39
#define TFT_RST  40
#define TFT_SCK  36
#define TFT_MISO 37
#define TFT_MOSI 35

// ── SD card pins ─────────────────────────────────────────────────────────────
#define SD_SCK   12
#define SD_MOSI  11
#define SD_MISO  13
#define SD_CS    10

// ── Input: Rotary Encoder (KY-040) + Back button ─────────────────────────────
// CW = DOWN, CCW = UP, SW push = SELECT
#define PIN_ENC_CLK  4
#define PIN_ENC_DT   5
#define PIN_ENC_SW   6
#define PIN_BACK     21

// ── I2C bus (shared: svc_air + svc_tempir both use pins 8/9) ────────────────
// Devices: SCD40 @ 0x62 (air quality), MLX90614 @ 0x5A (IR temp)
// Wire.begin(8,9) is safe to call from multiple services; ESP32 no-ops it.
// Pins 8/9 are free on ESP32-S3 devkitc-1; no other module uses them.
// svc_air.h re-defines these as AIR_I2C_SDA / AIR_I2C_SCL for clarity.
#define PIN_I2C_SDA  8
#define PIN_I2C_SCL  9

// ── RFID scanner (MFRC522) — shares FSPI bus (SCK=36,MISO=37,MOSI=35) ────
#define RFID_CS_PIN   14   // MFRC522 chip-select
#define RFID_RST_PIN  15   // MFRC522 reset

// ── Haptics motor / buzzer ──────────────────────────────────────────────────
#define PIN_HAPTIC 47   // vibration motor/buzzer; set to -1 to disable

// ── Backlight ────────────────────────────────────────────────────────────────
#define HAS_BACKLIGHT_PWM 0
#define PIN_BACKLIGHT 46

// ── Idle / watchface timeouts ─────────────────────────────────────────────────
#define IDLE_WATCHFACE_MS  20000UL   // enter watchface after 20s idle
#define IDLE_SLEEP_MS      60000UL   // blank display after 60s idle

// ── Layout constants ─────────────────────────────────────────────────────────
const int W        = 320;
const int H        = 240;
const int STATUS_H = 20;
const int TITLE_H  = 34;
const int FOOTER_H = 24;
const int BODY_Y   = STATUS_H + TITLE_H;

// ── NVS keys ─────────────────────────────────────────────────────────────────
static const char* PREF_NS       = "netcore";
static const char* KEY_THEME     = "theme";
static const char* KEY_BRIGHT    = "bright";
static const char* KEY_FX_SCAN   = "fxscan";
static const char* KEY_FX_SHIM   = "fxshim";
static const char* KEY_FX_TYPE   = "fxtype";
static const char* KEY_SOUND     = "sound";
static const char* KEY_WF_SECS   = "wfsecs";  // watchfaceShowSeconds (bool)
static const char* KEY_WF_LOWP   = "wflowp";  // watchfaceLowPower    (bool)
// WiFi creds — written by WiFi Manager in PASS C; read for auto-connect
static const char* KEY_WIFI_SSID = "wssid";
static const char* KEY_WIFI_PASS = "wpass";
static const char* KEY_WIFI_AUTO = "wauto";   // bool: auto-connect on boot

// ── Time service constants ────────────────────────────────────────────────────
// Stale: if NTP synced but last sync > TIME_STALE_SEC ago, show stale indicator.
#define TIME_STALE_SEC  21600UL   // 6 hours

// ── Render governance — centralised frame caps ────────────────────────────────
// All modules reference these; change policy here, not in each file.
#define RENDER_FRAME_MS_ACTIVE   33UL   // apps mode     : ~30 fps cap
#define RENDER_FRAME_MS_SECS    200UL   // watchface+secs:  ~5 fps cap
#define RENDER_FRAME_MS_MIN    1000UL   // watchface min :   1 fps cap
#define RENDER_STALL_MS         250UL   // skip optional draws if prev frame took >this

// ── Performance debug overlay ─────────────────────────────────────────────────
// Set to 1 to enable fps/stall overlay on watchface status strip.
// Always 0 for production builds.
#define DEBUG_PERF 0

// ── Globals defined in sketch.ino ────────────────────────────────────────────
extern Adafruit_ILI9341 tft;
extern Preferences prefs;
