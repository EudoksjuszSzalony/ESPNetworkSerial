#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=common.sh
. "$SCRIPT_DIR/common.sh"

MODE="install"
REGENERATE=0
ASSUME_YES=0

usage() {
  cat <<'EOF'
Usage: ./install.sh [--repair] [--regenerate-key] [--yes]

  --repair          Re-run integration for installed/new ESP32 core versions.
                    Existing authentication key is reused.
  --regenerate-key  Generate a new host/firmware authentication key.
                    Existing ESP32 firmware must then be recompiled/reflashed.
  --yes             Confirm --regenerate-key without an interactive prompt.
EOF
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --repair)
      MODE="repair"
      ;;
    --regenerate-key)
      REGENERATE=1
      ;;
    --yes)
      ASSUME_YES=1
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      usage >&2
      exit 1
      ;;
  esac
  shift
done

espns_init_terminal
espns_banner

espns_step 1 6 "Detecting platform"
set +e
espns_detect_platform
detect_result="$?"
set -e
if [ "$detect_result" -ne 0 ]; then
  if [ "$detect_result" -eq 2 ]; then
    espns_die "Unsupported CPU architecture: $(uname -m)"
  fi
  espns_die "Unsupported operating system: $(uname -s)"
fi
espns_info "OS: $ESPNS_OS_LABEL"
espns_info "Architecture: $ESPNS_ARCH"
espns_ok "Supported platform"

APP_DIR="${ESPNS_INSTALL_ROOT:-$(espns_default_app_dir)}"
ARDUINO_ROOT="${ESPNS_ARDUINO_DATA_ROOT:-$(espns_default_arduino_root)}"
MONITOR_SOURCE="$SCRIPT_DIR/espnetworkserial-monitor"
MONITOR_PATH="$APP_DIR/espnetworkserial-monitor"
CONFIG_PATH="$APP_DIR/config.json"
STATUS_PATH="$APP_DIR/integration-status.txt"

case "$MONITOR_PATH" in
  *'"'*) espns_die "Install path contains an unsupported quote character." ;;
esac

espns_step 2 6 "Installing host monitor"
if [ ! -f "$MONITOR_SOURCE" ]; then
  espns_die "Package is incomplete: espnetworkserial-monitor is missing."
fi

mkdir -p "$APP_DIR"
chmod 700 "$APP_DIR" 2>/dev/null || true
espns_copy_if_different "$MONITOR_SOURCE" "$MONITOR_PATH" 0755
espns_copy_if_different "$SCRIPT_DIR/install.sh" "$APP_DIR/install.sh" 0755
espns_copy_if_different "$SCRIPT_DIR/uninstall.sh" "$APP_DIR/uninstall.sh" 0755
espns_copy_if_different "$SCRIPT_DIR/common.sh" "$APP_DIR/common.sh" 0644
espns_copy_if_different "$SCRIPT_DIR/README.md" "$APP_DIR/README.md" 0644
espns_copy_if_different "$SCRIPT_DIR/LICENSE" "$APP_DIR/LICENSE" 0644
espns_copy_if_different "$SCRIPT_DIR/config.example.json" "$APP_DIR/config.example.json" 0644

if ! MONITOR_VERSION="$("$MONITOR_PATH" --version 2>&1)"; then
  if [ "$ESPNS_OS" = "macos" ]; then
    espns_warn "macOS refused to start the unsigned monitor binary."
    espns_info "The current release is not notarized. Check Gatekeeper/quarantine settings for this GitHub release asset."
  fi
  espns_die "Installed monitor could not be executed: $MONITOR_VERSION"
fi
espns_info "Monitor: $MONITOR_PATH"
espns_info "$MONITOR_VERSION"
espns_ok "Host monitor installed"

espns_step 3 6 "Configuring secure ESPNS authentication"
if [ "$REGENERATE" -eq 1 ]; then
  espns_warn "A new key will invalidate the key embedded in previously compiled ESP32 firmware."
  espns_warn "Those devices must be recompiled/reflashed before secure monitoring works again."

  if [ "$ASSUME_YES" -ne 1 ]; then
    if [ ! -t 0 ]; then
      espns_die "--regenerate-key requires confirmation; rerun interactively or add --yes."
    fi
    printf '      Continue? [y/N] '
    read -r answer
    case "$answer" in
      y|Y|yes|YES) ;;
      *) espns_die "Key regeneration cancelled." ;;
    esac
  fi

  AUTH_OUTPUT="$(ESPNS_CONFIG="$CONFIG_PATH" "$MONITOR_PATH" --regenerate-auth 2>&1)" ||
    espns_die "$AUTH_OUTPUT"
else
  AUTH_OUTPUT="$(ESPNS_CONFIG="$CONFIG_PATH" "$MONITOR_PATH" --provision-auth 2>&1)" ||
    espns_die "$AUTH_OUTPUT"
