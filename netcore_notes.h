#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// netcore_notes.h — Notes storage backend (for Notes app)
// Goal: provide a safe, fixed-memory notes buffer with optional SD persistence.
// Constraints: no heap churn in hot paths; I/O only on explicit save/load calls.
// ─────────────────────────────────────────────────────────────────────────────
#include "netcore_config.h"
#include <stdint.h>
#include <stdbool.h>

#define NOTES_MAX_LINES  64
#define NOTES_LINE_LEN   48

// In-RAM ring buffer (fixed).
int  notesCount();                 // number of valid lines [0..NOTES_MAX_LINES]
const char* notesLine(int i);      // i=0 oldest, returns nullptr if OOB

void notesClear();
bool notesAppend(const char* line); // truncates to NOTES_LINE_LEN-1

// Persistence on SD (if present). Uses /NETCORE/notes.txt
bool notesLoadFromSD();            // clears RAM first
bool notesSaveToSD();              // writes all lines

// Convenience: add a timestamped line (YYYY-MM-DD HH:MM) if timeSvc available
bool notesAppendTimestamped(const char* msg);
