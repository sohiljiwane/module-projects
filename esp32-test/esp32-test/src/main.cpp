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
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  Wire.requestFrom(0x38, 5);
  if (Wire.available() < 5) {
    return false;
  }
  uint8_t n = Wire.read() & 0x0F;
  uint8_t b[4];
  for (int i = 0; i < 4; i++) {
    Wire.read();
  }
  if (n == 0) { return false; }
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

  Wire.setClock(100000);
  // I2C scan
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) Serial.printf("I2C device at 0x%02X\n", a);
  }
  // chip ID (FT6336 should read 0x64 at reg 0xA3)
  Wire.beginTransmission(0x38); Wire.write(0xA3); Wire.endTransmission(false);
  Wire.requestFrom(0x38, 1);
  if (Wire.available()) Serial.printf("Chip ID reg 0xA3: 0x%02X\n", Wire.read());

  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("ESP32-S3 alive", 10, 10, 4);
  tft.drawString("Touch the screen", 10, 50, 2);
  Serial.println("ESP32-S3 alive");
}

void loop1() {
  Wire.beginTransmission(0x38);
  Wire.write(0x00);
  Wire.endTransmission(false);
  Wire.requestFrom(0x38, 12);
  Serial.print("raw:");
  while (Wire.available()) {
    Serial.printf(" %02X", Wire.read());
  }
  Serial.println();
  delay(300);
}

void loop() {
  Wire.beginTransmission(0x38);
  Wire.write(0x02);
  if (Wire.endTransmission(false) == 0 && Wire.requestFrom(0x38, 5) == 5) {
    uint8_t n  = Wire.read() & 0x0F;
    uint8_t b0 = Wire.read(), b1 = Wire.read();
    uint8_t b2 = Wire.read(), b3 = Wire.read();
    if (n > 0 && n <= 2) {
      int x = ((b0 & 0x0F) << 8) | b1;
      int y = ((b2 & 0x0F) << 8) | b3;
      if (x < 240 && y < 320) {
        Serial.printf("touch %d,%d\n", x, y);
        tft.fillCircle(x, y, 4, TFT_GREEN);
      }
    }
  }
  delay(10);
}
