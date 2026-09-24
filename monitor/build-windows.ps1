$ErrorActionPreference = "Stop"

Push-Location $PSScriptRoot
try {
    if (-not (Get-Command go -ErrorAction SilentlyContinue)) {
        throw "Go was not found in PATH. Install Go from https://go.dev/dl/ and reopen PowerShell."
    }

    Write-Host "Running monitor tests..."
    go test ./...
    if ($LASTEXITCODE -ne 0) {
        throw "Monitor tests failed with exit code $LASTEXITCODE. Build aborted."
    }

    Write-Host "Building espnetworkserial-monitor.exe..."
    go build -trimpath -ldflags "-s -w" -o espnetworkserial-monitor.exe .
    if ($LASTEXITCODE -ne 0) {
        throw "Go build failed with exit code $LASTEXITCODE."
    }

    Write-Host ""
    Write-Host "Built: $PSScriptRoot\espnetworkserial-monitor.exe"
} finally {
    Pop-Location
}
