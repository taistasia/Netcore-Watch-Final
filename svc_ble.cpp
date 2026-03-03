#include "svc_ble.h"

#if BLE_ENABLED

#include "svc_notify.h"
#include "svc_events.h"
#include <string.h>

// FreeRTOS (ESP32) — used to keep BLE connect + GATT discovery out of tick().
#if defined(ARDUINO_ARCH_ESP32)
  #include <freertos/FreeRTOS.h>
  #include <freertos/task.h>
#endif

// Optional NimBLE support (preferred on ESP32-S3)
#if defined(ARDUINO_ARCH_ESP32) && __has_include(<NimBLEDevice.h>)
  #include <NimBLEDevice.h>
  #define NETCORE_HAVE_NIMBLE 1
#else
  #define NETCORE_HAVE_NIMBLE 0
#endif

// ── NVS keys (local to BLE service) ─────────────────────────────────────────
static const char* KEY_BLE_EN  = "bleen";   // bool
static const char* KEY_BLE_WP  = "blewp";   // bool

// ── Provisioning pop buffer (fixed) ─────────────────────────────────────────
static bool g_wpAvail = false;
static bool g_wpConnectNow = false;
static char g_wpSsid[33];
static char g_wpPass[65];

static bool g_bleEnabled = false;
static bool g_bleConnected = false;
static bool g_bleWpEnabled = false;

static bool loadBoolNvs(const char* key, bool defVal) {
  return prefs.getUChar(key, defVal ? 1 : 0) != 0;
}

static void saveBoolNvs(const char* key, bool val) {
  prefs.putUChar(key, val ? 1 : 0);
}

#if NETCORE_HAVE_NIMBLE

// ANCS UUIDs (128-bit)
static NimBLEUUID UUID_ANCS_SVC("7905F431-B5CE-4E99-A40F-4B1E122D00D0");

// NOTE: Do NOT retain NimBLEAdvertisedDevice* across callbacks; lifetime is
// owned by NimBLE scan internals.
static bool     g_havePeer = false;
static uint8_t  g_peerAddr[6];
static uint8_t  g_peerType = 0;

static NimBLEClient* g_client = nullptr;
static uint32_t g_nextConnectAttemptMs = 0;

static TaskHandle_t g_bleTask = nullptr;
static volatile bool g_connectRequest = false;

// Forward hook into svc_ancs.cpp (kept void* to avoid NimBLE types in headers)
#if NETCORE_ANCS_ENABLE
extern void ancsBleAttach(void* nimbleClient);
extern void ancsBleDetach();
#else
static inline void ancsBleAttach(void*) {}
static inline void ancsBleDetach() {}
#endif

class NetcoreClientCB : public NimBLEClientCallbacks {
  void onConnect(NimBLEClient* /*pClient*/) override {
    g_bleConnected = true;
    publishEvent(EVT_BLE_CONNECTED);
    notifySvcPost(NOTIFY_OK, "BLE", "Connected", 1500);
  }

  void onDisconnect(NimBLEClient* /*pClient*/) override {
    g_bleConnected = false;
    publishEvent(EVT_BLE_DISCONNECTED);
    notifySvcPost(NOTIFY_WARN, "BLE", "Disconnected", 1500);
    ancsBleDetach();
  }
};

class NetcoreScanCB : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice* adv) override {
    if (!g_bleEnabled) return;
    if (g_bleConnected) return;
    // Prefer devices advertising ANCS service (iPhone when "Show Notifications" enabled)
    if (adv->isAdvertisingService(UUID_ANCS_SVC)) {
      // Capture address + type (fixed storage) for connect task.
      NimBLEAddress a = adv->getAddress();
      memcpy(g_peerAddr, a.getNative(), 6);
      g_peerType = (uint8_t)a.getType();
      g_havePeer = true;
      g_connectRequest = true;
    }
  }
};

static NetcoreClientCB g_clientCB;
static NetcoreScanCB   g_scanCB;

static void bleStartScanIfNeeded() {
  NimBLEScan* scan = NimBLEDevice::getScan();
  if (!scan) return;

  scan->setAdvertisedDeviceCallbacks(&g_scanCB, false);
  scan->setInterval(45);
  scan->setWindow(15);
  scan->setActiveScan(true);

  // Non-blocking scan: callback-based.
  // duration=0 means "continuous" in NimBLE-Arduino.
  scan->start(0, nullptr, false);
}

static void bleEnsureClient() {
  if (g_client) return;
  g_client = NimBLEDevice::createClient();
  g_client->setClientCallbacks(&g_clientCB, false);
  // Security: require bonding when possible.
  // (Actual security mode enforcement is handled by NimBLE's security settings)
}

