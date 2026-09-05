[CmdletBinding()]
param([switch]$AllowDirty)

$ErrorActionPreference = 'Stop'

$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

Import-Module (Join-Path $ProjectRoot 'cameraunlock-core\powershell\NightlyRelease.psm1') -Force

$cmakeLists = Get-Content (Join-Path $ProjectRoot 'CMakeLists.txt') -Raw
if ($cmakeLists -notmatch 'project\(DishonoredHeadTracking VERSION (\d+\.\d+\.\d+)') {
    throw "Could not parse version from CMakeLists.txt"
}
$version = $Matches[1]

Publish-NightlyBuild `
    -ModId 'dishonored' `
    -ModName 'DishonoredHeadTracking' `
    -Version $version `
    -ProjectRoot $ProjectRoot `
    -AllowDirty:$AllowDirty
