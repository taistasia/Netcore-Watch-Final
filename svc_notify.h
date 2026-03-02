#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// svc_notify.h  —  Notification Manager
//
// Purpose  : Ring-buffered event log + non-blocking banner overlay.
//            Single definition: all functions live in svc_notify.cpp only.
//            No other .cpp defines notifyPost or notifTick.
//
// Usage ───────────────────────────────────────────────────────────────────────
//   notifySvcInit()
//   notifySvcTick()               — call every loop(); handles auto-dismiss
//
//   notifySvcPost(type, title, body, ttlMs)
//   notifySvcPostSimple(msg)      — convenience wrapper, ttl=NOTIFY_TTL_DEFAULT
//
//   notifySvcIsActive()           — true while banner is showing
//   notifySvcCount()              — entries in ring buffer (0–10)
//   notifySvcGet(i)               — 0=newest … 9=oldest; nullptr if OOB
//
// Types ───────────────────────────────────────────────────────────────────────
//   NOTIFY_INFO, NOTIFY_OK, NOTIFY_WARN, NOTIFY_ERROR
//
// Backward-compat shims (defined here, map to notifySvcPost) ─────────────────
//   notifyPost(msg)   — old callers still compile without changes
//   notifTick()       — old callers still compile
//   notifIsActive()   — old callers still compile
//   notifGetCount()
//   notifGetRecent(i)
// ─────────────────────────────────────────────────────────────────────────────
#include "netcore_config.h"
#include <stdint.h>

// ── Constants ─────────────────────────────────────────────────────────────────
#define NOTIFY_RING_SIZE     10
#define NOTIFY_TTL_DEFAULT   3000UL   // ms
#define NOTIFY_MSG_LEN       48

// ── Notification type ─────────────────────────────────────────────────────────
enum NotifyType {
  NOTIFY_INFO  = 0,
  NOTIFY_OK    = 1,
  NOTIFY_WARN  = 2,
  NOTIFY_ERROR = 3,
  NOTIFY_ERR   = 3,   // alias: spec uses NOTIFY_ERR, codebase uses NOTIFY_ERROR
};

// ── Entry ─────────────────────────────────────────────────────────────────────
struct NotifyEntry {
  NotifyType type;
  char       title[NOTIFY_MSG_LEN];   // short label (≤16 chars shows nicely)
  char       body[NOTIFY_MSG_LEN];    // full message
  uint32_t   ttlMs;
};

// ── Lifecycle ─────────────────────────────────────────────────────────────────
void notifySvcInit();
void notifySvcTick();

// ── Post ──────────────────────────────────────────────────────────────────────
void notifySvcPost(NotifyType type, const char* title, const char* body, uint32_t ttlMs);
void notifySvcPostSimple(const char* msg);   // NOTIFY_INFO, ttl=default

// ── Query ─────────────────────────────────────────────────────────────────────
bool               notifySvcIsActive();
int                notifySvcCount();
const NotifyEntry* notifySvcGet(int i);   // 0=newest, nullptr if OOB

// ── Back-compat shims ─────────────────────────────────────────────────────────
// Inline wrappers so netcore_ui.cpp callers and svc_wifi.cpp callers compile.
inline void        notifyPost(const char* msg)    { notifySvcPostSimple(msg); }
inline void        notifTick()                    { notifySvcTick(); }
inline bool        notifIsActive()                { return notifySvcIsActive(); }
inline int         notifGetCount()                { return notifySvcCount(); }
inline const char* notifGetRecent(int i) {
  const NotifyEntry* e = notifySvcGet(i);
  return e ? e->body : "";
}
