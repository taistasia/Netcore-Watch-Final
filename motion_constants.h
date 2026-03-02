// motion_constants.h
// Centralised motion language — durations + easing semantics.
// All tween call sites reference these; change timing policy here only.
#pragma once
#include "svc_anim.h"

// ── Duration constants (ms) ─────────────────────────────────────────────────
#define MOTION_DUR_MICRO       80u    // micro-interaction (button tap feedback)
#define MOTION_DUR_FAST       150u    // quick snap (highlight shift, select pulse)
#define MOTION_DUR_NORMAL     300u    // standard transition (app slide, panel move)
#define MOTION_DUR_SLIDE      400u    // app enter/exit slide
#define MOTION_DUR_SLOW       600u    // deliberate motion (settings toggle)
#define MOTION_DUR_BREATHE    900u    // breathing pulse (menu hover FX)
#define MOTION_DUR_SWEEP      850u    // cursor scan-sweep (menu terminal FX)
#define MOTION_DUR_DEMO      1800u    // ANIM demo progress bar

// ── Easing semantics (map intent to AnimEase enum) ──────────────────────────
#define MOTION_EASE_SNAP       EASE_OUT_CUBIC    // quick settle (cursor, highlight)
#define MOTION_EASE_SLIDE      EASE_OUT_EXPO     // slide in/out (app transitions)
#define MOTION_EASE_BREATHE    EASE_INOUT_SINE   // gentle pulse (breathing FX)
#define MOTION_EASE_BOUNCE     EASE_OUT_QUAD     // soft decel (micro-interactions)
#define MOTION_EASE_ENTER      EASE_OUT_CUBIC    // element enter (appear/expand)
#define MOTION_EASE_EXIT       EASE_IN_QUAD      // element exit (shrink/dismiss)
