#include "netcore_config.h"
#include "netcore_theme.h"
#include "netcore_settings.h"
#include "netcore_buttons.h"
#include "svc_input.h"          // hold detection + rotary acceleration
#include "netcore_ui.h"
#include "netcore_boot.h"
#include "netcore_apps.h"
#include "netcore_sd.h"
#include "netcore_watchface.h"
#include "netcore_time.h"      // time service — NTP FSM + mock clock
#include "svc_wifi.h"          // WiFi service — the ONLY caller of WiFi.begin()
#include "svc_notify.h"        // notification manager
#include "svc_statusbar.h"     // unified status bar
#include "svc_tasks.h"         // long-task runner
#include "svc_perf.h"          // perf guard — loop stall + fps
#include "svc_anim.h"          // deterministic tween engine
#include "svc_events.h"        // event bus + system snapshot
#include "svc_haptics.h"       // non-blocking vibration motor
#include "svc_air.h"           // air quality (SCD40 / demo mode)
#include "svc_tempir.h"        // IR temperature sensor (MLX90614 / demo)
#include "svc_rfid.h"          // RFID scanner (MFRC522 / demo)

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);
Preferences prefs;

// ── System state machine ──────────────────────────────────────────────────────
enum SystemState { SYS_ACTIVE, SYS_WATCHFACE, SYS_SLEEP };

static SystemState sysState    = SYS_ACTIVE;
static uint32_t    lastInputMs = 0;

static void touchInput() { lastInputMs = millis(); }

static void wakeSystem() {
  watchfaceExit();
  sysState    = SYS_ACTIVE;
  lastInputMs = millis();
  mode        = MODE_MENU;
  runningApp  = -1;
  renderMenuFull();
}

void setup() {
  Serial.begin(115200);
  delay(300);

  SPI.begin(TFT_SCK, TFT_MISO, TFT_MOSI);
  tft.begin();
  tft.setRotation(3);

  prefs.begin(PREF_NS, false);
  settingsLoad();

  uint8_t savedTheme = loadU8(KEY_THEME, 1);  // default: PHOSPHOR (cassette futurism)
  if (savedTheme >= (uint8_t)THEME_COUNT) savedTheme = 1;
  setThemeIndex((int)savedTheme, false);

  Buttons::begin();
  inputSvcInit();   // hold detection + accelerated rotary (must follow Buttons::begin)

  // ── Service init order matters ────────────────────────────────────────────
  // 1. Time: sets TZ/tzset before any configTime() can be called
  timeSvcInit();
  // 2. Notify: needs TFT ready (draw calls); needs no other service
  notifySvcInit();
  // 3. Status bar: needs time + WiFi refs (reads their state in tick)
  statusBarInit();
  // 4. WiFi: reads NVS creds; may call WiFi.begin() immediately
  wifiSvcInit();
  // 5. Task runner: stateless init
  taskSvcInit();
  // 6. Perf guard: zero counters, open windows
  perfInit();
  // 6.5 Anim: fixed-pool tweens; must tick before UI draws
  animSvcInit();
  // 7. Event bus: register default notification handler, zero queue
  eventBusInit();
  // 8. Haptics: GPIO-only, no deps; safe to init any time after setup()
  hapticsInit(PIN_HAPTIC);
  // 9. Air quality: starts I2C + SCD40 warmup (or demo wave in demo mode)
  airSvcInit();
  // 10. IR temp sensor: shares I2C bus; Wire already up if air real-mode
  tempIrInit();
  // 11. RFID: shares FSPI bus with TFT; SPI already begin()'d above
  rfidSvcInit();

  Serial.println(">> SD init start");
  bool sdOk = sdInit();
  Serial.print(">> sdPresent: "); Serial.println(sdPresent);
  Serial.print(">> cartLoaded: "); Serial.println(cartLoaded);

  if (sdPresent)
    notifySvcPost(NOTIFY_OK, "SD", "Card mounted", 2000);

  runBootScreen();
  renderMenuFull();
  lastInputMs = millis();
}

