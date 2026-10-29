// BLE peripheral: one service, one Notify characteristic carrying the ASCII command
// ("0", "1", "2", "UNDO"), the same strings the old Arduino sent over USB serial.
// Callbacks (NimBLE host task) only post events to the app task via app_post().
#pragma once
#include <Arduino.h>

bool ble_init();                          // builds the NS-XXXX name, GATT server, advertising data
const char* ble_name();                   // "NS-3FA9" (valid after ble_init, or call ble_make_name)
void ble_start_advertising(bool fast);    // (re)starts advertising with fast or slow intervals
bool ble_notify(const char* cmd);         // false if not connected/subscribed or the send failed
void ble_disconnect();                    // drop the current link (used to recover from errors)
void ble_stop();                          // stop everything before deep sleep
