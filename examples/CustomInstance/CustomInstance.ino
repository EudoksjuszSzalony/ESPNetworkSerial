#include <WiFi.h>
#include <ESPNetworkSerial.h>

// Custom instances stay explicit: unlike the global ESPSerial convenience
// object, they do not automatically claim or initialize Arduino's Serial.
ESPNetworkSerial DebugSerial;

void setup() {
  Serial.begin(115200);

  // Connect Wi-Fi before begin() in a real sketch.
  WiFi.mode(WIFI_STA);

  // A custom instance can mirror any companion Stream you choose.
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
