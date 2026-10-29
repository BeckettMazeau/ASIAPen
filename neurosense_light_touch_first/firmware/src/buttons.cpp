#include "buttons.h"
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include "config.h"
#include "app.h"
#include "debug.h"

// Debounce: a pin interrupt fires on the first edge, disables itself (so contact bounce
// cannot flood the queue) and posts one event. The app task then samples the pin every
// DEBOUNCE_MS; once two consecutive samples agree the level is accepted. Only a change
// to "pressed" produces a command (press only, like the original Due sketch).

static constexpr int PRESSED = ACTIVE_HIGH ? HIGH : LOW;

struct Btn {
  uint8_t stable;        // accepted level
  uint8_t sample;        // last sampled level while debouncing
  uint32_t deadline;     // absolute ms of next sample, 0 = idle (interrupt armed)
};
static Btn btn[NUM_BUTTONS];

static void IRAM_ATTR isr(void* arg) {
  uint8_t idx = (uint8_t)(uintptr_t)arg;
  gpio_intr_disable((gpio_num_t)BTN_PINS[idx]);
  app_post_isr(EV_EDGE, idx);
}

void buttons_init() {
  for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
    if (i < WAKE_BUTTONS) rtc_gpio_deinit((gpio_num_t)BTN_PINS[i]);  // release deep-sleep pad config
    pinMode(BTN_PINS[i], ACTIVE_HIGH ? INPUT : INPUT_PULLUP);
    uint8_t lvl = digitalRead(BTN_PINS[i]);
    btn[i] = {lvl, lvl, 0};   // a button already held at boot is NOT reported as a press
    attachInterruptArg(BTN_PINS[i], isr, (void*)(uintptr_t)i, CHANGE);
  }
}

void buttons_on_edge(uint8_t idx, uint32_t now) {
  if (idx >= NUM_BUTTONS) return;
  btn[idx].sample = digitalRead(BTN_PINS[idx]);
  btn[idx].deadline = now + DEBOUNCE_MS;
  if (btn[idx].deadline == 0) btn[idx].deadline = 1;  // 0 is reserved for "idle"
}

int buttons_service(uint32_t now) {
  for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
    Btn& b = btn[i];
    if (!b.deadline || (int32_t)(now - b.deadline) < 0) continue;
    uint8_t lvl = digitalRead(BTN_PINS[i]);
    if (lvl != b.sample) {               // still moving: restart the window
      b.sample = lvl;
      b.deadline = now + DEBOUNCE_MS;
      if (!b.deadline) b.deadline = 1;
      DBG_INC(debounceRetries);
      continue;
    }
    b.deadline = 0;                      // settled
    bool wasStable = (lvl == b.stable);
    b.stable = lvl;
    gpio_intr_enable((gpio_num_t)BTN_PINS[i]);  // watch for the next edge
    if (!wasStable && lvl == PRESSED) return i;
  }
  return -1;
}

uint32_t buttons_next_deadline() {
  uint32_t best = 0;
  uint32_t now = millis();
  for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
    uint32_t d = btn[i].deadline;
    if (d && (!best || (int32_t)(d - now) < (int32_t)(best - now))) best = d;
  }
  return best;
}

void buttons_rearm_idle() {
  // Idle buttons: make sure the interrupt is armed, and if the pin level disagrees with
  // what we accepted (an edge was lost), start a debounce window as if it had arrived.
  uint32_t now = millis();
  for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
    if (btn[i].deadline) continue;
    gpio_intr_enable((gpio_num_t)BTN_PINS[i]);
    if (digitalRead(BTN_PINS[i]) != btn[i].stable) buttons_on_edge(i, now);
  }
}

uint8_t buttons_pressed_mask() {
  uint8_t m = 0;
  for (uint8_t i = 0; i < NUM_BUTTONS; i++)
    if (digitalRead(BTN_PINS[i]) == PRESSED) m |= (1u << i);
  return m;
}
