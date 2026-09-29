[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$MonitorPath,
    [string]$ArduinoDataRoot = $(if ($env:LOCALAPPDATA) { Join-Path $env:LOCALAPPDATA "Arduino15" } else { "" }),
    [string]$StatusPath = "",
    [string]$ConfigPath = ""
)

$ErrorActionPreference = "Stop"
$platformBeginMarker = "# ESPNetworkSerial BEGIN"
$platformEndMarker = "# ESPNetworkSerial END"
$legacyBoardsBeginMarker = "# ESPNetworkSerial PROMPTLESS OTA BEGIN"
$legacyBoardsEndMarker = "# ESPNetworkSerial PROMPTLESS OTA END"
$secureOtaBoardsBeginMarker = "# ESPNetworkSerial SECURE OTA BEGIN"
$secureOtaBoardsEndMarker = "# ESPNetworkSerial SECURE OTA END"
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

function Invoke-ESPNSMonitor {
    param([string[]]$Arguments)

    $hadConfig = Test-Path Env:ESPNS_CONFIG
    $previousConfig = $env:ESPNS_CONFIG
    try {
        $env:ESPNS_CONFIG = $ConfigPath
        $output = @(& $MonitorPath @Arguments 2>&1)
        $exitCode = $LASTEXITCODE
        if ($exitCode -ne 0) {
            $details = ($output | ForEach-Object { "$_" }) -join [Environment]::NewLine
            throw ("ESPNetworkSerial monitor command failed with exit code " + $exitCode + [Environment]::NewLine + $details)
        }
        return @($output | ForEach-Object { "$_" })
    }
    finally {
        if ($hadConfig) {
            $env:ESPNS_CONFIG = $previousConfig
        } else {
            Remove-Item Env:ESPNS_CONFIG -ErrorAction SilentlyContinue
        }
    }
}

