#!/usr/bin/env pwsh
#Requires -Version 5.1
# Custom packaging for Dishonored Head Tracking (C++ project, no .csproj).
# Produces two ZIPs:
#   - DishonoredHeadTracking-v{version}-installer.zip (GitHub Release)
#   - DishonoredHeadTracking-v{version}-nexus.zip     (Nexus, extract to game folder)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference    = 'SilentlyContinue'

$scriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir

Import-Module (Join-Path $projectDir 'cameraunlock-core/powershell/ReleaseWorkflow.psm1') -Force

$cmakeLists = Get-Content (Join-Path $projectDir 'CMakeLists.txt') -Raw
if ($cmakeLists -notmatch 'project\(DishonoredHeadTracking VERSION (\d+\.\d+\.\d+)') {
    throw "Could not parse version from CMakeLists.txt"
}
$version = $Matches[1]
$modName = 'DishonoredHeadTracking'

Write-Host ""
Write-Host "=== Packaging $modName v$version ===" -ForegroundColor Magenta
Write-Host ""

$releaseDir = Join-Path $projectDir 'release'
if (-not (Test-Path $releaseDir)) { New-Item -ItemType Directory -Path $releaseDir -Force | Out-Null }

$asiPath = Join-Path $projectDir "bin/Release/$modName.asi"
if (-not (Test-Path $asiPath)) {
    throw "$modName.asi not found at: $asiPath. Run 'pixi run build-release' first."
}

$vendorAsiDir = Join-Path $projectDir 'vendor/ultimate-asi-loader'
$vendorAsiDll = Join-Path $vendorAsiDir 'dinput8.dll'
if (-not (Test-Path $vendorAsiDll)) {
    throw "Bundled ASI loader missing: $vendorAsiDll. Run 'pixi run update-deps' first."
}

# The installer ZIP redistributes that binary, and the upstream x86 loader carries
# binkw32.dll (RAD Game Tools, proprietary), wndmode.dll and vorbisfile.dll as
# RCDATA resources. None of the three is ours to ship, so a loader that still
# has them never reaches a release. See vendor/ultimate-asi-loader/README.md.
& (Join-Path $scriptDir 'strip-loader-payload.ps1') -Path $vendorAsiDll -VerifyOnly

$scriptsDir = Join-Path $projectDir 'scripts'
foreach ($s in @('install.cmd', 'uninstall.cmd')) {
    if (-not (Test-Path (Join-Path $scriptsDir $s))) {
        throw "Required script not found: $s"
    }
}

$launcherManifestPath = Join-Path $projectDir 'launcher-manifest.json'
if (-not (Test-Path $launcherManifestPath)) {
    throw "launcher-manifest.json not found at: $launcherManifestPath"
}

# Both ZIPs are binary distributions of MinHook and Hacker Disassembler Engine
# (BSD-2-Clause), which require the notices to accompany the binary. Neither ZIP
# may ship without these.
$requiredDocs = @('README.md', 'LICENSE', 'CHANGELOG.md', 'THIRD-PARTY-NOTICES.md')
foreach ($doc in $requiredDocs) {
    if (-not (Test-Path (Join-Path $projectDir $doc))) {
        throw "Required document not found: $doc"
    }
}

# --- Installer ZIP -----------------------------------------------------
Write-Host '--- Installer ZIP ---' -ForegroundColor Yellow

$ghStaging = Join-Path $releaseDir 'staging-installer'
if (Test-Path $ghStaging) { Remove-Item -Recurse -Force $ghStaging }
New-Item -ItemType Directory -Path $ghStaging -Force | Out-Null

foreach ($s in @('install.cmd', 'uninstall.cmd')) {
    Copy-Item (Join-Path $scriptsDir $s) -Destination $ghStaging -Force
}

# install.cmd / uninstall.cmd resolve the game via shared/find-game.ps1.
# Bundle that shim alongside them so the release ZIP is self-contained.
Copy-SharedBundle -StagingDir $ghStaging

$pluginsDir = Join-Path $ghStaging 'plugins'
New-Item -ItemType Directory -Path $pluginsDir -Force | Out-Null
Copy-Item $asiPath -Destination $pluginsDir -Force

$ghVendorDir = Join-Path $ghStaging 'vendor/ultimate-asi-loader'
New-Item -ItemType Directory -Path $ghVendorDir -Force | Out-Null
# Raw dinput8.dll + LICENSE/README serve the legacy install.cmd path (it copies
# the bare DLL into Binaries/Win32). LICENSE travels for MIT attribution.
foreach ($vendorFile in @('dinput8.dll', 'LICENSE', 'README.md')) {
    $src = Join-Path $vendorAsiDir $vendorFile
    if (Test-Path $src) { Copy-Item $src -Destination $ghVendorDir -Force }
}

# No synthesized ultimate-asi-loader.zip here, deliberately. launcher-manifest.json
# deploys the raw dinput8.dll through `files`, which is the canonical ASI wiring; it has
# no `loader` block, so a zip built here would ship ~3.9 MB of a second copy of the same
# DLL that nothing references. Pointing a `loader.archives` entry at such a zip is the
# drift that shipped arkham-city / dishonored / witcher-3 broken - if a loader block is
# ever needed, vendor the archive rather than synthesizing one at package time.

