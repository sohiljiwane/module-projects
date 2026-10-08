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
    Serial.printf("Touch %d,%d\n", x, y);
    tft.fillCircle(x, y, 4, TFT_GREEN);
  }
  delay(20);
}
