#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MONITOR_SOURCE="$SCRIPT_DIR/espnetworkserial-monitor"

fail() {
  printf 'ASSERT FAILED: %s\n' "$1" >&2
  exit 1
}

[ -x "$MONITOR_SOURCE" ] || fail "built monitor fixture missing: $MONITOR_SOURCE"

ROOT="$(mktemp -d)"
trap 'rm -rf "$ROOT"' EXIT

export ESPNS_INSTALL_ROOT="$ROOT/app"
export ESPNS_ARDUINO_DATA_ROOT="$ROOT/Arduino15"
export ESPNS_NO_COLOR=1

CORE="$ESPNS_ARDUINO_DATA_ROOT/packages/esp32/hardware/esp32/3.3.10"
mkdir -p "$CORE/cores/esp32"
printf 'compiler.warning_flags=-Wall\n' > "$CORE/platform.local.txt"
cat > "$CORE/boards.local.txt" <<'EOF'
# ESPNetworkSerial PROMPTLESS OTA BEGIN
some.legacy.setting=1
# ESPNetworkSerial PROMPTLESS OTA END
EOF

bash "$SCRIPT_DIR/install.sh"

[ -x "$ESPNS_INSTALL_ROOT/espnetworkserial-monitor" ] || fail "monitor not installed"
[ -f "$ESPNS_INSTALL_ROOT/config.json" ] || fail "config.json not generated"
[ -f "$CORE/cores/esp32/ESPNetworkSerialConfig.h" ] || fail "firmware config not generated"

KEY="$(sed -n 's/.*"authKey": "\([^"]*\)".*/\1/p' "$ESPNS_INSTALL_ROOT/config.json")"
[ "${#KEY}" -eq 64 ] || fail "generated key should be 64 hex characters"
grep -Fq "#define ESPNS_DEFAULT_AUTH_KEY \"$KEY\"" "$CORE/cores/esp32/ESPNetworkSerialConfig.h" ||
  fail "firmware key does not match host key"
grep -Fq 'compiler.warning_flags=-Wall' "$CORE/platform.local.txt" ||
  fail "unrelated platform content lost"
[ "$(grep -Fc '# ESPNetworkSerial BEGIN' "$CORE/platform.local.txt")" -eq 1 ] ||
  fail "managed block should occur once"
[ ! -f "$CORE/boards.local.txt" ] || fail "empty legacy boards.local should be removed"

cp "$ESPNS_INSTALL_ROOT/config.json" "$ROOT/config.before-repair"
bash "$ESPNS_INSTALL_ROOT/install.sh" --repair
cmp "$ROOT/config.before-repair" "$ESPNS_INSTALL_ROOT/config.json" >/dev/null ||
  fail "repair changed the authentication key"
[ "$(grep -Fc '# ESPNetworkSerial BEGIN' "$CORE/platform.local.txt")" -eq 1 ] ||
  fail "repair duplicated managed block"

OLD_KEY="$KEY"
bash "$ESPNS_INSTALL_ROOT/install.sh" --regenerate-key --yes
KEY="$(sed -n 's/.*"authKey": "\([^"]*\)".*/\1/p' "$ESPNS_INSTALL_ROOT/config.json")"
[ "$KEY" != "$OLD_KEY" ] || fail "regenerate-key did not rotate the key"
grep -Fq "#define ESPNS_DEFAULT_AUTH_KEY \"$KEY\"" "$CORE/cores/esp32/ESPNetworkSerialConfig.h" ||
  fail "firmware config was not updated after key rotation"

CONFLICT="$ESPNS_ARDUINO_DATA_ROOT/packages/esp32/hardware/esp32/3.4.0"
mkdir -p "$CONFLICT/cores/esp32"
printf 'pluggable_monitor.pattern.network="/opt/OtherMonitor"\n' > "$CONFLICT/platform.local.txt"

set +e
bash "$ESPNS_INSTALL_ROOT/install.sh" --repair
RESULT="$?"
set -e
[ "$RESULT" -eq 2 ] || fail "third-party monitor conflict should return 2"
grep -Fq '/opt/OtherMonitor' "$CONFLICT/platform.local.txt" ||
  fail "third-party monitor recipe was modified"
! grep -Fq '# ESPNetworkSerial BEGIN' "$CONFLICT/platform.local.txt" ||
  fail "conflicting core received managed monitor block"
[ -f "$CONFLICT/cores/esp32/ESPNetworkSerialConfig.h" ] ||
  fail "firmware auth config should still be provisioned for conflicting core"

bash "$ESPNS_INSTALL_ROOT/uninstall.sh"
[ ! -d "$ESPNS_INSTALL_ROOT" ] || fail "application directory should be removed"
! grep -Fq '# ESPNetworkSerial BEGIN' "$CORE/platform.local.txt" ||
  fail "managed block survived uninstall"
grep -Fq 'compiler.warning_flags=-Wall' "$CORE/platform.local.txt" ||
  fail "uninstall removed unrelated platform content"
grep -Fq '/opt/OtherMonitor' "$CONFLICT/platform.local.txt" ||
  fail "uninstall removed third-party monitor recipe"
[ ! -f "$CORE/cores/esp32/ESPNetworkSerialConfig.h" ] ||
  fail "managed firmware config survived uninstall"
[ ! -f "$CONFLICT/cores/esp32/ESPNetworkSerialConfig.h" ] ||
  fail "managed conflict-core firmware config survived uninstall"

printf 'Unix installer integration + auth provisioning tests PASS\n'
