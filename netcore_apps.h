#pragma once
#include "netcore_config.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include "netcore_console.h"

enum Mode { MODE_MENU = 0, MODE_APP = 1 };

struct App {
  const char* name;
  const char* sub;
  void (*enter)();
  void (*tick)();
  void (*exit)();
};

extern Mode mode;
extern int runningApp;
extern App apps[];
extern const int APP_COUNT;

void appsTick();

// System event log — any module can push entries
void sysLogPush(const char* msg);
void diagUpdateStall(uint32_t loopStartMs);  // no-op shim; svc_perf owns tracking now
