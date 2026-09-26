[CmdletBinding()]
param(
    [string]$SourceMonitor = (Join-Path $PSScriptRoot "payload\espnetworkserial-monitor.exe")
)

$ErrorActionPreference = "Stop"

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw "ASSERT FAILED: $Message" }
}

if (-not (Test-Path -LiteralPath $SourceMonitor -PathType Leaf)) {
    throw "Built monitor payload not found: $SourceMonitor"
}

$root = Join-Path ([System.IO.Path]::GetTempPath()) ("espns-installer-test-" + [Guid]::NewGuid().ToString("N"))
$arduinoRoot = Join-Path $root "Arduino15"
$appRoot = Join-Path $root "app"
$monitor = Join-Path $appRoot "espnetworkserial-monitor.exe"
$config = Join-Path $appRoot "config.json"
$status = Join-Path $appRoot "integration-status.txt"

try {
    New-Item -ItemType Directory -Path $appRoot -Force | Out-Null
    Copy-Item -LiteralPath $SourceMonitor -Destination $monitor -Force

    $core = Join-Path $arduinoRoot "packages\esp32\hardware\esp32\3.3.10"
    $coreRuntime = Join-Path $core "cores\esp32"
    New-Item -ItemType Directory -Path $coreRuntime -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $core "platform.local.txt") -Value "compiler.warning_flags=-Wall"

    $legacyBoards = @"
# ESPNetworkSerial PROMPTLESS OTA BEGIN
some.legacy.setting=1
# ESPNetworkSerial PROMPTLESS OTA END
"@
    Set-Content -LiteralPath (Join-Path $core "boards.local.txt") -Value $legacyBoards

    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$PSScriptRoot\register-arduino.ps1" -MonitorPath $monitor -ArduinoDataRoot $arduinoRoot -StatusPath $status -ConfigPath $config
    Assert-True ($LASTEXITCODE -eq 0) "first registration should succeed"

    Assert-True (Test-Path -LiteralPath $config -PathType Leaf) "config.json should be provisioned"
    $cfg = Get-Content -LiteralPath $config -Raw | ConvertFrom-Json
    Assert-True ($cfg.authKey.Length -eq 64) "generated authentication key should be 64 hex characters"
    Assert-True (-not $cfg.allowUnauthenticated) "generated config should refuse unauthenticated downgrade"
    $firstKey = [string]$cfg.authKey

    $firmwareConfig = Join-Path $coreRuntime "ESPNetworkSerialConfig.h"
    Assert-True (Test-Path -LiteralPath $firmwareConfig -PathType Leaf) "firmware default auth header should be created"
    $firmwareText = Get-Content -LiteralPath $firmwareConfig -Raw
    $expectedDefine = '#define ESPNS_DEFAULT_AUTH_KEY "' + $firstKey + '"'
    Assert-True ($firmwareText -match [regex]::Escape($expectedDefine)) "firmware header should contain the host key"

    $platform = Get-Content -LiteralPath (Join-Path $core "platform.local.txt") -Raw
    Assert-True ($platform -match [regex]::Escape("# ESPNetworkSerial BEGIN")) "managed block should exist"
    Assert-True (($platform | Select-String -Pattern "# ESPNetworkSerial BEGIN" -AllMatches).Matches.Count -eq 1) "managed block should occur once"
    Assert-True ($platform -match "compiler\.warning_flags=-Wall") "unrelated platform.local content must be preserved"
    Assert-True ($platform -match "pluggable_monitor\.pattern\.network=") "network monitor recipe should exist"
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $core "boards.local.txt"))) "empty legacy boards.local should be removed"

    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$PSScriptRoot\register-arduino.ps1" -MonitorPath $monitor -ArduinoDataRoot $arduinoRoot -StatusPath $status -ConfigPath $config
    Assert-True ($LASTEXITCODE -eq 0) "second registration should be idempotent"

    $cfgAfterRepair = Get-Content -LiteralPath $config -Raw | ConvertFrom-Json
    Assert-True ([string]$cfgAfterRepair.authKey -eq $firstKey) "Repair must reuse the existing authentication key"

    $platform = Get-Content -LiteralPath (Join-Path $core "platform.local.txt") -Raw
    Assert-True (($platform | Select-String -Pattern "# ESPNetworkSerial BEGIN" -AllMatches).Matches.Count -eq 1) "idempotent registration must not duplicate the block"

    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$PSScriptRoot\rotate-auth.ps1" -MonitorPath $monitor -RegisterScriptPath "$PSScriptRoot\register-arduino.ps1" -Force
    Assert-True ($LASTEXITCODE -eq 0) "explicit key rotation should succeed"

    $cfgAfterRotation = Get-Content -LiteralPath $config -Raw | ConvertFrom-Json
    $rotatedKey = [string]$cfgAfterRotation.authKey
    Assert-True ($rotatedKey -ne $firstKey) "key rotation must generate a new key"
    $firmwareTextAfterRotation = Get-Content -LiteralPath $firmwareConfig -Raw
    $expectedRotatedDefine = '#define ESPNS_DEFAULT_AUTH_KEY "' + $rotatedKey + '"'
    Assert-True ($firmwareTextAfterRotation -match [regex]::Escape($expectedRotatedDefine)) "firmware header should receive the rotated key"

    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$PSScriptRoot\unregister-arduino.ps1" -ArduinoDataRoot $arduinoRoot
    Assert-True ($LASTEXITCODE -eq 0) "unregister should succeed"

    $platform = Get-Content -LiteralPath (Join-Path $core "platform.local.txt") -Raw
    Assert-True (-not ($platform -match [regex]::Escape("# ESPNetworkSerial BEGIN"))) "managed block should be removed"
    Assert-True ($platform -match "compiler\.warning_flags=-Wall") "unregister must preserve unrelated content"
    Assert-True (-not (Test-Path -LiteralPath $firmwareConfig)) "unregister should remove the managed firmware auth header"

    $conflictCore = Join-Path $arduinoRoot "packages\esp32\hardware\esp32\3.4.0"
    New-Item -ItemType Directory -Path (Join-Path $conflictCore "cores\esp32") -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $conflictCore "platform.local.txt") -Value 'pluggable_monitor.pattern.network="C:/OtherMonitor/monitor.exe"'

    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$PSScriptRoot\register-arduino.ps1" -MonitorPath $monitor -ArduinoDataRoot $arduinoRoot -StatusPath $status -ConfigPath $config
    Assert-True ($LASTEXITCODE -eq 2) "third-party monitor conflict should return exit code 2"

    $conflictText = Get-Content -LiteralPath (Join-Path $conflictCore "platform.local.txt") -Raw
    Assert-True ($conflictText -match "OtherMonitor") "conflicting third-party recipe must not be overwritten"
    Assert-True (-not ($conflictText -match [regex]::Escape("# ESPNetworkSerial BEGIN"))) "conflicting core must not receive ESPNetworkSerial monitor block"
    Assert-True (Test-Path -LiteralPath (Join-Path $conflictCore "cores\esp32\ESPNetworkSerialConfig.h")) "firmware auth defaults should still be available even when monitor recipe conflicts"

    Write-Host "Windows integration + auth provisioning tests PASS"
    exit 0
}
finally {
    Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
}
