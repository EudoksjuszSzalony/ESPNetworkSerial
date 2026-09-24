$ErrorActionPreference = "Stop"

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw "ASSERT FAILED: $Message" }
}

$root = Join-Path ([System.IO.Path]::GetTempPath()) ("espns-installer-test-" + [Guid]::NewGuid().ToString("N"))
$arduinoRoot = Join-Path $root "Arduino15"
$appRoot = Join-Path $root "app"
$monitor = Join-Path $appRoot "espnetworkserial-monitor.exe"
$status = Join-Path $appRoot "integration-status.txt"

try {
    New-Item -ItemType Directory -Path $appRoot -Force | Out-Null
    New-Item -ItemType File -Path $monitor -Force | Out-Null

    $core = Join-Path $arduinoRoot "packages\esp32\hardware\esp32\3.3.10"
    New-Item -ItemType Directory -Path $core -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $core "platform.local.txt") -Value "compiler.warning_flags=-Wall"

    $legacyBoards = @"
# ESPNetworkSerial PROMPTLESS OTA BEGIN
some.legacy.setting=1
# ESPNetworkSerial PROMPTLESS OTA END
"@
    Set-Content -LiteralPath (Join-Path $core "boards.local.txt") -Value $legacyBoards

    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$PSScriptRoot\register-arduino.ps1" -MonitorPath $monitor -ArduinoDataRoot $arduinoRoot -StatusPath $status
    Assert-True ($LASTEXITCODE -eq 0) "first registration should succeed"

    $platform = Get-Content -LiteralPath (Join-Path $core "platform.local.txt") -Raw
    Assert-True ($platform -match [regex]::Escape("# ESPNetworkSerial BEGIN")) "managed block should exist"
    Assert-True (($platform | Select-String -Pattern "# ESPNetworkSerial BEGIN" -AllMatches).Matches.Count -eq 1) "managed block should occur once"
    Assert-True ($platform -match "compiler\.warning_flags=-Wall") "unrelated platform.local content must be preserved"
    Assert-True ($platform -match "pluggable_monitor\.pattern\.network=") "network monitor recipe should exist"
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $core "boards.local.txt"))) "empty legacy boards.local should be removed"

    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$PSScriptRoot\register-arduino.ps1" -MonitorPath $monitor -ArduinoDataRoot $arduinoRoot -StatusPath $status
    Assert-True ($LASTEXITCODE -eq 0) "second registration should be idempotent"

    $platform = Get-Content -LiteralPath (Join-Path $core "platform.local.txt") -Raw
    Assert-True (($platform | Select-String -Pattern "# ESPNetworkSerial BEGIN" -AllMatches).Matches.Count -eq 1) "idempotent registration must not duplicate the block"

    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$PSScriptRoot\unregister-arduino.ps1" -ArduinoDataRoot $arduinoRoot
    Assert-True ($LASTEXITCODE -eq 0) "unregister should succeed"

    $platform = Get-Content -LiteralPath (Join-Path $core "platform.local.txt") -Raw
    Assert-True (-not ($platform -match [regex]::Escape("# ESPNetworkSerial BEGIN"))) "managed block should be removed"
    Assert-True ($platform -match "compiler\.warning_flags=-Wall") "unregister must preserve unrelated content"

    $conflictCore = Join-Path $arduinoRoot "packages\esp32\hardware\esp32\3.4.0"
    New-Item -ItemType Directory -Path $conflictCore -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $conflictCore "platform.local.txt") -Value 'pluggable_monitor.pattern.network="C:/OtherMonitor/monitor.exe"'

    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$PSScriptRoot\register-arduino.ps1" -MonitorPath $monitor -ArduinoDataRoot $arduinoRoot -StatusPath $status
    Assert-True ($LASTEXITCODE -eq 2) "third-party monitor conflict should return exit code 2"

    $conflictText = Get-Content -LiteralPath (Join-Path $conflictCore "platform.local.txt") -Raw
    Assert-True ($conflictText -match "OtherMonitor") "conflicting third-party recipe must not be overwritten"
    Assert-True (-not ($conflictText -match [regex]::Escape("# ESPNetworkSerial BEGIN"))) "conflicting core must not receive ESPNetworkSerial block"

    Write-Host "Windows integration script tests PASS"
}
finally {
    Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
}
