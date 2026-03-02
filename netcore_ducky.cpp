// ─────────────────────────────────────────────────────────────────────────────
// netcore_ducky.cpp
// Ducky Script parser and executor for NETCORE Field Terminal
//
// HOW IT WORKS:
//   1. duckyLoad()  reads a .duck file from SD into a line buffer
//   2. duckyRun()   iterates lines, parses each command, executes it
//   3. Each command calls the log callback so the UI can update in real time
//
// DRY RUN vs LIVE:
//   Controlled by DUCKY_DRY_RUN in netcore_ducky.h
//   In dry run, all key actions are skipped.
//   In live mode, USB HID keystrokes are sent via USBHIDKeyboard.
// ─────────────────────────────────────────────────────────────────────────────

#include "netcore_ducky.h"
#include "netcore_sd.h"

#if !DUCKY_DRY_RUN
  #include <USB.h>
  #include <USBHIDKeyboard.h>
  static USBHIDKeyboard Keyboard;
  static bool usbReady = false;
#endif

static char _lines[DUCKY_MAX_LINES][DUCKY_LINE_LEN];
static int  _lineCount = 0;

// ─── Helpers ──────────────────────────────────────────────────────────────────

static void trimRight(char* s) {
  int len = strlen(s);
  while (len > 0 && (s[len-1]=='\r'||s[len-1]=='\n'||s[len-1]==' '||s[len-1]=='\t'))
    s[--len] = '\0';
}

// Returns pointer to everything after the first word + whitespace
static const char* afterFirstWord(const char* line) {
  while (*line && *line != ' ') line++;
  while (*line == ' ') line++;
  return line;
}

// Returns pointer to everything after two words + whitespace (for CTRL ALT etc)
static const char* afterTwoWords(const char* line) {
  line = afterFirstWord(line);  // skip first word
  while (*line && *line != ' ') line++;  // skip second word
  while (*line == ' ') line++;
  return line;
}

static bool startsWith(const char* line, const char* keyword) {
  int klen = strlen(keyword);
  return (strncasecmp(line, keyword, klen) == 0) &&
         (line[klen] == '\0' || line[klen] == ' ');
}

// Map key name to USB HID keycode
static uint8_t resolveKey(const char* keyName) {
  if (strlen(keyName) == 1 && isalpha(keyName[0])) return (uint8_t)tolower(keyName[0]);
  if (keyName[0]=='F'||keyName[0]=='f') {
    int n = atoi(keyName + 1);
    if (n >= 1 && n <= 12) return 0xC3 + (n - 1);
  }
  if (strcasecmp(keyName,"ENTER")    ==0||strcasecmp(keyName,"RETURN") ==0) return 0xB0;
  if (strcasecmp(keyName,"ESC")      ==0||strcasecmp(keyName,"ESCAPE") ==0) return 0xB1;
  if (strcasecmp(keyName,"BACKSPACE")==0) return 0xB2;
  if (strcasecmp(keyName,"TAB")      ==0) return 0xB3;
  if (strcasecmp(keyName,"SPACE")    ==0) return ' ';
  if (strcasecmp(keyName,"DELETE")   ==0) return 0xD4;
  if (strcasecmp(keyName,"UP")       ==0) return 0xDA;
  if (strcasecmp(keyName,"DOWN")     ==0) return 0xD9;
  if (strcasecmp(keyName,"LEFT")     ==0) return 0xD8;
  if (strcasecmp(keyName,"RIGHT")    ==0) return 0xD7;
  if (strcasecmp(keyName,"HOME")     ==0) return 0xD2;
  if (strcasecmp(keyName,"END")      ==0) return 0xD5;
  if (strcasecmp(keyName,"PAGEUP")   ==0) return 0xD3;
  if (strcasecmp(keyName,"PAGEDOWN") ==0) return 0xD6;
  if (strcasecmp(keyName,"CAPSLOCK") ==0) return 0xC1;
  return 0;
}

static uint8_t resolveModifier(const char* modName) {
  if (strcasecmp(modName,"CTRL") ==0) return 0x80;
  if (strcasecmp(modName,"SHIFT")==0) return 0x81;
  if (strcasecmp(modName,"ALT")  ==0) return 0x82;
  if (strcasecmp(modName,"GUI")  ==0) return 0x83;
  return 0;
}

// ─── Key senders ──────────────────────────────────────────────────────────────

static void sendString(const char* text) {
#if !DUCKY_DRY_RUN
  if (usbReady) Keyboard.print(text);
#endif
}

