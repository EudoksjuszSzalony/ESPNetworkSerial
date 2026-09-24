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
constexpr size_t ECHO_BUFFER_SIZE = ESPNETWORKSERIAL_SECURE_MAX_RECORD;
constexpr uint32_t USB_PROGRESS_EVERY_BYTES = 256 * 1024;
#ifdef BUTTON
constexpr uint8_t FAULT_TEST_BUTTON_PIN = BUTTON;
#else
constexpr uint8_t FAULT_TEST_BUTTON_PIN = 38;
#endif
constexpr uint32_t FAULT_TEST_DURATION_MS = 8000;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 30;
constexpr uint32_t MULTICLICK_GAP_MS = 650;
const char *OTA_HOSTNAME = "espnetworkserial-stress";

enum class FaultTestState : uint8_t {
  Idle,
  WiFiDisconnected,
  WiFiOff,
  WiFiOffReconnecting,
};

FaultTestState faultTestState = FaultTestState::Idle;
uint32_t faultTestRestoreAt = 0;

bool buttonStablePressed = false;
bool buttonLastRawPressed = false;
uint32_t buttonLastChangeAt = 0;
uint32_t buttonLastReleaseAt = 0;
uint8_t pendingClicks = 0;
bool networkServicesActive = true;

void printFaultTestMenu() {
  Serial.println("[FAULT TEST] SW38 test selector:");
  Serial.println("[FAULT TEST]   1 click  = ESP.restart()");
  Serial.println("[FAULT TEST]   2 clicks = WiFi.disconnect() for 8 s");
  Serial.println("[FAULT TEST]   3 clicks = WIFI_OFF for 8 s");
  Serial.println("[FAULT TEST]   4 clicks = application delay(8000)");
  Serial.println("[FAULT TEST]   5 clicks = close active ESPNS TCP client only");
}

void requestWiFiReconnect() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  if (WIFI_DISABLE_SLEEP) {
    WiFi.setSleep(false);
  }
  WiFi.begin(ESPNS_WIFI_SSID, ESPNS_WIFI_PASSWORD);
}

void suspendNetworkServicesForWiFiOff() {
  if (!networkServicesActive) {
    return;
  }

  Serial.println("[FAULT TEST] suspending ESPNS and ArduinoOTA before WIFI_OFF");
  StressSerial.end();
  ArduinoOTA.end();
  networkServicesActive = false;
}

void resumeNetworkServicesAfterWiFiOff() {
  if (networkServicesActive) {
    return;
  }

  Serial.println("[FAULT TEST] Wi-Fi is back; restarting ESPNS and ArduinoOTA");
  ArduinoOTA.begin();
  StressSerial.begin();
  networkServicesActive = true;
}

void runFaultTest(uint8_t clicks) {
  Serial.print("[FAULT TEST] selected by ");
  Serial.print(clicks);
  Serial.println(clicks == 1 ? " click" : " clicks");

  switch (clicks) {
    case 1:
      Serial.println("[FAULT TEST] restarting ESP32 in 250 ms");
      Serial.flush();
      delay(250);
      ESP.restart();
      break;

    case 2:
      Serial.println("[FAULT TEST] disconnecting station from AP for 8 s");
      WiFi.setAutoReconnect(false);
      WiFi.disconnect(false, false);
      faultTestState = FaultTestState::WiFiDisconnected;
      faultTestRestoreAt = millis() + FAULT_TEST_DURATION_MS;
      break;

    case 3:
      Serial.println("[FAULT TEST] disabling Wi-Fi subsystem for 8 s");
      suspendNetworkServicesForWiFiOff();
      WiFi.setAutoReconnect(false);
      WiFi.mode(WIFI_OFF);
      faultTestState = FaultTestState::WiFiOff;
      faultTestRestoreAt = millis() + FAULT_TEST_DURATION_MS;
      break;

    case 4:
      Serial.print("[FAULT TEST] application stall: delay(");
      Serial.print(FAULT_TEST_DURATION_MS);
      Serial.println(")");
      delay(FAULT_TEST_DURATION_MS);
      Serial.println("[FAULT TEST] application resumed; Wi-Fi/TCP were left untouched");
      break;

    case 5:
      Serial.println("[FAULT TEST] closing active ESPNS TCP client; Wi-Fi remains connected");
      StressSerial.tcp().disconnectClient();
      break;

    default:
      Serial.println("[FAULT TEST] no test assigned to this click count");
      printFaultTestMenu();
      break;
  }
}

