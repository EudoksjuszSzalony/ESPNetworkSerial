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

// Give ArduinoOTA more tolerance for brief Wi-Fi stalls than the ESP32 core
// default. This only affects OTA receive timeout handling.
constexpr uint32_t OTA_TIMEOUT_MS = 5000;

// For development/testing, favor Wi-Fi latency/reliability over power saving.
// ESP32 modem sleep can be re-enabled later if low power matters more.
constexpr bool WIFI_DISABLE_SLEEP = true;

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

  WiFi.onEvent(
      [](WiFiEvent_t event, WiFiEventInfo_t info) {
        Serial.print("[WiFi] disconnected, reason=");
        Serial.println(info.wifi_sta_disconnected.reason);
      },
      WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  WiFi.onEvent(
      [](WiFiEvent_t event, WiFiEventInfo_t info) {
        Serial.print("[WiFi] got IP: ");
        Serial.println(IPAddress(info.got_ip.ip_info.ip.addr));
      },
      WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  if (WIFI_DISABLE_SLEEP) {
    WiFi.setSleep(false);
  }
  WiFi.begin(ESPNS_WIFI_SSID, ESPNS_WIFI_PASSWORD);

  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setTimeout(OTA_TIMEOUT_MS);

  // OTA diagnostics go to USB Serial only, so diagnostics do not add traffic
  // to the Wi-Fi Serial connection during an OTA transfer.
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
        total > 0 ? static_cast<int>((static_cast<uint64_t>(progress) * 100U) / total) : 0;

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
    Serial.print(static_cast<unsigned int>(error));
    Serial.print(" (");

    switch (error) {
      case OTA_AUTH_ERROR: Serial.print("auth"); break;
      case OTA_BEGIN_ERROR: Serial.print("begin"); break;
      case OTA_CONNECT_ERROR: Serial.print("connect"); break;
      case OTA_RECEIVE_ERROR: Serial.print("receive"); break;
      case OTA_END_ERROR: Serial.print("end"); break;
      default: Serial.print("unknown"); break;
    }

    Serial.println(")");
  });

  ArduinoOTA.begin();

  NetworkSerial.begin();

  // One API, two bidirectional streams.
  ESPSerial.addStream(Serial);
  ESPSerial.addStream(NetworkSerial);

  // Connection wait modes live here, next to the actual call:
  //   no wait:  remove this waitForConnection() block entirely
  //   timed:    NetworkSerial.waitForConnection(12000)
  //   required: NetworkSerial.waitForConnection()
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


  ESPSerial.println();
  ESPSerial.println("ESPNetworkSerial BasicMonitor");
  ESPSerial.print("IP: ");
  ESPSerial.println(WiFi.localIP());
  ESPSerial.print("Wi-Fi sleep: ");
  ESPSerial.println(WIFI_DISABLE_SLEEP ? "disabled" : "enabled");
  ESPSerial.print("Wi-Fi RSSI: ");
  ESPSerial.print(WiFi.RSSI());
  ESPSerial.println(" dBm");
  ESPSerial.print("OTA hostname: ");
  ESPSerial.println(OTA_HOSTNAME);
  ESPSerial.print("OTA receive timeout: ");
  ESPSerial.print(OTA_TIMEOUT_MS);
  ESPSerial.println(" ms");
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
    ESPSerial.print(millis());
    ESPSerial.print(" rssi_dbm=");
    ESPSerial.println(WiFi.RSSI());
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
