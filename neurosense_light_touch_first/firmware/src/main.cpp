// NeuroSense BLE button box - Seeed XIAO ESP32C6.
//
// Concurrency model (kept deliberately small):
//   * ONE app task (the Arduino loopTask) owns every piece of state: debounce, LED,
//     pending-send queue, sleep. loop() blocks on a queue with a timeout equal to the next
//     deadline, so the CPU idles instead of polling.
//   * The GPIO ISR and the NimBLE callbacks only post AppEvents to one static queue.
//   * No heap use after setup() in our code (static buffers, no String/std::string).
// See firmware/README or the plan for the LED legend and error codes.
#include <Arduino.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include "config.h"
#include "app.h"
#include "debug.h"
#include "buttons.h"
#include "led.h"
#include "ble_link.h"

// ---- event queue (static storage) -----------------------------------------------------
static constexpr size_t EVQ_LEN = 16;
static StaticQueue_t evqCtl;
static uint8_t evqStore[EVQ_LEN * sizeof(AppEvent)];
static QueueHandle_t evq;

void app_post(uint8_t type, uint8_t arg) {
  AppEvent e{type, arg, (uint32_t)xTaskGetTickCount()};
  if (xQueueSend(evq, &e, 0) != pdTRUE) DBG_INC(queueDrops);
}

void IRAM_ATTR app_post_isr(uint8_t type, uint8_t arg) {
  BaseType_t woken = pdFALSE;
  AppEvent e{type, arg, (uint32_t)xTaskGetTickCountFromISR()};
  if (xQueueSendFromISR(evq, &e, &woken) != pdTRUE) DBG_INC(isrDrops);
  if (woken) portYIELD_FROM_ISR();
}

// ---- app state (app task only) ------------------------------------------------------------
static bool connected = false, subscribed = false;
static bool advFast = true, sleeping = false, stuckChecked = false;
static uint32_t advStart = 0, lastActivity = 0, bootMs = 0, fatalAt = 0;
static uint8_t notifyFails = 0;

struct Pending { uint8_t btn; uint32_t t; };
static Pending pend[PENDING_MAX];
static uint8_t pendHead = 0, pendCount = 0;

static const LedPattern PATTERN_FOR[NUM_BUTTONS] = {LP_SCORE0, LP_SCORE1, LP_SCORE2, LP_UNDO};

static void pendPush(uint8_t btn, uint32_t now) {
  if (pendCount == PENDING_MAX) {           // full: drop the oldest
    pendHead = (pendHead + 1) % PENDING_MAX;
    pendCount--;
    DBG_INC(pendingDropped);
  }
  pend[(pendHead + pendCount) % PENDING_MAX] = {btn, now};
  pendCount++;
}

static void pendExpire(uint32_t now) {
  while (pendCount && (uint32_t)(now - pend[pendHead].t) > PENDING_TTL_MS) {
    pendHead = (pendHead + 1) % PENDING_MAX;
    pendCount--;
    DBG_INC(pendingExpired);
    DBG_LOG("main", "pending press expired");
  }
}

// Try a notify; on repeated failure force a reconnect and flag error 4.
static bool sendNow(uint8_t btn) {
  if (ble_notify(BTN_CMDS[btn])) { notifyFails = 0; return true; }
  if (++notifyFails >= NOTIFY_FAIL_LIMIT) {
    notifyFails = 0;
    DBG_LOG("main", "notify failed %u times -> error 4, reconnect", NOTIFY_FAIL_LIMIT);
    led_error(4);
    ble_disconnect();
  }
  return false;
}

static void flushPending() {
  while (pendCount && connected && subscribed) {
    uint8_t btn = pend[pendHead].btn;
    if (!sendNow(btn)) break;
    pendHead = (pendHead + 1) % PENDING_MAX;
    pendCount--;
    led_flash(PATTERN_FOR[btn]);            // delayed confirmation of a queued press
  }
}

static void onPress(uint8_t btn, uint32_t now) {
  lastActivity = now;
  DBG_INC(presses);
  DBG_LOG("main", "press %s", BTN_CMDS[btn]);
  if (connected && subscribed && pendCount == 0 && sendNow(btn)) {
    led_flash(PATTERN_FOR[btn]);
  } else {
    pendPush(btn, now);                     // delivered when the link comes back, if still fresh
    led_flash(LP_NOTSENT);
  }
}

