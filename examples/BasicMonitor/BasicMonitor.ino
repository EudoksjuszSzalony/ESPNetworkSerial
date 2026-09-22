#include <WiFi.h>
#include <ArduinoOTA.h>
#include <ESPNetworkSerial.h>

const char *WIFI_SSID = "YOUR_SSID";
const char *WIFI_PASSWORD = "YOUR_PASSWORD";
const char *OTA_HOSTNAME = "espnetworkserial-test";

ESPNetworkSerialTCP NetworkSerial;

void setup() {
  Serial.begin(115200);
  delay(200);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.begin();

  NetworkSerial.begin();

  // One API, two bidirectional streams.
  ESPSerial.addStream(Serial);
  ESPSerial.addStream(NetworkSerial);

  ESPSerial.println();
  ESPSerial.println("ESPNetworkSerial BasicMonitor");
  ESPSerial.print("IP: ");
  ESPSerial.println(WiFi.localIP());
  ESPSerial.print("OTA hostname: ");
  ESPSerial.println(OTA_HOSTNAME);
  ESPSerial.print("TCP port: ");
  ESPSerial.println(NetworkSerial.port());
  ESPSerial.println(
      "Pre-alpha transport: unauthenticated plaintext TCP. "
      "Use only on a trusted LAN.");
}

void loop() {
  ArduinoOTA.handle();
  NetworkSerial.handle();

  static uint32_t lastStatus = 0;
  if (millis() - lastStatus >= 5000) {
    lastStatus = millis();
    ESPSerial.print("uptime_ms=");
    ESPSerial.println(millis());
  }

  // Input may arrive from USB Serial or from Arduino IDE over Wi-Fi.
  if (ESPSerial.available() > 0) {
    ESPSerial.print("RX: ");

    while (ESPSerial.available() > 0) {
      const int value = ESPSerial.read();
      if (value >= 0) {
        ESPSerial.write(static_cast<uint8_t>(value));
      }
    }
  }
}
