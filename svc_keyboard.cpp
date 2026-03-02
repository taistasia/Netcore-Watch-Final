#include "svc_keyboard.h"

#include "netcore_theme.h"
#include "netcore_ui.h"       // for BODY_Y etc
#include "netcore_config.h"
#include "svc_input.h"
#include "netcore_buttons.h"

// tft lives in sketch.ino
extern Adafruit_ILI9341 tft;

// Layout (within the app body area)
static const int KB_X = 10;
static const int KB_W = 240 - 20;
static const int KB_LINE_H = 12;
static const int KB_Y = BODY_Y + 18;

static const uint32_t KB_REPEAT_MS_START = 240;
static const uint32_t KB_REPEAT_MS_FAST  = 80;

// Charset ordering tuned for fast rotary usage.
static const char* SETS[] = {
  "abcdefghijklmnopqrstuvwxyz",
  "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
  "0123456789",
  "!@#$%^&*()-_=+[]{};:,.?/\\|\"'`"
};
static const int SET_COUNT = (int)(sizeof(SETS)/sizeof(SETS[0]));

enum KbSpecial {
  KBSP_NONE = 0,
  KBSP_SPACE,
  KBSP_BKSP,
  KBSP_OK
};

static bool s_active=false, s_finished=false, s_cancelled=false;
static bool s_mask=false;
static char  s_title[16];

static char*  s_target=nullptr;
static uint8_t s_targetMax=0;

static char   s_work[64];
static uint8_t s_len=0;

static int s_set=0;
static int s_idx=0;
static KbSpecial s_sp=KBSP_NONE;

// Dirty tracking
static uint32_t s_lastDrawMs=0;
static bool s_dirtyAll=true;
static bool s_dirtyLine=true;
static bool s_dirtyStrip=true;

// Backspace repeat
static bool s_repArmed=false;
static uint32_t s_repStartMs=0;
static uint32_t s_repLastMs=0;

static void _markAllDirty() {
  s_dirtyAll = true;
  s_dirtyLine = true;
  s_dirtyStrip = true;
}

static void _clearArea() {
  // Only clear the keyboard region, not full screen.
  tft.fillRect(KB_X, KB_Y, KB_W, KB_LINE_H*5, COL_BG());
}

static void _drawLine(int row, const char* label, const char* value, bool invert=false) {
  int y = KB_Y + row*KB_LINE_H;
  tft.fillRect(KB_X, y, KB_W, KB_LINE_H, invert ? COL_HILITE() : COL_BG());
  tft.setTextSize(1);
  tft.setTextColor(invert ? COL_BG() : COL_FG(), invert ? COL_HILITE() : COL_BG());
  tft.setCursor(KB_X, y);
  if (label) { tft.print(label); }
  if (value) { tft.print(value); }
}

static void _renderAll() {
  _clearArea();

  // Title
  char titleLine[24];
  snprintf(titleLine, sizeof(titleLine), "[%s]", s_title);
  _drawLine(0, titleLine, nullptr, true);

  // Value line (masked optional)
  char buf[80];
  if (s_mask) {
    int n = (s_len > 50) ? 50 : s_len;
    for (int i=0;i<n;i++) buf[i] = '*';
    buf[n] = 0;
  } else {
    snprintf(buf, sizeof(buf), "%s", s_work);
  }
  _drawLine(1, "", buf, false);

  // Caret / length
  char meta[32];
  snprintf(meta, sizeof(meta), "len:%d/%d", (int)s_len, (int)(s_targetMax? (s_targetMax-1):0));
  _drawLine(2, meta, nullptr, false);

  // Selector strip
  // Show current set name and current token
  const char* setName = (s_set==0?"abc": s_set==1?"ABC": s_set==2?"123":"sym");
  char token[8] = {0};
  if (s_sp == KBSP_SPACE) snprintf(token, sizeof(token), "SPC");
  else if (s_sp == KBSP_BKSP) snprintf(token, sizeof(token), "BKSP");
  else if (s_sp == KBSP_OK) snprintf(token, sizeof(token), "OK");
  else {
    char c = SETS[s_set][s_idx];
    token[0] = c; token[1] = 0;
  }
  char strip[64];
  snprintf(strip, sizeof(strip), "%s  [%s]", setName, token);
  _drawLine(3, strip, nullptr, true);
  _drawLine(4, "SEL:add  hold:bksp  BACK:cancel  hold:OK", nullptr, false);

  s_dirtyAll = s_dirtyLine = s_dirtyStrip = false;
}

