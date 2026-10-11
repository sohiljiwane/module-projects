# module-projects
## ESP32-S3 2.8" Touch Display: Setup and Test

Flashing and testing the ESP32-S3 2.8" capacitive touch display (believed to be the LCDWIKI ES3C28P: ILI9341V display, FT6336G touch) from a CLI-only Ubuntu server over USB-C, using PlatformIO.

### Hardware

| Item | Value |
|---|---|
| MCU | ESP32-S3 (16 MB flash, octal PSRAM assumed, N16R8) |
| Display | 2.8" IPS, ILI9341V, SPI, 240x320 |
| Touch | FT6336G, I2C address `0x38` |

Pins:

| Signal | GPIO |
|---|---|
| TFT SCLK / MOSI / MISO | 12 / 11 / 13 |
| TFT CS / DC / RST | 10 / 46 / none (shared with EN) |
| TFT backlight | 45 |
| Touch SDA / SCL | 16 / 15 |
| Touch INT / RST | 17 / 18 |

### 1. Connect and verify

Plug the board into the server with a **data** USB-C cable (charge-only cables won't enumerate).

```bash
lsusb
ls /dev/ttyACM*          # expect /dev/ttyACM0
sudo dmesg | tail
```

### 2. Permissions and conflicting services

```bash
sudo usermod -aG dialout $USER     # then log out and back in
sudo apt remove -y brltty          # can grab the serial port
sudo systemctl disable --now ModemManager
```

### 3. Install PlatformIO

```bash
sudo apt update && sudo apt install -y python3-venv git
python3 -m venv ~/pio && source ~/pio/bin/activate
pip install platformio
```

### 4. Project files

```bash
mkdir -p ~/esp32-test/src ~/esp32-test/include && cd ~/esp32-test
```

**`platformio.ini`**

```ini
[env:es3c28p]
platform = espressif32@6.9.0
board = esp32-s3-devkitc-1
framework = arduino
monitor_speed = 115200
board_build.arduino.memory_type = qio_opi
board_upload.flash_size = 16MB
lib_deps = bodmer/TFT_eSPI@^2.5.43
build_flags =
    -DARDUINO_USB_CDC_ON_BOOT=1
    -include include/tft_setup.h
```

Notes:
- `espressif32@6.9.0` is pinned because TFT_eSPI doesn't compile against the newer Arduino-ESP32 3.x core.
- `ARDUINO_USB_CDC_ON_BOOT=1` sends `Serial` output over the native USB port (`ttyACM0`).
- The display settings live in a header rather than `-D` flags, because pin flags pasted into `platformio.ini` got garbled and broke the build.

**`include/tft_setup.h`**

```c
#define USER_SETUP_LOADED 1
#define USE_HSPI_PORT
#define ILI9341_DRIVER
#define TFT_WIDTH 240
#define TFT_HEIGHT 320
#define TFT_MOSI 11
#define TFT_SCLK 12
#define TFT_MISO 13
#define TFT_CS 10
#define TFT_DC 46
#define TFT_RST -1
#define TFT_BL 45
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define SPI_FREQUENCY 40000000
```

`USE_HSPI_PORT` is required. Without it the board boots into a crash loop (`StoreProhibited`) at `tft.init()`.

**`src/main.cpp`**

```cpp
#include <Arduino.h>
#include <Wire.h>
#include <TFT_eSPI.h>

#define TP_SDA 16
#define TP_SCL 15
#define TP_INT 17
#define TP_RST 18
#define BL_PIN 45

TFT_eSPI tft;

bool readTouch(int &x, int &y) {
  Wire.beginTransmission(0x38);
  Wire.write(0x02);
  if (Wire.endTransmission(false) != 0) return false;
  Wire.requestFrom(0x38, 5);
  if (Wire.available() < 5) return false;
  uint8_t n = Wire.read() & 0x0F;
  uint8_t b[4];
  for (int i = 0; i < 4; i++) b[i] = Wire.read();
  if (n == 0) return false;
  x = ((b[0] & 0x0F) << 8) | b[1];
  y = ((b[2] & 0x0F) << 8) | b[3];
  return true;
}

void setup() {
  Serial.begin(115200);
  pinMode(BL_PIN, OUTPUT);
  digitalWrite(BL_PIN, HIGH);

  pinMode(TP_RST, OUTPUT);
  digitalWrite(TP_RST, LOW); delay(10);
  digitalWrite(TP_RST, HIGH); delay(50);
  Wire.begin(TP_SDA, TP_SCL);

  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("ESP32-S3 alive", 10, 10, 4);
  tft.drawString("Touch the screen", 10, 50, 2);
  Serial.println("ESP32-S3 alive");
}

void loop() {
  int x, y;
  if (readTouch(x, y)) {
    Serial.printf("touch %d,%d\n", x, y);
    tft.fillRect(0, 80, 240, 20, TFT_BLACK);
    tft.setCursor(10, 80);
    tft.printf("x=%d y=%d", x, y);
    if (x >= 0 && x < 240 && y >= 0 && y < 320) {
      tft.fillCircle(x, y, 4, TFT_GREEN);
    }
  }
  delay(20);
}
```

### 5. Build, flash, monitor

```bash
source ~/pio/bin/activate
cd ~/esp32-test
pio run -t upload
pio device monitor        # Ctrl+C to exit
```

Expected: "ESP32-S3 alive" on the screen and in the monitor, and `touch x,y` lines when the screen is touched.

Close the monitor before uploading. Only one program can hold the port.

### Troubleshooting

| Symptom | Fix |
|---|---|
| No `/dev/ttyACM0` | Swap to a data cable, try the other USB-C port, check `dmesg` |
| Upload times out | Hold **BOOT**, tap **RESET**, release BOOT, re-run upload (port may become `ttyACM1`) |
| Boot loop with `StoreProhibited` | Make sure `#define USE_HSPI_PORT` is in `include/tft_setup.h` |
| `TFT_eSPI.h: No such file` | `lib_deps` missing from `platformio.ini`; add it and run `rm -rf .pio` |
| `TFT_CS` / "missing binary operator" error | Move pin settings into `include/tft_setup.h`, not `-D` flags |
| Colors inverted | Add `#define TFT_INVERSION_ON` to `tft_setup.h` |
| Boot loop with PSRAM or flash errors | Remove the `memory_type` and `flash_size` lines, or try `qio_qspi` |
| Touch logged but no dots on screen | Check the four corner values; the coordinates may need swapping or mirroring (still to verify) |
| Garbled characters in config | Retype it; chat or web copy/paste can turn `-` into a typographic dash |

### Safe disconnect

Unplug any time except while an upload is running. Close the serial monitor first.

## ESP32-S3 WiFi Test

Standalone PlatformIO project to verify the WiFi connectivity of an ESP32-S3 module. It connects to your network over serial and reports the full link status, then exercises DNS and HTTP to confirm end-to-end internet access.

### What it tests

1. **Association** — connects to your WiFi (20 s timeout, auto-retries every 10 s if the link drops).
2. **Link status** — every 10 s while connected it prints:
   - status, IP, subnet mask, gateway, DNS server
   - MAC address, RSSI (signal strength), BSSID, channel
3. **DNS** — resolves `www.google.com`.
4. **HTTP** — issues a `GET /` to `www.google.com` and reports the number of bytes received.

Seeing `HTTP OK: received NNNN bytes` confirms the whole path works: radio → DHCP → DNS → internet. If it associates but DNS/HTTP fail, the problem is upstream of the module.

### Project layout

```
wifi-test/
├── platformio.ini        # build config (esp32-s3-devkitc-1, 16 MB flash, USB CDC)
├── include/
│   └── config.h          # your WiFi credentials — edit this
└── src/
    └── main.cpp          # the test
```

### Setup

1. Edit `wifi-test/include/config.h` and set your network:

   ```c
   #define WIFI_SSID "YOUR_WIFI_SSID"
   #define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
   ```

   The SSID is the name of your WiFi network (shown in your phone/laptop's WiFi list, or printed on a label on the router).

2. Optional — use a fixed IP instead of DHCP by uncommenting and editing:

   ```c
   #define STATIC_IP "192.168.1.100"
   #define STATIC_GW  "192.168.1.1"
   #define STATIC_MASK "255.255.255.0"
   ```

### Build & flash

```bash
cd wifi-test
pio run -t upload
pio run -t monitor
```

The serial monitor (115200 baud) shows the connection progress and the periodic status report.

### Troubleshooting

| Symptom | Likely cause |
| --- | --- |
| `connect failed` / `no ssid` | Wrong SSID or the network is out of range |
| `auth failure` | Wrong password |
| Connects, but `DNS lookup FAILED` | Router's DNS is unreachable; check the gateway |
| Connects, DNS OK, `HTTP connection FAILED` | Outbound traffic blocked (firewall / captive portal) |
| No serial output | Check the USB port and that `ARDUINO_USB_CDC_ON_BOOT` is set (it is, in `platformio.ini`) |
