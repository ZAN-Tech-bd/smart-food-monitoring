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

  POWER-AWARE BOOT: this board runs off plain USB power (no dedicated 5V
  supply), and WiFi radio + LCD backlight + DHT11 + HX711 all drawing at
  once was causing brownout resets during boot. Two things address that:
    1. WiFi transmit power is capped (WIFI_POWER_13dBm instead of the ~20dBm
       default) and modem sleep is enabled once connected - both cut the
       current spikes WiFi causes, at the cost of some range/throughput
       that doesn't matter for a device sitting near its router.
    2. Peripheral init is staggered with short settle delays instead of
       firing every init back-to-back, so current spikes from consecutive
       steps don't stack on top of each other.
  Boot is still WiFi-first (before any sensor), and every sensor still
  initializes with its own timeout so a missing/faulty one can't hang the
  board - it just reports 0 until it's connected.

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

// Gap between power-hungry boot steps, letting the supply rail recover
// before the next peripheral draws its own current spike.
#define BOOT_SETTLE_MS 150
// Capped WiFi TX power - cuts the current spikes WiFi association/transmit
// bursts cause. Fine for a device close to its access point.
#define WIFI_TX_POWER WIFI_POWER_13dBm

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
  WiFi.setTxPower(WIFI_TX_POWER);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.setSleep(true); // modem sleep between packets - lower average draw
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
  // MQ-5's raw ADC value is noisy sample-to-sample - average a handful of
  // quick samples (analogRead never blocks) so a single noise spike can't
  // falsely trip the Warning/Danger threshold.
  long sum = 0;
  const int samples = 10;
  for (int i = 0; i < samples; i++) {
    sum += analogRead(MQ5_PIN);
    delay(2);
  }
  return sum / samples;
}

void readSensors() {
  // DHT library has its own internal timeout and returns NaN on failure -
  // never blocks, so a disconnected/faulty DHT11 can't hang the board. Only
  // overwrite the last good reading when this one actually succeeded.
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  if (!isnan(h)) lastHumidity = h + DHT_HUMIDITY_OFFSET;
  if (!isnan(t)) lastTemperature = t + DHT_TEMP_OFFSET;

  lastGasRaw = readGasRaw();

  // is_ready() is a quick non-blocking pin check - never call get_units()
  // (which blocks waiting for data) unless it's already true.
  if (scale.is_ready()) {
    lastWeightG = scale.get_units(2) - HX711_WEIGHT_OFFSET;
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
      lcd.print(isnan(lastTemperature) ? 0 : lastTemperature, 1);
      lcd.print("C");
      lcd.setCursor(0, 1);
      lcd.print("Humidity: ");
      lcd.print(isnan(lastHumidity) ? 0 : lastHumidity, 0);
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
  Serial.println("BOOT: Serial up");

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  Serial.println("BOOT: pins configured");

  // WiFi/server connection comes first, before touching any sensor, and at
  // reduced TX power to avoid a big current spike right at boot. The board
  // is online even if a sensor below is missing, slow, or hanging.
  connectWiFi();
  Serial.println("BOOT: connectWiFi() returned");
  delay(BOOT_SETTLE_MS);

  // Every sensor init below is best-effort with its own timeout: if a given
  // sensor doesn't respond in time, we move on immediately and that sensor
  // just reports 0 (see readSensors()/postReading()) until it's connected -
  // we never block waiting for every sensor to be present. Each step also
  // gets a brief settle pause afterward so its current draw doesn't stack
  // on top of the next step's.

  Wire.begin();
  Wire.setTimeOut(1000); // ESP32 core: abort a stuck I2C transaction instead of hanging forever
  Serial.println("BOOT: Wire (I2C) started");
  delay(BOOT_SETTLE_MS);

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Food Monitor");
  lcd.setCursor(0, 1);
  lcd.print("Starting...");
  Serial.println("BOOT: LCD initialized");
  delay(BOOT_SETTLE_MS);

  dht.begin();
  Serial.println("BOOT: DHT started");
  delay(BOOT_SETTLE_MS);

  scale.begin(HX711_DT_PIN, HX711_SCK_PIN);
  scale.set_scale(HX711_CALIBRATION_FACTOR);

  // HX711's tare()/read() has NO built-in timeout - if the module isn't
  // wired/powered correctly it waits forever for a "data ready" signal that
  // never comes. Wait for it ourselves with a timeout so a bad HX711
  // connection can't hang the board; weight just stays 0 until it's ready.
  unsigned long hxWaitStart = millis();
  while (!scale.is_ready() && millis() - hxWaitStart < 2000) {
    delay(10);
  }
  if (scale.is_ready()) {
    scale.tare(); // Make sure the scale is empty when the board boots.
    Serial.println("BOOT: HX711 tared");
  } else {
    Serial.println("BOOT: HX711 not responding within 2s - weight will read 0 until it's connected.");
  }

  Serial.println("BOOT: setup complete, entering loop()");
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