// ---- deep sleep -------------------------------------------------------------------------
static void beginSleep() {
  DBG_LOG("main", "idle %lu ms -> deep sleep", (unsigned long)IDLE_SLEEP_MS);
  sleeping = true;
  ble_stop();
  led_set_status(LS_OFF);
  led_flash(LP_SLEEP);
}

[[noreturn]] static void deepSleep() {
  uint64_t mask = 0;
  for (uint8_t i = 0; i < WAKE_BUTTONS; i++) {
    gpio_num_t p = (gpio_num_t)BTN_PINS[i];
    mask |= 1ULL << p;
    rtc_gpio_init(p);
    rtc_gpio_set_direction(p, RTC_GPIO_MODE_INPUT_ONLY);
    if (ACTIVE_HIGH) { rtc_gpio_pullup_dis(p); rtc_gpio_pulldown_en(p); }
    else             { rtc_gpio_pulldown_dis(p); rtc_gpio_pullup_en(p); }
  }
  esp_sleep_enable_ext1_wakeup_io(mask, ACTIVE_HIGH ? ESP_EXT1_WAKEUP_ANY_HIGH : ESP_EXT1_WAKEUP_ANY_LOW);
  led_hold_for_sleep();
  esp_deep_sleep_start();
}

// ---- setup / loop -----------------------------------------------------------------------------
void setup() {
  DBG_INIT();
  setCpuFrequencyMhz(CPU_MHZ);
  bootMs = millis();
  lastActivity = bootMs;

  esp_reset_reason_t rr = esp_reset_reason();   // always read: needed for error 5
  bool wokeByButton = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1;
  DBG_LOG("main", "boot reset=%d wake=%d fw " FW_VERSION, (int)rr, (int)wokeByButton);

  evq = xQueueCreateStatic(EVQ_LEN, sizeof(AppEvent), evqStore, &evqCtl);
  DBG_SET_QUEUE(evq);

  led_init();
  buttons_init();
  enableLoopWDT();                              // a hung app task reboots to a clean state

  if (!ble_init()) {
    DBG_LOG("main", "BLE init FAILED");
    led_error(2);
    fatalAt = bootMs + FATAL_RESTART_MS;        // show the code, then reboot
    return;
  }

  if (wokeByButton) {                           // the press that woke us: send it once connected
    uint64_t st = esp_sleep_get_ext1_wakeup_status();
    for (uint8_t i = 0; i < WAKE_BUTTONS; i++)
      if (st & (1ULL << BTN_PINS[i])) { pendPush(i, bootMs); break; }
  } else if (rr == ESP_RST_PANIC || rr == ESP_RST_INT_WDT || rr == ESP_RST_TASK_WDT ||
             rr == ESP_RST_WDT || rr == ESP_RST_BROWNOUT) {
    led_error(5);                               // previous run crashed or browned out
  } else {
    led_flash(LP_BOOT);
  }

  advFast = true;
  advStart = bootMs;
  ble_start_advertising(true);
  led_set_status(LS_ADVERTISING);
}

static void soonest(uint32_t& wait, uint32_t absMs, uint32_t now) {
  if (!absMs) return;
  int32_t d = (int32_t)(absMs - now);
  uint32_t r = d < 0 ? 0 : (uint32_t)d;
  if (r < wait) wait = r;
}

