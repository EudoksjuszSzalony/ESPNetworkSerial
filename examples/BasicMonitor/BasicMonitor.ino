#include <WiFi.h>
#include <ESPmDNS.h>
#include <ESPNetworkSerial.h>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#warning "Using placeholder Wi-Fi credentials. Copy secrets.example.h to secrets.h and fill in your Wi-Fi credentials."
#endif

const char *MONITOR_HOSTNAME = "espnetworkserial-basic";
constexpr uint32_t NETWORK_SERIAL_WAIT_MS = 12000;

void setup() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ESPNS_WIFI_SSID, ESPNS_WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
  }

  // ESPNetworkSerial.h automatically applies, in priority order:
  //   1) a key set programmatically before begin();
  //   2) ESPNS_AUTH_KEY from this sketch;
  //   3) ESPNS_DEFAULT_AUTH_KEY installed by ESPNetworkSerial Setup.
  ESPSerial.begin();

#if defined(ESPNS_AUTH_KEY) || \
    (defined(ESPNS_DEFAULT_AUTH_KEY) && !defined(ESPNS_DISABLE_DEFAULT_AUTH_KEY))
  if (!ESPSerial.authenticationEnabled()) {
    ESPSerial.println("FATAL: configured ESPNS auth key must be 16..128 bytes.");
    while (true) {
      delay(1000);
    }
  }
#endif

  if (MDNS.begin(MONITOR_HOSTNAME)) {
    MDNS.enableArduino(ESPSerial.port(), false);
  } else {
    ESPSerial.println("WARNING: mDNS discovery could not be started.");
  }

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
  ESPSerial.println("ESPNetworkSerial BasicMonitor");
  ESPSerial.print("TCP port: ");
  ESPSerial.println(ESPSerial.port());
  ESPSerial.print("ESPNS auth: ");
  ESPSerial.println(
      ESPSerial.authenticationEnabled() ? "hmac-sha256 + aes256-gcm"
                                        : "none + raw");
  ESPSerial.println("OTA: disabled in this example");
}

void loop() {
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
