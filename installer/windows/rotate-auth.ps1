[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$MonitorPath,
    [Parameter(Mandatory = $true)]
    [string]$RegisterScriptPath,
    [string]$ArduinoDataRoot = "",
    [switch]$Force
)

$ErrorActionPreference = "Stop"

try {
    $MonitorPath = [System.IO.Path]::GetFullPath($MonitorPath)
    $RegisterScriptPath = [System.IO.Path]::GetFullPath($RegisterScriptPath)

    if (-not (Test-Path -LiteralPath $MonitorPath -PathType Leaf)) {
        throw "Monitor executable not found: $MonitorPath"
    }
    if (-not (Test-Path -LiteralPath $RegisterScriptPath -PathType Leaf)) {
        throw "Registration script not found: $RegisterScriptPath"
    }

    $appRoot = Split-Path -Parent $MonitorPath
    $configPath = Join-Path $appRoot "config.json"

    Write-Host ""
    Write-Host "ESPNetworkSerial authentication key rotation" -ForegroundColor Cyan
    Write-Host "----------------------------------------------------"
    Write-Warning "A new key will invalidate the key embedded in previously compiled ESP32 firmware."
    Write-Warning "Those devices must be recompiled/reflashed before secure monitoring works again."
    Write-Host ""

    if (-not $Force) {
        $answer = Read-Host "Generate a new key and update detected ESP32 core configs? [y/N]"
        if ($answer -notmatch '^(?i:y|yes)$') {
            Write-Host "Key rotation cancelled."
            exit 0
        }
    }

    $hadConfig = Test-Path Env:ESPNS_CONFIG
    $previousConfig = $env:ESPNS_CONFIG
    try {
        $env:ESPNS_CONFIG = $configPath
        & $MonitorPath --regenerate-auth
        if ($LASTEXITCODE -ne 0) {
            throw "Monitor key regeneration failed with exit code $LASTEXITCODE."
        }
    }
    finally {
        if ($hadConfig) {
            $env:ESPNS_CONFIG = $previousConfig
        } else {
            Remove-Item Env:ESPNS_CONFIG -ErrorAction SilentlyContinue
        }
    }

    Write-Host ""
    Write-Host "Updating Arduino ESP32 core integration..."
    $registerArgs = @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", $RegisterScriptPath,
        "-MonitorPath", $MonitorPath,
        "-ConfigPath", $configPath
    )
    if (-not [string]::IsNullOrWhiteSpace($ArduinoDataRoot)) {
        $registerArgs += @("-ArduinoDataRoot", $ArduinoDataRoot)
    }

    & powershell.exe @registerArgs
    $registerExit = $LASTEXITCODE
    if ($registerExit -ne 0 -and $registerExit -ne 2) {
        throw "Arduino integration update failed with exit code $registerExit."
    }

    if ($registerExit -eq 2) {
        Write-Warning "The key was rotated, but one or more ESP32 cores have a third-party monitor recipe conflict."
        Write-Warning "Firmware default auth headers were still updated. See integration-status.txt."
    }

    Write-Host ""
    Write-Host "Authentication key rotated successfully." -ForegroundColor Green
    Write-Host "Recompile/reflash ESP32 firmware before reconnecting securely."
    exit 0
}
catch {
    Write-Error $_
    exit 1
}
