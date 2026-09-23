#include <WiFi.h>
#include <ArduinoOTA.h>
#include <ESPNetworkSerial.h>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#warning "Using placeholder Wi-Fi credentials. Copy secrets.example.h to secrets.h and fill in your Wi-Fi credentials."
#endif

ESPNetworkSerial StressSerial;

constexpr bool WIFI_DISABLE_SLEEP = true;
constexpr size_t ECHO_BUFFER_SIZE = 256;
constexpr uint32_t USB_PROGRESS_EVERY_BYTES = 256 * 1024;
const char *OTA_HOSTNAME = "espnetworkserial-stress";

void setup() {
  Serial.begin(115200);
  delay(200);

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
  ArduinoOTA.begin();

#ifdef ESPNS_AUTH_KEY
  if (!StressSerial.setAuthKey(ESPNS_AUTH_KEY)) {
    Serial.println("FATAL: ESPNS_AUTH_KEY must be 16..128 bytes.");
    while (true) {
      delay(1000);
    }
  }
#endif

  StressSerial.begin();

  Serial.println("ESPNetworkSerial StressEcho ready");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("TCP port: ");
  Serial.println(StressSerial.port());
  Serial.print("ESPNS auth: ");
  Serial.println(StressSerial.authenticationEnabled() ? "hmac-sha256 + aes256-gcm" : "none + raw");
  Serial.print("OTA hostname: ");
  Serial.println(OTA_HOSTNAME);
}

void loop() {
  ArduinoOTA.handle();
  StressSerial.handle();

  static uint64_t totalEchoed = 0;
  static uint64_t nextUsbProgress = USB_PROGRESS_EVERY_BYTES;

  uint8_t buffer[ECHO_BUFFER_SIZE];
  size_t count = 0;

  while (count < sizeof(buffer) && StressSerial.available() > 0) {
    const int value = StressSerial.read();
    if (value < 0) {
      break;
    }
    buffer[count++] = static_cast<uint8_t>(value);
  }

  if (count > 0) {
    const size_t written = StressSerial.write(buffer, count);
    totalEchoed += written;

    if (totalEchoed >= nextUsbProgress) {
      Serial.print("[StressEcho] echoed_bytes=");
      Serial.println(static_cast<unsigned long long>(totalEchoed));
      while (nextUsbProgress <= totalEchoed) {
        nextUsbProgress += USB_PROGRESS_EVERY_BYTES;
      }
    }
  }
}
