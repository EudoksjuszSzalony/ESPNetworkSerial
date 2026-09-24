[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$MonitorPath,
    [string]$ArduinoDataRoot = $(if ($env:LOCALAPPDATA) { Join-Path $env:LOCALAPPDATA "Arduino15" } else { "" }),
    [string]$StatusPath = ""
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
    return [regex]::Replace($Content, $pattern, "").TrimEnd()
}

function Write-OptionalFile {
    param([string]$Path, [string]$Content)
    if ($Content.Trim().Length -eq 0) {
        if (Test-Path -LiteralPath $Path -PathType Leaf) { Remove-Item -LiteralPath $Path }
        return
    }
    [System.IO.File]::WriteAllText($Path, $Content.TrimEnd() + [Environment]::NewLine, $utf8NoBom)
}

function Write-Status {
    param([string[]]$Lines)
    if ([string]::IsNullOrWhiteSpace($StatusPath)) { return }
    $parent = Split-Path -Parent $StatusPath
    if ($parent -and -not (Test-Path -LiteralPath $parent -PathType Container)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
    [System.IO.File]::WriteAllLines($StatusPath, $Lines, $utf8NoBom)
}

$status = New-Object System.Collections.Generic.List[string]
$configured = New-Object System.Collections.Generic.List[string]
$conflicts = New-Object System.Collections.Generic.List[string]

try {
    $MonitorPath = [System.IO.Path]::GetFullPath($MonitorPath)
    if (-not (Test-Path -LiteralPath $MonitorPath -PathType Leaf)) {
        throw "Monitor executable not found: $MonitorPath"
    }
    if ([string]::IsNullOrWhiteSpace($ArduinoDataRoot)) {
        throw "Arduino data root is unavailable because LOCALAPPDATA is not set."
    }

    $ArduinoDataRoot = [System.IO.Path]::GetFullPath($ArduinoDataRoot)
    if ([string]::IsNullOrWhiteSpace($StatusPath)) {
        $StatusPath = Join-Path (Split-Path -Parent $MonitorPath) "integration-status.txt"
    }

    $status.Add("ESPNetworkSerial Arduino integration")
    $status.Add("Monitor: $MonitorPath")
    $status.Add("Arduino data root: $ArduinoDataRoot")
    $status.Add("Timestamp: $([DateTime]::Now.ToString('s'))")
    $status.Add("")

    $coreRoot = Join-Path $ArduinoDataRoot "packages\esp32\hardware\esp32"
    if (-not (Test-Path -LiteralPath $coreRoot -PathType Container)) {
        $status.Add("No ESP32 Arduino core installation was found.")
        $status.Add("Install the ESP32 Arduino core, then run the Repair Arduino integration shortcut.")
        Write-Status $status
        Write-Warning "No ESP32 Arduino core found under: $coreRoot"
        exit 0
    }

    $versions = @(Get-ChildItem -LiteralPath $coreRoot -Directory | Sort-Object Name)
    if ($versions.Count -eq 0) {
        $status.Add("No ESP32 Arduino core versions were found.")
        Write-Status $status
        exit 0
    }

    $recipePath = $MonitorPath.Replace('\', '/')

    foreach ($version in $versions) {
        $platformLocalPath = Join-Path $version.FullName "platform.local.txt"
        $platformContent = ""
        if (Test-Path -LiteralPath $platformLocalPath -PathType Leaf) {
            $platformContent = [System.IO.File]::ReadAllText($platformLocalPath)
        }

        $platformContent = Remove-ManagedBlock -Content $platformContent -BeginMarker $platformBeginMarker -EndMarker $platformEndMarker

        if ($platformContent -match '(?m)^\s*pluggable_monitor\.pattern\.network\s*=') {
            $conflicts.Add("$($version.Name): existing third-party pluggable_monitor.pattern.network")
            continue
        }

        $recipeLine = [string]::Format('pluggable_monitor.pattern.network="{0}"', $recipePath)
        $block = @($platformBeginMarker, $recipeLine, $platformEndMarker) -join [Environment]::NewLine

        if ($platformContent.Length -gt 0) {
            $platformContent = $platformContent + [Environment]::NewLine + [Environment]::NewLine + $block
        } else {
            $platformContent = $block
        }
        Write-OptionalFile -Path $platformLocalPath -Content $platformContent

        $boardsLocalPath = Join-Path $version.FullName "boards.local.txt"
        if (Test-Path -LiteralPath $boardsLocalPath -PathType Leaf) {
            $boardsContent = [System.IO.File]::ReadAllText($boardsLocalPath)
            $boardsContent = Remove-ManagedBlock -Content $boardsContent -BeginMarker $legacyBoardsBeginMarker -EndMarker $legacyBoardsEndMarker
            Write-OptionalFile -Path $boardsLocalPath -Content $boardsContent
        }

        $configured.Add($version.Name)
    }

    if ($configured.Count -gt 0) {
        $status.Add("Configured ESP32 core versions:")
        foreach ($item in $configured) { $status.Add("  - $item") }
        $status.Add("")
    }

    if ($conflicts.Count -gt 0) {
        $status.Add("Skipped because another network pluggable monitor is already configured:")
        foreach ($item in $conflicts) { $status.Add("  - $item") }
        $status.Add("")
        $status.Add("ESPNetworkSerial did not overwrite those existing monitor recipes.")
        Write-Status $status
        Write-Warning "One or more core versions have a monitor conflict. See: $StatusPath"
        exit 2
    }

    $status.Add("Result: integration configured successfully.")
    $status.Add("Restart Arduino IDE before using the network Serial Monitor.")
    $status.Add("After installing a new ESP32 core version, run the repair shortcut again.")
    Write-Status $status

    Write-Host "ESPNetworkSerial Arduino integration configured."
    Write-Host "Status: $StatusPath"
    exit 0
}
catch {
    $status.Add("")
    $status.Add("ERROR: $($_.Exception.Message)")
    Write-Status $status
    Write-Error $_
    exit 1
}
