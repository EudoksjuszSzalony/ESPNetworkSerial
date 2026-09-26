#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=common.sh
. "$SCRIPT_DIR/common.sh"

espns_init_terminal
printf '\n%sESPNetworkSerial Uninstall%s\n' "$ESPNS_BOLD" "$ESPNS_RESET"
printf '%s────────────────────────────────────────────────────%s\n\n' "$ESPNS_DIM" "$ESPNS_RESET"

espns_step 1 4 "Detecting platform"
if ! espns_detect_platform; then
  espns_die "Unsupported platform: $(uname -s) / $(uname -m)"
fi
APP_DIR="${ESPNS_INSTALL_ROOT:-$(espns_default_app_dir)}"
ARDUINO_ROOT="${ESPNS_ARDUINO_DATA_ROOT:-$(espns_default_arduino_root)}"
espns_info "OS: $ESPNS_OS_LABEL"
espns_info "Arduino data: $ARDUINO_ROOT"
espns_ok "Platform detected"

espns_step 2 4 "Removing Arduino integration"
CORE_ROOT="$ARDUINO_ROOT/packages/esp32/hardware/esp32"
REMOVED=0
if [ -d "$CORE_ROOT" ]; then
  for core_dir in "$CORE_ROOT"/*; do
    [ -d "$core_dir" ] || continue
    espns_remove_core_integration "$core_dir"
    REMOVED=$((REMOVED + 1))
    espns_ok "Cleaned ESP32 core $(basename "$core_dir")"
  done
fi
if [ "$REMOVED" -eq 0 ]; then
  espns_info "No ESP32 core installations required cleanup."
fi

espns_step 3 4 "Removing host monitor and local authentication config"
if [ -d "$APP_DIR" ]; then
  rm -rf "$APP_DIR"
  espns_ok "Removed $APP_DIR"
else
  espns_info "Application directory was already absent."
fi

espns_step 4 4 "Finishing"
espns_ok "ESPNetworkSerial integration removed"
printf '\nUnrelated Arduino platform.local.txt content was preserved.\n\n'