static void bleTaskMain(void* /*arg*/) {
  // Dedicated task so we never block tick() on connect/discovery.
  while (true) {
    // Sleep lightly; BLE stack runs on its own tasks.
    vTaskDelay(pdMS_TO_TICKS(25));

    if (!g_bleEnabled) continue;
    if (g_bleConnected) continue;
    if (!g_connectRequest) continue;
    if (!g_havePeer) { g_connectRequest = false; continue; }

    uint32_t now = millis();
    if (now < g_nextConnectAttemptMs) continue;
    g_connectRequest = false;

    bleEnsureClient();
    if (!g_client) continue;

    // Build peer address from fixed storage.
    NimBLEAddress peer(g_peerAddr, (NimBLEAddressType)g_peerType);

    // Connect is potentially blocking — allowed here (NOT in tick()).
    bool ok = g_client->connect(peer);
    if (!ok) {
      g_nextConnectAttemptMs = now + 3000;
      continue;
    }

    // Attach ANCS client (service discovery happens inside svc_ancs).
    ancsBleAttach((void*)g_client);
  }
}

static void bleEnsureTask() {
  if (g_bleTask) return;
#if defined(ARDUINO_ARCH_ESP32)
  xTaskCreatePinnedToCore(
    bleTaskMain,
    "netcore_ble",
    4096,
    nullptr,
    1,
    &g_bleTask,
    0
  );
#endif
}

#endif // NETCORE_HAVE_NIMBLE

void bleSvcInit() {
  // Persisted enable flags
  g_bleEnabled   = loadBoolNvs(KEY_BLE_EN, false);
  g_bleWpEnabled = loadBoolNvs(KEY_BLE_WP, false);

#if NETCORE_HAVE_NIMBLE
  NimBLEDevice::init("NETCORE");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);

  // Security defaults: enable bonding; MITM may not be possible w/out IO.
  NimBLESecurity* sec = new NimBLESecurity();
  sec->setAuthenticationMode(ESP_LE_AUTH_BOND);
  sec->setCapability(ESP_IO_CAP_NONE);
  sec->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

  if (g_bleEnabled) {
    bleEnsureTask();
    bleStartScanIfNeeded();
  }
#else
  // BLE enabled but NimBLE not available: keep silent unless user turned it on.
  if (g_bleEnabled) {
    notifySvcPost(NOTIFY_ERROR, "BLE", "No stack", 2500);
    g_bleEnabled = false;
    saveBoolNvs(KEY_BLE_EN, false);
  }
#endif
}

void bleSvcTick() {
  if (!g_bleEnabled) return;
#if NETCORE_HAVE_NIMBLE
  // Tick must remain non-blocking. Connect/discovery happen in bleTaskMain().
  bleEnsureTask();
#endif
}

bool bleSvcIsConnected() { return g_bleConnected; }

bool bleSvcIsEnabled() { return g_bleEnabled; }

void bleSvcSetEnabled(bool on) {
  if (on == g_bleEnabled) return;
  g_bleEnabled = on;
  saveBoolNvs(KEY_BLE_EN, on);

#if NETCORE_HAVE_NIMBLE
  if (on) {
    notifySvcPost(NOTIFY_INFO, "BLE", "Enabled", 1200);
    bleEnsureTask();
    bleStartScanIfNeeded();
  } else {
    notifySvcPost(NOTIFY_INFO, "BLE", "Disabled", 1200);
    if (g_client && g_client->isConnected()) g_client->disconnect();
    g_bleConnected = false;
    ancsBleDetach();
    NimBLEDevice::getScan()->stop();
    g_havePeer = false;
    g_connectRequest = false;
  }
#else
  // If no NimBLE, force off.
  if (on) {
    notifySvcPost(NOTIFY_ERROR, "BLE", "No stack", 2500);
    g_bleEnabled = false;
    saveBoolNvs(KEY_BLE_EN, false);
  }
#endif
}

bool bleSvcWifiProvisionEnabled() { return g_bleWpEnabled; }

void bleSvcSetWifiProvisionEnabled(bool on) {
  g_bleWpEnabled = on;
  saveBoolNvs(KEY_BLE_WP, on);
}

bool bleWifiProvisionAvailable() {
  return g_bleWpEnabled && g_wpAvail;
}

bool bleWifiProvisionPop(char* ssidOut, int ssidMax, char* passOut, int passMax, bool* connectNow) {
  if (!g_bleWpEnabled) return false;
  if (!g_wpAvail) return false;

  // Copy out (fixed buffers)
  if (ssidOut && ssidMax > 0) {
    strncpy(ssidOut, g_wpSsid, ssidMax - 1);
    ssidOut[ssidMax - 1] = 0;
  }
  if (passOut && passMax > 0) {
    strncpy(passOut, g_wpPass, passMax - 1);
    passOut[passMax - 1] = 0;
  }
  if (connectNow) *connectNow = g_wpConnectNow;

  g_wpAvail = false;
  g_wpSsid[0] = 0;
  g_wpPass[0] = 0;
  g_wpConnectNow = false;
  return true;
}

// NOTE: PASS 10 is ANCS-only. Companion GATT + WiFi provisioning RX are not
// implemented here yet. When added later, they must ONLY fill g_wpSsid/g_wpPass
// and set g_wpAvail=true (no WiFi.connect calls from BLE callbacks).

#endif // BLE_ENABLED