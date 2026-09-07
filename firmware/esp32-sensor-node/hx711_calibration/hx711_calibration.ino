/*
  HX711 Load Cell Calibration Utility

  Standalone sketch to find the HX711_CALIBRATION_FACTOR used by
  firmware/esp32-sensor-node/config.h. Not part of the main sensor node
  sketch — upload this once, note the number it prints, then flash the
  main esp32-sensor-node.ino sketch with that value.

  Wiring (same pins as the main sensor node — see docs/wiring.md):
    HX711 DT  -> GPIO 16
    HX711 SCK -> GPIO 17

  Required library: HX711 by Bogdan Necula (bogde/HX711)

  How to use:
    1. Upload this sketch, open Serial Monitor at 115200 baud, and make
       sure nothing is on the scale.
    2. Wait for "Taring done." — the scale is now zeroed.
    3. Place an object of known weight on the scale.
    4. Type its weight in grams into the Serial Monitor input box and
       press Enter (e.g. "100" for a 100g reference weight).
    5. Copy the printed HX711_CALIBRATION_FACTOR value into config.h's
       HX711_CALIBRATION_FACTOR, then re-flash the main sensor node sketch.

  You can repeat step 3-4 with a different known weight to sanity-check
  the result — the printed factor should come out close each time.
*/

#include <HX711.h>

#define HX711_DT_PIN 16
#define HX711_SCK_PIN 17

HX711 scale;

unsigned long lastRawPrintMs = 0;

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("=== HX711 Calibration ===");

  scale.begin(HX711_DT_PIN, HX711_SCK_PIN);

  Serial.println("Remove everything from the scale...");
  delay(3000);

  scale.set_scale();  // use raw units (factor 1.0) until we calculate the real one
  scale.tare();        // zero out with nothing on the scale

  Serial.println("Taring done. Scale is zeroed.");
  Serial.println();
  Serial.println("Now place a known weight on the scale, type its weight in");
  Serial.println("grams below, and press Enter (e.g. 100).");
  Serial.println();
}

void loop() {
  // Print the live raw reading periodically so you can watch it settle
  // before typing in the known weight.
  if (scale.is_ready() && millis() - lastRawPrintMs >= 500) {
    lastRawPrintMs = millis();
    Serial.print("Raw reading: ");
    Serial.println(scale.get_units(5), 0);
  }

  if (Serial.available()) {
    float knownWeightG = Serial.parseFloat();
    while (Serial.available()) Serial.read();  // discard the trailing newline

    if (knownWeightG <= 0) {
      Serial.println("Enter a positive number of grams and press Enter.");
      return;
    }

    float rawUnits = scale.get_units(10);
    float calibrationFactor = rawUnits / knownWeightG;

    Serial.println();
    Serial.print("Known weight: ");
    Serial.print(knownWeightG, 2);
    Serial.println(" g");
    Serial.print("Raw reading:  ");
    Serial.println(rawUnits, 0);
    Serial.print(">>> HX711_CALIBRATION_FACTOR = ");
    Serial.println(calibrationFactor, 2);
    if (calibrationFactor < 0) {
      Serial.println("(Negative is expected/fine here - it just means your load cell's");
      Serial.println(" signal wires are polarized opposite to the HX711's A+/A-. Use the");
      Serial.println(" value above including the minus sign, no rewiring needed.)");
    }
    Serial.println();
    Serial.println("Copy that value into config.h, then re-flash esp32-sensor-node.ino.");
    Serial.println("(Add another known weight now to double check, or reset the board to re-tare.)");
    Serial.println();
  }
}
