// ─────────────────────────────────────────────────────────────────────────────
// netcore_boot.cpp  —  Cinematic boot sequence for NETCORE Field Terminal
//
// ANIMATION PHILOSOPHY:
//   Every phase has a clear dramatic purpose. Nothing moves at constant speed.
//   Easing is applied to all motion so the device feels physical, not robotic.
//   Glitch moments signal system state changes — corruption clearing to signal.
//   The boot screen tells a story: dead → awakening → calibrating → ready.
// ─────────────────────────────────────────────────────────────────────────────

#include "netcore_boot.h"
#include "netcore_settings.h"
#include "netcore_sd.h"

// ─── Easing library ───────────────────────────────────────────────────────────
// All timing uses these — never hardcode linear delays in animation loops.

// Ease-out cubic: starts fast, decelerates. Use for things arriving/settling.
static inline int easeOutMs(int step, int total, int fastMs, int slowMs) {
  float t = 1.0f - (float)step / (float)(total > 0 ? total : 1);
  return (int)(fastMs + (slowMs - fastMs) * t * t * t);
}

// Ease-in cubic: starts slow, accelerates. Use for things launching/escaping.
static inline int easeInMs(int step, int total, int slowMs, int fastMs) {
  float t = (float)step / (float)(total > 0 ? total : 1);
  return (int)(fastMs + (slowMs - fastMs) * (1.0f - t) * (1.0f - t));
}

// Ease-in-out: slow start, fast middle, slow end. Use for full traversals.
static inline int easeInOutMs(int step, int total, int slowMs, int fastMs) {
  float t = (float)step / (float)(total > 0 ? total : 1);
  float ease = t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;
  return (int)(fastMs + (slowMs - fastMs) * (1.0f - ease));
}

// ─── Cinematic primitives ─────────────────────────────────────────────────────

// Scatter random noise pixels — simulates digital corruption or signal noise.
// Call with COL_BG() to erase the noise afterward.
static void noiseScatter(int x, int y, int w, int h, int count, uint16_t col) {
  for (int i = 0; i < count; i++) {
    tft.drawPixel(x + random(w), y + random(h), col);
  }
}

// Horizontal line sweep with 3-layer phosphor trail.
// Layer 0 (leading): col (full bright)
// Layer 1 (trail-1): COL_DARK() (fading)
// Layer 2 (trail-2): COL_BG()  (erased — screen returns to background)
// midY = center, r = current radius. Call with increasing r.
static void phosphorLine(int midY, int r, uint16_t leadCol) {
  if (r > 2) {
    tft.drawFastHLine(0, midY - r + 2, W, COL_BG());
    tft.drawFastHLine(0, midY + r - 2, W, COL_BG());
  }
  if (r > 1) {
    tft.drawFastHLine(0, midY - r + 1, W, COL_DARK());
    tft.drawFastHLine(0, midY + r - 1, W, COL_DARK());
  }
  tft.drawFastHLine(0, midY - r, W, leadCol);
  tft.drawFastHLine(0, midY + r, W, leadCol);
}

// Single horizontal sweep — leading bright pixel, dim trail, BG eraser.
// Used for separator lines and progress sweeps.
static void sweepLine(int y, int x0, int x1, uint16_t leadCol, int msPerStep) {
  for (int x = x0; x <= x1; x += 2) {
    if (x + 4 < x1) tft.drawPixel(x + 4, y, leadCol);
    if (x + 2 < x1) tft.drawPixel(x + 2, y, COL_DIM());
    tft.drawPixel(x,     y, COL_DARK());
    if (x - 2 >= x0) tft.drawPixel(x - 2, y, COL_BG());
    delay(msPerStep);
  }
  // Solidify the completed line at DIM
  tft.drawFastHLine(x0, y, x1 - x0, COL_DARK());
}

// ─── Phase 1: CRT power-on ────────────────────────────────────────────────────
// Screen wakes from complete black. Scanlines bloom outward from center.
// Eases in (slow warm-up at center → accelerates → slams to edges).
// Ends with a collapse-to-line CRT shutdown impression before clearing.

