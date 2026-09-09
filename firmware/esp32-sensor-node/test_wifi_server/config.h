// Copy this file to config.h and fill in your own values. config.h is gitignored.

#define WIFI_SSID     "Helloo World"
#define WIFI_PASSWORD "bikan9700"

// Local IP (or hostname) of the PC/Pi running the server, e.g. 192.168.1.50
#define SERVER_HOST "192.168.0.4"
#define SERVER_PORT 3000

// How often to sample sensors (ms). DHT11 tops out around 1 reading/second.
#define SENSOR_READ_INTERVAL_MS 1000
// How often to POST a reading to the server (ms) - matches the read interval
// for a near-real-time dashboard.
#define SERVER_POST_INTERVAL_MS 1000

// DHT11 additive correction, from dht11_calibration.ino, added to every raw
// reading. Leave at 0 until you've calibrated against a trusted reference.
#define DHT_TEMP_OFFSET 0.0f
#define DHT_HUMIDITY_OFFSET 0.0f

// MQ-5 raw ADC thresholds (0-4095 on ESP32), from mq5_calibration.ino - clean
// open-air baseline settled at ~391 (converged tightly over several samples,
// down from ~1872 right after the wiring fix, then ~258 - MQ-5 baselines
// keep dropping as the heater element stabilizes with more total warm-up
// time). Re-run that sketch again if you move/reseat the sensor.
#define GAS_WARNING_THRESHOLD 691
#define GAS_DANGER_THRESHOLD  1191

// HX711 calibration factor: raw_reading / calibration_factor = grams.
// Get this from firmware/esp32-sensor-node/hx711_calibration/hx711_calibration.ino.
// It can come out NEGATIVE depending on your load cell's wiring polarity -
// that's expected, just use it as printed, minus sign included.
#define HX711_CALIBRATION_FACTOR -397.80f

// Extra correction subtracted from every weight reading, on top of the
// boot-time tare(). The empty scale was observed reading ~1350-1360g instead
// of 0 (tare landing off-zero, likely temperature drift or mounting stress) -
// this cancels that out. Re-measure with the scale empty and adjust if it
// drifts again.
#define HX711_WEIGHT_OFFSET 1355.0f

// LCD I2C address - try 0x27 first, 0x3F is the other common default.
#define LCD_I2C_ADDRESS 0x27
