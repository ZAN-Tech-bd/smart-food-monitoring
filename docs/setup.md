# Setup

## 1. Server

```bash
cd server
npm install
cp .env.example .env
```

Edit `server/.env`:
- `GEMINI_API_KEY` — get one at https://aistudio.google.com/apikey
- `PORT` — defaults to 3000
- `GAS_WARNING_THRESHOLD` / `GAS_DANGER_THRESHOLD` — should match the firmware's `config.h` values

Run it:

```bash
npm run start
```

Open `http://localhost:3000` (or `http://<this-pc's-LAN-IP>:3000` from another device on the same WiFi) to see the dashboard.

Find your PC's local IP (needed for the firmware configs below):
- Windows: `ipconfig` → look for "IPv4 Address" under your active adapter
- Mac/Linux: `ifconfig` or `ip addr`

## 2. Arduino IDE setup (both boards)

1. Install the ESP32 board package: File → Preferences → "Additional Boards Manager URLs" → add
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`,
   then Tools → Board → Boards Manager → install "esp32".
2. Install libraries via Sketch → Include Library → Manage Libraries:
   - `DHT sensor library` (Adafruit) + `Adafruit Unified Sensor`
   - `HX711` (bogde/HX711)
   - `LiquidCrystal I2C` (Frank de Brabander)
   - (ESP32-CAM needs no extra libraries — `esp_camera.h` ships with the ESP32 board package)

## 3. Sensor node (ESP32 Dev Board)

```bash
cd firmware/esp32-sensor-node
cp config.h.example config.h
```

Edit `config.h` with your WiFi credentials and the server's local IP. Wire the board per `docs/wiring.md`, tare the load cell with nothing on it, select your ESP32 dev board under Tools → Board, then upload `esp32-sensor-node.ino`. Watch the Serial Monitor (115200 baud) for WiFi connect and `POST /api/sensors` logs.

## 4. Camera node (ESP32-CAM)

```bash
cd firmware/esp32-cam-node
cp config.h.example config.h
```

Edit `config.h` the same way. Select Tools → Board → "AI Thinker ESP32-CAM", wire the FTDI adapter and GPIO0→GND per `docs/wiring.md` for flashing, upload `esp32-cam-node.ino`, then disconnect GPIO0 and reset. It captures and uploads a photo shortly after boot, then every 5 minutes.

## 5. Verify end-to-end

- Dashboard cards should populate within ~30 seconds of the sensor node booting.
- The "Latest Capture" panel should show a photo within a few seconds of the cam node's first upload, then update to a Gemini verdict a few seconds after that (requires a valid `GEMINI_API_KEY`).