static void _appendChar(char c) {
  if (s_len + 1 >= sizeof(s_work)) return;
  if (s_targetMax && s_len + 1 >= s_targetMax) return;
  s_work[s_len++] = c;
  s_work[s_len] = 0;
  s_dirtyLine = true;
}

static void _backspaceOnce() {
  if (s_len == 0) return;
  s_len--;
  s_work[s_len] = 0;
  s_dirtyLine = true;
}

static void _confirm() {
  if (!s_target || s_targetMax == 0) {
    s_cancelled = true;
  } else {
    // Copy out.
    uint8_t n = s_len;
    if (n >= s_targetMax) n = s_targetMax - 1;
    for (uint8_t i=0;i<n;i++) s_target[i] = s_work[i];
    s_target[n] = 0;
    s_finished = true;
  }
  s_active = false;
}

static void _cancel() {
  s_cancelled = true;
  s_active = false;
}

void kbStart(const char* title, char* targetBuffer, uint8_t maxLen, bool maskInput) {
  s_active = true;
  s_finished = false;
  s_cancelled = false;

  s_target = targetBuffer;
  s_targetMax = maxLen;
  s_mask = maskInput;

  memset(s_work, 0, sizeof(s_work));
  s_len = 0;
  s_set = 0;
  s_idx = 0;
  s_sp = KBSP_NONE;

  memset(s_title, 0, sizeof(s_title));
  if (title) {
    strncpy(s_title, title, sizeof(s_title)-1);
  } else {
    strncpy(s_title, "INPUT", sizeof(s_title)-1);
  }

  s_repArmed = false;
  s_repStartMs = s_repLastMs = 0;
  _markAllDirty();
  _renderAll();
}

bool kbActive() { return s_active; }
bool kbFinished() { return s_finished; }
bool kbCancelled() { return s_cancelled; }

static void _advanceToken(int dir) {
  // token sequence: charset chars, then SPACE, BKSP, OK.
  int setLen = (int)strlen(SETS[s_set]);
  int total = setLen + 3;
  int cur;
  if (s_sp == KBSP_NONE) cur = s_idx;
  else if (s_sp == KBSP_SPACE) cur = setLen;
  else if (s_sp == KBSP_BKSP) cur = setLen+1;
  else cur = setLen+2;

  cur += dir;
  if (cur < 0) cur = total-1;
  if (cur >= total) cur = 0;

  if (cur < setLen) {
    s_sp = KBSP_NONE;
    s_idx = cur;
  } else if (cur == setLen) s_sp = KBSP_SPACE;
  else if (cur == setLen+1) s_sp = KBSP_BKSP;
  else s_sp = KBSP_OK;
  s_dirtyStrip = true;
}

static void _cycleSet() {
  s_set = (s_set + 1) % SET_COUNT;
  s_idx = 0;
  s_sp = KBSP_NONE;
  s_dirtyStrip = true;
}

void kbTick() {
  if (!s_active) return;

  // Rotary with acceleration
  int up = inputRotaryUp();
  int dn = inputRotaryDown();
  if (up) _advanceToken(+up);
  if (dn) _advanceToken(-dn);

  // Cycle charset on select-hold event (premium shortcut) if not already repeating
  if (inputHoldSelect()) {
    _cycleSet();
    // Also arm backspace repeat if currently on BKSP
  }

  // Back button: tap cancels, hold confirms
  if (Buttons::back.consume()) {
    _cancel();
    return;
  }
  if (inputHoldBack()) {
    _confirm();
    return;
  }

  // SELECT tap
  if (Buttons::select.consume()) {
    if (s_sp == KBSP_SPACE) _appendChar(' ');
    else if (s_sp == KBSP_BKSP) _backspaceOnce();
    else if (s_sp == KBSP_OK) _confirm();
    else _appendChar(SETS[s_set][s_idx]);
  }

  // Non-blocking backspace repeat while SELECT held down AND BKSP selected
  if (inputIsSelectDown() && s_sp == KBSP_BKSP) {
    uint32_t now = millis();
    if (!s_repArmed) {
      s_repArmed = true;
      s_repStartMs = now;
      s_repLastMs = now;
    } else {
      uint32_t elapsed = now - s_repStartMs;
      uint32_t interval = (elapsed > 900) ? KB_REPEAT_MS_FAST : KB_REPEAT_MS_START;
      if (now - s_repLastMs >= interval) {
        s_repLastMs = now;
        _backspaceOnce();
      }
    }
  } else {
    s_repArmed = false;
  }

  if (s_dirtyAll || s_dirtyLine || s_dirtyStrip) {
    _renderAll();
  }
}