void handleFaultTestRecovery() {
  if (faultTestState == FaultTestState::Idle) {
    return;
  }

  if (faultTestState == FaultTestState::WiFiOffReconnecting) {
    if (WiFi.status() == WL_CONNECTED) {
      resumeNetworkServicesAfterWiFiOff();
      faultTestState = FaultTestState::Idle;
    }
    return;
  }

  if (static_cast<int32_t>(millis() - faultTestRestoreAt) < 0) {
    return;
  }

  if (faultTestState == FaultTestState::WiFiDisconnected) {
    Serial.println("[FAULT TEST] 8 s elapsed; requesting Wi-Fi reconnect");
    faultTestState = FaultTestState::Idle;
    requestWiFiReconnect();
    return;
  }

  Serial.println("[FAULT TEST] 8 s elapsed; re-enabling Wi-Fi and requesting reconnect");
  faultTestState = FaultTestState::WiFiOffReconnecting;
  requestWiFiReconnect();
}

void handleFaultTestButton() {
  const uint32_t now = millis();
  const bool rawPressed = digitalRead(FAULT_TEST_BUTTON_PIN) == LOW;

  if (rawPressed != buttonLastRawPressed) {
    buttonLastRawPressed = rawPressed;
    buttonLastChangeAt = now;
  }

  if (rawPressed != buttonStablePressed &&
      static_cast<uint32_t>(now - buttonLastChangeAt) >= BUTTON_DEBOUNCE_MS) {
    buttonStablePressed = rawPressed;

    if (!buttonStablePressed) {
      if (pendingClicks < 255) {
        ++pendingClicks;
      }
      buttonLastReleaseAt = now;
    }
  }

  if (pendingClicks > 0 && !buttonStablePressed &&
      static_cast<uint32_t>(now - buttonLastReleaseAt) >= MULTICLICK_GAP_MS) {
    const uint8_t clicks = pendingClicks;
    pendingClicks = 0;
    runFaultTest(clicks);
  }
}

void reportWiFiStateChanges() {
  static wl_status_t previousStatus = WL_NO_SHIELD;
  const wl_status_t currentStatus = WiFi.status();

  if (currentStatus == previousStatus) {
    return;
  }

  previousStatus = currentStatus;
  Serial.print("[FAULT TEST] Wi-Fi status=");
  Serial.println(static_cast<int>(currentStatus));

  if (currentStatus == WL_CONNECTED) {
    Serial.print("[FAULT TEST] Wi-Fi connected, IP=");
    Serial.println(WiFi.localIP());
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);

  // SW38/GPIO38 is input-only on Feather ESP32 V2 and already has an
  // on-board pull-up. INPUT_PULLUP asks ESP32 for an unsupported internal PU.
  pinMode(FAULT_TEST_BUTTON_PIN, INPUT);

  requestWiFiReconnect();

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
  printFaultTestMenu();
}

void loop() {
  handleFaultTestButton();
  handleFaultTestRecovery();
  reportWiFiStateChanges();

  if (networkServicesActive) {
    ArduinoOTA.handle();
    StressSerial.handle();
  }

  static uint64_t totalEchoed = 0;
  static uint64_t nextUsbProgress = USB_PROGRESS_EVERY_BYTES;

  if (!networkServicesActive) {
    delay(1);
    return;
  }

  uint8_t buffer[ECHO_BUFFER_SIZE];
  const size_t count = StressSerial.read(buffer, sizeof(buffer));

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
