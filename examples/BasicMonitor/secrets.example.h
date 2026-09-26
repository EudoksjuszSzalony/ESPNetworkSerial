#pragma once

// Copy this file to "secrets.h" in the same sketch directory and edit the copy.
// secrets.h is ignored by Git, so your local credentials will not be committed.

#define ESPNS_WIFI_SSID "YOUR_SSID"
#define ESPNS_WIFI_PASSWORD "YOUR_PASSWORD"

// Optional per-sketch ESPNetworkSerial authentication override.
//
// ESPNetworkSerial Setup can provision a machine-local ESPNS_DEFAULT_AUTH_KEY
// automatically, so most installed setups do not need a key here.
//
// Define ESPNS_AUTH_KEY only when this sketch should use a different key.
// It takes priority over the installer-provided default.
//
// Minimum: 16 bytes. Recommended: a generated 32-byte / 64-hex-character key.
// This key is separate from any ArduinoOTA password.
// #define ESPNS_AUTH_KEY "PASTE_GENERATED_KEY_HERE"
