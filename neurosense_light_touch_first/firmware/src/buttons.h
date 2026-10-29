// Interrupt-assisted debounce. All functions except the ISR run in the app task.
#pragma once
#include <Arduino.h>

void buttons_init();
void buttons_on_edge(uint8_t idx, uint32_t now);  // handle EV_EDGE
int  buttons_service(uint32_t now);               // returns index of a confirmed press, else -1 (call until -1)
uint32_t buttons_next_deadline();                 // absolute millis of the next debounce check, 0 = none
void buttons_rearm_idle();                        // safety net: re-enable interrupts / catch a lost edge
uint8_t buttons_pressed_mask();                   // bit i set = button i currently held
