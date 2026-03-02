#include "netcore_settings.h"

uint8_t uiBrightness = 100;
bool fxScanlines  = true;
bool fxShimmer    = true;
bool fxTyping     = false;
bool fxSound      = true;
bool wfShowSeconds = false;  // OFF by default — safe for Wokwi and real HW
bool wfLowPower    = true;   // ON by default — only redraw on minute change

uint8_t loadU8(const char* key, uint8_t defVal) {
  return (uint8_t)prefs.getUChar(key, defVal);
}

static void saveU8(const char* key, uint8_t val) {
  prefs.putUChar(key, val);
}

void settingsApplyBrightness() {
#if HAS_BACKLIGHT_PWM
  static bool inited = false;
  if (!inited) {
    ledcSetup(0, 5000, 8);
    ledcAttachPin(PIN_BACKLIGHT, 0);
    inited = true;
  }
  uint8_t duty = (uint8_t)((uiBrightness * 255) / 100);
  ledcWrite(0, duty);
#else
  // Wokwi LED tied to 3V3, no visible dim
#endif
}

void settingsSetBrightness(uint8_t pct, bool persist) {
  if (pct > 100) pct = 100;
  uiBrightness = pct;
  if (persist) saveU8(KEY_BRIGHT, uiBrightness);
  settingsApplyBrightness();
}

void settingsSetFxScanlines(bool on, bool persist) {
  fxScanlines = on;
  if (persist) saveU8(KEY_FX_SCAN, (uint8_t)(fxScanlines ? 1 : 0));
}

void settingsSetFxShimmer(bool on, bool persist) {
  fxShimmer = on;
  if (persist) saveU8(KEY_FX_SHIM, (uint8_t)(fxShimmer ? 1 : 0));
}

void settingsSetFxTyping(bool on, bool persist) {
  fxTyping = on;
  if (persist) saveU8(KEY_FX_TYPE, (uint8_t)(fxTyping ? 1 : 0));
}

void settingsSetFxSound(bool on, bool persist) {
  fxSound = on;
  if (persist) saveU8(KEY_SOUND, (uint8_t)(fxSound ? 1 : 0));
}

void settingsSetWfShowSeconds(bool on, bool persist) {
  wfShowSeconds = on;
  if (persist) saveU8(KEY_WF_SECS, (uint8_t)(wfShowSeconds ? 1 : 0));
}

void settingsSetWfLowPower(bool on, bool persist) {
  wfLowPower = on;
  if (persist) saveU8(KEY_WF_LOWP, (uint8_t)(wfLowPower ? 1 : 0));
}

void settingsLoad() {
  uint8_t savedTheme = loadU8(KEY_THEME, 0);
  (void)savedTheme; // theme is handled in theme module (we’ll hook it in next step)

  uint8_t b = loadU8(KEY_BRIGHT, 100);
  if (b > 100) b = 100;
  uiBrightness = b;

  fxScanlines = (loadU8(KEY_FX_SCAN, 1) != 0);
  fxShimmer   = (loadU8(KEY_FX_SHIM, 1) != 0);
  fxTyping    = (loadU8(KEY_FX_TYPE, 0) != 0);
  fxSound     = (loadU8(KEY_SOUND,   1) != 0);
  wfShowSeconds = (loadU8(KEY_WF_SECS, 0) != 0);  // default OFF
  wfLowPower    = (loadU8(KEY_WF_LOWP, 1) != 0);  // default ON

  settingsApplyBrightness();
}