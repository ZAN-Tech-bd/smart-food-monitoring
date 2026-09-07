# Smart Food Monitoring

An ESP32-based food storage monitor. A sensor node tracks temperature, humidity, gas/smoke, and weight and shows them on an LCD; a second board (ESP32-CAM) photographs the food every 5 minutes; a local Node.js server logs everything to SQLite, sends each photo to Google Gemini (along with the latest sensor readings) for a freshness/spoilage assessment, and shows it all live on a web dashboard.

This README is self-contained: follow it top to bottom and you can build the whole thing from this repo alone — no prior ESP32 or Node experience assumed.

---

## Table of contents

1. [How it works](#how-it-works)
2. [Bill of materials](#bill-of-materials)
3. [Circuit diagrams / wiring](#circuit-diagrams--wiring)
4. [Firmware: Arduino IDE setup](#firmware-arduino-ide-setup)
5. [Firmware: flashing the sensor node](#firmware-flashing-the-sensor-node)
6. [Firmware: flashing the camera node](#firmware-flashing-the-camera-node)
7. [Server: install and run](#server-install-and-run)
8. [Getting a Gemini API key](#getting-a-gemini-api-key)
9. [API reference](#api-reference)
10. [Dashboard](#dashboard)
11. [Project structure](#project-structure)
12. [Troubleshooting](#troubleshooting)

---

## How it works

```
                 WiFi (HTTP POST /api/sensors, every 30s)
 [ESP32 Sensor Node] ─────────────────────────────────────────┐
   DHT11, MQ-5,                                                │
   HX711+load cell,                                            ▼
   16x2 I2C LCD                                     ┌────────────────────┐
                                                     │   Node.js server    │
                 WiFi (HTTP POST /api/images,        │  Express + SQLite   │
                  multipart JPEG, every 5 min)       │  + Socket.IO        │
 [ESP32-CAM] ───────────────────────────────────────▶│                    │
                                                     │  on new image:      │
                                                     │   → call Gemini API │
                                                     │   → store verdict   │
                                                     └─────────┬──────────┘
                                                               │ Socket.IO (live push)
                                                               ▼
                                                     ┌────────────────────┐
                                                     │  Admin dashboard    │
                                                     │  (browser)          │
                                                     └────────────────────┘
```

- The **sensor node** is a plain ESP32 dev board wired to all the sensors. It reads them continuously, rotates the readout across the 16x2 LCD, and POSTs a JSON reading to the server every 30 seconds.
- The **camera node** is a separate ESP32-CAM (it has almost no free GPIO pins once the camera is wired up, so it does nothing else). It wakes up every 5 minutes, takes a JPEG, and uploads it to the server.
- The **server** stores every sensor reading and every image in SQLite. When an image arrives, it grabs the most recent sensor reading as a "snapshot" and sends the image + those readings to the **Gemini API**, asking for a `Fresh / Caution / Spoiled / Unclear` verdict with a short explanation. The result is saved back to the database.
- The **dashboard** is a web page served by the same server. It loads the latest state on open and then updates live over a WebSocket (Socket.IO) as new readings and images come in — no manual refresh needed.

---

## Bill of materials

| Qty | Part | Purpose |
|---|---|---|
| 1 | ESP32 Development Board (any generic "ESP32 DevKit" / "ESP32-WROOM-32" board) | Sensor node — reads all sensors, drives the LCD |
| 1 | ESP32-CAM (AI-Thinker module) | Camera node — takes and uploads photos |
| 1 | FTDI / USB-to-serial adapter (3.3V or 5V logic) | Needed to flash the ESP32-CAM, which has no onboard USB |
| 1 | MQ-5 smoke/LPG gas sensor module | Gas/smoke detection |
| 1 | DHT11 temperature/humidity sensor | Temperature + humidity |
| 1 | 16x2 LCD with I2C backpack (PCF8574, address `0x27` or `0x3F`) | On-device readout |
| 1 | Momentary push button | Toggles the LCD between sensor readings and the latest AI (Gemini) verdict |
| 1 | Load cell (e.g. 1kg/5kg bar-type) + HX711 amplifier board | Weight of stored food |
| — | Breadboard, jumper wires, 10kΩ resistor (for DHT11 pull-up if not built into your module), 5V power supply(s) | General build |

---

## Circuit diagrams / wiring

### Sensor node (ESP32 Dev Board)

Pins are chosen to avoid the ESP32's boot-strapping pins (0, 2, 12, 15), which can prevent boot if held high/low by an external circuit.

```
                              ┌───────────────────────────┐
                              │        ESP32 DevKit         │
                              │                             │
     DHT11 ── VCC ───────────►│ 3V3                         │
     DHT11 ── DATA ──────────►│ GPIO4                       │
     DHT11 ── GND ───────────►│ GND                         │
                              │                             │
     MQ-5  ── VCC ───────────►│ 5V (VIN)                    │
     MQ-5  ── AOUT ──────────►│ GPIO34 (ADC1, input-only)   │
     MQ-5  ── GND ───────────►│ GND                         │
                              │                             │
     HX711 ── VCC ───────────►│ 3V3 (or 5V, check module)   │
     HX711 ── DT ────────────►│ GPIO16                      │
     HX711 ── SCK ───────────►│ GPIO17                      │
     HX711 ── GND ───────────►│ GND                         │
     HX711 ── E+/E-/A+/A- ────── to load cell's 4 wires      │
                              │                             │
     LCD I2C ── VCC ─────────►│ 5V (VIN)                    │
     LCD I2C ── SDA ─────────►│ GPIO21                      │
     LCD I2C ── SCL ─────────►│ GPIO22                      │
     LCD I2C ── GND ─────────►│ GND                         │
                              │                             │
     BUTTON ── one leg ──────►│ GPIO13                      │
     BUTTON ── other leg ────►│ GND                         │
                              └───────────────────────────┘
```

Pin table:

| Component | Pin | ESP32 Pin | Notes |
|---|---|---|---|
| DHT11 | DATA | GPIO 4 | Add a 10kΩ pull-up resistor between DATA and 3.3V if your module doesn't already include one |
| MQ-5 | AOUT | GPIO 34 | Input-only ADC1 pin — fine for `analogRead`. MQ-5 modules run on 5V; use one with an onboard comparator/divider so AOUT stays within 0–3.3V, or add your own voltage divider |
| HX711 | DT | GPIO 16 | |
| HX711 | SCK | GPIO 17 | |
| HX711 | E+, E-, A+, A- | — | Wired to the load cell's 4 wires (usually red/black = E+/E-, white/green = A+/A-; check your load cell's datasheet, colors vary) |
| LCD1602 (I2C backpack) | SDA | GPIO 21 | Default ESP32 I2C bus |
| LCD1602 (I2C backpack) | SCL | GPIO 22 | Default ESP32 I2C bus |
| Push button | one leg | GPIO 13 | The other leg goes straight to GND — no external resistor needed, the firmware uses the pin's internal pull-up |

Power notes:
- DHT11, MQ-5, and the LCD's I2C backpack typically run on 5V (from the ESP32's `VIN` pin, itself powered from USB or an external 5V supply); their signal lines are fine with the ESP32's 3.3V logic.
- HX711 breakout boards run on 3.3–5V depending on the model — check yours.
- **All grounds must be tied together** (ESP32, DHT11, MQ-5, HX711, LCD, button).
- Find your LCD's I2C address if unsure by running an "I2C scanner" sketch (search "ESP32 I2C scanner" — a few lines of code that print any address found on the bus). Common values are `0x27` and `0x3F`; set it in `firmware/esp32-sensor-node/config.h` as `LCD_I2C_ADDRESS`.
- Before first boot, make sure nothing is resting on the load cell — the firmware tares (zeroes) the scale automatically on startup.

### Camera node (ESP32-CAM, AI-Thinker module)

No sensors attach to this board — nearly every GPIO is committed to the camera interface. It only needs power, and a temporary serial connection to flash it.

**Flashing wiring** (FTDI adapter ↔ ESP32-CAM):

| FTDI adapter | ESP32-CAM |
|---|---|
| 5V | 5V |
| GND | GND |
| TX | U0R (RX) |
| RX | U0T (TX) |
| — | GPIO0 → GND (jumper wire, only while flashing) |

Flashing procedure:
1. Wire the FTDI adapter as above, including the GPIO0→GND jumper.
2. Press the ESP32-CAM's reset button (or power-cycle it) to enter flashing mode.
3. Upload the sketch from the Arduino IDE.
4. Remove the GPIO0→GND jumper and reset the board again — it will now boot normally and start capturing photos.

**Normal operation**: just 5V and GND power (from the FTDI adapter, a USB power bank, or a bench supply) — no jumper, no data lines needed once flashed.

---

## Firmware: Arduino IDE setup

Do this once, before flashing either board.

1. Install [Arduino IDE](https://www.arduino.cc/en/software) (2.x recommended).
2. Add ESP32 board support: **File → Preferences → Additional Boards Manager URLs**, add:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
   Then **Tools → Board → Boards Manager**, search "esp32", install the package by Espressif Systems.
3. Install libraries via **Sketch → Include Library → Manage Libraries**, search for and install:
   - `DHT sensor library` (by Adafruit) — and let it also install its dependency `Adafruit Unified Sensor`
   - `HX711` (by bogde)
   - `LiquidCrystal I2C` (by Frank de Brabander)
   - The ESP32-CAM sketch needs **no extra libraries** — `esp_camera.h` and `WiFi.h`/`HTTPClient.h` ship with the ESP32 board package installed above.

---

## Firmware: flashing the sensor node

1. Open `firmware/esp32-sensor-node/esp32-sensor-node.ino` in Arduino IDE.
2. Copy the config template and edit it:
   ```bash
   cd firmware/esp32-sensor-node
   cp config.h.example config.h
   ```
   Open `config.h` and set:
   - `WIFI_SSID`, `WIFI_PASSWORD` — your WiFi network
   - `SERVER_HOST` — the local IP address of the PC that will run the server (see [Server: install and run](#server-install-and-run) for how to find it), e.g. `192.168.1.50`
   - `SERVER_PORT` — leave as `3000` unless you changed it on the server
   - `GAS_WARNING_THRESHOLD` / `GAS_DANGER_THRESHOLD` — raw ADC thresholds (0–4095) for the "Warning"/"Danger" gas labels; tune to your MQ-5 module and environment
   - `HX711_CALIBRATION_FACTOR` — see calibration note below
   - `LCD_I2C_ADDRESS` — from your I2C scan, usually `0x27` or `0x3F`
3. Wire the board per [Circuit diagrams / wiring](#circuit-diagrams--wiring) above. Make sure nothing is on the load cell.
4. In Arduino IDE: **Tools → Board** → select your ESP32 dev board (e.g. "ESP32 Dev Module"), **Tools → Port** → select the board's serial port.
5. Click **Upload**.
6. Open **Tools → Serial Monitor** at 115200 baud. You should see WiFi connect, then periodic `POST /api/sensors -> 201` log lines (once the server is running — see below) and the LCD should start cycling through Temp/Humidity → Gas → Weight screens.

**The push button** toggles the LCD to a second mode: it fetches the latest Gemini verdict + notes from the server and shows it (scrolling the notes line if it's longer than 16 characters), refreshing every 5 seconds while you're on that screen. Press it again to go back to the sensor screens.

**Calibrating the load cell** (`HX711_CALIBRATION_FACTOR`): use the standalone `firmware/esp32-sensor-node/hx711_calibration/hx711_calibration.ino` sketch instead of guessing values in the main sketch. Upload it, follow the Serial Monitor prompts (tare with nothing on the scale, then type in the weight of a known reference object), and it prints the exact `HX711_CALIBRATION_FACTOR` to paste into `config.h`. Then re-flash the main `esp32-sensor-node.ino` sketch.

---

## Firmware: flashing the camera node

1. Open `firmware/esp32-cam-node/esp32-cam-node.ino` in Arduino IDE.
2. Copy the config template and edit it:
   ```bash
   cd firmware/esp32-cam-node
   cp config.h.example config.h
   ```
   Set `WIFI_SSID`, `WIFI_PASSWORD`, `SERVER_HOST`, `SERVER_PORT` the same way as above. `CAPTURE_INTERVAL_MS` defaults to 300000 (5 minutes).
3. Wire the FTDI adapter per [Circuit diagrams / wiring](#circuit-diagrams--wiring), including the GPIO0→GND flashing jumper.
4. In Arduino IDE: **Tools → Board** → "AI Thinker ESP32-CAM", **Tools → Port** → the FTDI adapter's port.
5. Reset the board to enter flashing mode, then click **Upload**.
6. After upload finishes, remove the GPIO0→GND jumper and reset the board again.
7. Open Serial Monitor at 115200 baud (optional, useful for debugging) — you'll see WiFi connect, then `POST /api/images -> 201` shortly after boot and every 5 minutes after that.

---

## Server: install and run

Requires [Node.js](https://nodejs.org/) 18 or newer.

```bash
cd server
npm install
cp .env.example .env
```

Edit `server/.env`:

| Variable | Description |
|---|---|
| `PORT` | Port the server listens on (default `3000`) |
| `GEMINI_API_KEY` | Your Gemini API key — see [Getting a Gemini API key](#getting-a-gemini-api-key). Leave blank to run everything except AI analysis (images are still stored; the verdict will just show "Unknown") |
| `GEMINI_MODEL` | Gemini model to use (default `gemini-2.0-flash`) |
| `GAS_WARNING_THRESHOLD` / `GAS_DANGER_THRESHOLD` | Should match the values in `firmware/esp32-sensor-node/config.h` so the dashboard's gas badge agrees with the LCD |

Run the server:

```bash
npm run start
```

You should see:

```
Smart Food Monitoring server running at http://localhost:3000
```

Open `http://localhost:3000` in a browser on the same PC, or `http://<pc-local-ip>:3000` from any other device on the same WiFi network (phone, laptop, etc.) to view the dashboard.

**Finding the PC's local IP** (needed for the ESP32 `config.h` files above):
- Windows: open Command Prompt, run `ipconfig`, look for "IPv4 Address" under your active network adapter (Wi-Fi or Ethernet).
- macOS/Linux: run `ifconfig` (or `ip addr` on Linux) and look for the address on your active interface.

The server stores its SQLite database at `server/data/food-monitoring.sqlite` and uploaded photos at `server/uploads/` — both are created automatically on first run and are gitignored.

For development, `npm run dev` restarts the server automatically on file changes (uses Node's built-in `--watch`).

---

## Getting a Gemini API key

1. Go to [Google AI Studio](https://aistudio.google.com/apikey) and sign in with a Google account.
2. Click **Create API key** (a free tier is available; check current quotas/pricing on that page).
3. Copy the key into `server/.env` as `GEMINI_API_KEY=...`.
4. Restart the server (`npm run start`) after adding or changing the key.

Without a key set, the server still runs and stores everything normally — the Gemini step just returns a verdict of `"Unknown"` with an explanatory note instead of erroring out.

---

## API reference

All endpoints are served by the Node server (default `http://<server-ip>:3000`). JSON in, JSON out, except image upload which is `multipart/form-data`.

### `POST /api/sensors`

Called by the **sensor node** every 30 seconds. Stores one reading and broadcasts it live to the dashboard.

Request body (JSON):
```json
{
  "temperature": 22.5,
  "humidity": 55,
  "gas_raw": 900,
  "weight_g": 312.4
}
```
All four fields are required numbers. Responds `201 Created` with the stored row (including `id` and `created_at`), or `400 Bad Request` if any field is missing/non-numeric.

Example:
```bash
curl -X POST http://localhost:3000/api/sensors \
  -H "Content-Type: application/json" \
  -d '{"temperature":22.5,"humidity":55,"gas_raw":900,"weight_g":312.4}'
```

### `GET /api/sensors/latest`

Returns the most recent sensor reading (or `null` if none yet).

### `GET /api/sensors/history?limit=50`

Returns up to `limit` (max 500, default 50) recent readings, oldest first — used to draw the dashboard's chart.

### `POST /api/images`

Called by the **camera node** every 5 minutes. Multipart form upload with a single field:

| Field | Type | Description |
|---|---|---|
| `image` | file (JPEG) | The captured photo |

The server saves the file under `server/uploads/`, snapshots the latest sensor reading onto the new row, responds `201 Created` immediately with the stored row, and **asynchronously** calls the Gemini API in the background. When analysis finishes, the row is updated with `gemini_verdict` / `gemini_notes` and pushed to the dashboard live.

Example:
```bash
curl -X POST http://localhost:3000/api/images -F "image=@photo.jpg"
```

### `GET /api/images?page=1&limit=5`

Paginated list of images, newest first, with their sensor snapshot and Gemini verdict — used for the dashboard's image log. `limit` maxes out at 50 (default 5).

```json
{ "items": [ /* up to `limit` images */ ], "page": 1, "limit": 5, "total": 12, "totalPages": 3 }
```

### `GET /api/images/latest`

Returns the single most recent image row as JSON (or `null` if none yet).

Add `?format=text` to get a plain two-line body instead of JSON — `<verdict>\n<notes>` — used by the **sensor node's push button** so the ESP32 doesn't need a JSON parser to show the AI verdict on the LCD:
```bash
curl "http://localhost:3000/api/images/latest?format=text"
```
```
Fresh
No visible spoilage; color and texture look normal.
```

### `GET /api/dashboard/summary`

Returns everything needed to render the dashboard on initial page load in one call:
```json
{
  "latestReading": { "id": 1, "temperature": 22.5, "humidity": 55, "gas_raw": 900, "weight_g": 312.4, "created_at": "..." },
  "latestImage": { "id": 1, "filepath": "/uploads/....jpg", "gemini_verdict": "Fresh", "gemini_notes": "...", "..." },
  "history": [ /* up to 300 recent readings, ~5 min at the default 1s post interval */ ],
  "thresholds": { "gasWarning": 1500, "gasDanger": 2800 }
}
```

### Real-time updates (Socket.IO)

The dashboard connects to the server's Socket.IO endpoint (same host/port, path `/socket.io`) and listens for:

| Event | Payload | Fired when |
|---|---|---|
| `sensor:update` | a sensor reading row | a new `POST /api/sensors` is received |
| `image:new` | an image row (verdict fields `null`) | a new `POST /api/images` is received, before Gemini has responded |
| `image:analyzed` | the same image row, updated | the Gemini analysis for that image completes |

Any client (not just the built-in dashboard) can connect with the [socket.io-client](https://www.npmjs.com/package/socket.io-client) library and listen for these events to build a custom UI.

---

## Dashboard

Open the server's root URL (`http://<server-ip>:3000`) in any browser on the same network. It shows:
- Live cards for temperature, humidity, gas level (with a Normal/Warning/Danger badge), and weight
- The latest photo with its Gemini verdict and explanation (shows "Analyzing..." until Gemini responds)
- A line chart of recent temperature/humidity/gas history
- A scrollable gallery of past photos with their verdicts

No login is required — it's intended for local network use only (see [Deployment](#) note below). If you plan to expose it beyond your local network, add authentication and HTTPS first.

---

## Project structure

```
smart-food-monitoring/
  firmware/
    esp32-sensor-node/
      esp32-sensor-node.ino     # reads sensors, drives LCD, POSTs readings
      config.h.example          # copy to config.h and fill in WiFi/server details
      hx711_calibration/
        hx711_calibration.ino     # standalone sketch to find HX711_CALIBRATION_FACTOR
    esp32-cam-node/
      esp32-cam-node.ino        # captures + uploads a photo every 5 minutes
      config.h.example
  server/
    package.json
    .env.example                # copy to .env and fill in GEMINI_API_KEY etc.
    src/
      index.js                  # Express app + Socket.IO bootstrap
      db.js                     # SQLite schema + connection
      routes/
        sensors.js               # /api/sensors endpoints
        images.js                 # /api/images endpoints
        dashboard.js               # /api/dashboard/summary
      services/
        gemini.js                  # builds the prompt, calls Gemini, parses the verdict
      public/                    # dashboard CSS/JS
      views/
        dashboard.ejs             # dashboard HTML template
    uploads/                    # saved photos (gitignored)
    data/                       # SQLite database file (gitignored)
  docs/
    wiring.md                  # wiring reference (mirrors this README's wiring section)
    setup.md                   # condensed setup steps (mirrors this README)
```

---

## Troubleshooting

| Symptom | Likely cause / fix |
|---|---|
| ESP32 board won't connect to WiFi | Double-check `WIFI_SSID`/`WIFI_PASSWORD` in `config.h`; ESP32 only supports 2.4GHz networks, not 5GHz |
| Sensor node's `POST /api/sensors` logs a non-201 status or times out | Confirm `SERVER_HOST`/`SERVER_PORT` in `config.h` match the PC's actual local IP and the server's `PORT`; make sure the PC's firewall allows inbound connections on that port; confirm the phone/ESP32 and PC are on the same WiFi network (not a guest network that isolates clients) |
| LCD shows nothing / garbled characters | Wrong I2C address — run an I2C scanner sketch and update `LCD_I2C_ADDRESS`; check SDA/SCL wiring and that the LCD has power |
| Button press does nothing / toggles randomly on its own | Confirm the button's other leg actually reaches GND (not floating) — the firmware relies on the internal pull-up, so a floating pin will read noise and flip modes on its own; check the wire to GPIO 13 isn't loose |
| AI screen shows "No WiFi" | The board lost its WiFi connection — check `printWifiStatus()`'s periodic Serial log; the AI screen only fetches over WiFi, unlike the sensor screens which just show cached local readings |
| AI screen shows "Fetch failed" | The server is unreachable at `SERVER_HOST:SERVER_PORT` from the sensor node, or it's not running — same checks as the sensor POST failures above |
| Weight reading is wildly wrong | Re-run the calibration steps in [Flashing the sensor node](#firmware-flashing-the-sensor-node); make sure the scale was empty at boot (tare happens on startup) |
| Gas reading always low/high | MQ-5 sensors need a short warm-up period after power-on and are sensitive to their specific module's onboard potentiometer/comparator setting; adjust `GAS_WARNING_THRESHOLD`/`GAS_DANGER_THRESHOLD` to your observed baseline |
| ESP32-CAM won't flash / upload fails | Make sure GPIO0 is tied to GND and the board was reset right before clicking Upload; some FTDI adapters need to be set to 5V, others 3.3V — check your CAM board's specs; try a slower upload speed in Tools if it fails repeatedly |
| Camera image never appears on dashboard | Check the CAM node's Serial Monitor for the `POST /api/images` status code; confirm the server is reachable at `SERVER_HOST:SERVER_PORT` from the CAM's network |
| Dashboard shows verdict "Unknown" with a GEMINI_API_KEY message | You haven't set `GEMINI_API_KEY` in `server/.env` yet, or it's invalid — see [Getting a Gemini API key](#getting-a-gemini-api-key), then restart the server |
| `npm install` fails building `better-sqlite3` | It compiles a native module — make sure you're running a supported Node.js version (18+) and, on Windows, that you're using a normal terminal (not one missing `node` on PATH); on Linux you may need build tools (`build-essential`/`python3`) installed |
