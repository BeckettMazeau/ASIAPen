// NeuroSense BLE button box: all tunables in one place.
// Target: Seeed Studio XIAO ESP32C6 (3.3 V logic).
#pragma once
#include <Arduino.h>

#define FW_VERSION "1.0.0"

// ---- Buttons ---------------------------------------------------------------
// true : RF receiver outputs (idle LOW, external pull-down, pressed = HIGH)
// false: bare buttons to GND (internal pull-up, pressed = LOW)
static constexpr bool ACTIVE_HIGH = true;

static constexpr uint8_t NUM_BUTTONS = 4;
// D0, D1, D2, D3. GPIO0-2 are the only deep-sleep wake capable pins on the header,
// so the three score buttons take them. UNDO (GPIO21) cannot wake the board.
static constexpr uint8_t BTN_PINS[NUM_BUTTONS] = {0, 1, 2, 21};
static const char* const BTN_CMDS[NUM_BUTTONS] = {"0", "1", "2", "UNDO"};
static constexpr uint8_t WAKE_BUTTONS = 3;       // first N buttons can wake from deep sleep

static constexpr uint32_t DEBOUNCE_MS = 30;      // level must hold this long to count
static constexpr uint32_t STUCK_BUTTON_MS = 5000; // pressed this long after boot -> error 3

// ---- LED -------------------------------------------------------------------
static constexpr uint8_t LED_PIN = 15;           // on-board user LED
static constexpr bool LED_ACTIVE_LOW = true;     // XIAO ESP32C6 user LED is active LOW (verify)

// Optional external RGB LED (common cathode, resistor per channel): true colour score flashes.
// Without it the on-board LED shows 0/1/2 as 1/2/3 blinks.
#ifndef HAS_RGB_LED
#define HAS_RGB_LED 0
#endif
static constexpr uint8_t RGB_PIN_R = 19;         // D8
static constexpr uint8_t RGB_PIN_G = 20;         // D9
static constexpr uint8_t RGB_PIN_B = 18;         // D10
static constexpr bool RGB_ACTIVE_HIGH = true;

// ---- BLE -------------------------------------------------------------------
// Lowercase UUIDs (Web Bluetooth requires lowercase). Keep in sync with index.html.
#define NS_SERVICE_UUID "4e535301-7a3c-4b1e-9d52-0c6f9a1e2b00"
#define NS_CHAR_UUID    "4e535302-7a3c-4b1e-9d52-0c6f9a1e2b00"
#define NS_NAME_PREFIX  "NS-"                    // device name is NS-XXXX (last 2 MAC bytes)

static constexpr int8_t BLE_TX_DBM = 0;          // lower = less power and range

// Advertising: fast after boot/disconnect so the host reconnects quickly, then slow.
static constexpr uint32_t ADV_FAST_MS = 30000;
static constexpr uint16_t ADV_FAST_MIN = 160, ADV_FAST_MAX = 240;    // x0.625 ms = 100-150 ms
static constexpr uint16_t ADV_SLOW_MIN = 1600, ADV_SLOW_MAX = 2400;  // 1.0-1.5 s

// Preferred connection parameters (the host may override them).
static constexpr uint16_t CONN_MIN = 80, CONN_MAX = 120;  // x1.25 ms = 100-150 ms
static constexpr uint16_t CONN_LATENCY = 4;
static constexpr uint16_t CONN_TIMEOUT = 400;             // x10 ms = 4 s

// ---- Delivery / power ------------------------------------------------------
static constexpr uint8_t PENDING_MAX = 4;        // presses held while disconnected
static constexpr uint32_t PENDING_TTL_MS = 8000; // older presses are dropped, never replayed late
static constexpr uint8_t NOTIFY_FAIL_LIMIT = 3;  // consecutive failures -> error 4 + reconnect
static constexpr uint32_t IDLE_SLEEP_MS = 10UL * 60UL * 1000UL;
static constexpr uint32_t WDT_WAKE_MS = 1000;    // max time the app task blocks (feeds the watchdog)
static constexpr uint32_t CPU_MHZ = 80;
static constexpr uint32_t FATAL_RESTART_MS = 10500;

// ---- Debug (only used when NS_DEBUG=1, see debug.h) ---------------------------
#ifndef NS_DEBUG_SELFTEST
#define NS_DEBUG_SELFTEST 0
#endif
static constexpr uint32_t DBG_REPORT_MS = 10000;        // memory + task report period
static constexpr uint32_t DBG_INTEGRITY_MS = 60000;     // full heap integrity check period
static constexpr uint32_t SELFTEST_PRESS_MS = 2000;
static constexpr uint32_t SELFTEST_DISCONNECT_MS = 180000;
