#pragma once
#include "netcore_config.h"

extern uint8_t uiBrightness;   // 0..100
extern bool fxScanlines;
extern bool fxShimmer;
extern bool fxTyping;
extern bool fxSound;
extern bool wfShowSeconds;     // show seconds + blink colon on watchface (default OFF)
extern bool wfLowPower;        // low-power mode: redraw only on minute change (default ON)

uint8_t loadU8(const char* key, uint8_t defVal);

void settingsLoad();
void settingsApplyBrightness();
void settingsSetBrightness(uint8_t pct, bool persist);
void settingsSetFxScanlines(bool on, bool persist);
void settingsSetFxShimmer(bool on, bool persist);
void settingsSetFxTyping(bool on, bool persist);
void settingsSetFxSound(bool on, bool persist);
void settingsSetWfShowSeconds(bool on, bool persist);
void settingsSetWfLowPower(bool on, bool persist);