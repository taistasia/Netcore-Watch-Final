#pragma once
// ═════════════════════════════════════════════════════════════════════════════
// sd_manifest_spec.h  —  SD Cartridge Platform Specification
//
// STATUS: SPEC — structs and constants only.
//         Not yet parsed at runtime; sdInit() will call sdManifestLoad() once
//         this is wired in.
//
// ── FOLDER LAYOUT (SD card root) ─────────────────────────────────────────────
//
//   /CART/cart.txt          ← cartridge manifest (INI-style, easy to parse)
//   /PAYLOADS/*.duck        ← ducky script payloads (existing)
//   /THEMES/*/theme.txt     ← optional custom themes (name, colors)
//   /ANIMS/*/anim.txt       ← optional animations (future)
//   /ICONS/*.bmp            ← 16×16 or 32×32 1bpp icon glyphs (optional)
//
// ── MANIFEST FORMAT: cart.txt ─────────────────────────────────────────────────
//   Plain text, INI-style. One key=value per line. # = comment.
//   Sections: [cart], [app0], [app1], ...
//
//   Example:
//     [cart]
//     name=My Toolkit
//     version=1.2
//     fw_min=0.9.0
//     glyph_id=0
//     accent_color=0x07E0
//
//     [app0]
//     name=EXFIL
//     sub=DUCKY
//     file=/PAYLOADS/exfil.duck
//     type=ducky
//     requires=hid,sd
//
//     [app1]
//     name=RECON
//     sub=NET
//     file=/PAYLOADS/recon.duck
//     type=ducky
//     requires=wifi,hid
//
// ── INDEX RULES ───────────────────────────────────────────────────────────────
//   1. sdManifestLoad() called by taskRun(TASK_SD_INDEX) after card insert.
//   2. Results cached in _sdManifest global. Never re-read from SD in tick().
//   3. On card remove: sdManifestClear() zeros the struct; publishEvent(EVT_SD_REMOVED).
//   4. "Rescan SD" option in SYS TOOLS → taskRun(TASK_SD_INDEX).
//   5. Payload list (payloadList[] in netcore_sd.h) is also rebuilt during index.
// ═════════════════════════════════════════════════════════════════════════════
#include "netcore_config.h"
#include <stdint.h>

// ── Sizing constraints ────────────────────────────────────────────────────────
#define SD_CART_NAME_LEN     32
#define SD_CART_VER_LEN      12
#define SD_APP_FILE_LEN      48
#define SD_APP_REQUIRES_LEN  32
#define SD_MAX_CART_APPS      8     // max apps in a single cartridge manifest

// ── Cart app capabilities flags ───────────────────────────────────────────────
#define SD_CAP_WIFI   (1 << 0)
#define SD_CAP_SD     (1 << 1)
#define SD_CAP_HID    (1 << 2)
#define SD_CAP_BLE    (1 << 3)

// App types
enum SdAppType : uint8_t {
  SD_APP_DUCKY    = 0,
  SD_APP_SCRIPT   = 1,    // future: Lua/JS? keep extensible
  SD_APP_NATIVE   = 2,    // future: plugin binary (not planned near-term)
};

// ── Cart app entry ────────────────────────────────────────────────────────────
struct SdCartApp {
  char       name[SD_CART_NAME_LEN];
  char       sub[SD_CART_NAME_LEN];
  char       file[SD_APP_FILE_LEN];
  SdAppType  type;
  uint8_t    capabilities;   // bitmask of SD_CAP_*
  bool       valid;
};

// ── Cart manifest ─────────────────────────────────────────────────────────────
struct SdManifest {
  bool       loaded;
  char       name[SD_CART_NAME_LEN];
  char       version[SD_CART_VER_LEN];
  char       fwMin[SD_CART_VER_LEN];    // min firmware version required
  uint8_t    glyphId;                    // index into built-in glyph table
  uint16_t   accentColor;                // RGB565 colour for this cart's theme
  int        appCount;
  SdCartApp  apps[SD_MAX_CART_APPS];
};

// ── API (to be implemented in netcore_sd.cpp when wired) ─────────────────────
bool sdManifestLoad();     // called from TASK_SD_INDEX; reads /CART/cart.txt
void sdManifestClear();    // called on SD remove
const SdManifest* sdManifestGet();   // read-only accessor
