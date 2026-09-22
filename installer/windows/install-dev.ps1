param(
    [string]$MonitorPath = (Join-Path $PSScriptRoot "..\..\monitor\espnetworkserial-monitor.exe"),
    [switch]$PromptlessOTA
)

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
    return [regex]::Replace($Content, $pattern, "").TrimEnd()
}

function Write-OptionalFile {
    param(
        [string]$Path,
        [string]$Content,
        [System.Text.Encoding]$Encoding
    )

    if ($Content.Trim().Length -eq 0) {
        if (Test-Path -LiteralPath $Path -PathType Leaf) {
            Remove-Item -LiteralPath $Path
        }
        return
    }

    [System.IO.File]::WriteAllText(
        $Path,
        $Content.TrimEnd() + [Environment]::NewLine,
        $Encoding
    )
}

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
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)

foreach ($version in $versions) {
    $platformLocalPath = Join-Path $version.FullName "platform.local.txt"
    $platformContent = ""
    if (Test-Path -LiteralPath $platformLocalPath -PathType Leaf) {
        $platformContent = [System.IO.File]::ReadAllText($platformLocalPath)
    }

    $platformContent = Remove-ManagedBlock `
        -Content $platformContent `
        -BeginMarker $platformBeginMarker `
        -EndMarker $platformEndMarker

    if ($platformContent -match '(?m)^\s*pluggable_monitor\.pattern\.network\s*=') {
        throw "A different network pluggable monitor is already configured in $platformLocalPath. Remove or reconcile it manually before installing ESPNetworkSerialMonitor."
    }

    $platformLines = @(
        $platformBeginMarker,
        "pluggable_monitor.pattern.network=`"$recipePath`""
    )

    if ($PromptlessOTA) {
        $platformLines += @(
            "",
            "# Development-only no-password OTA recipe.",
            "# This intentionally declares no upload.field.password, so Arduino IDE does not show the password dialog.",
            "tools.espns_ota.cmd=python3 `"{runtime.platform.path}/tools/espota.py`" -r",
            "tools.espns_ota.cmd.windows=`"{runtime.platform.path}\tools\espota.exe`" -r",
            "tools.espns_ota.upload.protocol=network",
            "tools.espns_ota.upload.params.verbose=",
            "tools.espns_ota.upload.params.quiet=",
            "tools.espns_ota.upload.pattern={cmd} -i {upload.port.address} -p {upload.port.properties.port} -f `"{build.path}/{build.project_name}.bin`""
        )
    }

    $platformLines += $platformEndMarker
    $platformBlock = $platformLines -join [Environment]::NewLine

    if ($platformContent.Length -gt 0) {
        $newPlatformContent = $platformContent + [Environment]::NewLine + [Environment]::NewLine + $platformBlock
    } else {
        $newPlatformContent = $platformBlock
    }

    Write-OptionalFile -Path $platformLocalPath -Content $newPlatformContent -Encoding $utf8NoBom
    Write-Host "Configured ESP32 core $($version.Name): $platformLocalPath"

    $boardsLocalPath = Join-Path $version.FullName "boards.local.txt"
    $boardsContent = ""
    if (Test-Path -LiteralPath $boardsLocalPath -PathType Leaf) {
        $boardsContent = [System.IO.File]::ReadAllText($boardsLocalPath)
    }

    $boardsContent = Remove-ManagedBlock `
        -Content $boardsContent `
        -BeginMarker $boardsBeginMarker `
        -EndMarker $boardsEndMarker

    if ($PromptlessOTA) {
        if ($boardsContent -match '(?m)^\s*[^#\s][^=]*\.upload\.tool\.network\s*=') {
            throw "A custom network upload-tool override already exists in $boardsLocalPath. Remove or reconcile it manually before enabling -PromptlessOTA."
        }

        $boardsPath = Join-Path $version.FullName "boards.txt"
        if (-not (Test-Path -LiteralPath $boardsPath -PathType Leaf)) {
            throw "boards.txt not found: $boardsPath"
        }

        $boardIds = New-Object System.Collections.Generic.HashSet[string]
        foreach ($line in [System.IO.File]::ReadLines($boardsPath)) {
            if ($line -match '^([^.\s=]+)\.name=') {
                [void]$boardIds.Add($matches[1])
            }
        }

        if ($boardIds.Count -eq 0) {
            throw "Could not discover board IDs from: $boardsPath"
        }

        $overrideLines = @($boardsBeginMarker)
        foreach ($boardId in @($boardIds) | Sort-Object) {
            $overrideLines += "$boardId.upload.tool.network=espns_ota"
        }
        $overrideLines += $boardsEndMarker
        $overrideBlock = $overrideLines -join [Environment]::NewLine

        if ($boardsContent.Length -gt 0) {
            $boardsContent = $boardsContent + [Environment]::NewLine + [Environment]::NewLine + $overrideBlock
        } else {
            $boardsContent = $overrideBlock
        }

        Write-OptionalFile -Path $boardsLocalPath -Content $boardsContent -Encoding $utf8NoBom
        Write-Host "Enabled promptless no-password OTA for ESP32 core $($version.Name): $boardsLocalPath"
    } else {
        Write-OptionalFile -Path $boardsLocalPath -Content $boardsContent -Encoding $utf8NoBom
    }
}

Write-Host ""
Write-Host "ESPNetworkSerialMonitor development integration installed."
if ($PromptlessOTA) {
    Write-Warning "Promptless OTA is enabled for network uploads in the installed ESP32 core versions."
    Write-Warning "Password-protected ArduinoOTA uploads will not work while this development override is enabled."
    Write-Host "Run this installer again without -PromptlessOTA to restore the core's normal password-capable OTA recipe."
}
Write-Host "Restart Arduino IDE before testing."
Write-Host "If the ESP32 core is updated later, run this installer again for the new core version."