fi
chmod 600 "$CONFIG_PATH" 2>/dev/null || true
espns_info "$AUTH_OUTPUT"
if [ "$REGENERATE" -eq 1 ]; then
  espns_ok "New host authentication key provisioned"
elif [ "$MODE" = "repair" ]; then
  espns_ok "Existing authentication configuration preserved"
else
  espns_ok "Host authentication configured"
fi

espns_step 4 6 "Detecting Arduino ESP32 installations"
CORE_ROOT="$ARDUINO_ROOT/packages/esp32/hardware/esp32"
CORE_COUNT=0
if [ -d "$CORE_ROOT" ]; then
  for core_dir in "$CORE_ROOT"/*; do
    [ -d "$core_dir" ] || continue
    CORE_COUNT=$((CORE_COUNT + 1))
    espns_info "Found ESP32 core $(basename "$core_dir")"
  done
fi

if [ "$CORE_COUNT" -eq 0 ]; then
  espns_warn "No ESP32 Arduino core versions found under:"
  espns_info "$CORE_ROOT"
  espns_info "Install the ESP32 core, then run:"
  espns_info ""$APP_DIR/install.sh" --repair"
else
  espns_ok "Found $CORE_COUNT ESP32 core version(s)"
fi

espns_step 5 6 "Configuring Arduino integration"
CONFIGURED=0
CONFLICTS=0
FIRMWARE_CONFIGS=0

if [ "$CORE_COUNT" -gt 0 ]; then
  for core_dir in "$CORE_ROOT"/*; do
    [ -d "$core_dir" ] || continue
    version="$(basename "$core_dir")"
    firmware_dir="$core_dir/cores/esp32"

    if [ -d "$firmware_dir" ]; then
      firmware_config="$firmware_dir/ESPNetworkSerialConfig.h"
      FW_OUTPUT="$(ESPNS_CONFIG="$CONFIG_PATH" "$MONITOR_PATH" --write-firmware-config "$firmware_config" 2>&1)" ||
        espns_die "$FW_OUTPUT"
      if [ -f "$firmware_config" ]; then
        chmod 600 "$firmware_config" 2>/dev/null || true
        FIRMWARE_CONFIGS=$((FIRMWARE_CONFIGS + 1))
      fi
    else
      espns_warn "ESP32 core $version has no cores/esp32 directory; default auth header skipped."
    fi

    set +e
    espns_configure_platform_file "$core_dir" "$MONITOR_PATH"
    result="$?"
    set -e

    if [ "$result" -eq 0 ]; then
      CONFIGURED=$((CONFIGURED + 1))
      espns_ok "ESP32 core $version configured"
    elif [ "$result" -eq 2 ]; then
      CONFLICTS=$((CONFLICTS + 1))
      espns_warn "ESP32 core $version already has another pluggable network monitor; recipe left untouched."
    else
      espns_die "Could not configure ESP32 core $version."
    fi
  done
else
  espns_info "Nothing to configure yet."
fi

espns_step 6 6 "Verifying installation"
{
  printf 'ESPNetworkSerial integration status\n'
  printf 'OS: %s\n' "$ESPNS_OS_LABEL"
  printf 'Architecture: %s\n' "$ESPNS_ARCH"
  printf 'Monitor: %s\n' "$MONITOR_PATH"
  printf 'Host config: %s\n' "$CONFIG_PATH"
  printf 'Arduino data root: %s\n' "$ARDUINO_ROOT"
  printf 'ESP32 cores found: %s\n' "$CORE_COUNT"
  printf 'Monitor recipes configured: %s\n' "$CONFIGURED"
  printf 'Monitor conflicts: %s\n' "$CONFLICTS"
  printf 'Firmware auth configs: %s\n' "$FIRMWARE_CONFIGS"
} > "$STATUS_PATH"
chmod 600 "$STATUS_PATH" 2>/dev/null || true

[ -x "$MONITOR_PATH" ] || espns_die "Monitor executable verification failed."
[ -f "$CONFIG_PATH" ] || espns_die "Host config verification failed."
espns_ok "Monitor executable OK"
espns_ok "Authentication configuration OK"
if [ "$CORE_COUNT" -gt 0 ] && [ "$CONFIGURED" -gt 0 ]; then
  espns_ok "Arduino network monitor integration OK"
fi

printf '\n%sESPNetworkSerial is ready.%s\n' "$ESPNS_BOLD" "$ESPNS_RESET"
printf 'Restart Arduino IDE before using the network Serial Monitor.\n'
printf 'Repair command: "%s/install.sh" --repair\n' "$APP_DIR"
printf 'Uninstall command: "%s/uninstall.sh"\n\n' "$APP_DIR"

if [ "$CONFLICTS" -gt 0 ]; then
  exit 2
fi
