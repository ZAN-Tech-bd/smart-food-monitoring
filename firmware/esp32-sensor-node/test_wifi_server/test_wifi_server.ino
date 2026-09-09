/*
  Isolated test: WiFi + server connectivity ONLY.

  No sensors, no LCD, no button - just connects to WiFi and POSTs a
  placeholder reading to the server every second. Use this first when
  debugging boot problems: if this sketch runs cleanly (connects, keeps
  posting, never resets) but the full esp32-sensor-node.ino hangs, the
  problem is in a sensor/peripheral, not WiFi or the server. If THIS sketch
  itself won't boot cleanly, the problem is power/WiFi/network related and
  no sensor is at fault.

  Wiring: none needed - just the bare board plus its USB power/data cable.
*/

#include <WiFi.h>
#include <HTTPClient.h>

#include "config.h"

#define WIFI_STATUS_INTERVAL_MS 5000

unsigned long lastWifiStatusMs = 0;
unsigned long lastServerPostMs = 0;

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

void printWifiStatus() {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("WiFi: Connected (IP %s)\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("WiFi: Disconnected");
  }
}

void postReading() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi down, skipping POST.");
    return;
  }

  String url = String("http://") + SERVER_HOST + ":" + SERVER_PORT + "/api/sensors";
  String body = "{\"temperature\":0,\"humidity\":0,\"gas_raw\":0,\"weight_g\":0}";

  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(5000);

  int status = http.POST(body);
  if (status > 0) {
    Serial.printf("POST /api/sensors -> %d\n", status);
  } else {
    Serial.printf("POST /api/sensors failed: %s\n", http.errorToString(status).c_str());
  }
  http.end();
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("BOOT: Serial up");

  connectWiFi();
  Serial.println("BOOT: connectWiFi() returned, entering loop()");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  unsigned long now = millis();

  if (now - lastWifiStatusMs >= WIFI_STATUS_INTERVAL_MS) {
    lastWifiStatusMs = now;
    printWifiStatus();
  }

  if (now - lastServerPostMs >= SERVER_POST_INTERVAL_MS) {
    lastServerPostMs = now;
    postReading();
  }
}