static void phase_crtWakeup() {
  tft.fillScreen(ILI9341_BLACK);
  int midY = H / 2;
  int total = midY + 4;

  for (int r = 0; r <= total; r++) {
    phosphorLine(midY, r, COL_FG());
    // Ease-in: warm-up slow (20ms), then ramps to 1ms at edges
    delay(easeInMs(r, total, 20, 1));
  }

  delay(55);

  // Contract — screen collapses back to a center line (old CRT off)
  for (int r = total; r >= 0; r -= 3) {
    tft.drawFastHLine(0, midY - r, W, ILI9341_BLACK);
    tft.drawFastHLine(0, midY + r, W, ILI9341_BLACK);
    delay(2);
  }
  tft.fillScreen(ILI9341_BLACK);

  // Single re-ignition flash — device catches, restarts
  delay(30);
  tft.drawFastHLine(0, midY, W, COL_HILITE()); delay(15);
  tft.drawFastHLine(0, midY, W, COL_FG());     delay(20);
  tft.drawFastHLine(0, midY, W, COL_DARK());   delay(18);
  tft.drawFastHLine(0, midY, W, ILI9341_BLACK);delay(12);

  tft.fillScreen(COL_BG());
  delay(35);
}

// ─── Phase 2: Frame materialises ─────────────────────────────────────────────
// Corner brackets arrive one-by-one in TL→TR→BL→BR order (targeting lock).
// Inner rect appears last as a contained "system border locked" confirmation.

static void phase_frameLock() {
  const int arm = 16;
  const uint16_t fc = COL_DIM();

  // TL bracket
  tft.drawFastHLine(2, 2, arm, fc);           delay(18);
  tft.drawFastVLine(2, 2, arm, fc);           delay(18);
  // TR bracket
  tft.drawFastHLine(W - arm - 2, 2, arm, fc); delay(18);
  tft.drawFastVLine(W - 3, 2, arm, fc);       delay(18);
  // BL bracket
  tft.drawFastHLine(2, H - 3, arm, fc);       delay(18);
  tft.drawFastVLine(2, H - arm - 3, arm, fc); delay(18);
  // BR bracket
  tft.drawFastHLine(W - arm - 2, H - 3, arm, fc); delay(18);
  tft.drawFastVLine(W - 3, H - arm - 3, arm, fc); delay(18);

  // Brief flicker on the brackets — signal lock confirmation
  delay(25);
  tft.drawRect(1, 1, W - 2, H - 2, COL_BG());   delay(12);
  tft.drawRect(1, 1, W - 2, H - 2, COL_DIM());  delay(20);
  tft.drawRect(3, 3, W - 6, H - 6, COL_DARK()); delay(30);
}

// ─── Phase 3: Logo assembles ──────────────────────────────────────────────────
// Each element of the crosshair logo draws with deliberate pauses.
// The crosshair arms extend pixel-by-pixel from center outward.
// A brief "bloom" flash signals calibration complete.

static void phase_logoAssemble(int cx, int cy) {
  // Outer box — appears with a stutter
  tft.drawRect(cx - 22, cy - 22, 44, 44, COL_DARK()); delay(18);
  tft.drawRect(cx - 22, cy - 22, 44, 44, COL_BG());   delay(10);
  tft.drawRect(cx - 22, cy - 22, 44, 44, COL_DIM());  delay(30);

  // Inner box
  tft.drawRect(cx - 20, cy - 20, 40, 40, COL_DARK()); delay(22);

  // Circle — appears instantly (single draw call) then pause for drama
  tft.drawCircle(cx, cy, 12, COL_DIM()); delay(55);

  // Crosshair arms extend outward from center — pixel by pixel
  for (int d = 1; d <= 16; d++) {
    tft.drawPixel(cx - d, cy, COL_DARK());
    tft.drawPixel(cx + d, cy, COL_DARK());
    tft.drawPixel(cx, cy - d, COL_DARK());
    tft.drawPixel(cx, cy + d, COL_DARK());
    delay(easeOutMs(d, 16, 2, 7));  // fast start, slows as they reach box edge
  }
  delay(35);

  // Center dot — the "lock" moment. Snaps in with weight.
  tft.fillCircle(cx, cy, 2, COL_HILITE());
  delay(18);
  // Satellite dot
  tft.fillCircle(cx + 12, cy - 10, 2, COL_HILITE());
  delay(45);

  // Bloom: circle flashes bright then settles (calibration pulse)
  tft.drawCircle(cx, cy, 12, COL_HILITE()); delay(35);
  tft.drawCircle(cx, cy, 12, COL_FG());     delay(20);
  tft.drawCircle(cx, cy, 12, COL_DIM());    delay(25);
}

