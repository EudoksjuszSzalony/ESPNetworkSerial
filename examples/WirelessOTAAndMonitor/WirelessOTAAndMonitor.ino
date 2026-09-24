#include <WiFi.h>
#include <ArduinoOTA.h>
#include <ESPNetworkSerial.h>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#define ESPNS_WIFI_SSID "YOUR_SSID"
#define ESPNS_WIFI_PASSWORD "YOUR_PASSWORD"
#warning "Using placeholder Wi-Fi credentials. Create secrets.h in this sketch folder and define ESPNS_WIFI_SSID / ESPNS_WIFI_PASSWORD."
#endif

const char *OTA_HOSTNAME = "espnetworkserial-ota";
constexpr uint32_t OTA_TIMEOUT_MS = 5000;
constexpr uint32_t NETWORK_SERIAL_WAIT_MS = 12000;
constexpr bool WIFI_DISABLE_SLEEP = true;

void setup() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  if (WIFI_DISABLE_SLEEP) {
    WiFi.setSleep(false);
  }
  WiFi.begin(ESPNS_WIFI_SSID, ESPNS_WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
  }

#ifdef ESPNS_AUTH_KEY
  ESPSerial.setAuthKey(ESPNS_AUTH_KEY);
#endif

  ESPSerial.begin();

#ifdef ESPNS_AUTH_KEY
  if (!ESPSerial.authenticationEnabled()) {
    ESPSerial.println("FATAL: ESPNS_AUTH_KEY must be 16..128 bytes.");
    while (true) {
      delay(1000);
    }
  }
#endif

  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setTimeout(OTA_TIMEOUT_MS);

  // Intentional exception: OTA diagnostics stay local on Serial so an OTA
  // transfer does not create extra traffic on the Wi-Fi monitor connection.
  // Serial is already initialized by ESPSerial.begin().
  ArduinoOTA.onStart([]() {
    Serial.print("[OTA] start, RSSI=");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("[OTA] complete");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    static int lastReported = -10;
    const int percent =
        total > 0
            ? static_cast<int>(
                  (static_cast<uint64_t>(progress) * 100U) / total)
            : 0;

    if (percent >= lastReported + 10 || percent == 100) {
      lastReported = percent;
      Serial.print("[OTA] progress=");
      Serial.print(percent);
      Serial.print("% RSSI=");
      Serial.print(WiFi.RSSI());
      Serial.println(" dBm");
    }
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.print("[OTA] error=");
    Serial.println(static_cast<unsigned int>(error));
  });

  ArduinoOTA.begin();

  ESPSerial.print("Wi-Fi connected, IP: ");
  ESPSerial.println(WiFi.localIP());
  ESPSerial.print("Waiting for Wi-Fi Serial Monitor (max ");
  ESPSerial.print(NETWORK_SERIAL_WAIT_MS);
  ESPSerial.println(" ms)...");

  if (ESPSerial.waitForConnection(NETWORK_SERIAL_WAIT_MS)) {
    ESPSerial.println("Wi-Fi Serial Monitor connected.");
  } else {
    ESPSerial.println("Monitor wait timed out; continuing normally.");
  }

  ESPSerial.println();
  ESPSerial.println("ESPNetworkSerial WirelessOTAAndMonitor");
  ESPSerial.print("TCP port: ");
  ESPSerial.println(ESPSerial.port());
  ESPSerial.print("OTA hostname: ");
  ESPSerial.println(OTA_HOSTNAME);
  ESPSerial.print("OTA receive timeout: ");
  ESPSerial.print(OTA_TIMEOUT_MS);
  ESPSerial.println(" ms");
  ESPSerial.print("ESPNS auth: ");
  ESPSerial.println(
      ESPSerial.authenticationEnabled() ? "hmac-sha256 + aes256-gcm"
                                        : "none + raw");
}

void loop() {
  ArduinoOTA.handle();
  ESPSerial.handle();

  static uint32_t lastStatus = 0;
  if (millis() - lastStatus >= 5000) {
    lastStatus = millis();
    ESPSerial.print("uptime_ms=");
    ESPSerial.print(millis());
    ESPSerial.print(" rssi_dbm=");
    ESPSerial.println(WiFi.RSSI());
  }

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
