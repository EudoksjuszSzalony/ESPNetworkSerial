# Windows End-User Installer

> Status: Windows installer is released in v0.1.0. The v0.1.1 development line adds automatic authentication provisioning shared with Linux/macOS setup.

The Windows installer removes the development requirement to clone the repository, install Go, build the monitor manually, or edit Arduino files by hand.

## What the installer does

The per-user installer:

- installs `espnetworkserial-monitor.exe` under the current user's Local AppData;
- installs the MIT license, monitor README, example host configuration, and Arduino integration helper scripts;
- scans installed ESP32 Arduino core versions under `%LOCALAPPDATA%\Arduino15`;
- writes only the managed ESPNetworkSerial block in each core's `platform.local.txt`;
- leaves the stock ESP32 ArduinoOTA upload recipe untouched;
- refuses to overwrite another existing `pluggable_monitor.pattern.network` implementation;
- creates a Start-menu **Repair Arduino integration** shortcut for newly installed ESP32 core versions;
- generates a 256-bit ESPNS authentication key on first install when no host `config.json` exists;
- writes the same key to installer-managed `ESPNetworkSerialConfig.h` files in detected ESP32 cores;
- reuses the existing key on Repair/upgrade instead of silently rotating it;
- removes only ESPNetworkSerial-managed blocks/config headers during uninstall.

Administrator rights are not required.

## Installed location

The default install path is:

~~~text
%LOCALAPPDATA%\Programs\ESPNetworkSerial
~~~

The monitor reads `config.json` from the same directory as the executable.

On first install, when that file does not already exist, Setup automatically generates a random 256-bit ESPNS key and writes:

~~~json
{
  "authKey": "<generated-key>",
  "allowUnauthenticated": false
}
~~~

The same key is written into each detected ESP32 core as:

~~~text
cores\esp32\ESPNetworkSerialConfig.h
~~~

with:

~~~cpp
#define ESPNS_DEFAULT_AUTH_KEY "<generated-key>"
~~~

`ESPNetworkSerial.h` imports that installer-managed file automatically. A sketch can override the machine-local default with `ESPNS_AUTH_KEY`, use `setAuthKey(...)` before `begin()`, or deliberately disable the machine-local default with `ESPNS_DISABLE_DEFAULT_AUTH_KEY`.

If `config.json` already exists, Repair/upgrade preserves it. An existing config with an empty `authKey` is treated as an intentional request to leave authentication disabled; Setup does not silently replace it.

## Core updates

Arduino installs each ESP32 core version in its own directory. Installing a new ESP32 core version after ESPNetworkSerial therefore requires one repair step:

~~~text
Start menu
  -> ESPNetworkSerial
     -> Repair Arduino integration
~~~

The repair operation is idempotent and can be run repeatedly. It also propagates the existing host key into newly installed ESP32 core versions. Repair never rotates a valid existing key.

## Existing network pluggable monitor

If a core already contains another non-ESPNetworkSerial:

~~~text
pluggable_monitor.pattern.network=...
~~~

the installer does not overwrite it. Other compatible ESP32 core versions are still configured, and details are written to:

~~~text
integration-status.txt
~~~

in the ESPNetworkSerial installation folder.

## Uninstall

The uninstaller runs the unregister helper before deleting application files. It removes only blocks delimited by:

~~~text
# ESPNetworkSerial BEGIN
...
# ESPNetworkSerial END
~~~

Other `platform.local.txt` and `boards.local.txt` content is preserved. Installer-managed `ESPNetworkSerialConfig.h` files are removed, while an unrelated file with the same name but without the ESPNetworkSerial managed marker is preserved.

The uninstaller also removes the generated `integration-status.txt` and local `config.json`. The latter contains the ESPNS pre-shared authentication key by default, so uninstall does not intentionally leave that secret behind in the application directory.

## Signing status

The current pre-release installer is not Authenticode-signed. Windows SmartScreen may therefore warn about an unknown publisher. SHA-256 release checksums verify downloaded file integrity, but checksums are not a substitute for code signing.
