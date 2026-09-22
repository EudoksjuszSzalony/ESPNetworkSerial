param(
    [string]$MonitorPath = (Join-Path $PSScriptRoot "..\..\monitor\espnetworkserial-monitor.exe")
)

$ErrorActionPreference = "Stop"

$beginMarker = "# ESPNetworkSerial BEGIN"
$endMarker = "# ESPNetworkSerial END"

$MonitorPath = [System.IO.Path]::GetFullPath($MonitorPath)
if (-not (Test-Path -LiteralPath $MonitorPath -PathType Leaf)) {
    throw "Monitor executable not found: $MonitorPath`nRun monitor\build-windows.ps1 first, or pass -MonitorPath."
}

if (-not $env:LOCALAPPDATA) {
    throw "LOCALAPPDATA is not set."
}

$coreRoot = Join-Path $env:LOCALAPPDATA "Arduino15\packages\esp32\hardware\esp32"
if (-not (Test-Path -LiteralPath $coreRoot -PathType Container)) {
    throw "ESP32 Arduino core directory not found: $coreRoot"
}

$versions = @(Get-ChildItem -LiteralPath $coreRoot -Directory | Sort-Object Name)
if ($versions.Count -eq 0) {
    throw "No installed ESP32 Arduino core versions were found under: $coreRoot"
}

$recipePath = $MonitorPath.Replace('\', '/')
$block = @"
$beginMarker
pluggable_monitor.pattern.network="$recipePath"
$endMarker
"@

foreach ($version in $versions) {
    $localPath = Join-Path $version.FullName "platform.local.txt"
    $content = ""
    if (Test-Path -LiteralPath $localPath -PathType Leaf) {
        $content = [System.IO.File]::ReadAllText($localPath)
    }

    $escapedBegin = [regex]::Escape($beginMarker)
    $escapedEnd = [regex]::Escape($endMarker)
    $managedPattern = "(?ms)^$escapedBegin\r?\n.*?^$escapedEnd\r?\n?"
    $contentWithoutManagedBlock = [regex]::Replace($content, $managedPattern, "").TrimEnd()

    if ($contentWithoutManagedBlock -match '(?m)^\s*pluggable_monitor\.pattern\.network\s*=') {
        throw "A different network pluggable monitor is already configured in $localPath. Remove or reconcile it manually before installing ESPNetworkSerialMonitor."
    }

    if ($contentWithoutManagedBlock.Length -gt 0) {
        $newContent = $contentWithoutManagedBlock + [Environment]::NewLine + [Environment]::NewLine + $block.Trim() + [Environment]::NewLine
    } else {
        $newContent = $block.Trim() + [Environment]::NewLine
    }

    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($localPath, $newContent, $utf8NoBom)
    Write-Host "Configured ESP32 core $($version.Name): $localPath"
}

Write-Host ""
Write-Host "ESPNetworkSerialMonitor development integration installed."
Write-Host "Restart Arduino IDE before testing the network Serial Monitor."
Write-Host "If the ESP32 core is updated later, run this installer again for the new core version."
