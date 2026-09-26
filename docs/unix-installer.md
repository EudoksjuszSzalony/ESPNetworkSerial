# Linux and macOS Terminal Setup

> Status: implemented on the v0.1.1 development line and covered by native Linux/macOS CI, including install, Repair, key rotation, conflict handling and uninstall. Dedicated user-machine validation is deferred; platform-specific issues can be handled from community reports.

The Linux/macOS setup packages provide the same core integration model as the Windows installer without requiring a graphical installer.

## Release packages

The release workflow builds architecture-specific setup archives:

~~~text
espnetworkserial-setup-<version>-linux-amd64.tar.gz
espnetworkserial-setup-<version>-linux-arm64.tar.gz
espnetworkserial-setup-<version>-macos-amd64.tar.gz
espnetworkserial-setup-<version>-macos-arm64.tar.gz
~~~

Each archive contains:

- the matching standalone `espnetworkserial-monitor` binary;
- `install.sh`;
- `uninstall.sh`;
- the shared terminal/integration helper;
- monitor README/config example and MIT license.

No Python, Node.js, Homebrew, package manager, or root access is required.

## Install

Extract the archive, open a terminal in that directory, then run:

~~~bash
./install.sh
~~~

The script prints each stage explicitly:

~~~text
ESPNetworkSerial Setup
────────────────────────────────────────────────────

[1/6] Detecting platform
[2/6] Installing host monitor
[3/6] Configuring secure ESPNS authentication
[4/6] Detecting Arduino ESP32 installations
[5/6] Configuring Arduino integration
[6/6] Verifying installation
~~~

Default install locations are:

### Linux

~~~text
~/.local/share/ESPNetworkSerial
~~~

If `XDG_DATA_HOME` is set, it is used instead of `~/.local/share`.

### macOS

~~~text
~/Library/Application Support/ESPNetworkSerial
~~~

The monitor, local `config.json`, repair/uninstall scripts and integration status are kept together in that directory.

## Arduino data locations

The default Arduino package-data roots are:

### Linux

~~~text
~/.arduino15
~~~

### macOS

~~~text
~/Library/Arduino15
~~~

For testing or non-standard installations, the scripts accept environment overrides:

~~~bash
ESPNS_INSTALL_ROOT="/custom/app/path" \
ESPNS_ARDUINO_DATA_ROOT="/custom/Arduino15" \
./install.sh
~~~

## Automatic authentication provisioning

On first install, when no host `config.json` exists, the monitor generates a random 256-bit key and writes:

~~~json
{
  "authKey": "<generated-key>",
  "allowUnauthenticated": false
}
~~~

The key is not printed to the terminal.

For every detected ESP32 core, Setup also writes:

~~~text
cores/esp32/ESPNetworkSerialConfig.h
~~~

containing:

~~~cpp
#define ESPNS_DEFAULT_AUTH_KEY "<same-generated-key>"
~~~

`ESPNetworkSerial.h` imports that file automatically because the ESP32 core directory is already part of the Arduino compiler include path.

The compile-time priority is:

~~~text
setAuthKey(...) before begin()
        ↓
ESPNS_AUTH_KEY
        ↓
ESPNS_DEFAULT_AUTH_KEY
        ↓
no key
~~~

Define `ESPNS_DISABLE_DEFAULT_AUTH_KEY` to deliberately ignore the machine-local default. Sketch-local `ESPNS_AUTH_KEY` / `ESPNS_DISABLE_DEFAULT_AUTH_KEY` definitions must appear before `#include <ESPNetworkSerial.h>`.

## Repair

After installing a new ESP32 core version, or whenever integration should be refreshed, run the installed script:

~~~bash
"<install-directory>/install.sh" --repair
~~~

Repair is idempotent:

- the existing authentication key is reused;
- the managed `platform.local.txt` block is not duplicated;
- new ESP32 core versions receive the same `ESPNetworkSerialConfig.h`;
- unrelated Arduino configuration is preserved;
- an existing third-party `pluggable_monitor.pattern.network` is not overwritten.

## Key rotation

Key rotation is deliberately explicit:

~~~bash
"<install-directory>/install.sh" --regenerate-key
~~~

The script displays a warning and asks for confirmation.

Rotating the key updates the host config and all detected installer-managed firmware config headers. Previously compiled ESP32 firmware still contains the old key and must therefore be rebuilt/reflashed.

For non-interactive automation, confirmation can be supplied with:

~~~bash
"<install-directory>/install.sh" --regenerate-key --yes
~~~

## Uninstall

Run:

~~~bash
"<install-directory>/uninstall.sh"
~~~

Uninstall removes:

- ESPNetworkSerial-managed network monitor blocks;
- installer-managed `ESPNetworkSerialConfig.h` files;
- the host monitor;
- the local host authentication config;
- the ESPNetworkSerial application directory.

Unrelated `platform.local.txt`, `boards.local.txt`, third-party monitor recipes, and non-managed files are preserved.

## Existing host config

Setup never silently replaces an existing valid `config.json` during normal install/Repair.

If an existing config contains an empty `authKey`, authentication is treated as deliberately disabled and no default firmware key is installed.

Malformed or invalid key configuration causes Setup to stop rather than overwrite the user's file.

## macOS signing status

The current macOS monitor is not code-signed or notarized. Depending on how the archive was downloaded and local Gatekeeper policy, macOS may require the user to approve the binary before Arduino IDE can launch it.

This limitation is separate from the setup logic and remains explicitly tracked for a future release.
