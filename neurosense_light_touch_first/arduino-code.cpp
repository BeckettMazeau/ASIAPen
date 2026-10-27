// NeuroSense Simple RF Receiver / Button Logger
// Target board: Arduino DUE (SAM3X8E, 32-bit ARM)
// Baud Rate: 115200 (Matches index.html Web Serial config)
//
// Connect the Due via the PROGRAMMING USB port (the one next to the DC jack) so that
// "Serial" reaches the PC. Buttons are read as digital inputs on A0-A3.
// WARNING: Due I/O is 3.3 V ONLY. Never drive A0-A3 above 3.3 V (e.g. from a 5 V RF
// receiver); use a level shifter/divider or power the receiver from 3.3 V.

const bool ACTIVE_HIGH = true; // Set 'true' for standard 4-ch RF receivers, 'false' for bare buttons to GND (internal pull-up)
const unsigned long DEBOUNCE_MS = 50;

const int NUM_BUTTONS = 4;
const int PINS[NUM_BUTTONS] = {A0, A1, A2, A3}; // A0='0', A1='1', A2='2', A3='Delete/Undo'
const char* COMMANDS[NUM_BUTTONS] = {"0", "1", "2", "UNDO"};

int lastReading[NUM_BUTTONS];
int stableState[NUM_BUTTONS];
unsigned long lastDebounceTime[NUM_BUTTONS];

void setup() {
  Serial.begin(115200);
  
  int idleState = ACTIVE_HIGH ? LOW : HIGH;
  for (int i = 0; i < NUM_BUTTONS; i++) {
    pinMode(PINS[i], ACTIVE_HIGH ? INPUT : INPUT_PULLUP);
    lastReading[i] = idleState;
    stableState[i] = idleState;
    lastDebounceTime[i] = 0;
  }
  
  Serial.println("READY");
}

void loop() {
  unsigned long now = millis();
  int pressedState = ACTIVE_HIGH ? HIGH : LOW;

  for (int i = 0; i < NUM_BUTTONS; i++) {
    int reading = digitalRead(PINS[i]);

    // Reset debounce timer if the pin state flickered
    if (reading != lastReading[i]) {
      lastDebounceTime[i] = now;
    }
    lastReading[i] = reading;

    // If the state has been stable longer than the debounce delay
    if ((now - lastDebounceTime[i]) > DEBOUNCE_MS) {
      if (reading != stableState[i]) {
        stableState[i] = reading;
        
        // Trigger only on the press down, not release
        if (stableState[i] == pressedState) {
          Serial.println(COMMANDS[i]);
        }
      }
    }
  }
}