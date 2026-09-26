[CmdletBinding()]
param(
    [string]$RepoPath = ""
)

$ErrorActionPreference = "Stop"

function Resolve-ESPNSGitExecutable {
    $command = Get-Command git.exe -ErrorAction SilentlyContinue
    if ($command -and $command.Source) {
        return $command.Source
    }

    $candidates = New-Object System.Collections.Generic.List[string]

    if ($env:ProgramFiles) {
        $candidates.Add((Join-Path $env:ProgramFiles "Git\cmd\git.exe"))
    }

    if (${env:ProgramFiles(x86)}) {
        $candidates.Add((Join-Path ${env:ProgramFiles(x86)} "Git\cmd\git.exe"))
    }

    if ($env:LOCALAPPDATA) {
        $githubDesktopRoot = Join-Path $env:LOCALAPPDATA "GitHubDesktop"
        if (Test-Path -LiteralPath $githubDesktopRoot -PathType Container) {
            $desktopGit = Get-ChildItem -LiteralPath $githubDesktopRoot -Recurse -Filter git.exe -File -ErrorAction SilentlyContinue |
                Where-Object { $_.FullName -match '\\resources\\app\\git\\cmd\\git\.exe$' } |
                Sort-Object LastWriteTime -Descending |
                Select-Object -First 1 -ExpandProperty FullName

            if ($desktopGit) {
                $candidates.Insert(0, $desktopGit)
            }
        }
    }

    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            return [System.IO.Path]::GetFullPath($candidate)
        }
    }

    throw "Git executable not found. Install Git or GitHub Desktop first."
}

if ([string]::IsNullOrWhiteSpace($RepoPath)) {
    $RepoPath = Split-Path -Parent $PSScriptRoot
}

$RepoPath = [System.IO.Path]::GetFullPath($RepoPath)

if (-not (Test-Path -LiteralPath (Join-Path $RepoPath ".git") -PathType Container)) {
    throw "Not a Git repository: $RepoPath"
}

$gitPath = Resolve-ESPNSGitExecutable

$global:ESPNSGitPath = $gitPath
$global:ESPNSRepoPath = $RepoPath

function global:git {
    & $global:ESPNSGitPath @args
}

function global:espns-repo {
    Set-Location -LiteralPath $global:ESPNSRepoPath
}

Set-Location -LiteralPath $RepoPath

$origin = ""
try {
    $origin = (& $gitPath -C $RepoPath remote get-url origin 2>$null).Trim()
}
catch {
    $origin = ""
}

$branch = (& $gitPath -C $RepoPath branch --show-current 2>$null).Trim()
$head = (& $gitPath -C $RepoPath rev-parse --short HEAD 2>$null).Trim()

Write-Host ""
Write-Host "ESPNetworkSerial Git shell" -ForegroundColor Cyan
Write-Host "-----------------------------------------------"
Write-Host ("Git:    {0}" -f $gitPath)
Write-Host ("Repo:   {0}" -f $RepoPath)
if ($origin) {
    Write-Host ("Origin: {0}" -f $origin)
}
if ($branch) {
    Write-Host ("Branch: {0}" -f $branch)
}
if ($head) {
    Write-Host ("HEAD:   {0}" -f $head)
}
Write-Host ""
Write-Host "Ready. You can now use normal Git commands, for example:" -ForegroundColor Green
Write-Host "  git status"
Write-Host "  git fetch origin"
Write-Host '  git tag -a v0.1.1 <commit> -m "ESPNetworkSerial v0.1.1"'
Write-Host "  git push origin v0.1.1"
Write-Host ""
Write-Host "Run 'espns-repo' at any time to jump back to this repository."
