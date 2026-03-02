#include "netcore_sd.h"
#include <SPI.h>

// ─── Globals ──────────────────────────────────────────────────────────────────
bool     sdPresent  = false;
bool     cartLoaded = false;
CartInfo cartInfo   = {};
SdFat    sd;

int          payloadCount = 0;
PayloadEntry payloadList[PAYLOAD_MAX];



// ─── Internal helpers ─────────────────────────────────────────────────────────

static void trimRight(char* s) {
  int len = strlen(s);
  while (len > 0 && (s[len-1]=='\r'||s[len-1]=='\n'||s[len-1]==' '||s[len-1]=='\t'))
    s[--len] = '\0';
}

static bool matchKey(const char* line, const char* key, char* out, int outLen) {
  int klen = strlen(key);
  if (strncmp(line, key, klen) != 0) return false;
  if (line[klen] != '=') return false;
  strncpy(out, line + klen + 1, outLen - 1);
  out[outLen - 1] = '\0';
  trimRight(out);
  return true;
}

// ─── sdInit ───────────────────────────────────────────────────────────────────

bool sdInit() {
  sdPresent  = false;
  cartLoaded = false;
  payloadCount = 0;
  memset(&cartInfo, 0, sizeof(cartInfo));



  // SHARED_SPI reuses the display bus (same pins in Wokwi). No separate sdSPI needed.
  if (!sd.begin(SdSpiConfig(SD_CS, SHARED_SPI, SD_SCK_MHZ(4), &SPI)))
    return false;

  sdPresent = true;
  sdScanPayloads();      // scan /PAYLOADS/ for .duck files
  return sdLoadManifest();
}

// ─── sdLoadManifest ───────────────────────────────────────────────────────────

bool sdLoadManifest() {
  cartLoaded = false;
  memset(&cartInfo, 0, sizeof(cartInfo));
  if (!sdPresent) return false;
  if (!sd.exists("/CART.TXT")) return false;

  File32 f = sd.open("/CART.TXT", O_RDONLY);
  if (!f) return false;

  strncpy(cartInfo.name,    "UNKNOWN", CART_NAME_LEN - 1);
  strncpy(cartInfo.version, "?",       CART_VER_LEN  - 1);
  cartInfo.appCount = 0;
  cartInfo.glyph    = 0;
  cartInfo.color    = 0;

  char line[80];
  int  li = 0;

  while (f.available()) {
    char c = f.read();
    if (c == '\n' || li >= (int)sizeof(line) - 1) {
      line[li] = '\0'; li = 0;
      trimRight(line);
      if (line[0] == '\0' || line[0] == '#') continue;

      char val[CART_DESC_LEN];
      char tmp[8];

      matchKey(line, "NAME",  cartInfo.name,    CART_NAME_LEN) ||
      matchKey(line, "VER",   cartInfo.version, CART_VER_LEN)  ||
      matchKey(line, "DESC",  cartInfo.desc,    CART_DESC_LEN);

      if (matchKey(line, "GLYPH", tmp, sizeof(tmp))) cartInfo.glyph = (uint8_t)atoi(tmp);
      if (matchKey(line, "COLOR", tmp, sizeof(tmp))) cartInfo.color = (uint8_t)atoi(tmp);

      for (int i = 0; i < CART_MAX_APPS; i++) {
        char key[20];
        snprintf(key, sizeof(key), "APP%d_NAME", i);
        if (matchKey(line, key, val, sizeof(val))) {
          if (i >= cartInfo.appCount) cartInfo.appCount = i + 1;
          strncpy(cartInfo.apps[i].name, val, CART_NAME_LEN - 1);
        }
        snprintf(key, sizeof(key), "APP%d_SUB", i);
        if (matchKey(line, key, val, sizeof(val)))
          strncpy(cartInfo.apps[i].sub, val, CART_SUB_LEN - 1);
        snprintf(key, sizeof(key), "APP%d_FILE", i);
        if (matchKey(line, key, val, sizeof(val)))
          strncpy(cartInfo.apps[i].file, val, CART_FILE_LEN - 1);
      }
    } else {
      line[li++] = c;
    }
  }
  f.close();
  cartLoaded = (cartInfo.appCount > 0);
  return cartLoaded;
}

// ─── sdRunScript ──────────────────────────────────────────────────────────────

static char scriptLines[18][48];
static int  scriptLineCount = 0;

const char* sdScriptLine(int i) {
  if (i < 0 || i >= scriptLineCount) return "";
  return scriptLines[i];
}