// ─── Phase 4: Glitch burst ────────────────────────────────────────────────────
// Signal corruption artifact — random noise pixels appear then clear.
// Signals transition between logo calibration and text reveal.
// Adds unpredictability that makes the boot feel alive.

static void phase_glitchBurst(int cx, int cy) {
  int gx = cx - 45, gy = cy - 25, gw = 90, gh = 50;

  // Two waves: appear → partial clear → appear again → full clear
  noiseScatter(gx, gy, gw, gh, 14, COL_DARK());  delay(30);
  noiseScatter(gx, gy, gw, gh,  7, COL_BG());    delay(18);
  noiseScatter(gx, gy, gw, gh, 10, COL_DIM());   delay(25);
  noiseScatter(gx, gy, gw, gh, 20, COL_BG());    delay(20);
}

// ─── Phase 5: Title reveal ────────────────────────────────────────────────────
// "NETCORE" types in with eased timing and a motion-blur dim ghost effect.
// First letters arrive fast (system eager), last letter slows (dramatic pause).
// Subtitle fades in dim → then brightens one step.

static void phase_titleReveal(int x, int y) {
  const char* title = "NETCORE";
  int len = 7;

  tft.setTextSize(2);

  for (int i = 0; i < len; i++) {
    // Motion-blur: briefly render previous char dim, then restore bright
    if (i > 0) {
      tft.setTextColor(COL_DIM(), COL_BG());
      tft.setCursor(x + (i - 1) * 12, y);
      tft.print(title[i - 1]);
      delay(10);
      tft.setTextColor(COL_FG(), COL_BG());
      tft.setCursor(x + (i - 1) * 12, y);
      tft.print(title[i - 1]);
    }

    // Type current letter
    tft.setTextColor(COL_FG(), COL_BG());
    tft.setCursor(x + i * 12, y);
    tft.print(title[i]);

    // Ease-in: first chars 35ms, ramps to 95ms at end (anticipation build)
    delay(easeInMs(i, len - 1, 35, 95));
  }

  delay(50);

  // Subtitle — appears dark, then brightens
  tft.setTextSize(1);
  tft.setTextColor(COL_DARK(), COL_BG());
  tft.setCursor(x + 2, y + 18);
  tft.print("FIELD TERMINAL  v"); tft.print(FW_VERSION);
  delay(25);
  tft.setTextColor(COL_DIM(), COL_BG());
  tft.setCursor(x + 2, y + 18);
  tft.print("FIELD TERMINAL  v"); tft.print(FW_VERSION);
  delay(40);
}

// ─── Phase 6: System checks ───────────────────────────────────────────────────
// Each system label types in, then pauses before revealing its status.
// Failed items flicker before settling — they "tried and failed."
// Pass items snap in cleanly.

