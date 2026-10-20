// NeuroSense Simple RF Receiver / Button Logger
// Baud Rate: 115200 (Matches index.html Web Serial config)

const bool ACTIVE_HIGH = true; // Set 'true' for standard 4-ch RF receivers, 'false' for bare buttons to GND
const unsigned long DEBOUNCE_MS = 50;

const int NUM_BUTTONS = 4;
const int PINS[NUM_BUTTONS] = {2, 3, 4, 5}; // D2='0', D3='1', D4='2', D5='Delete/Undo'
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