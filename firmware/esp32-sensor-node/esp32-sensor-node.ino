/*
  Smart Food Monitoring - Sensor Node (ESP32 Dev Board)

  Reads DHT11 (temp/humidity), MQ-5 (gas/smoke), HX711 + load cell (weight),
  shows a rotating readout on a 16x2 I2C LCD, and POSTs a JSON reading to the
  server on an interval.

  A push button toggles the LCD between two modes:
    - Sensor mode (default): rotates Temp/Humidity -> Gas -> Weight screens.
    - AI mode: fetches the latest Gemini verdict + notes from the server and
      shows it, scrolling the notes line if it's longer than 16 characters.
  Press the button again to switch back to sensor mode.

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
// Momentary push button, other leg to GND. Uses the internal pull-up, so no
// external resistor is needed - the pin reads LOW when pressed.
#define BUTTON_PIN 13

DHT dht(DHT_PIN, DHT_TYPE);
HX711 scale;
LiquidCrystal_I2C lcd(LCD_I2C_ADDRESS, 16, 2);

// LCD screens rotate on their own timer so they stay readable even though
// sensors are sampled once a second for the live dashboard.
#define LCD_ROTATE_INTERVAL_MS 2500
// How fast the AI notes line scrolls when it's longer than 16 characters.
#define LCD_SCROLL_INTERVAL_MS 400
// How often to re-fetch the AI verdict while the button has AI mode selected.
#define API_FETCH_INTERVAL_MS 5000
#define BUTTON_DEBOUNCE_MS 50

unsigned long lastSensorReadMs = 0;
unsigned long lastServerPostMs = 0;
unsigned long lastLcdRotateMs = 0;
unsigned long lastApiFetchMs = 0;
uint8_t lcdScreen = 0;

float lastTemperature = NAN;
float lastHumidity = NAN;
int lastGasRaw = 0;
float lastWeightG = 0;

enum DisplayMode { DISPLAY_SENSORS = 0, DISPLAY_AI = 1 };
DisplayMode displayMode = DISPLAY_SENSORS;

int lastButtonReading = HIGH;
int buttonState = HIGH;
unsigned long lastButtonDebounceMs = 0;

String apiVerdict = "--";
String apiNotes = "Press button to load AI feedback";
uint16_t apiScrollOffset = 0;

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

void fetchApiResponse() {
  if (WiFi.status() != WL_CONNECTED) {
    apiVerdict = "No WiFi";
    apiNotes = "Cannot reach server";
    return;
  }

  HTTPClient http;
  String url = String("http://") + SERVER_HOST + ":" + SERVER_PORT + "/api/images/latest?format=text";
  http.begin(url);
  http.setTimeout(5000);

  int status = http.GET();
  if (status == 200) {
    String body = http.getString();
    int splitAt = body.indexOf('\n');
    if (splitAt == -1) {
      apiVerdict = body;
      apiNotes = "";
    } else {
      apiVerdict = body.substring(0, splitAt);
      apiNotes = body.substring(splitAt + 1);
    }
    apiVerdict.trim();
    apiNotes.trim();
  } else {
    apiVerdict = "Fetch failed";
    apiNotes = http.errorToString(status);
  }
  http.end();
  apiScrollOffset = 0;
}

void toggleDisplayMode() {
  displayMode = (displayMode == DISPLAY_SENSORS) ? DISPLAY_AI : DISPLAY_SENSORS;
  lcdScreen = 0;
  apiScrollOffset = 0;
  lastLcdRotateMs = millis();

  if (displayMode == DISPLAY_AI) {
    fetchApiResponse();
    lastApiFetchMs = millis();
  }
}

void handleButton() {
  int reading = digitalRead(BUTTON_PIN);

  if (reading != lastButtonReading) {
    lastButtonDebounceMs = millis();
  }

  if (millis() - lastButtonDebounceMs > BUTTON_DEBOUNCE_MS && reading != buttonState) {
    buttonState = reading;
    if (buttonState == LOW) {  // pressed (active-low with internal pull-up)
      toggleDisplayMode();
      updateLcd();
    }
  }

  lastButtonReading = reading;
}

int readGasRaw() {
  // MQ-5's raw ADC value is noisy sample-to-sample (can swing +/-30-40%
  // around the true level) - average a handful of quick samples so a single
  // noise spike can't falsely trip the Warning/Danger threshold.
  long sum = 0;
  const int samples = 10;
  for (int i = 0; i < samples; i++) {
    sum += analogRead(MQ5_PIN);
    delay(2);
  }
  return sum / samples;
}

void readSensors() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  if (!isnan(h)) lastHumidity = h;
  if (!isnan(t)) lastTemperature = t;

  lastGasRaw = readGasRaw();

  if (scale.is_ready()) {
    lastWeightG = scale.get_units(2);
    if (lastWeightG < 0) lastWeightG = 0;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("WiFi: Connected (IP %s) | T=%.1fC H=%.1f%% Gas=%d (%s) Weight=%.1fg\n",
                  WiFi.localIP().toString().c_str(), lastTemperature, lastHumidity,
                  lastGasRaw, gasLevelLabel(lastGasRaw), lastWeightG);
  } else {
    Serial.printf("WiFi: Disconnected | T=%.1fC H=%.1f%% Gas=%d (%s) Weight=%.1fg\n",
                  lastTemperature, lastHumidity, lastGasRaw, gasLevelLabel(lastGasRaw), lastWeightG);
  }
}

void updateSensorScreen() {
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

void updateApiScreen() {
  lcd.clear();

  String line0 = "AI: " + apiVerdict;
  if (line0.length() > 16) line0 = line0.substring(0, 16);
  lcd.setCursor(0, 0);
  lcd.print(line0);

  String text = apiNotes.length() ? apiNotes : String("(no notes)");
  lcd.setCursor(0, 1);

  if (text.length() <= 16) {
    lcd.print(text);
    return;
  }

  // Scroll: pad with a gap so the wrap-around reads cleanly, then print a
  // rolling 16-char window into it.
  String padded = text + "    ";
  String window;
  for (int i = 0; i < 16; i++) {
    window += padded[(apiScrollOffset + i) % padded.length()];
  }
  lcd.print(window);
  apiScrollOffset++;
}

void updateLcd() {
  if (displayMode == DISPLAY_AI) {
    updateApiScreen();
  } else {
    updateSensorScreen();
  }
}

void postReading() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi down, skipping POST.");
    return;
  }

  String url = String("http://") + SERVER_HOST + ":" + SERVER_PORT + "/api/sensors";

  String body = "{";
  body += "\"temperature\":" + String(isnan(lastTemperature) ? 0 : lastTemperature, 2) + ",";
  body += "\"humidity\":" + String(isnan(lastHumidity) ? 0 : lastHumidity, 2) + ",";
  body += "\"gas_raw\":" + String(lastGasRaw) + ",";
  body += "\"weight_g\":" + String(lastWeightG, 2);
  body += "}";

  for (int attempt = 1; attempt <= 2; attempt++) {
    HTTPClient http;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(5000);

    int status = http.POST(body);
    if (status > 0) {
      Serial.printf("POST /api/sensors -> %d\n", status);
      http.end();
      return;
    }

    Serial.printf("POST /api/sensors failed (attempt %d): %s\n", attempt, http.errorToString(status).c_str());
    http.end();
    if (attempt == 1) delay(1000);
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

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

  handleButton();

  unsigned long now = millis();

  if (now - lastSensorReadMs >= SENSOR_READ_INTERVAL_MS) {
    lastSensorReadMs = now;
    readSensors();
  }

  if (displayMode == DISPLAY_AI) {
    if (now - lastLcdRotateMs >= LCD_SCROLL_INTERVAL_MS) {
      lastLcdRotateMs = now;
      updateLcd();
    }
    if (now - lastApiFetchMs >= API_FETCH_INTERVAL_MS) {
      lastApiFetchMs = now;
      fetchApiResponse();
    }
  } else {
    if (now - lastLcdRotateMs >= LCD_ROTATE_INTERVAL_MS) {
      lastLcdRotateMs = now;
      updateLcd();
    }
  }

  if (now - lastServerPostMs >= SERVER_POST_INTERVAL_MS) {
    lastServerPostMs = now;
    postReading();
  }
}
