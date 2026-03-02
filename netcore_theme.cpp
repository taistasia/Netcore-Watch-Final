#include "netcore_theme.h"

Theme themes[] = {
  { "GREEN",     ILI9341_GREEN,    0x03E0, 0x07E0, 0x0200 },
  { "PHOSPHOR",  0x87F0,           0x4FE8, 0xC7F8, 0x2364 },
  { "AMBER",     0xFD20,           0x9A80, 0xFFE0, 0x4200 },
  { "GOLD",      0xFEA0,           0xBCE0, 0xFFE0, 0x52A0 },
  { "CYAN",      ILI9341_CYAN,     0x07FF, 0xBFFF, 0x02AA },
  { "ICE",       0xB7FF,           0x6D7F, 0xE7FF, 0x2B5A },
  { "VIOLET",    0xF1BF,           0x88AF, 0xFDFF, 0x4108 },
  { "RED",       ILI9341_RED,      0x7800, 0xF800, 0x3000 },
  { "WHITE",     ILI9341_WHITE,    0xC618, 0xFFFF, 0x4208 },
};

const int THEME_COUNT = sizeof(themes) / sizeof(themes[0]);
int themeIndex = 0;

uint16_t COL_BG()     { return ILI9341_BLACK; }
uint16_t COL_FG()     { return themes[themeIndex].fg; }
uint16_t COL_DIM()    { return themes[themeIndex].dim; }
uint16_t COL_HILITE() { return themes[themeIndex].hilite; }
uint16_t COL_DARK()   { return themes[themeIndex].dark; }

uint16_t COL_OK()   { return ILI9341_GREEN; }
uint16_t COL_WARN() { return ILI9341_YELLOW; }
uint16_t COL_BAD()  { return ILI9341_RED; }

static inline void saveU8(const char* key, uint8_t val) { prefs.putUChar(key, val); }

void setThemeIndex(int idx, bool persist) {
  if (idx < 0) idx = 0;
  if (idx >= THEME_COUNT) idx = THEME_COUNT - 1;
  themeIndex = idx;
  if (persist) saveU8(KEY_THEME, (uint8_t)themeIndex);
}