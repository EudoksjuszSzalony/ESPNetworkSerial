#!/usr/bin/env bash

ESPNS_PLATFORM_BEGIN="# ESPNetworkSerial BEGIN"
ESPNS_PLATFORM_END="# ESPNetworkSerial END"
ESPNS_LEGACY_BOARDS_BEGIN="# ESPNetworkSerial PROMPTLESS OTA BEGIN"
ESPNS_LEGACY_BOARDS_END="# ESPNetworkSerial PROMPTLESS OTA END"
ESPNS_FIRMWARE_MARKER="// ESPNetworkSerial installer-managed configuration"

espns_init_terminal() {
  if [ -t 1 ] && [ "${ESPNS_NO_COLOR:-0}" != "1" ]; then
    ESPNS_BOLD="$(printf '\033[1m')"
    ESPNS_DIM="$(printf '\033[2m')"
    ESPNS_GREEN="$(printf '\033[32m')"
    ESPNS_YELLOW="$(printf '\033[33m')"
    ESPNS_RED="$(printf '\033[31m')"
    ESPNS_CYAN="$(printf '\033[36m')"
    ESPNS_RESET="$(printf '\033[0m')"
  else
    ESPNS_BOLD=""
    ESPNS_DIM=""
    ESPNS_GREEN=""
    ESPNS_YELLOW=""
    ESPNS_RED=""
    ESPNS_CYAN=""
    ESPNS_RESET=""
  fi
}

espns_banner() {
  printf '\n%sESPNetworkSerial Setup%s\n' "$ESPNS_BOLD" "$ESPNS_RESET"
  printf '%s────────────────────────────────────────────────────%s\n\n' "$ESPNS_DIM" "$ESPNS_RESET"
}

espns_step() {
  printf '%s[%s/%s]%s %s%s%s\n' "$ESPNS_CYAN" "$1" "$2" "$ESPNS_RESET" "$ESPNS_BOLD" "$3" "$ESPNS_RESET"
}

espns_ok() {
  printf '      %s✓%s %s\n' "$ESPNS_GREEN" "$ESPNS_RESET" "$1"
}

espns_info() {
  printf '      %s\n' "$1"
}

espns_warn() {
  printf '      %s!%s %s\n' "$ESPNS_YELLOW" "$ESPNS_RESET" "$1"
}

espns_die() {
  printf '      %s✗%s %s\n' "$ESPNS_RED" "$ESPNS_RESET" "$1" >&2
  exit 1
}

espns_detect_platform() {
  local kernel machine
  kernel="$(uname -s)"
  machine="$(uname -m)"

  case "$kernel" in
    Linux)
      ESPNS_OS="linux"
      ESPNS_OS_LABEL="Linux"
      ;;
    Darwin)
      ESPNS_OS="macos"
      ESPNS_OS_LABEL="macOS"
      ;;
    *)
      return 1
      ;;
  esac

  case "$machine" in
    x86_64|amd64)
      ESPNS_ARCH="amd64"
      ;;
    arm64|aarch64)
      ESPNS_ARCH="arm64"
      ;;
    *)
      return 2
      ;;
  esac
}

espns_default_app_dir() {
  if [ "$ESPNS_OS" = "macos" ]; then
    printf '%s\n' "$HOME/Library/Application Support/ESPNetworkSerial"
  else
    printf '%s\n' "${XDG_DATA_HOME:-$HOME/.local/share}/ESPNetworkSerial"
  fi
}

espns_default_arduino_root() {
  if [ "$ESPNS_OS" = "macos" ]; then
    printf '%s\n' "$HOME/Library/Arduino15"
  else
    printf '%s\n' "$HOME/.arduino15"
  fi
}

espns_strip_managed_block() {
  local path begin end temp
  path="$1"
  begin="$2"
  end="$3"

  [ -f "$path" ] || return 0

  temp="$path.espns.$"
  awk -v begin="$begin" -v end="$end" '
    function flush_buffer() {
      if (buffer != "") {
        printf "%s", buffer
        buffer = ""
      }
    }

    !skip && $0 == begin {
      skip = 1
      buffer = $0 ORS
      next
    }

    skip {
      buffer = buffer $0 ORS
      if ($0 == end) {
        skip = 0
        buffer = ""
      }
      next
    }

    { print }

    END {
      if (skip) {
        flush_buffer()
      }
    }
  ' "$path" > "$temp"

  if grep -q '[^[:space:]]' "$temp"; then
    mv "$temp" "$path"
  else
    rm -f "$temp" "$path"
  fi
}

espns_configure_platform_file() {
  local core_dir monitor_path platform_file boards_file
  core_dir="$1"
  monitor_path="$2"
  platform_file="$core_dir/platform.local.txt"
  boards_file="$core_dir/boards.local.txt"

  espns_strip_managed_block "$platform_file" "$ESPNS_PLATFORM_BEGIN" "$ESPNS_PLATFORM_END"

  if [ -f "$platform_file" ] && grep -Eq '^[[:space:]]*pluggable_monitor\.pattern\.network[[:space:]]*=' "$platform_file"; then
    return 2
  fi

  if [ -s "$platform_file" ]; then
    printf '\n' >> "$platform_file"
  fi
  {
    printf '%s\n' "$ESPNS_PLATFORM_BEGIN"
    printf 'pluggable_monitor.pattern.network="%s"\n' "$monitor_path"
    printf '%s\n' "$ESPNS_PLATFORM_END"
  } >> "$platform_file"

  espns_strip_managed_block "$boards_file" "$ESPNS_LEGACY_BOARDS_BEGIN" "$ESPNS_LEGACY_BOARDS_END"
  return 0
}

espns_remove_core_integration() {
  local core_dir firmware_config
  core_dir="$1"

  espns_strip_managed_block "$core_dir/platform.local.txt" "$ESPNS_PLATFORM_BEGIN" "$ESPNS_PLATFORM_END"
  espns_strip_managed_block "$core_dir/boards.local.txt" "$ESPNS_LEGACY_BOARDS_BEGIN" "$ESPNS_LEGACY_BOARDS_END"

  firmware_config="$core_dir/cores/esp32/ESPNetworkSerialConfig.h"
  if [ -f "$firmware_config" ] && grep -Fq "$ESPNS_FIRMWARE_MARKER" "$firmware_config"; then
    rm -f "$firmware_config"
  fi
}

espns_copy_if_different() {
  local source destination mode
  source="$1"
  destination="$2"
  mode="$3"

  [ -f "$source" ] || return 0
  if [ "$source" = "$destination" ]; then
    chmod "$mode" "$destination"
    return 0
  fi
  install -m "$mode" "$source" "$destination"
}
