#!/usr/bin/env pwsh
#Requires -Version 5.1
# Run uninstall.cmd against every installed copy of Dishonored.
#
# uninstall.cmd is the launcher contract and takes one game path, which is
# right for the launcher: it knows which install it deployed to. A dev
# uninstall wants the other thing - the mod removed from every copy on the
# machine, so a later test cannot pick up an .asi left behind in the copy the
# single-path resolver did not choose.

param(
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$scriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir

Import-Module (Join-Path $projectDir 'cameraunlock-core/powershell/GamePathDetection.psm1') -Force
$gamePaths = @(Find-AllGamePaths -GameId 'dishonored')

$uninstall = Join-Path $scriptDir 'uninstall.cmd'
if ($gamePaths.Count -eq 0) {
    # No path to pass, so let uninstall.cmd run its own resolver and print the
    # not-found diagnostic it exists to print.
    & cmd.exe /c $uninstall /y
    exit $LASTEXITCODE
}

Write-Host "Found $($gamePaths.Count) installation(s) of Dishonored" -ForegroundColor Cyan
foreach ($path in $gamePaths) {
    Write-Host ""
    Write-Host "--- $path" -ForegroundColor Cyan
    if ($Force) {
        & cmd.exe /c $uninstall $path /y /force
    } else {
        & cmd.exe /c $uninstall $path /y
    }
    if ($LASTEXITCODE -ne 0) {
        throw "uninstall.cmd failed for $path (exit $LASTEXITCODE)"
    }
}
