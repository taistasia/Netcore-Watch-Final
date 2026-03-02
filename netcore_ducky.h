// ─────────────────────────────────────────────────────────────────────────────
// netcore_ducky.h
// Ducky Script parser and executor for NETCORE Field Terminal
//
// QUICK START:
//   - Set DUCKY_DRY_RUN true  → Wokwi / debug mode (shows output on screen)
//   - Set DUCKY_DRY_RUN false → Hardware mode (fires real USB HID keystrokes)
//
// SUPPORTED COMMANDS:
//   REM <text>          Comment, ignored during execution
//   DELAY <ms>          Wait for milliseconds
//   STRING <text>       Type a string of characters
//   ENTER               Press Enter key
//   TAB                 Press Tab key
//   ESCAPE / ESC        Press Escape key
//   BACKSPACE           Press Backspace key
//   SPACE               Press Space key
//   UP / DOWN / LEFT / RIGHT   Arrow keys
//   GUI <key>           Windows/Command key + another key  (e.g. GUI r)
//   ALT <key>           Alt + key                          (e.g. ALT F4)
//   CTRL <key>          Ctrl + key                         (e.g. CTRL c)
//   SHIFT <key>         Shift + key                        (e.g. SHIFT TAB)
//   CTRL ALT <key>      Three-key combo                    (e.g. CTRL ALT DELETE)
//   CTRL SHIFT <key>    Three-key combo
// ─────────────────────────────────────────────────────────────────────────────

#pragma once
#include "netcore_config.h"

// ── MODE FLAG ────────────────────────────────────────────────────────────────
// true  = dry run (Wokwi / testing) — shows what WOULD be typed on screen
// false = live    (real hardware)   — fires actual USB HID keystrokes
#define DUCKY_DRY_RUN true

// ── LIMITS ───────────────────────────────────────────────────────────────────
#define DUCKY_MAX_LINES   64    // max script lines to load into memory
#define DUCKY_LINE_LEN    80    // max characters per line
#define DUCKY_LOG_LINES   12    // lines shown in the on-screen execution log

// ── EXECUTION STATUS ─────────────────────────────────────────────────────────
enum DuckyStatus {
  DUCKY_OK = 0,         // ran successfully
  DUCKY_ERR_NO_FILE,    // file not found on SD
  DUCKY_ERR_NO_SD,      // SD card not present
  DUCKY_ERR_EMPTY,      // file is empty
  DUCKY_ERR_USB,        // USB HID failed to init (hardware mode only)
};

// ── CALLBACK TYPE ─────────────────────────────────────────────────────────────
// Called after each command executes so the UI can update the screen log.
// 'line' is a short description of what just ran (e.g. "[STRING] hello world")
typedef void (*DuckyLogCallback)(const char* line);

// ── PUBLIC API ────────────────────────────────────────────────────────────────

// Load a .duck file from SD into memory. Returns line count or 0 on error.
int  duckyLoad(const char* filename);

// Execute the currently loaded script.
// logCb is called after each command with a description string.
// Returns a DuckyStatus code.
DuckyStatus duckyRun(DuckyLogCallback logCb);

// Get a loaded line by index (for preview display before running)
const char* duckyGetLine(int index);

// How many lines are currently loaded
int duckyLineCount();


// ─────────────────────────────────────────────────────────────────────────────
// Non-blocking runner (recommended for taskSvc / app ticks)
//
// duckyRun() is a blocking implementation (uses delay()) kept for compatibility.
// For NETCORE rules, use duckyStart()+duckyTick() instead.
//
// Usage:
//   duckyLoad(filename);
//   duckyStart(logCb);
//   while (duckyIsRunning()) duckyTick();  // called from a service tick
//
// duckyTick() executes at most ONE command per call (or returns while waiting).
// ─────────────────────────────────────────────────────────────────────────────
bool duckyStart(DuckyLogCallback logCb);
bool duckyIsRunning();
DuckyStatus duckyGetLastStatus();
void duckyStop();
void duckyTick();
