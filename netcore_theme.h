#pragma once
#include "netcore_config.h"

struct Theme {
  const char* name;
  uint16_t fg;
  uint16_t dim;
  uint16_t hilite;
  uint16_t dark;
};

extern Theme themes[];
extern const int THEME_COUNT;
extern int themeIndex;

uint16_t COL_BG();
uint16_t COL_FG();
uint16_t COL_DIM();
uint16_t COL_HILITE();
uint16_t COL_DARK();

// Status colors (fixed, not theme-dependent)
uint16_t COL_OK();
uint16_t COL_WARN();
uint16_t COL_BAD();

void setThemeIndex(int idx, bool persist);