#pragma once

// Copy this file to "secrets.h" in the same sketch directory and edit the copy.
// secrets.h is ignored by Git, so your local credentials will not be committed.

#define ESPNS_WIFI_SSID "YOUR_SSID"
#define ESPNS_WIFI_PASSWORD "YOUR_PASSWORD"

// Optional ESPNetworkSerial mutual authentication.
// Generate a key with:
//   .\monitor\espnetworkserial-monitor.exe --generate-key
// Then put the same value in monitor/config.json.
//
// Minimum: 16 bytes. Recommended: the generated 32-byte / 64-hex-character key.
// This key is separate from any ArduinoOTA password.
// #define ESPNS_AUTH_KEY "PASTE_GENERATED_KEY_HERE"
