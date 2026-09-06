/*
  Smart Food Monitoring - Camera Node (ESP32-CAM, AI-Thinker module)

  Captures a JPEG every CAPTURE_INTERVAL_MS and uploads it to the server as
  multipart/form-data.

  Board setting in Arduino IDE: "AI Thinker ESP32-CAM"
  Flashing: connect an FTDI/USB-serial adapter, tie GPIO0 to GND while
  resetting to enter flash mode, remove the jumper and reset again to run
  normally. See docs/wiring.md and docs/setup.md.

  No extra libraries needed beyond the ESP32 board package (esp_camera.h and
  WiFi.h/HTTPClient.h ship with it).
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include "esp_camera.h"

#include "config.h"

// AI-Thinker ESP32-CAM pin map
#define PWDN_GPIO_NUM  32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM  0
#define SIOD_GPIO_NUM  26
#define SIOC_GPIO_NUM  27
#define Y9_GPIO_NUM    35
#define Y8_GPIO_NUM    34
#define Y7_GPIO_NUM    39
#define Y6_GPIO_NUM    36
#define Y5_GPIO_NUM    21
#define Y4_GPIO_NUM    19
#define Y3_GPIO_NUM    18
#define Y2_GPIO_NUM    5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM  23
#define PCLK_GPIO_NUM  22

unsigned long lastCaptureMs = 0;

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

bool initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_SVGA; // 800x600
    config.jpeg_quality = 12;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 15;
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    return false;
  }
  return true;
}

bool uploadImage(camera_fb_t *fb) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi down, skipping upload.");
    return false;
  }

  String boundary = "FoodMonitorBoundary";
  String head = "--" + boundary + "\r\n"
                "Content-Disposition: form-data; name=\"image\"; filename=\"capture.jpg\"\r\n"
                "Content-Type: image/jpeg\r\n\r\n";
  String tail = "\r\n--" + boundary + "--\r\n";

  size_t totalLen = head.length() + fb->len + tail.length();

  HTTPClient http;
  String url = String("http://") + SERVER_HOST + ":" + SERVER_PORT + "/api/images";
  http.begin(url);
  http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);

  uint8_t *buf = (uint8_t *)malloc(totalLen);
  if (!buf) {
    Serial.println("Not enough memory to build upload buffer.");
    return false;
  }
  size_t pos = 0;
  memcpy(buf + pos, head.c_str(), head.length()); pos += head.length();
  memcpy(buf + pos, fb->buf, fb->len); pos += fb->len;
  memcpy(buf + pos, tail.c_str(), tail.length()); pos += tail.length();

  int status = http.POST(buf, totalLen);
  Serial.printf("POST /api/images -> %d\n", status);
  free(buf);
  http.end();
  return status >= 200 && status < 300;
}

void captureAndSend() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed.");
    return;
  }

  bool ok = uploadImage(fb);
  if (!ok) {
    Serial.println("Upload failed, retrying once...");
    delay(2000);
    uploadImage(fb);
  }

  esp_camera_fb_return(fb);
}

void setup() {
  Serial.begin(115200);
  delay(200);

  if (!initCamera()) {
    Serial.println("Halting: camera init failed.");
    while (true) delay(1000);
  }

  connectWiFi();

  // Capture once shortly after boot so the dashboard has an image right away.
  lastCaptureMs = millis() - CAPTURE_INTERVAL_MS + 5000;
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  unsigned long now = millis();
  if (now - lastCaptureMs >= CAPTURE_INTERVAL_MS) {
    lastCaptureMs = now;
    captureAndSend();
  }
}