# LICENSE and THIRD-PARTY-NOTICES.md carry the MIT and BSD-2-Clause notices for
# everything compiled into the .asi. Throw rather than skip: a silent skip turns
# a licence violation into a green build.
foreach ($doc in $requiredDocs) {
    $p = Join-Path $projectDir $doc
    if (-not (Test-Path $p)) { throw "Required document missing: $doc" }
    Copy-Item -Path $p -Destination $ghStaging -Force
}

# Canonical launcher manifest the launcher ingests to deploy the package.
# Stamp mod_info.version from the build so the shipped manifest can never
# disagree with the built .asi.
$stagedManifest = Join-Path $ghStaging 'launcher-manifest.json'
$manifestText = Get-Content $launcherManifestPath -Raw
$manifestText = $manifestText -replace '("version":\s*")\d+\.\d+\.\d+(")', "`${1}$version`$2"
[System.IO.File]::WriteAllText($stagedManifest, $manifestText, (New-Object System.Text.UTF8Encoding $false))
Write-Host "  launcher-manifest.json (version $version)" -ForegroundColor Green

$installerZip = Join-Path $releaseDir "$modName-v$version-installer.zip"
if (Test-Path $installerZip) { Remove-Item $installerZip -Force }
Push-Location $ghStaging
try { Compress-Archive -Path '.\*' -DestinationPath $installerZip -Force } finally { Pop-Location }
Remove-Item -Recurse -Force $ghStaging

$installerKb = [math]::Round((Get-Item $installerZip).Length / 1KB, 1)
Write-Host ("  $installerZip ({0:N1} KB)" -f $installerKb) -ForegroundColor Green

# --- Nexus ZIP ---------------------------------------------------------
Write-Host ''
Write-Host '--- Nexus ZIP ---' -ForegroundColor Yellow

$nexusStaging = Join-Path $releaseDir 'staging-nexus'
if (Test-Path $nexusStaging) { Remove-Item -Recurse -Force $nexusStaging }

# Nexus users manage their own ASI loader, so the nexus ZIP ships only the
# mod's .asi - never the vendored dinput8.dll.
$nexusGameDir = Join-Path $nexusStaging 'Binaries\Win32'
New-Item -ItemType Directory -Path $nexusGameDir -Force | Out-Null

Copy-Item $asiPath -Destination $nexusGameDir -Force

$nexusZip = Join-Path $releaseDir "$modName-v$version-nexus.zip"
if (Test-Path $nexusZip) { Remove-Item $nexusZip -Force }
# The .asi statically links MinHook and Hacker Disassembler Engine
# (BSD-2-Clause), whose second condition requires the copyright notice, the
# conditions and the disclaimer to accompany a binary redistribution. A ZIP
# holding only the .asi satisfies neither that nor our own MIT terms, so the
# notices ship at the ZIP root beside the Binaries/ tree.
foreach ($noticeDoc in @('LICENSE', 'THIRD-PARTY-NOTICES.md', 'README.md')) {
    $noticeSrc = Join-Path $projectDir $noticeDoc
    if (-not (Test-Path $noticeSrc)) {
        throw "Required notice file not found: $noticeDoc. Every published ZIP is a binary distribution and must carry it."
    }
    Copy-Item $noticeSrc -Destination $nexusStaging -Force
    Write-Host "  $noticeDoc" -ForegroundColor Green
}
Push-Location $nexusStaging
try { Compress-Archive -Path '.\*' -DestinationPath $nexusZip -Force } finally { Pop-Location }
Remove-Item -Recurse -Force $nexusStaging

$nexusKb = [math]::Round((Get-Item $nexusZip).Length / 1KB, 1)
Write-Host ("  $nexusZip ({0:N1} KB)" -f $nexusKb) -ForegroundColor Green

# --- Validate ----------------------------------------------------------
# Checks every launcher-manifest.json `files[].source` actually exists inside the built
# installer ZIP, and that the third-party notices cover what ships. Renaming a staged
# path here without updating the manifest (or the reverse) otherwise produces a ZIP that
# CI calls good and the launcher rejects at deploy time. The broken-source bugs this
# catches shipped precisely because nobody ran it.
Write-Host ''
Write-Host '--- Validate ---' -ForegroundColor Yellow

if (-not (Get-Command node -ErrorAction SilentlyContinue)) {
    throw "node is required to validate the built package (cameraunlock-core/scripts/validate-manifest.mjs). Install Node.js and re-run; a release must not be published unvalidated."
}

foreach ($validator in @('validate-manifest.mjs', 'validate-notices.mjs')) {
    $script = Join-Path $projectDir "cameraunlock-core/scripts/$validator"
    if (-not (Test-Path $script)) {
        throw "Validator not found: $script"
    }
    node $script
    if ($LASTEXITCODE -ne 0) {
        throw "$validator failed - the built package does not match what it declares."
    }
}

Write-Host ''
Write-Host '=== Package Complete ===' -ForegroundColor Magenta

Write-Output $installerZip
Write-Output $nexusZip
