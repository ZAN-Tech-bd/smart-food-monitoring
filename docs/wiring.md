# Wiring

## ESP32 Dev Board — sensor node

Pins were chosen to avoid the boot-strapping pins (0, 2, 12, 15).

| Component        | ESP32 Pin | Notes |
|-------------------|-----------|-------|
| DHT22 data         | GPIO 4    | Add a 10kΩ pull-up resistor between data and 3.3V if your module doesn't already have one. |
| MQ-5 analog out (AO) | GPIO 34 | Input-only ADC1 pin — fine for `analogRead`. MQ-5 runs on 5V; use a module with an onboard analog comparator/divider so AO stays within 0–3.3V, or add a voltage divider. |
| HX711 DT           | GPIO 16   | |
| HX711 SCK          | GPIO 17   | |
| LCD1602 I2C SDA     | GPIO 21   | Default ESP32 I2C bus |
| LCD1602 I2C SCL     | GPIO 22   | Default ESP32 I2C bus |

Power: DHT22, MQ-5, and the LCD's I2C backpack typically run off 5V (VIN) with logic-level I/O tolerant of 3.3V from the ESP32; HX711 modules are usually fine on 3.3V or 5V — check your specific board. Common ground between the ESP32, all sensors, and the load cell amplifier is required.

LCD I2C address: scan with an I2C scanner sketch if unsure — common defaults are `0x27` and `0x3F`.

## ESP32-CAM — camera node (AI-Thinker module)

No sensors are attached to this board — its GPIOs are almost entirely committed to the camera interface. It just needs power and WiFi.

Flashing requires an external FTDI/USB-serial adapter (the board has no onboard USB):

| FTDI adapter | ESP32-CAM |
|---|---|
| 5V | 5V |
| GND | GND |
| TX | U0R (RX) |
| RX | U0T (TX) |

To enter flashing mode: connect GPIO0 to GND, press the reset button, upload, then disconnect GPIO0 from GND and reset again to run normally.