int sdScriptLineCount() { return scriptLineCount; }

int sdRunScript(const char* filename, int maxLines) {
  scriptLineCount = 0;
  if (!sdPresent || !cartLoaded) return 0;
  if (maxLines > 18) maxLines = 18;

  char path[32];
  snprintf(path, sizeof(path), "/%s", filename);

  if (!sd.exists(path)) {
    strncpy(scriptLines[0], "ERR: FILE NOT FOUND", 47);
    scriptLineCount = 1; return 1;
  }

  File32 f = sd.open(path, O_RDONLY);
  if (!f) {
    strncpy(scriptLines[0], "ERR: OPEN FAILED", 47);
    scriptLineCount = 1; return 1;
  }

  char line[50];
  int  li = 0;
  while (f.available() && scriptLineCount < maxLines) {
    char c = f.read();
    if (c == '\n' || li >= (int)sizeof(line) - 1) {
      line[li] = '\0'; li = 0;
      trimRight(line);
      if (line[0] == '\0' || line[0] == '#') continue;
      strncpy(scriptLines[scriptLineCount], line, 47);
      scriptLines[scriptLineCount][47] = '\0';
      scriptLineCount++;
    } else { line[li++] = c; }
  }
  f.close();
  return scriptLineCount;
}

// ─── sdScanPayloads ───────────────────────────────────────────────────────────
// Scans /PAYLOADS/ for .duck files, falls back to root if folder missing.
// Uses File32 (same as rest of codebase) to avoid SdFat API mismatches.

int sdScanPayloads() {
  payloadCount = 0;
  if (!sdPresent) return 0;

  // Prefer /PAYLOADS subfolder; fall back to root for Wokwi where
  // the SD card UI doesn't support creating subdirectories.
  const char* scanPath = sd.exists("/PAYLOADS") ? "/PAYLOADS" : "/";
  bool isRoot = (scanPath[1] == '\0');

  Serial.print(">> Scanning: "); Serial.println(scanPath);

  File32 dir = sd.open(scanPath);
  if (!dir) {
    Serial.println(">> sdScanPayloads: failed to open dir");
    return 0;
  }

  File32 entry;
  while ((entry = dir.openNextFile()) && payloadCount < PAYLOAD_MAX) {
    if (entry.isDirectory()) { entry.close(); continue; }

    char name[PAYLOAD_NAME_LEN];
    entry.getName(name, PAYLOAD_NAME_LEN - 1);
    entry.close();

    Serial.print(">> Found file: "); Serial.println(name);

    // Only .duck files
    int len = strlen(name);
    if (len < 6) continue;
    if (strcasecmp(name + len - 5, ".duck") != 0) continue;

    // Build full path for duckyLoad()
    char fullPath[PAYLOAD_NAME_LEN];
    if (isRoot) {
      snprintf(fullPath, sizeof(fullPath), "/%s", name);
    } else {
      snprintf(fullPath, sizeof(fullPath), "/PAYLOADS/%s", name);
    }
    strncpy(payloadList[payloadCount].filename, fullPath, PAYLOAD_NAME_LEN - 1);
    payloadList[payloadCount].filename[PAYLOAD_NAME_LEN - 1] = '\0';

    // Derive label: strip .duck, uppercase, underscores -> spaces
    int labelLen = len - 5;
    if (labelLen >= PAYLOAD_NAME_LEN) labelLen = PAYLOAD_NAME_LEN - 1;
    strncpy(payloadList[payloadCount].label, name, labelLen);
    payloadList[payloadCount].label[labelLen] = '\0';
    for (int i = 0; payloadList[payloadCount].label[i]; i++) {
      char c = payloadList[payloadCount].label[i];
      payloadList[payloadCount].label[i] = (c == '_') ? ' ' : (char)toupper(c);
    }

    Serial.print(">> Added payload: "); Serial.println(payloadList[payloadCount].label);
    payloadCount++;
  }
  dir.close();

  Serial.print(">> Total payloads: "); Serial.println(payloadCount);
  return payloadCount;
}

// ─── sdCartAccentColor ────────────────────────────────────────────────────────

uint16_t sdCartAccentColor() {
  switch (cartInfo.color) {
    case 1: return 0x07FF; // cyan
    case 2: return 0xFD20; // amber
    case 3: return 0xF800; // red
    case 4: return 0xF81F; // magenta
    default: return 0x07E0; // green
  }
}