// ─────────────────────────────────────────────────────────────────────────────
// svc_perf.cpp  —  Performance Guard Implementation
//
// All counters are uint32_t; no heap allocation; no String.
// Thread safety: all state is written only from loop() (single Arduino task).
// ─────────────────────────────────────────────────────────────────────────────
#include "svc_perf.h"

// ── Loop stall tracking ───────────────────────────────────────────────────────
static uint32_t _maxStallMs     = 0;      // worst stall in current window
static uint32_t _lastLoopMs     = 0;      // millis() at last loop end
static uint32_t _windowStartMs  = 0;      // when the stall window opened
static uint32_t _lastLoopStallMs = 0;     // stall measured in most recent loop

// ── Loop counter ──────────────────────────────────────────────────────────────
static uint32_t _loopCount = 0;

// ── Draw accounting ───────────────────────────────────────────────────────────
static uint32_t _drawCount       = 0;     // total draw calls ever
static uint32_t _drawStartMs     = 0;     // millis() when current draw began
static uint32_t _drawTotalMsWin  = 0;     // total draw time in fps window
static uint32_t _drawCallsWin    = 0;     // draw calls in fps window
static uint32_t _fpsWindowStartMs = 0;    // when the fps window opened
static float    _fps             = 0.0f;  // last computed fps
static uint32_t _loopTotalMsWin  = 0;     // sum of loop durations in fps window
static uint8_t  _cpuPercent      = 0;

// ── Public API ────────────────────────────────────────────────────────────────

void perfInit() {
  uint32_t now   = millis();
  _maxStallMs    = 0;
  _lastLoopMs    = now;
  _windowStartMs = now;
  _fpsWindowStartMs = now;
  _loopCount     = 0;
  _drawCount     = 0;
  _fps           = 0.0f;
  _cpuPercent    = 0;
}

uint32_t perfLoopBegin() {
  return millis();
}

void perfLoopEnd(uint32_t t0) {
  uint32_t now     = millis();
  uint32_t elapsed = now - t0;

  _loopCount++;
  _lastLoopStallMs = elapsed;

  // Update stall window
  if (elapsed > _maxStallMs) _maxStallMs = elapsed;
  if (now - _windowStartMs > PERF_WINDOW_MS) {
    _maxStallMs    = elapsed;    // reset window; seed with current
    _windowStartMs = now;
  }

  // Accumulate for CPU% estimate
  _loopTotalMsWin += elapsed;
  if (_drawTotalMsWin > _loopTotalMsWin) _drawTotalMsWin = _loopTotalMsWin; // clamp

  // FPS window rollover
  uint32_t fpsWinElapsed = now - _fpsWindowStartMs;
  if (fpsWinElapsed >= PERF_FPS_WINDOW_MS) {
    if (fpsWinElapsed > 0)
      _fps = (float)_drawCallsWin * 1000.0f / (float)fpsWinElapsed;
    if (_loopTotalMsWin > 0)
      _cpuPercent = (uint8_t)((_drawTotalMsWin * 100UL) / _loopTotalMsWin);

    _drawCallsWin    = 0;
    _drawTotalMsWin  = 0;
    _loopTotalMsWin  = 0;
    _fpsWindowStartMs = now;
  }

  _lastLoopMs = now;
}

void perfDrawBegin() {
  _drawStartMs = millis();
}

void perfDrawEnd() {
  uint32_t dt = millis() - _drawStartMs;
  _drawCount++;
  _drawCallsWin++;
  _drawTotalMsWin += dt;
}

bool perfShouldSkipOptionals() {
  return (_lastLoopStallMs > PERF_STALL_SKIP_MS);
}

uint32_t perfGetMaxStallMs() { return _maxStallMs; }
float    perfGetFps()         { return _fps; }
uint32_t perfGetDrawCount()   { return _drawCount; }
uint32_t perfGetLoopCount()   { return _loopCount; }
uint8_t  perfGetCpuPercent()  { return _cpuPercent; }
