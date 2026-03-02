#pragma once
#include <SdFat.h>
#include "netcore_config.h"

// ─── SD pins ──────────────────────────────────────────────────────────────────
#define SD_SCK   12
#define SD_MOSI  11
#define SD_MISO  13
#define SD_CS    10

// ─── Cartridge limits ─────────────────────────────────────────────────────────
#define CART_MAX_APPS    4
#define CART_NAME_LEN   20
#define CART_SUB_LEN     8
#define CART_FILE_LEN   24
#define CART_DESC_LEN   48
#define CART_VER_LEN     8

// ─── Glyph IDs ────────────────────────────────────────────────────────────────
#define CART_GLYPH_CROSSHAIR  0
#define CART_GLYPH_SHIELD     1
#define CART_GLYPH_SATELLITE  2
#define CART_GLYPH_GHOST      3
#define CART_GLYPH_SKULL      4

// ─── Cartridge structs ────────────────────────────────────────────────────────
struct CartApp {
  char name[CART_NAME_LEN];
  char sub[CART_SUB_LEN];
  char file[CART_FILE_LEN];
};

struct CartInfo {
  char    name[CART_NAME_LEN];
  char    version[CART_VER_LEN];
  char    desc[CART_DESC_LEN];
  uint8_t glyph;
  uint8_t color;
  int     appCount;
  CartApp apps[CART_MAX_APPS];
};

// ─── Payload library ──────────────────────────────────────────────────────────
#define PAYLOAD_MAX      16
#define PAYLOAD_NAME_LEN 32

struct PayloadEntry {
  char filename[PAYLOAD_NAME_LEN];  // e.g. "cache_clear.duck"
  char label[PAYLOAD_NAME_LEN];     // e.g. "CACHE CLEAR"
};

extern int          payloadCount;
extern PayloadEntry payloadList[PAYLOAD_MAX];

// ─── Globals ──────────────────────────────────────────────────────────────────
extern bool     sdPresent;
extern bool     cartLoaded;
extern CartInfo cartInfo;
extern SdFat    sd;

// ─── API ──────────────────────────────────────────────────────────────────────
bool        sdInit();
bool        sdLoadManifest();
int         sdRunScript(const char* filename, int maxLines);
const char* sdScriptLine(int i);
int         sdScriptLineCount();
int         sdScanPayloads();
uint16_t    sdCartAccentColor();