static void handleEvent(const AppEvent& e, uint32_t now) {
  DBG_MAX(maxQueueLatencyMs, (uint32_t)xTaskGetTickCount() - e.tick);
  switch (e.type) {
    case EV_EDGE:
      buttons_on_edge(e.arg, now);
      break;
    case EV_BLE_CONNECT:
      connected = true; subscribed = false; notifyFails = 0;
      lastActivity = now;
      led_set_status(LS_OFF);
      led_flash(LP_CONNECTED);
      DBG_LOG("main", "connected");
      DBG_REPORT("connect");
      break;
    case EV_BLE_SUBSCRIBE:
      subscribed = e.arg != 0;
      DBG_LOG("main", "subscribed=%d pending=%u", (int)subscribed, (unsigned)pendCount);
      if (subscribed) flushPending();
      break;
    case EV_BLE_DISCONNECT:
      connected = subscribed = false;
      DBG_INC(reconnects);
      DBG_LOG("main", "disconnected");
      DBG_REPORT("disconnect");
      if (!sleeping && !fatalAt) {              // auto-reconnect: advertise again right away
        advFast = true;
        advStart = now;
        ble_start_advertising(true);
        led_set_status(LS_ADVERTISING);
      }
      break;
  }
}

#if NS_DEBUG_SELFTEST  // [NS_DEBUG]
static uint32_t stPressAt = 0, stDiscAt = 0;
static uint8_t stIdx = 0;
#endif

void loop() {
  uint32_t now = millis();

  // Sleep until the earliest of: debounce check, LED step, adv slow-down, pending expiry,
  // idle sleep, fatal restart, or the watchdog heartbeat.
  uint32_t wait = WDT_WAKE_MS;
  soonest(wait, buttons_next_deadline(), now);
  soonest(wait, led_next_deadline(), now);
  if (!connected && advFast && !sleeping && !fatalAt) soonest(wait, advStart + ADV_FAST_MS, now);
  if (pendCount) soonest(wait, pend[pendHead].t + PENDING_TTL_MS + 1, now);
  if (!sleeping) soonest(wait, lastActivity + IDLE_SLEEP_MS, now);
  if (fatalAt) soonest(wait, fatalAt, now);
#if NS_DEBUG_SELFTEST  // [NS_DEBUG]
  soonest(wait, stPressAt, now);
#endif

  AppEvent e;
  feedLoopWDT();
  if (xQueueReceive(evq, &e, pdMS_TO_TICKS(wait)) == pdTRUE) {
    DBG_STMT(uint32_t t0 = micros();)           // handler timing
    DBG_MAX(queueDepthMax, (uint32_t)uxQueueMessagesWaiting(evq) + 1);
    do {
      handleEvent(e, millis());
    } while (xQueueReceive(evq, &e, 0) == pdTRUE);
    DBG_MAX(maxEventUs, micros() - t0);
  }
  now = millis();

  // ---- time-based work -------------------------------------------------------------
  int btn;
  while ((btn = buttons_service(now)) >= 0) onPress((uint8_t)btn, now);
  buttons_rearm_idle();
  led_service(now);
  pendExpire(now);

  if (!connected && advFast && !sleeping && !fatalAt && (uint32_t)(now - advStart) >= ADV_FAST_MS) {
    advFast = false;                            // nobody found us: slow down to save power
    ble_start_advertising(false);
  }

  if (!stuckChecked && (uint32_t)(now - bootMs) >= STUCK_BUTTON_MS) {
    stuckChecked = true;
    if (buttons_pressed_mask()) { DBG_LOG("main", "button stuck"); led_error(3); }
  }

  if (fatalAt && (int32_t)(now - fatalAt) >= 0) ESP.restart();

  if (!sleeping && !fatalAt && (uint32_t)(now - lastActivity) >= IDLE_SLEEP_MS) {
    if (buttons_pressed_mask()) lastActivity = now;  // someone is holding a button: not idle
    else beginSleep();
  }
  if (sleeping && !led_busy()) deepSleep();

#if NS_DEBUG_SELFTEST  // [NS_DEBUG] synthetic presses and forced disconnects (bench soak only)
  if (!stPressAt) { stPressAt = now + SELFTEST_PRESS_MS; stDiscAt = now + SELFTEST_DISCONNECT_MS; }
  if ((int32_t)(now - stPressAt) >= 0) { stPressAt = now + SELFTEST_PRESS_MS; onPress(stIdx++ % NUM_BUTTONS, now); }
  if ((int32_t)(now - stDiscAt) >= 0) { stDiscAt = now + SELFTEST_DISCONNECT_MS; DBG_LOG("selftest", "forced disconnect"); ble_disconnect(); }
#endif

  DBG_PERIODIC(now);
}