static void sendKey(uint8_t keycode) {
#if !DUCKY_DRY_RUN
  if (usbReady) { Keyboard.press(keycode); delay(20); Keyboard.release(keycode); }
#endif
}

static void sendCombo(uint8_t modifier, uint8_t keycode) {
#if !DUCKY_DRY_RUN
  if (usbReady) {
    Keyboard.press(modifier); delay(10);
    Keyboard.press(keycode);  delay(20);
    Keyboard.releaseAll();
  }
#endif
}

static void sendCombo3(uint8_t mod1, uint8_t mod2, uint8_t keycode) {
#if !DUCKY_DRY_RUN
  if (usbReady) {
    Keyboard.press(mod1); Keyboard.press(mod2); delay(10);
    Keyboard.press(keycode); delay(20);
    Keyboard.releaseAll();
  }
#endif
}

// ─── duckyLoad ────────────────────────────────────────────────────────────────
// Accepts full path (e.g. "/PAYLOADS/cache_clear.duck") or bare filename.
// If the path doesn't start with '/', prepends '/'.

int duckyLoad(const char* filename) {
  _lineCount = 0;

  // Build path — accept both "/PAYLOADS/foo.duck" and "foo.duck"
  char path[64];
  if (filename[0] == '/') {
    strncpy(path, filename, sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
  } else {
    snprintf(path, sizeof(path), "/%s", filename);
  }

  if (!sd.exists(path)) return 0;

  File32 f = sd.open(path, O_RDONLY);
  if (!f) return 0;

  char line[DUCKY_LINE_LEN];
  int  li = 0;

  while (f.available() && _lineCount < DUCKY_MAX_LINES) {
    char c = f.read();
    if (c == '\n' || li >= DUCKY_LINE_LEN - 1) {
      line[li] = '\0'; li = 0;
      trimRight(line);
      if (line[0] == '\0') continue;  // skip blank lines
      strncpy(_lines[_lineCount], line, DUCKY_LINE_LEN - 1);
      _lines[_lineCount][DUCKY_LINE_LEN - 1] = '\0';
      _lineCount++;
    } else { line[li++] = c; }
  }
  f.close();
  return _lineCount;
}

// ─── duckyRun ─────────────────────────────────────────────────────────────────

DuckyStatus duckyRun(DuckyLogCallback logCb) {
  if (_lineCount == 0) return DUCKY_ERR_EMPTY;

#if !DUCKY_DRY_RUN
  if (!usbReady) { USB.begin(); Keyboard.begin(); usbReady = true; delay(1000); }
#endif

  char logBuf[DUCKY_LINE_LEN];

  for (int i = 0; i < _lineCount; i++) {
    const char* line = _lines[i];

    // ── REM ────────────────────────────────────────────────────────────────
    if (startsWith(line, "REM")) {
      snprintf(logBuf, sizeof(logBuf), "[REM] %s", afterFirstWord(line));
      if (logCb) logCb(logBuf);
      continue;
    }

    // ── DELAY ──────────────────────────────────────────────────────────────
    if (startsWith(line, "DELAY")) {
      int ms = atoi(afterFirstWord(line));
      snprintf(logBuf, sizeof(logBuf), "[DELAY] %dms", ms);
      if (logCb) logCb(logBuf);
      delay(ms);
      continue;
    }

    // ── STRING ─────────────────────────────────────────────────────────────
    if (startsWith(line, "STRING")) {
      const char* text = afterFirstWord(line);
      snprintf(logBuf, sizeof(logBuf), "[STRING] %s", text);
      if (logCb) logCb(logBuf);
      sendString(text);
      continue;
    }

    // ── ENTER ──────────────────────────────────────────────────────────────
    if (startsWith(line, "ENTER") || startsWith(line, "RETURN")) {
      snprintf(logBuf, sizeof(logBuf), "[KEY] ENTER");
      if (logCb) logCb(logBuf); sendKey(0xB0); continue;
    }

    // ── TAB ────────────────────────────────────────────────────────────────
    if (startsWith(line, "TAB")) {
      snprintf(logBuf, sizeof(logBuf), "[KEY] TAB");
      if (logCb) logCb(logBuf); sendKey(0xB3); continue;
    }

    // ── ESCAPE ─────────────────────────────────────────────────────────────
    if (startsWith(line, "ESCAPE") || startsWith(line, "ESC")) {
      snprintf(logBuf, sizeof(logBuf), "[KEY] ESCAPE");
      if (logCb) logCb(logBuf); sendKey(0xB1); continue;
    }

    // ── BACKSPACE ──────────────────────────────────────────────────────────
    if (startsWith(line, "BACKSPACE")) {
      snprintf(logBuf, sizeof(logBuf), "[KEY] BACKSPACE");
      if (logCb) logCb(logBuf); sendKey(0xB2); continue;
    }

    // ── SPACE ──────────────────────────────────────────────────────────────
    if (startsWith(line, "SPACE")) {
      snprintf(logBuf, sizeof(logBuf), "[KEY] SPACE");
      if (logCb) logCb(logBuf); sendKey(' '); continue;
    }

    // ── Arrow keys ─────────────────────────────────────────────────────────
    if (startsWith(line, "UP"))    { snprintf(logBuf,sizeof(logBuf),"[KEY] UP");    if(logCb)logCb(logBuf); sendKey(0xDA); continue; }
    if (startsWith(line, "DOWN"))  { snprintf(logBuf,sizeof(logBuf),"[KEY] DOWN");  if(logCb)logCb(logBuf); sendKey(0xD9); continue; }
    if (startsWith(line, "LEFT"))  { snprintf(logBuf,sizeof(logBuf),"[KEY] LEFT");  if(logCb)logCb(logBuf); sendKey(0xD8); continue; }
    if (startsWith(line, "RIGHT")) { snprintf(logBuf,sizeof(logBuf),"[KEY] RIGHT"); if(logCb)logCb(logBuf); sendKey(0xD7); continue; }

    // ── DELETE (standalone key, not backspace) ─────────────────────────────
    if (startsWith(line, "DELETE")) {
      snprintf(logBuf, sizeof(logBuf), "[KEY] DELETE");
      if (logCb) logCb(logBuf); sendKey(0xD4); continue;
    }

    // ── Three-modifier combos: CTRL ALT / CTRL SHIFT / etc ─────────────────
    // Must check BEFORE single-modifier so "CTRL ALT" doesn't match as "CTRL"
    bool isThree = (startsWith(line,"CTRL ALT")   || startsWith(line,"CTRL SHIFT") ||
                    startsWith(line,"ALT CTRL")    || startsWith(line,"ALT SHIFT")  ||
                    startsWith(line,"SHIFT CTRL")  || startsWith(line,"SHIFT ALT")  ||
                    startsWith(line,"GUI SHIFT")   || startsWith(line,"GUI ALT")    ||
                    startsWith(line,"GUI CTRL"));

    if (isThree) {
      char copy[DUCKY_LINE_LEN]; strncpy(copy, line, DUCKY_LINE_LEN - 1);
      char* t1 = strtok(copy, " ");
      char* t2 = strtok(NULL,  " ");
      char* t3 = strtok(NULL,  " ");
      if (t1 && t2 && t3) {
        uint8_t m1  = resolveModifier(t1);
        uint8_t m2  = resolveModifier(t2);
        uint8_t key = resolveKey(t3);
        snprintf(logBuf, sizeof(logBuf), "[COMBO] %s+%s+%s", t1, t2, t3);
        if (logCb) logCb(logBuf);
        sendCombo3(m1, m2, key);
      }
      continue;
    }

    // ── Single modifier combos: GUI, ALT, CTRL, SHIFT ──────────────────────
    bool isMod = (startsWith(line,"GUI")  || startsWith(line,"ALT") ||
                  startsWith(line,"CTRL") || startsWith(line,"SHIFT"));
    if (isMod) {
      char copy[DUCKY_LINE_LEN]; strncpy(copy, line, DUCKY_LINE_LEN - 1);
      char* modStr = strtok(copy, " ");
      char* keyStr = strtok(NULL,  " ");
      if (modStr && keyStr) {
        uint8_t mod = resolveModifier(modStr);
        uint8_t key = resolveKey(keyStr);
        snprintf(logBuf, sizeof(logBuf), "[COMBO] %s+%s", modStr, keyStr);
        if (logCb) logCb(logBuf);
        sendCombo(mod, key);
      }
      continue;
    }

    // ── Unknown ────────────────────────────────────────────────────────────
    snprintf(logBuf, sizeof(logBuf), "[???] %s", line);
    if (logCb) logCb(logBuf);
  }

  return DUCKY_OK;
}

// ─── Getters ──────────────────────────────────────────────────────────────────

const char* duckyGetLine(int index) {
  if (index < 0 || index >= _lineCount) return "";
  return _lines[index];
}

int duckyLineCount() { return _lineCount; }


// ─────────────────────────────────────────────────────────────────────────────
// Non-blocking runner (NETCORE-compliant)
// ─────────────────────────────────────────────────────────────────────────────

static bool            _nbRunning = false;
static int             _nbLine = 0;
static uint32_t        _nbWaitUntilMs = 0;
static DuckyStatus     _nbLast = DUCKY_OK;
static DuckyLogCallback _nbLogCb = nullptr;

#if !DUCKY_DRY_RUN
static bool _nbUsbKick = false;
#endif

bool duckyStart(DuckyLogCallback logCb) {
  if (_lineCount == 0) { _nbLast = DUCKY_ERR_EMPTY; return false; }
  _nbRunning = true;
  _nbLine = 0;
  _nbWaitUntilMs = 0;
  _nbLast = DUCKY_OK;
  _nbLogCb = logCb;

#if !DUCKY_DRY_RUN
  // Kick USB init but DO NOT delay(); allow host to enumerate while we wait.
  if (!usbReady) {
    USB.begin();
    Keyboard.begin();
    usbReady = true;
    _nbUsbKick = true;
    _nbWaitUntilMs = millis() + 1000; // emulate prior delay(1000) non-blocking
  } else {
    _nbUsbKick = false;
  }
#endif
  return true;
}

bool duckyIsRunning() { return _nbRunning; }

DuckyStatus duckyGetLastStatus() { return _nbLast; }

void duckyStop() {
  _nbRunning = false;
  _nbLine = 0;
  _nbWaitUntilMs = 0;
  _nbLogCb = nullptr;
}

static void nbLogf(const char* fmt, const char* arg) {
  if (!_nbLogCb) return;
  char buf[DUCKY_LINE_LEN];
  if (arg) snprintf(buf, sizeof(buf), fmt, arg);
  else snprintf(buf, sizeof(buf), "%s", fmt);
  _nbLogCb(buf);
}

void duckyTick() {
  if (!_nbRunning) return;

  // Non-blocking wait gate (handles initial USB settle + DELAY commands)
  if (_nbWaitUntilMs != 0 && (int32_t)(millis() - _nbWaitUntilMs) < 0) return;
  _nbWaitUntilMs = 0;

  if (_nbLine >= _lineCount) {
    _nbLast = DUCKY_OK;
    duckyStop();
    return;
  }

  const char* line = _lines[_nbLine];

  // REM
  if (startsWith(line, "REM")) {
    nbLogf("[REM] %s", afterFirstWord(line));
    _nbLine++;
    return;
  }

  // DELAY (non-blocking)
  if (startsWith(line, "DELAY")) {
    int ms = atoi(afterFirstWord(line));
    char b[DUCKY_LINE_LEN];
    snprintf(b, sizeof(b), "[DELAY] %dms", ms);
    if (_nbLogCb) _nbLogCb(b);
    _nbWaitUntilMs = millis() + (uint32_t)((ms < 0) ? 0 : ms);
    _nbLine++;
    return;
  }

  // STRING
  if (startsWith(line, "STRING")) {
    const char* text = afterFirstWord(line);
    nbLogf("[STRING] %s", text);

#if !DUCKY_DRY_RUN
    Keyboard.print(text);
#endif
    _nbLine++;
    return;
  }

  // ENTER
  if (startsWith(line, "ENTER")) {
    if (_nbLogCb) _nbLogCb("[ENTER]");
#if !DUCKY_DRY_RUN
    Keyboard.write(KEY_RETURN);
#endif
    _nbLine++;
    return;
  }

  // TAB
  if (startsWith(line, "TAB")) {
    if (_nbLogCb) _nbLogCb("[TAB]");
#if !DUCKY_DRY_RUN
    Keyboard.write(KEY_TAB);
#endif
    _nbLine++;
    return;
  }

  // Default: treat as key chord line (e.g. CTRL ALT DEL)
  // Reuse existing parse+exec by calling duckyExecLine() helper if present.
  // In this codebase, the blocking runner implements chord parsing inline.
  // So we perform minimal mapping here:
  if (_nbLogCb) _nbLogCb(line);

#if !DUCKY_DRY_RUN
  // Very small subset: single key names only (A, F1, ESC, etc.)
  uint8_t k = resolveKey(line);
  if (k != 0) Keyboard.write(k);
#endif

  _nbLine++;
}
