/*
  Smart Food Monitoring - Modified for GC2145 (RHYX M21-45) Camera

  Captures a JPEG every CAPTURE_INTERVAL_MS and uploads it to the server (for
  Gemini analysis), and also runs a lightweight MJPEG live-video server on
  port 81 so the dashboard can show a real-time feed on demand - useful for
  checking camera positioning/focus/exposure without waiting for the next
  scheduled capture.

  The dashboard's browser connects to http://<this board's IP>:81/stream
  directly (not through the main server) since both are on the same local
  network. This board reports its current IP to the server periodically so
  the dashboard can find it automatically.

  Note: while someone is actively watching the live stream, the periodic
  capture-and-upload is paused (both use the same camera hardware) - it
  resumes as soon as the stream viewer disconnects.
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

#define WIFI_STATUS_INTERVAL_MS 5000
#define STREAM_PORT 81
// Gap between frames while streaming - throttles the live feed's frame rate.
#define STREAM_FRAME_DELAY_MS 50

unsigned long lastCaptureMs = 0;
unsigned long lastWifiStatusMs = 0;

WiFiServer streamServer(STREAM_PORT);

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

void reportCameraStatus() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  String url = String("http://") + SERVER_HOST + ":" + SERVER_PORT + "/api/camera/status";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(3000);
  http.POST("{\"ip\":\"" + WiFi.localIP().toString() + "\"}");
  http.end();
}

void printWifiStatus() {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi: Connected (IP ");
    Serial.print(WiFi.localIP());
    Serial.println(")");
    reportCameraStatus();
  } else {
    Serial.println("WiFi: Disconnected");
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

  // === RHYX M21-45 Specific Hardware Settings ===
  config.xclk_freq_hz = 10000000;         // Lower clock frequency for stability
  config.pixel_format = PIXFORMAT_RGB565; // Must capture raw formats
  config.frame_size = FRAMESIZE_QVGA;     // 320x240 limit to prevent PSRAM overflow during software encoding
  config.jpeg_quality = 12;
  config.fb_count = 1;

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

  // === Convert Raw RGB565 to JPEG ===
  uint8_t * jpeg_buf = NULL;
  size_t jpeg_len = 0;
  bool jpeg_converted = frame2jpg(fb, 80, &jpeg_buf, &jpeg_len); // Quality set to 80

  if (!jpeg_converted) {
    Serial.println("JPEG compression failed");
    return false;
  }

  String boundary = "FoodMonitorBoundary";
  String head = "--" + boundary + "\r\n"
                "Content-Disposition: form-data; name=\"image\"; filename=\"capture.jpg\"\r\n"
                "Content-Type: image/jpeg\r\n\r\n";
  String tail = "\r\n--" + boundary + "--\r\n";

  // Calculate payload size based on the new compressed JPEG length
  size_t totalLen = head.length() + jpeg_len + tail.length();

  HTTPClient http;
  String url = String("http://") + SERVER_HOST + ":" + SERVER_PORT + "/api/images";
  http.begin(url);
  http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);

  uint8_t *buf = (uint8_t *)malloc(totalLen);
  if (!buf) {
    Serial.println("Not enough memory to build upload buffer.");
    free(jpeg_buf); // Free the software-encoded buffer
    return false;
  }

  size_t pos = 0;
  memcpy(buf + pos, head.c_str(), head.length()); pos += head.length();

  // Copy the converted JPEG buffer instead of raw camera buffer
  memcpy(buf + pos, jpeg_buf, jpeg_len); pos += jpeg_len;

  memcpy(buf + pos, tail.c_str(), tail.length()); pos += tail.length();

  int status = http.POST(buf, totalLen);
  Serial.printf("POST /api/images -> %d\n", status);

  // Clean up all allocated memory
  free(buf);
  free(jpeg_buf);

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

// Serves a standard MJPEG stream (multipart/x-mixed-replace) - browsers can
// show this directly in a plain <img> tag. Blocks for as long as a viewer
// stays connected, since it's one frame-grab loop per client; returns
// immediately if nobody's currently trying to connect.
void handleStreamClient() {
  WiFiClient client = streamServer.available();
  if (!client) return;

  Serial.println("Stream: client connected");
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: multipart/x-mixed-replace; boundary=frame");
  client.println("Connection: close");
  client.println();

  while (client.connected()) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) break;

    uint8_t *jpegBuf = NULL;
    size_t jpegLen = 0;
    bool converted = frame2jpg(fb, 80, &jpegBuf, &jpegLen);
    esp_camera_fb_return(fb);
    if (!converted) break;

    client.println("--frame");
    client.println("Content-Type: image/jpeg");
    client.printf("Content-Length: %u\r\n\r\n", (unsigned)jpegLen);
    client.write(jpegBuf, jpegLen);
    client.println();
    free(jpegBuf);

    if (!client.connected()) break;
    delay(STREAM_FRAME_DELAY_MS);
  }

  client.stop();
  Serial.println("Stream: client disconnected");
}

void setup() {
  Serial.begin(115200);
  delay(200);

  if (!initCamera()) {
    Serial.println("Halting: camera init failed.");
    while (true) delay(1000);
  }

  connectWiFi();
  streamServer.begin();
  lastCaptureMs = millis() - CAPTURE_INTERVAL_MS + 5000;
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  handleStreamClient();

  unsigned long now = millis();

  if (now - lastWifiStatusMs >= WIFI_STATUS_INTERVAL_MS) {
    lastWifiStatusMs = now;
    printWifiStatus();
  }

  if (now - lastCaptureMs >= CAPTURE_INTERVAL_MS) {
    lastCaptureMs = now;
    captureAndSend();
  }
}