static void phase_systemChecks(int x, int y) {
  const char* labels[] = { "DISPLAY", "INPUT", "UI CORE", "RADIO", "CARTRIDGE" };
  const bool  pass[]   = { true, true, true, false, cartLoaded };

  for (int i = 0; i < 5; i++) {
    tft.setTextSize(1);
    tft.setTextColor(COL_DIM(), COL_BG());
    tft.setCursor(x, y + i * 14);

    // Type the label in with consistent 20ms/char
    for (int c = 0; labels[i][c]; c++) {
      tft.print(labels[i][c]);
      delay(20);
    }

    // Suspense: cursor dots appear
    tft.setTextColor(COL_DARK(), COL_BG());
    tft.print("...");
    delay(90);

    // Erase dots
    int dotX = x + strlen(labels[i]) * 6;
    tft.fillRect(dotX, y + i * 14, 18, 8, COL_BG());
    tft.setCursor(dotX, y + i * 14);

    if (pass[i]) {
      // Clean snap-in
      tft.setTextColor(COL_HILITE(), COL_BG());
      tft.print(" [OK]");
      delay(22);
    } else {
      // Failure: flicker then settle
      tft.setTextColor(COL_DIM(), COL_BG()); tft.print(" [--]"); delay(45);
      tft.setCursor(dotX, y + i * 14);
      tft.setTextColor(COL_BG(),  COL_BG()); tft.print(" [--]"); delay(28);
      tft.setCursor(dotX, y + i * 14);
      tft.setTextColor(COL_DIM(), COL_BG()); tft.print(" [--]"); delay(22);
    }
  }
}

// ─── Phase 7: Progress bar ────────────────────────────────────────────────────
// Segmented fill with ease-in timing — starts fast, slows toward 100%.
// Two deliberate stutters simulate real loading (hesitation = believability).
// Final segment holds for a beat before boot completes.

static void phase_progressBar(int x, int y, int w, int h) {
  tft.drawRect(x - 1, y - 1, w + 2, h + 2, COL_DIM());

  const int SEGS = 24;
  int segW = (w - 2) / SEGS;

  for (int s = 0; s < SEGS; s++) {
    int sx = x + 1 + s * segW;
    int sw = (s == SEGS - 1) ? (x + w - 1) - sx : segW - 1;

    tft.fillRect(sx, y + 1, sw, h - 2, COL_HILITE());

    // Ease-in: fast start (12ms) slows to 50ms at end
    delay(easeInMs(s, SEGS - 1, 12, 50));

    // Stutter moments — system pausing mid-load
    if (s == 7) {
      delay(140);  // brief freeze
      // Flash the last segment as if retrying
      tft.fillRect(sx, y + 1, sw, h - 2, COL_DIM());  delay(35);
      tft.fillRect(sx, y + 1, sw, h - 2, COL_HILITE()); delay(20);
    }
    if (s == 17) {
      delay(90);   // second freeze, shorter
    }
  }
}

// ─── Main entry point ─────────────────────────────────────────────────────────

void runBootScreen() {
  // Phase 1: CRT wakes from black
  phase_crtWakeup();

  // Phase 2: Targeting frame locks in
  phase_frameLock();

  // Phase 3: Logo calibrates
  int logoCX = W / 2;
  int logoCY = 52;
  phase_logoAssemble(logoCX, logoCY);

  // Phase 4: Signal glitch before text (system "thinking")
  phase_glitchBurst(logoCX, logoCY);

  // Phase 5: Title types in
  int titX = (W / 2) - 42;
  int titY = 102;
  phase_titleReveal(titX, titY);

  // Phase 5b: Separator sweeps under title
  sweepLine(titY + 32, 4, W - 4, COL_FG(), 3);
  delay(18);

  // Phase 6: System check lines
  phase_systemChecks(8, titY + 44);

  // Phase 7: Boot progress bar
  delay(55);
  int barY = titY + 44 + 5 * 14 + 6;
  phase_progressBar(4, barY, W - 8, 10);

  // Phase 8: Boot complete — triple flash (more intense than double)
  delay(90);
  tft.invertDisplay(true);  delay(45);
  tft.invertDisplay(false); delay(30);
  tft.invertDisplay(true);  delay(25);
  tft.invertDisplay(false); delay(55);
}