// ─────────────────────────────────────────────────────────────────────────────
// netcore_console.cpp
// Scrolling text console widget for NETCORE Field Terminal
//
// USAGE:
//   consoleInit()   — call once to set up the window (draws border)
//   consoleAddLine()— add a line of text (auto-truncated to fit box width)
//   consoleRender() — draw all current lines (call after adding lines)
//   consoleReset()  — clear line list WITHOUT redrawing border (for live updates)
//   consoleTypeTick() / consoleCursorTick() — call from app tick loop
// ─────────────────────────────────────────────────────────────────────────────

#include "netcore_console.h"

// ── Internal state ────────────────────────────────────────────────────────────
struct ConsoleWin {
  int  x, y, w, h;       // pixel bounds
  ConsoleMode mode;
  const char* lines[18]; // line pointers
  int  lineCount;
  int  typeLine;         // current typewriter line
  int  typeChar;         // current typewriter character position
  uint32_t typeLastMs;
  bool cursorOn;
  uint32_t cursorLastMs;
};

static ConsoleWin con;

// ── How many chars fit per line and how many lines fit in the box ─────────────
static int maxCharsPerLine() { return (con.w - 16) / 6; }
static int maxLinesInBox()   { return (con.h - 16) / 12; }

// ─────────────────────────────────────────────────────────────────────────────
// consoleInit — set up window, draw border, reset state
// ─────────────────────────────────────────────────────────────────────────────
void consoleInit(int x, int y, int w, int h, ConsoleMode m) {
  con.x = x; con.y = y; con.w = w; con.h = h;
  con.mode         = m;
  con.lineCount    = 0;
  con.typeLine     = 0;
  con.typeChar     = 0;
  con.typeLastMs   = 0;
  con.cursorOn     = true;
  con.cursorLastMs = 0;

  tft.fillRect(con.x, con.y, con.w, con.h, COL_BG());
  tft.drawRect(con.x, con.y, con.w, con.h, COL_DARK());
}

// ─────────────────────────────────────────────────────────────────────────────
// consoleReset — clear line list WITHOUT touching the display border
// Use this when streaming live content (e.g. inject) to avoid border flicker
// ─────────────────────────────────────────────────────────────────────────────
void consoleReset() {
  con.lineCount  = 0;
  con.typeLine   = 0;
  con.typeChar   = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// consoleAddLine — add a pointer to a string
// ─────────────────────────────────────────────────────────────────────────────
void consoleAddLine(const char* s) {
  if (con.lineCount >= 18) return;
  con.lines[con.lineCount++] = s;
}

// ─────────────────────────────────────────────────────────────────────────────
// consoleRender — draw all lines, clipped to box width and height
// ─────────────────────────────────────────────────────────────────────────────
void consoleRender() {
  // Clear interior only (preserves border)
  tft.fillRect(con.x + 1, con.y + 1, con.w - 2, con.h - 2, COL_BG());

  tft.setTextSize(1);
  tft.setTextColor(COL_FG(), COL_BG());

  int lx      = con.x + 8;
  int ly      = con.y + 8;
  int maxCh   = maxCharsPerLine();
  int maxRows = maxLinesInBox();

  // Truncation buffer — reused per line
  char trunc[52];

  auto printLine = [&](int row, const char* s, int charLimit) {
    // Clip to box width
    int len = (charLimit >= 0) ? charLimit : (int)strlen(s);
    if (len > maxCh) len = maxCh;
    strncpy(trunc, s, len);
    trunc[len] = '\0';
    tft.setCursor(lx, ly + row * 12);
    tft.print(trunc);
  };

  if (con.mode == CONSOLE_INSTANT) {
    for (int i = 0; i < con.lineCount && i < maxRows; i++)
      printLine(i, con.lines[i], -1);

  } else {
    // Typewriter: draw completed lines fully, current line up to typeChar
    for (int i = 0; i < con.lineCount && i < maxRows; i++) {
      if (i < con.typeLine) {
        printLine(i, con.lines[i], -1);
      } else if (i == con.typeLine) {
        printLine(i, con.lines[i], con.typeChar);
      }
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// consoleTypeTick — advance typewriter by one character, 24ms per char
// ─────────────────────────────────────────────────────────────────────────────
void consoleTypeTick() {
  if (con.mode != CONSOLE_TYPE) return;
  if (con.typeLine >= con.lineCount) return;
  if (millis() - con.typeLastMs < 24) return;
  con.typeLastMs = millis();

  const char* s = con.lines[con.typeLine];
  if (!s) return;

  if (s[con.typeChar] == '\0') {
    con.typeLine++;
    con.typeChar = 0;
  } else {
    con.typeChar++;
  }

  consoleRender();
}

// ─────────────────────────────────────────────────────────────────────────────
// consoleCursorTick — blink a small underline cursor after last line
// ─────────────────────────────────────────────────────────────────────────────
void consoleCursorTick() {
  if (millis() - con.cursorLastMs < 400) return;
  con.cursorLastMs = millis();
  con.cursorOn = !con.cursorOn;

  int maxRows        = maxLinesInBox();
  int displayedLines = con.lineCount < maxRows ? con.lineCount : maxRows;

  int cx = con.x + 8;
  int cy = con.y + 8 + displayedLines * 12;

  tft.fillRect(cx - 2, cy + 9, 10, 6, COL_BG());
  if (con.cursorOn)
    tft.fillRect(cx - 2, cy + 9, 8, 2, COL_FG());
}