$status = New-Object System.Collections.Generic.List[string]
$configured = New-Object System.Collections.Generic.List[string]
$otaConfigured = New-Object System.Collections.Generic.List[string]
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
    if ([string]::IsNullOrWhiteSpace($ConfigPath)) {
        $ConfigPath = Join-Path (Split-Path -Parent $MonitorPath) "config.json"
    }
    $ConfigPath = [System.IO.Path]::GetFullPath($ConfigPath)

    $status.Add("ESPNetworkSerial Arduino integration")
    $status.Add("Monitor: $MonitorPath")
    $status.Add("Host config: $ConfigPath")
    $status.Add("Arduino data root: $ArduinoDataRoot")
    $status.Add("Timestamp: $([DateTime]::Now.ToString('s'))")
    $status.Add("")

    $authOutput = @(Invoke-ESPNSMonitor -Arguments @("--provision-auth"))
    $status.Add("Authentication provisioning:")
    foreach ($line in $authOutput) { $status.Add("  $line") }
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
        $firmwareCoreDir = Join-Path $version.FullName "cores\esp32"
        if (Test-Path -LiteralPath $firmwareCoreDir -PathType Container) {
            $firmwareConfigPath = Join-Path $firmwareCoreDir "ESPNetworkSerialConfig.h"
            $firmwareOutput = @(Invoke-ESPNSMonitor -Arguments @("--write-firmware-config", $firmwareConfigPath))
            foreach ($line in $firmwareOutput) { $status.Add("$($version.Name): $line") }
        } else {
            $status.Add("$($version.Name): WARNING: cores\esp32 directory not found; firmware default auth config was not installed.")
        }

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
        $otaToolLines = @(
            [string]::Format('tools.espns_ota.cmd="{0}"', $recipePath),
            'tools.espns_ota.upload.protocol=network',
            'tools.espns_ota.upload.params.verbose=',
            'tools.espns_ota.upload.params.quiet=',
            'tools.espns_ota.upload.pattern={cmd} --ota-upload --espota "{runtime.platform.path}\tools\espota.exe" --ip {upload.port.address} --port {upload.port.properties.port} --file "{build.path}/{build.project_name}.bin"'
        )
        $block = @($platformBeginMarker, $recipeLine) + $otaToolLines + @($platformEndMarker)
        $block = $block -join [Environment]::NewLine

        if ($platformContent.Length -gt 0) {
            $platformContent = $platformContent + [Environment]::NewLine + [Environment]::NewLine + $block
        } else {
            $platformContent = $block
        }
        Write-OptionalFile -Path $platformLocalPath -Content $platformContent

        $boardsLocalPath = Join-Path $version.FullName "boards.local.txt"
        $boardsContent = ""
        if (Test-Path -LiteralPath $boardsLocalPath -PathType Leaf) {
            $boardsContent = [System.IO.File]::ReadAllText($boardsLocalPath)
        }
        $boardsContent = Remove-ManagedBlock -Content $boardsContent -BeginMarker $legacyBoardsBeginMarker -EndMarker $legacyBoardsEndMarker
        $boardsContent = Remove-ManagedBlock -Content $boardsContent -BeginMarker $secureOtaBoardsBeginMarker -EndMarker $secureOtaBoardsEndMarker

        if ($boardsContent -match '(?m)^\s*[^#\s][^=]*\.upload\.tool\.network\s*=') {
            $conflicts.Add("$($version.Name): existing third-party network upload-tool override")
            Write-OptionalFile -Path $boardsLocalPath -Content $boardsContent
            continue
        }

        $boardsPath = Join-Path $version.FullName "boards.txt"
        if (-not (Test-Path -LiteralPath $boardsPath -PathType Leaf)) {
            $status.Add("$($version.Name): WARNING: boards.txt not found; secure promptless OTA was not registered.")
            Write-OptionalFile -Path $boardsLocalPath -Content $boardsContent
            $configured.Add($version.Name)
            continue
        }

        $boardIds = New-Object System.Collections.Generic.HashSet[string]
        foreach ($line in [System.IO.File]::ReadLines($boardsPath)) {
            if ($line -match '^([^.\s=]+)\.name=') {
                [void]$boardIds.Add($matches[1])
            }
        }

        if ($boardIds.Count -eq 0) {
            $status.Add("$($version.Name): WARNING: no board IDs found; secure promptless OTA was not registered.")
            Write-OptionalFile -Path $boardsLocalPath -Content $boardsContent
            $configured.Add($version.Name)
            continue
        }

        $overrideLines = @($secureOtaBoardsBeginMarker)
        foreach ($boardId in @($boardIds) | Sort-Object) {
            $overrideLines += "$boardId.upload.tool.network=espns_ota"
        }
        $overrideLines += $secureOtaBoardsEndMarker
        $overrideBlock = $overrideLines -join [Environment]::NewLine

        if ($boardsContent.Length -gt 0) {
            $boardsContent = $boardsContent + [Environment]::NewLine + [Environment]::NewLine + $overrideBlock
        } else {
            $boardsContent = $overrideBlock
        }
        Write-OptionalFile -Path $boardsLocalPath -Content $boardsContent

        $otaConfigured.Add($version.Name)
        $configured.Add($version.Name)
    }

    $status.Add("")
    if ($configured.Count -gt 0) {
        $status.Add("Configured ESP32 core versions:")
        foreach ($item in $configured) { $status.Add("  - $item") }
        $status.Add("")
        $status.Add("Secure promptless OTA configured:")
        foreach ($item in $otaConfigured) { $status.Add("  - $item") }
        $status.Add("")
    }

    if ($conflicts.Count -gt 0) {
        $status.Add("Skipped network-monitor integration because another implementation is already configured:")
        foreach ($item in $conflicts) { $status.Add("  - $item") }
        $status.Add("")
        $status.Add("ESPNetworkSerial did not overwrite those existing monitor recipes.")
        Write-Status $status
        Write-Warning "One or more core versions have a monitor conflict. See: $StatusPath"
        exit 2
    }

    $status.Add("Result: integration configured successfully.")
    $status.Add("OTA authentication: ESPNS config.json key is injected automatically by the host wrapper; Arduino IDE has no password field.")
    $status.Add("Restart Arduino IDE before using the network Serial Monitor or OTA upload.")
    $status.Add("After installing a new ESP32 core version, run the repair shortcut again.")
    Write-Status $status

    Write-Host "ESPNetworkSerial Arduino integration configured."
    Write-Host "Authentication: provisioned/reused from $ConfigPath"
    Write-Host "Secure OTA: config-backed authentication registered; no Arduino IDE password prompt"
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
