/*
  MQ-5 Gas Sensor Calibration / Diagnostic Utility

  Standalone sketch to find GAS_WARNING_THRESHOLD / GAS_DANGER_THRESHOLD for
  firmware/esp32-sensor-node/config.h, and to flag common wiring/power
  problems - like a reading permanently stuck at the ADC maximum (4095),
  which is a wiring issue, not real gas.

  Wiring (same pin as the main sensor node - see docs/wiring.md):
    MQ-5 AOUT -> GPIO 34

  Important notes about MQ-5 sensors:
  - Brand new sensors need a "burn-in" period - ideally leave the module
    powered continuously for 24-48 hours before trusting any reading. Early
    readings from a fresh sensor are commonly unstable or too high.
  - Every time it's power-cycled it also needs a few minutes to warm up
    before readings settle - this sketch waits 2 minutes before suggesting
    thresholds, but let it run longer if the baseline is still drifting.
  - The MQ-5 module must output within the ESP32's 0-3.3V ADC range. Many
    cheap modules run their sensing element on 5V but still have a 3.3V-safe
    AOUT (via an onboard comparator/divider) - if yours doesn't, add your own
    voltage divider, or you'll see exactly the "stuck at max" symptom this
    sketch checks for.

  How to use:
    1. Upload this sketch, open Serial Monitor at 115200 baud.
    2. Put the sensor in clean, still air and leave it alone.
    3. Watch the rolling baseline settle down as it warms up.
    4. Once warmed up, either copy the suggested thresholds it prints, or
       first do a sanity check: briefly hold an unlit lighter (do NOT
       ignite it) or an alcohol-dampened cloth a few cm from the sensor and
       watch the raw value rise, then fall back down once removed - that
       tells you how much headroom you actually have to work with.
*/

#include <Arduino.h>

#define MQ5_PIN 34
#define WARMUP_MS 120000UL  // 2 minutes minimum; let it run longer if the baseline is still moving
#define SAMPLE_INTERVAL_MS 1000
#define BASELINE_WINDOW 30  // rolling average window, in samples

unsigned long lastSampleMs = 0;
unsigned long startMs = 0;
int samples[BASELINE_WINDOW];
int sampleCount = 0;
int sampleIndex = 0;

int readAveraged() {
  long sum = 0;
  const int n = 10;
  for (int i = 0; i < n; i++) {
    sum += analogRead(MQ5_PIN);
    delay(5);
  }
  return sum / n;
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("=== MQ-5 Gas Sensor Calibration ===");
  Serial.println("Place the sensor in clean, still air and leave it undisturbed.");
  Serial.println("Warming up - this takes at least 2 minutes...");
  Serial.println();
  startMs = millis();
}

void loop() {
  if (millis() - lastSampleMs < SAMPLE_INTERVAL_MS) return;
  lastSampleMs = millis();

  int raw = readAveraged();
  samples[sampleIndex] = raw;
  sampleIndex = (sampleIndex + 1) % BASELINE_WINDOW;
  if (sampleCount < BASELINE_WINDOW) sampleCount++;

  long sum = 0;
  for (int i = 0; i < sampleCount; i++) sum += samples[i];
  int baseline = sum / sampleCount;

  bool warmedUp = millis() - startMs >= WARMUP_MS;

  Serial.printf("Raw: %4d   Rolling baseline: %4d   %s\n",
                raw, baseline, warmedUp ? "(warmed up)" : "(warming up...)");

  if (raw >= 4090) {
    Serial.println("!! Reading is pegged at/near the ADC maximum (4095). This is almost");
    Serial.println("   always a wiring problem, not real gas. Check:");
    Serial.println("   - AOUT isn't shorted to VCC or another 3.3V+ line");
    Serial.println("   - Your module's AOUT actually stays within 0-3.3V (some MQ-5");
    Serial.println("     boards output up to 5V, which just clips at the ESP32's ADC");
    Serial.println("     limit) - add a voltage divider, or use a 3.3V-safe module");
    Serial.println("   - GPIO34 itself isn't damaged - try another ADC1 pin as a test");
  }

  if (warmedUp && sampleCount == BASELINE_WINDOW) {
    if (baseline >= 4000) {
      Serial.println();
      Serial.println("=== Calibration inconclusive ===");
      Serial.println("Baseline is still pegged near the ADC maximum after warm-up - fix");
      Serial.println("the wiring issue above, then re-run this sketch. Suggested");
      Serial.println("thresholds would be meaningless while it's stuck like this.");
      Serial.println();
    } else {
      int warning = baseline + 300;
      int danger = baseline + 800;
      Serial.println();
      Serial.printf(">>> GAS_WARNING_THRESHOLD = %d\n", warning);
      Serial.printf(">>> GAS_DANGER_THRESHOLD  = %d\n", danger);
      Serial.println("(Starting points based on your clean-air baseline. Do the lighter/");
      Serial.println(" alcohol sniff test above to see how high a real gas event pushes");
      Serial.println(" the reading, then adjust these two numbers to sit sensibly below");
      Serial.println(" and above that. Copy the final values into config.h.)");
      Serial.println();
    }
  }
}
