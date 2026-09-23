#pragma once

// Copy this file to "secrets.h" in the same sketch directory and edit the copy.
// secrets.h is ignored by Git, so your local credentials will not be committed.

#define ESPNS_WIFI_SSID "YOUR_SSID"
#define ESPNS_WIFI_PASSWORD "YOUR_PASSWORD"

// Use the same ESPNS key as monitor/config.json for authenticated stress tests.
// Generate one with:
//   .\monitor\espnetworkserial-monitor.exe --generate-key
//
// #define ESPNS_AUTH_KEY "PASTE_GENERATED_KEY_HERE"
