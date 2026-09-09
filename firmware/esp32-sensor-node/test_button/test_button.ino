/*
  Isolated test: push button ONLY.

  No WiFi, no sensors, no LCD - just watches the button pin and prints to
  Serial every time it's pressed, so you can confirm the wiring/debounce
  logic works independent of anything else on the board.

  Wiring: button's other leg -> GND. Uses the internal pull-up, so no
  external resistor is needed - the pin should read HIGH when not pressed,
  LOW when pressed (see docs/wiring.md).
*/

#define BUTTON_PIN 13
#define DEBOUNCE_MS 50

int lastReading = HIGH;
int buttonState = HIGH;
unsigned long lastDebounceMs = 0;
int pressCount = 0;

void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  Serial.println("BOOT: button pin configured, watching for presses...");
  Serial.printf("Current raw pin state: %s\n", digitalRead(BUTTON_PIN) == HIGH ? "HIGH (not pressed)" : "LOW (pressed)");
}

void loop() {
  int reading = digitalRead(BUTTON_PIN);

  if (reading != lastReading) {
    lastDebounceMs = millis();
  }

  if (millis() - lastDebounceMs > DEBOUNCE_MS && reading != buttonState) {
    buttonState = reading;
    if (buttonState == LOW) {
      pressCount++;
      Serial.printf("Button pressed! (press #%d)\n", pressCount);
    } else {
      Serial.println("Button released");
    }
  }

  lastReading = reading;
}
