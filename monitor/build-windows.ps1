$ErrorActionPreference = "Stop"

Push-Location $PSScriptRoot
try {
    if (-not (Get-Command go -ErrorAction SilentlyContinue)) {
        throw "Go was not found in PATH. Install Go from https://go.dev/dl/ and reopen PowerShell."
    }

    Write-Host "Running monitor tests..."
    go test ./...

    Write-Host "Building espnetworkserial-monitor.exe..."
    go build -trimpath -ldflags "-s -w" -o espnetworkserial-monitor.exe .

    Write-Host ""
    Write-Host "Built: $PSScriptRoot\espnetworkserial-monitor.exe"
} finally {
    Pop-Location
}
