# Windows End-User Installer

> Status: pre-release installer candidate.

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
- removes only ESPNetworkSerial-managed blocks during uninstall.

Administrator rights are not required.

## Installed location

The default install path is:

~~~text
%LOCALAPPDATA%\Programs\ESPNetworkSerial
~~~

The monitor reads optional `config.json` from the same directory as the executable. The installer ships `config.example.json` but does not invent an authentication key automatically.

To enable authenticated ESPNS, copy the example to:

~~~text
config.json
~~~

and place the same generated key in the ESP32 sketch.

## Core updates

Arduino installs each ESP32 core version in its own directory. Installing a new ESP32 core version after ESPNetworkSerial therefore requires one repair step:

~~~text
Start menu
  -> ESPNetworkSerial
     -> Repair Arduino integration
~~~

The repair operation is idempotent and can be run repeatedly.

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

Other `platform.local.txt` and `boards.local.txt` content is preserved.

The uninstaller also removes the generated `integration-status.txt` and local `config.json`. The latter may contain the ESPNS pre-shared authentication key, so uninstall does not intentionally leave that secret behind in the application directory.

## Signing status

The current pre-release installer is not Authenticode-signed. Windows SmartScreen may therefore warn about an unknown publisher. SHA-256 release checksums verify downloaded file integrity, but checksums are not a substitute for code signing.
