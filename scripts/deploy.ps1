#!/usr/bin/env pwsh
#Requires -Version 5.1
# Deploy the built DishonoredHeadTracking.asi to the game's Binaries/Win32/
# directory for local testing.
#
# Usage: deploy.ps1 [Debug|Release] [GamePath]
# Defaults to Debug. An explicit GamePath wins over auto-detection
# (same contract as install.cmd) and targets that install alone.
# Without one, every installed copy is deployed to: owning Dishonored on
# more than one store is ordinary, and deploying to whichever one sorts
# first leaves the other running the build it was last given - which reads
# in game as a fix that did nothing.

param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [string]$GamePath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$scriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir

$asi = Join-Path $projectDir "bin/$Configuration/DishonoredHeadTracking.asi"
if (-not (Test-Path $asi)) {
    throw "Build output not found: $asi. Run 'pixi run build' or 'pixi run build-release' first."
}

if ($GamePath) {
    if (-not (Test-Path $GamePath)) {
        throw "Explicit game path does not exist: $GamePath"
    }
    $gamePaths = @($GamePath)
} else {
    Import-Module (Join-Path $projectDir 'cameraunlock-core/powershell/GamePathDetection.psm1') -Force
    $gamePaths = @(Find-AllGamePaths -GameId 'dishonored')
    if ($gamePaths.Count -eq 0) {
        throw "Could not locate Dishonored. Set DISHONORED_PATH, install via Steam, or pass the game path: deploy.ps1 $Configuration <path>"
    }
    Write-Host "Found $($gamePaths.Count) installation(s) of Dishonored" -ForegroundColor Cyan
}

foreach ($path in $gamePaths) {
    $exeDir = Join-Path $path 'Binaries\Win32'
    if (-not (Test-Path $exeDir)) {
        throw "Expected exe directory not found: $exeDir"
    }

    Copy-Item $asi -Destination $exeDir -Force
    Write-Host "Deployed: $asi -> $exeDir" -ForegroundColor Green
}
