[CmdletBinding()]
param(
    [string]$ArduinoDataRoot = $(if ($env:LOCALAPPDATA) { Join-Path $env:LOCALAPPDATA "Arduino15" } else { "" })
)

$ErrorActionPreference = "Stop"
$platformBeginMarker = "# ESPNetworkSerial BEGIN"
$platformEndMarker = "# ESPNetworkSerial END"
$legacyBoardsBeginMarker = "# ESPNetworkSerial PROMPTLESS OTA BEGIN"
$legacyBoardsEndMarker = "# ESPNetworkSerial PROMPTLESS OTA END"
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)

function Remove-ManagedBlock {
    param([string]$Content, [string]$BeginMarker, [string]$EndMarker)
    $escapedBegin = [regex]::Escape($BeginMarker)
    $escapedEnd = [regex]::Escape($EndMarker)
    $pattern = "(?ms)^$escapedBegin\r?\n.*?^$escapedEnd\r?\n?"
    return [regex]::Replace($Content, $pattern, "").Trim()
}

function Remove-ManagedFileBlock {
    param([string]$Path, [string]$BeginMarker, [string]$EndMarker)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return }
    $content = [System.IO.File]::ReadAllText($Path)
    $newContent = Remove-ManagedBlock -Content $content -BeginMarker $BeginMarker -EndMarker $EndMarker
    if ($newContent.Length -eq 0) {
        Remove-Item -LiteralPath $Path
    } else {
        [System.IO.File]::WriteAllText($Path, $newContent + [Environment]::NewLine, $utf8NoBom)
    }
}

if ([string]::IsNullOrWhiteSpace($ArduinoDataRoot)) { exit 0 }

$coreRoot = Join-Path ([System.IO.Path]::GetFullPath($ArduinoDataRoot)) "packages\esp32\hardware\esp32"
if (-not (Test-Path -LiteralPath $coreRoot -PathType Container)) { exit 0 }

$versions = @(Get-ChildItem -LiteralPath $coreRoot -Directory | Sort-Object Name)
foreach ($version in $versions) {
    Remove-ManagedFileBlock -Path (Join-Path $version.FullName "platform.local.txt") -BeginMarker $platformBeginMarker -EndMarker $platformEndMarker
    Remove-ManagedFileBlock -Path (Join-Path $version.FullName "boards.local.txt") -BeginMarker $legacyBoardsBeginMarker -EndMarker $legacyBoardsEndMarker
}

Write-Host "ESPNetworkSerial Arduino integration removed."
exit 0
