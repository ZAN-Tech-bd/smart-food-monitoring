/*
  DHT11 Temperature/Humidity Calibration Utility

  Standalone sketch to find DHT_TEMP_OFFSET / DHT_HUMIDITY_OFFSET for
  firmware/esp32-sensor-node/config.h. DHT11 sensors are only rated to about
  +/-2C and +/-5% RH out of the box - if you have a separate trusted
  thermometer/hygrometer (or a known-accurate reference reading for the
  room), this computes a simple additive correction for your specific unit.

  Wiring (same pin as the main sensor node - see docs/wiring.md):
    DHT11 DATA -> GPIO 4

  Required library: DHT sensor library (Adafruit) + Adafruit Unified Sensor

  How to use:
    1. Upload this sketch, open Serial Monitor at 115200 baud.
    2. Let it run for ~30 seconds in the room you want to calibrate for, so
       the readings settle.
    3. Using a separate trusted thermometer/hygrometer, note the room's
       actual temperature (C) and humidity (%).
    4. Type them into the Serial Monitor input box as "temp,humidity"
       (e.g. 24.5,52) and press Enter.
    5. Copy the printed DHT_TEMP_OFFSET / DHT_HUMIDITY_OFFSET into config.h,
       then re-flash the main esp32-sensor-node.ino sketch.

  You can enter another reference reading afterward to double-check, or
  reset the board to start over.
*/

#include <DHT.h>

#define DHT_PIN 4
#define DHT_TYPE DHT11
#define READ_INTERVAL_MS 2000

DHT dht(DHT_PIN, DHT_TYPE);

unsigned long lastReadMs = 0;
float lastTemp = NAN;
float lastHumidity = NAN;

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("=== DHT11 Calibration ===");
  Serial.println("Reading sensor - let it settle for about 30 seconds.");
  Serial.println();
  Serial.println("Then, using a separate trusted thermometer/hygrometer, type the");
  Serial.println("room's actual temperature and humidity as \"temp,humidity\"");
  Serial.println("(e.g. 24.5,52) into the input box above and press Enter.");
  Serial.println();

  dht.begin();
}

void loop() {
  if (millis() - lastReadMs >= READ_INTERVAL_MS) {
    lastReadMs = millis();
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    if (!isnan(h)) lastHumidity = h;
    if (!isnan(t)) lastTemp = t;
    Serial.printf("Sensor reads: Temp=%.1fC Humidity=%.1f%%\n", lastTemp, lastHumidity);
  }

  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();

    int commaAt = line.indexOf(',');
    if (commaAt == -1) {
      Serial.println("Type both values separated by a comma, e.g. 24.5,52");
      return;
    }

    if (isnan(lastTemp) || isnan(lastHumidity)) {
      Serial.println("No valid sensor reading yet - wait a moment and try again.");
      return;
    }

    float actualTemp = line.substring(0, commaAt).toFloat();
    float actualHumidity = line.substring(commaAt + 1).toFloat();

    float tempOffset = actualTemp - lastTemp;
    float humidityOffset = actualHumidity - lastHumidity;

    Serial.println();
    Serial.printf("Sensor read:  Temp=%.1fC Humidity=%.1f%%\n", lastTemp, lastHumidity);
    Serial.printf("You entered:  Temp=%.1fC Humidity=%.1f%%\n", actualTemp, actualHumidity);
    Serial.println();
    Serial.print(">>> DHT_TEMP_OFFSET = ");
    Serial.println(tempOffset, 2);
    Serial.print(">>> DHT_HUMIDITY_OFFSET = ");
    Serial.println(humidityOffset, 2);
    Serial.println();
    Serial.println("Copy both into config.h, then re-flash esp32-sensor-node.ino.");
    Serial.println("(Enter another reference reading now to double-check, if you like.)");
    Serial.println();
  }
}
