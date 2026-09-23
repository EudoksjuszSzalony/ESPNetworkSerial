#include <WiFi.h>
#include <ESPNetworkSerial.h>

// The facade can use any object name. This keeps the same easy API while
// leaving the built-in global ESPSerial available for sketches that want it.
ESPNetworkSerial DebugSerial;

void setup() {
  Serial.begin(115200);

  // Connect Wi-Fi before begin() in a real sketch.
  WiFi.mode(WIFI_STA);

  // Optional companion stream: mirror RX/TX to USB Serial as well.
  DebugSerial.addStream(Serial);

  DebugSerial.begin();

  DebugSerial.println("ESPNetworkSerial custom instance example");
  DebugSerial.print("TCP port: ");
  DebugSerial.println(DebugSerial.port());
}

void loop() {
  DebugSerial.handle();

  if (DebugSerial.available() > 0) {
    const int value = DebugSerial.read();
    if (value >= 0) {
      DebugSerial.write(static_cast<uint8_t>(value));
    }
  }
}
