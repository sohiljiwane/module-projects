#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <config.h>

const int CONNECT_TIMEOUT_MS = 20000;
const int RECONNECT_INTERVAL_MS = 10000;

void printStatus() {
  const char* st = "unknown";
  switch (WiFi.status()) {
    case WL_IDLE_STATUS:     st = "idle"; break;
    case WL_NO_SSID_AVAIL:   st = "no ssid"; break;
    case WL_SCAN_COMPLETED:  st = "scan done"; break;
    case WL_CONNECTED:       st = "connected"; break;
    case WL_CONNECT_FAILED:  st = "connect failed"; break;
    case WL_CONNECTION_LOST: st = "connection lost"; break;
    case WL_DISCONNECTED:    st = "disconnected"; break;
    case WL_NO_SHIELD:       st = "no shield"; break;
    default:                 st = "other"; break;
  }
  Serial.printf("Status: %s\n", st);
  Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("Subnet: %s\n", WiFi.subnetMask().toString().c_str());
  Serial.printf("Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
  Serial.printf("DNS: %s\n", WiFi.dnsIP(0).toString().c_str());
  Serial.printf("MAC: %s\n", WiFi.macAddress().c_str());
  Serial.printf("RSSI: %d dBm\n", WiFi.RSSI());
  const uint8_t* b = WiFi.BSSID();
  Serial.printf("BSSID: %02x:%02x:%02x:%02x:%02x:%02x\n",
    b[0], b[1], b[2], b[3], b[4], b[5]);
  Serial.printf("Channel: %d\n", WiFi.channel());
}

void applyStaticIP() {
#ifdef STATIC_IP
  IPAddress ip(STATIC_IP);
  IPAddress gw(STATIC_GW);
  IPAddress mask(STATIC_MASK);
  WiFi.config(ip, gw, mask);
  Serial.printf("Using static IP %s\n", ip.toString().c_str());
#endif
}

bool connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("esp32-s3-wifi-test");
  applyStaticIP();

  Serial.printf("Connecting to \"%s\"", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > CONNECT_TIMEOUT_MS) {
      Serial.println(" [timed out]");
      return false;
    }
    Serial.print(".");
    delay(500);
  }
  Serial.println(" [connected]");
  return true;
}

void testDNS(const char* host) {
  IPAddress ip;
  if (!WiFi.hostByName(host, ip)) {
    Serial.printf("DNS lookup FAILED for %s\n", host);
    return;
  }
  Serial.printf("DNS OK: %s -> %s\n", host, ip.toString().c_str());
}

void testHTTP() {
  WiFiClient client;
  const uint16_t port = 80;
  const char* host = "www.google.com";

  Serial.printf("HTTP GET to %s\n", host);
  if (client.connect(host, port, CONNECT_TIMEOUT_MS)) {
    client.println("GET / HTTP/1.1");
    client.printf("Host: %s\r\n", host);
    client.println("Connection: close\r\n\r\n");

    uint32_t start = millis();
    int bytes = 0;
    while (client.connected() && millis() - start < CONNECT_TIMEOUT_MS) {
      while (client.available()) {
        client.read();
        bytes++;
      }
    }
    client.stop();
    Serial.printf("HTTP OK: received %d bytes\n", bytes);
  } else {
    Serial.println("HTTP connection FAILED");
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n=== ESP32-S3 WiFi Test ===");

  if (!connectWiFi()) {
    Serial.println("WiFi connection failed. Will keep retrying...");
  }
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    static uint32_t lastReport = 0;
    if (millis() - lastReport >= 10000) {
      lastReport = millis();
      Serial.println("\n--- Connected ---");
      printStatus();
      testDNS("www.google.com");
      testHTTP();
    }
  } else {
    static uint32_t lastAttempt = 0;
    if (millis() - lastAttempt >= RECONNECT_INTERVAL_MS) {
      lastAttempt = millis();
      Serial.println("WiFi lost, reconnecting...");
      WiFi.disconnect();
      WiFi.reconnect();
      if (connectWiFi()) {
        Serial.println("Reconnected");
        printStatus();
      }
    }
  }
  delay(100);
}
