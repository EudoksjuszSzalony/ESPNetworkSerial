$ErrorActionPreference = "Stop"

$beginMarker = "# ESPNetworkSerial BEGIN"
$endMarker = "# ESPNetworkSerial END"

if (-not $env:LOCALAPPDATA) {
    throw "LOCALAPPDATA is not set."
}

$coreRoot = Join-Path $env:LOCALAPPDATA "Arduino15\packages\esp32\hardware\esp32"
if (-not (Test-Path -LiteralPath $coreRoot -PathType Container)) {
    throw "ESP32 Arduino core directory not found: $coreRoot"
}

$versions = @(Get-ChildItem -LiteralPath $coreRoot -Directory | Sort-Object Name)
$escapedBegin = [regex]::Escape($beginMarker)
$escapedEnd = [regex]::Escape($endMarker)
$managedPattern = "(?ms)^$escapedBegin\r?\n.*?^$escapedEnd\r?\n?"
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)

foreach ($version in $versions) {
    $localPath = Join-Path $version.FullName "platform.local.txt"
    if (-not (Test-Path -LiteralPath $localPath -PathType Leaf)) {
        continue
    }

    $content = [System.IO.File]::ReadAllText($localPath)
    $newContent = [regex]::Replace($content, $managedPattern, "").Trim()

    if ($newContent.Length -eq 0) {
        Remove-Item -LiteralPath $localPath
        Write-Host "Removed empty file: $localPath"
    } else {
        [System.IO.File]::WriteAllText($localPath, $newContent + [Environment]::NewLine, $utf8NoBom)
        Write-Host "Removed ESPNetworkSerial block from: $localPath"
    }
}

Write-Host ""
Write-Host "ESPNetworkSerialMonitor development integration removed. Restart Arduino IDE."
