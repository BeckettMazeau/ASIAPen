// Non-blocking LED pattern player (app task only).
//
// On-board LED (single colour) shows blink patterns; with HAS_RGB_LED=1 an external RGB
// LED additionally shows true colours (score 0 red, 1 yellow, 2 green).
#pragma once
#include <Arduino.h>

enum LedPattern : uint8_t {
  LP_BOOT,        // 3 quick blinks
  LP_CONNECTED,   // double blip when a host connects
  LP_SCORE0,      // 1 blink / red
  LP_SCORE1,      // 2 blinks / yellow
  LP_SCORE2,      // 3 blinks / green
  LP_UNDO,        // one long flash
  LP_NOTSENT,     // 5 fast blinks: press not delivered yet
  LP_SLEEP        // long flash before deep sleep
};

enum LedStatus : uint8_t {
  LS_OFF,         // dark (connected)
  LS_ADVERTISING  // short blip every 2 s (waiting for a host)
};

void led_init();
void led_set_status(LedStatus s);
void led_flash(LedPattern p);        // pre-empts the current pattern, then status resumes
void led_error(uint8_t code);        // `code` blinks, pause, repeated for ~10 s
bool led_busy();                     // a flash/error pattern is still playing
uint32_t led_next_deadline();        // absolute millis of next change, 0 = nothing scheduled
void led_service(uint32_t now);
void led_hold_for_sleep();           // latch the (off) pin levels through deep sleep
