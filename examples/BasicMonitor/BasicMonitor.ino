#include <WiFi.h>
#include <ArduinoOTA.h>
#include <ESPNetworkSerial.h>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#warning "Using placeholder Wi-Fi credentials. Copy secrets.example.h to secrets.h and fill in your Wi-Fi credentials."
#endif

const char *OTA_HOSTNAME = "espnetworkserial-test";

// Optional boot-time monitor wait:
//   0        = do not wait
//   12000    = wait up to 12 seconds
//   UINT32_MAX is not used here; call NetworkSerial.waitForConnection()
//              with no argument if a monitor connection is mandatory.
constexpr uint32_t NETWORK_SERIAL_WAIT_MS = 12000;

ESPNetworkSerialTCP NetworkSerial;

void setup() {
  Serial.begin(115200);
  delay(200);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ESPNS_WIFI_SSID, ESPNS_WIFI_PASSWORD);

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

  if (NETWORK_SERIAL_WAIT_MS > 0) {
    Serial.print("Waiting for Wi-Fi Serial Monitor (max ");
    Serial.print(NETWORK_SERIAL_WAIT_MS);
    Serial.println(" ms)...");

    if (NetworkSerial.waitForConnection(NETWORK_SERIAL_WAIT_MS)) {
      Serial.println("Wi-Fi Serial Monitor connected.");
    } else {
      Serial.println("Wi-Fi Serial Monitor wait timed out; continuing normally.");
    }
  }

  // Alternative modes.
  //   no wait:  remove the waitForConnection() call entirely
  //   required: NetworkSerial.waitForConnection();

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
