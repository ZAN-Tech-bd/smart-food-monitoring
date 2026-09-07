/*
  Smart Food Monitoring - Sensor Node (ESP32 Dev Board)

  Reads DHT11 (temp/humidity), MQ-5 (gas/smoke), HX711 + load cell (weight),
  shows a rotating readout on a 16x2 I2C LCD, and POSTs a JSON reading to the
  server on an interval.

  Required libraries (Arduino IDE Library Manager):
    - DHT sensor library (Adafruit) + Adafruit Unified Sensor
    - HX711 by Bogdan Necula (bogde/HX711)
    - LiquidCrystal I2C by Frank de Brabander

  Wiring: see docs/wiring.md
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <DHT.h>
#include <HX711.h>
#include <LiquidCrystal_I2C.h>

#include "config.h"

// ---- Pin assignments (see docs/wiring.md) ----
#define DHT_PIN 4
#define DHT_TYPE DHT11
#define MQ5_PIN 34
#define HX711_DT_PIN 16
#define HX711_SCK_PIN 17

DHT dht(DHT_PIN, DHT_TYPE);
HX711 scale;
LiquidCrystal_I2C lcd(LCD_I2C_ADDRESS, 16, 2);

unsigned long lastSensorReadMs = 0;
unsigned long lastServerPostMs = 0;
uint8_t lcdScreen = 0;

float lastTemperature = NAN;
float lastHumidity = NAN;
int lastGasRaw = 0;
float lastWeightG = 0;

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi connected, IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi connect timed out, will retry in loop().");
  }
}

const char *gasLevelLabel(int raw) {
  if (raw >= GAS_DANGER_THRESHOLD) return "DANGER";
  if (raw >= GAS_WARNING_THRESHOLD) return "Warning";
  return "Normal";
}

void readSensors() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  if (!isnan(h)) lastHumidity = h;
  if (!isnan(t)) lastTemperature = t;

  lastGasRaw = analogRead(MQ5_PIN);

  if (scale.is_ready()) {
    lastWeightG = scale.get_units(5);
    if (lastWeightG < 0) lastWeightG = 0;
  }

  Serial.printf("T=%.1fC H=%.1f%% Gas=%d (%s) Weight=%.1fg\n",
                lastTemperature, lastHumidity, lastGasRaw,
                gasLevelLabel(lastGasRaw), lastWeightG);
}

void updateLcd() {
  lcd.clear();
  switch (lcdScreen) {
    case 0:
      lcd.setCursor(0, 0);
      lcd.print("Temp: ");
      lcd.print(isnan(lastTemperature) ? -1 : lastTemperature, 1);
      lcd.print("C");
      lcd.setCursor(0, 1);
      lcd.print("Humidity: ");
      lcd.print(isnan(lastHumidity) ? -1 : lastHumidity, 0);
      lcd.print("%");
      break;
    case 1:
      lcd.setCursor(0, 0);
      lcd.print("Gas: ");
      lcd.print(lastGasRaw);
      lcd.setCursor(0, 1);
      lcd.print("Level: ");
      lcd.print(gasLevelLabel(lastGasRaw));
      break;
    case 2:
      lcd.setCursor(0, 0);
      lcd.print("Weight:");
      lcd.setCursor(0, 1);
      lcd.print(lastWeightG, 1);
      lcd.print(" g");
      break;
  }
  lcdScreen = (lcdScreen + 1) % 3;
}

void postReading() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi down, skipping POST.");
    return;
  }

  HTTPClient http;
  String url = String("http://") + SERVER_HOST + ":" + SERVER_PORT + "/api/sensors";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  String body = "{";
  body += "\"temperature\":" + String(isnan(lastTemperature) ? 0 : lastTemperature, 2) + ",";
  body += "\"humidity\":" + String(isnan(lastHumidity) ? 0 : lastHumidity, 2) + ",";
  body += "\"gas_raw\":" + String(lastGasRaw) + ",";
  body += "\"weight_g\":" + String(lastWeightG, 2);
  body += "}";

  int status = http.POST(body);
  Serial.printf("POST /api/sensors -> %d\n", status);
  http.end();
}

void setup() {
  Serial.begin(115200);
  delay(200);

  Wire.begin();
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Food Monitor");
  lcd.setCursor(0, 1);
  lcd.print("Starting...");

  dht.begin();

  scale.begin(HX711_DT_PIN, HX711_SCK_PIN);
  scale.set_scale(HX711_CALIBRATION_FACTOR);
  scale.tare(); // Make sure the scale is empty when the board boots.

  connectWiFi();
  delay(500);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  unsigned long now = millis();

  if (now - lastSensorReadMs >= SENSOR_READ_INTERVAL_MS) {
    lastSensorReadMs = now;
    readSensors();
    updateLcd();
  }

  if (now - lastServerPostMs >= SERVER_POST_INTERVAL_MS) {
    lastServerPostMs = now;
    postReading();
  }
}