void loop() {
  // ── Perf tracking ─────────────────────────────────────────────────────────
  uint32_t loopStart = perfLoopBegin();

  Buttons::poll();

  // ── Services tick — ALWAYS first, every loop ──────────────────────────────
  inputSvcTick();   // update hold timers; must run before any hold checks
  // eventBusTick: refreshes snapshot + dispatches queued events
  eventBusTick();
  timeSvcTick();
  wifiSvcTick();
  notifySvcTick();
  statusBarTick();
  taskSvcTick();
  // Anim tick: budgeted, deterministic. UI reads anim props later in this loop.
  animSvcTick(1200);
  hapticsTick();   // drives motor off-timer; ~1µs when idle
  airSvcTick();    // non-blocking sensor poll; fires alerts on threshold cross
  tempIrTick();   // IR temp poll; rate varies by app-open state
  rfidSvcTick();  // RFID poll; near-zero when unarmed

  // ── Watchface / sleep ─────────────────────────────────────────────────────
  if (sysState != SYS_ACTIVE) {
    bool woke = Buttons::up::consume()    |
                Buttons::down::consume()  |
                Buttons::select.consume() |
                Buttons::back.consume();
    if (woke) {
      wakeSystem();
    } else if (sysState == SYS_WATCHFACE) {
      watchfaceTick();
    }
    perfLoopEnd(loopStart);
    return;
  }

  // ── Active mode ───────────────────────────────────────────────────────────
  // Transition slide takes priority — no input during slide animation
  if (transitionActive()) {
    transitionTick();
    perfLoopEnd(loopStart);
    return;
  }

  // statusBarTick() above handles the status bar; statusTick() is now a
  // thin wrapper that calls statusBarTick() — calling it here too is safe
  // (second call is a no-op if state hasn't changed since the first).
  if (mode == MODE_MENU) {

    if (Buttons::up::consume()) {
      touchInput();
      if (fxSound) hapticsPattern(HAPTIC_CLICK);
      int oldSel = menuSel, oldScroll = menuScroll;
      menuSel = (menuSel + APP_COUNT - 1) % APP_COUNT;
      if (menuSel < menuScroll) menuScroll = menuSel;
      if (menuSel >= menuScroll + 5) menuScroll = menuSel - 5 + 1;
      if (menuScroll != oldScroll) renderMenuFull();
      else { drawMenuRowBase(oldSel, false); drawMenuRowBase(menuSel, true); }
    }

    if (Buttons::down::consume()) {
      touchInput();
      if (fxSound) hapticsPattern(HAPTIC_CLICK);
      int oldSel = menuSel, oldScroll = menuScroll;
      menuSel = (menuSel + 1) % APP_COUNT;
      if (menuSel >= menuScroll + 5) menuScroll = menuSel - 5 + 1;
      if (menuSel < menuScroll) menuScroll = 0;
      if (menuScroll != oldScroll) renderMenuFull();
      else { drawMenuRowBase(oldSel, false); drawMenuRowBase(menuSel, true); }
    }

    if (Buttons::select.consume()) { touchInput(); if (fxSound) hapticsPattern(HAPTIC_SUCCESS); launchApp(menuSel); return; }

    // BACK from menu = manual watchface trigger
    if (Buttons::back.consume()) {
      touchInput();
      sysState = SYS_WATCHFACE;
      watchfaceEnter();
      return;
    }

    menuFxTick();

  } else {
    noInterrupts();
    bool anyPending = Buttons::encUp || Buttons::encDown;
    interrupts();
    anyPending |= Buttons::select.latched | Buttons::back.latched;
    if (anyPending) touchInput();
    appsTick();
  }

  // ── Idle timeout → watchface ──────────────────────────────────────────────
  if (millis() - lastInputMs > IDLE_WATCHFACE_MS) {
    sysState = SYS_WATCHFACE;
    watchfaceEnter();
  }

  perfLoopEnd(loopStart);
}
