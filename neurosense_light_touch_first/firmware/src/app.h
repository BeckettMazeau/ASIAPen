// Event channel into the single app task. The GPIO ISR and the NimBLE callbacks only
// post small events here; all state lives in the app task (see main.cpp).
#pragma once
#include <Arduino.h>

enum AppEventType : uint8_t {
  EV_EDGE,            // arg = button index (GPIO interrupt fired)
  EV_BLE_CONNECT,
  EV_BLE_DISCONNECT,
  EV_BLE_SUBSCRIBE    // arg = 1 subscribed, 0 unsubscribed
};

struct AppEvent {
  uint8_t type;
  uint8_t arg;
  uint32_t tick;      // FreeRTOS tick (1 ms) when posted, used for latency checks in debug
};

void app_post(uint8_t type, uint8_t arg);       // task / callback context
void app_post_isr(uint8_t type, uint8_t arg);   // ISR context only
