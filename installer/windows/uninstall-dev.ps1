$ErrorActionPreference = "Stop"

$platformBeginMarker = "# ESPNetworkSerial BEGIN"
$platformEndMarker = "# ESPNetworkSerial END"
$boardsBeginMarker = "# ESPNetworkSerial PROMPTLESS OTA BEGIN"
$boardsEndMarker = "# ESPNetworkSerial PROMPTLESS OTA END"

function Remove-ManagedBlock {
    param(
        [string]$Content,
        [string]$BeginMarker,
        [string]$EndMarker
    )

    $escapedBegin = [regex]::Escape($BeginMarker)
    $escapedEnd = [regex]::Escape($EndMarker)
    $pattern = "(?ms)^$escapedBegin\r?\n.*?^$escapedEnd\r?\n?"
    return [regex]::Replace($Content, $pattern, "").Trim()
}

function Remove-ManagedFileBlock {
    param(
        [string]$Path,
        [string]$BeginMarker,
        [string]$EndMarker,
        [System.Text.Encoding]$Encoding
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return
    }

    $content = [System.IO.File]::ReadAllText($Path)
    $newContent = Remove-ManagedBlock -Content $content -BeginMarker $BeginMarker -EndMarker $EndMarker

    if ($newContent.Length -eq 0) {
        Remove-Item -LiteralPath $Path
        Write-Host "Removed empty file: $Path"
    } else {
        [System.IO.File]::WriteAllText($Path, $newContent + [Environment]::NewLine, $Encoding)
        Write-Host "Removed ESPNetworkSerial block from: $Path"
    }
}

if (-not $env:LOCALAPPDATA) {
    throw "LOCALAPPDATA is not set."
}

$coreRoot = Join-Path $env:LOCALAPPDATA "Arduino15\packages\esp32\hardware\esp32"
if (-not (Test-Path -LiteralPath $coreRoot -PathType Container)) {
    throw "ESP32 Arduino core directory not found: $coreRoot"
}

$versions = @(Get-ChildItem -LiteralPath $coreRoot -Directory | Sort-Object Name)
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)

foreach ($version in $versions) {
    Remove-ManagedFileBlock `
        -Path (Join-Path $version.FullName "platform.local.txt") `
        -BeginMarker $platformBeginMarker `
        -EndMarker $platformEndMarker `
        -Encoding $utf8NoBom

    Remove-ManagedFileBlock `
        -Path (Join-Path $version.FullName "boards.local.txt") `
        -BeginMarker $boardsBeginMarker `
        -EndMarker $boardsEndMarker `
        -Encoding $utf8NoBom
}

Write-Host ""
Write-Host "ESPNetworkSerialMonitor development integration removed. Restart Arduino IDE."
