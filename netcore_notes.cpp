#include "netcore_notes.h"
#include "netcore_sd.h"
#include "netcore_time.h"
#include <string.h>
#include <time.h>

static char s_lines[NOTES_MAX_LINES][NOTES_LINE_LEN];
static int  s_count = 0;

int notesCount() { return s_count; }

const char* notesLine(int i) {
  if (i < 0 || i >= s_count) return nullptr;
  return s_lines[i];
}

void notesClear() {
  for (int i = 0; i < NOTES_MAX_LINES; i++) s_lines[i][0] = '\0';
  s_count = 0;
}

static void copyLine(char* dst, const char* src) {
  if (!dst) return;
  if (!src) { dst[0] = '\0'; return; }
  int i = 0;
  for (; i < NOTES_LINE_LEN - 1 && src[i]; i++) dst[i] = src[i];
  dst[i] = '\0';
}

bool notesAppend(const char* line) {
  if (!line) return false;

  if (s_count < NOTES_MAX_LINES) {
    copyLine(s_lines[s_count], line);
    s_count++;
    return true;
  }

  // ring: drop oldest
  for (int i = 1; i < NOTES_MAX_LINES; i++) {
    memcpy(s_lines[i-1], s_lines[i], NOTES_LINE_LEN);
  }
  copyLine(s_lines[NOTES_MAX_LINES-1], line);
  return true;
}

bool notesAppendTimestamped(const char* msg) {
  if (!msg) return false;

  tm t;
  bool ntp = timeSvcGetLocal(&t);

  char stamp[24];
  if (ntp) {
    // YYYY-MM-DD HH:MM
    snprintf(stamp, sizeof(stamp), "%04d-%02d-%02d %02d:%02d",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
             t.tm_hour, t.tm_min);
  } else {
    // Fallback to mock clock (hour/min/sec available)
    snprintf(stamp, sizeof(stamp), "MOCK %02d:%02d",
             timeSvcMockHour(), timeSvcMockMin());
  }

  char line[NOTES_LINE_LEN];
  // Keep within NOTES_LINE_LEN
  snprintf(line, sizeof(line), "%s %s", stamp, msg);
  return notesAppend(line);
}

static const char* NOTES_DIR  = "/NETCORE";
static const char* NOTES_PATH = "/NETCORE/notes.txt";

bool notesLoadFromSD() {
  if (!sdPresent) return false;
  notesClear();

  if (!sd.exists(NOTES_DIR)) {
    // no directory yet
    return true;
  }

  SdFile f;
  if (!f.open(NOTES_PATH, O_READ)) return false;

  char buf[NOTES_LINE_LEN];
  while (f.fgets(buf, sizeof(buf)) > 0) {
    // trim newline
    int len = strlen(buf);
    while (len > 0 && (buf[len-1] == '\r' || buf[len-1] == '\n')) buf[--len] = '\0';
    if (len == 0) continue;
    notesAppend(buf);
    if (s_count >= NOTES_MAX_LINES) break;
  }
  f.close();
  return true;
}

bool notesSaveToSD() {
  if (!sdPresent) return false;

  if (!sd.exists(NOTES_DIR)) {
    if (!sd.mkdir(NOTES_DIR)) return false;
  }

  SdFile f;
  if (!f.open(NOTES_PATH, O_WRITE | O_CREAT | O_TRUNC)) return false;

  for (int i = 0; i < s_count; i++) {
    f.print(s_lines[i]);
    f.print("\n");
  }
  f.close();
  return true;
}
