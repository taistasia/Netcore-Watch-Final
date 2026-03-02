#pragma once
#include "netcore_config.h"
#include "netcore_theme.h"

enum ConsoleMode { CONSOLE_INSTANT = 0, CONSOLE_TYPE = 1 };

// Initialize console window — draws border, clears area, resets line list
void consoleInit(int x, int y, int w, int h, ConsoleMode m);

// Add a line to the console. Lines exceeding box width are auto-truncated.
void consoleAddLine(const char* s);

// Clear the line list WITHOUT redrawing the border — use for live-updating content
void consoleReset();

// Redraw all current lines inside the console box
void consoleRender();

// Advance the typewriter effect by one character tick
void consoleTypeTick();

// Blink the cursor at the bottom of current content
void consoleCursorTick();
