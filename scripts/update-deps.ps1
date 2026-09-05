#!/usr/bin/env pwsh
#Requires -Version 5.1
# Bump vendored Ultimate ASI Loader (dinput8.dll) to the latest upstream
# within the pinned range, strip the third-party DLLs it carries as resources,
# and rewrite vendor/ultimate-asi-loader/{LICENSE,README.md}.
# Manual: dev runs this when they want a fresh upstream bump, then commits the
# result. CI never refreshes; install.cmd extracts the committed vendor tree.
#
# Special case: Ultimate-ASI-Loader ships a DLL inside a release zip, not as a
# standalone asset, so this script extracts dinput8.dll rather than calling
# Update-VendoredLoader, which vendors the downloaded artifact whole.
#
# The extracted DLL is NOT vendored as it comes: the x86 build embeds binkw32.dll
# (RAD Game Tools, proprietary), wndmode.dll (VEG / menopem, no licence) and
# vorbisfile.dll (Xiph.Org) as RCDATA resources, and every release ZIP we publish
# would redistribute all three. strip-loader-payload.ps1 zeroes them before the
# copy is hashed and committed. Never skip that step.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference    = 'SilentlyContinue'

$scriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir

$module = Join-Path $projectDir 'cameraunlock-core/powershell/ModLoaderSetup.psm1'
if (-not (Test-Path $module)) {
    throw "ModLoaderSetup.psm1 not found at $module. Run 'pixi run sync' to update the cameraunlock-core submodule."
}
Import-Module $module -Force

$vendorAsiDir     = Join-Path $projectDir 'vendor/ultimate-asi-loader'
$vendorAsiDll     = Join-Path $vendorAsiDir 'dinput8.dll'
$vendorAsiLicense = Join-Path $vendorAsiDir 'LICENSE'
$vendorAsiReadme  = Join-Path $vendorAsiDir 'README.md'
if (-not (Test-Path $vendorAsiDir)) {
    New-Item -ItemType Directory -Path $vendorAsiDir -Force | Out-Null
}

$tempDir = Join-Path $env:TEMP ("asi-update-" + [IO.Path]::GetRandomFileName())
New-Item -ItemType Directory -Path $tempDir -Force | Out-Null
$tempZip     = Join-Path $tempDir 'upstream.zip'
$tempDll     = Join-Path $tempDir 'dinput8.dll'
$tempLicense = Join-Path $tempDir 'LICENSE'
try {
    Write-Host "Refreshing vendor/ultimate-asi-loader from upstream..." -ForegroundColor Cyan

    # Dishonored is a 32-bit UE3 game, so the x86 asset (Ultimate-ASI-Loader.zip)
    # is the pin - not the _x64 one.
    $meta = Invoke-FetchLatestLoader `
        -OutputPath $tempZip `
        -Owner 'ThirteenAG' -Repo 'Ultimate-ASI-Loader' `
        -VersionPrefix 'v9.' `
        -AssetPattern '^Ultimate-ASI-Loader\.zip$'

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::OpenRead($tempZip)
    try {
        $dllEntry = $zip.Entries | Where-Object { $_.Name -eq 'dinput8.dll' } | Select-Object -First 1
        if (-not $dllEntry) { throw "Upstream zip $($meta.AssetName) does not contain dinput8.dll." }
        [System.IO.Compression.ZipFileExtensions]::ExtractToFile($dllEntry, $tempDll, $true)

        $licenseEntry = $zip.Entries | Where-Object { $_.Name -match '^(license|LICENSE)(\..+)?$' -and $_.FullName -notmatch '/.+/' } | Select-Object -First 1
        if ($licenseEntry) {
            [System.IO.Compression.ZipFileExtensions]::ExtractToFile($licenseEntry, $tempLicense, $true)
        }
    } finally { $zip.Dispose() }

    # Fail fast on the wrong architecture: an x64 proxy in a 32-bit game's
    # Binaries/Win32 fails to load and the mod never runs.
    $bytes    = [IO.File]::ReadAllBytes($tempDll)
    $peOffset = [BitConverter]::ToInt32($bytes, 0x3C)
    $machine  = [BitConverter]::ToUInt16($bytes, $peOffset + 4)
    if ($machine -ne 0x014C) {
        throw ("Extracted ASI Loader is not x86 (machine=0x{0:X4}); refusing to vendor it." -f $machine)
    }

    $upstreamSha = (Get-FileHash -LiteralPath $tempDll -Algorithm SHA256).Hash.ToLower()

    # Strip before the hash the vendored copy is compared and recorded against, so
    # the idempotency check below sees the file that actually gets committed.
    Write-Host "Stripping the loader's embedded third-party DLLs..." -ForegroundColor Cyan
    $strip = Join-Path $scriptDir 'strip-loader-payload.ps1'
    & $strip -Path $tempDll
    & $strip -Path $tempDll -VerifyOnly   # throws if anything survived

    $dllSha = (Get-FileHash -LiteralPath $tempDll -Algorithm SHA256).Hash.ToLower()

    # Idempotency: an upstream that has not moved must leave the tree clean. Without
    # this the FetchedAt line rewrites README.md on every run, so `git status` after a
    # no-op refresh shows a timestamp-only diff with no artifact behind it.
    if ((Test-Path -LiteralPath $vendorAsiDll) -and (Test-Path -LiteralPath $vendorAsiLicense) -and (Test-Path -LiteralPath $vendorAsiReadme) -and
        ((Get-FileHash -LiteralPath $vendorAsiDll -Algorithm SHA256).Hash.ToLower() -eq $dllSha)) {
        Write-Host "  no change (tag=$($meta.Tag) sha256=$($dllSha.Substring(0,12))... matches on-disk vendor copy)" -ForegroundColor DarkGray
        Write-Host ""
        Write-Host "vendor/ultimate-asi-loader is already up to date." -ForegroundColor Green
        return
    }

    Move-Item -LiteralPath $tempDll -Destination $vendorAsiDll -Force

    if (Test-Path -LiteralPath $tempLicense) {
        Move-Item -LiteralPath $tempLicense -Destination $vendorAsiLicense -Force
    } else {
        $licenseUrl = "https://raw.githubusercontent.com/ThirteenAG/Ultimate-ASI-Loader/$($meta.Tag)/license"
        Invoke-WebRequest -Uri $licenseUrl -OutFile $vendorAsiLicense -UseBasicParsing -TimeoutSec 30 -Headers @{ "User-Agent" = "CameraUnlock-HeadTracking" }
    }

    $readme = @(
        '# Ultimate ASI Loader (vendored)',
        '',
        'Bundled copy of Ultimate ASI Loader (x86), the install-time source of truth.',
        'install.cmd copies from here and never reaches out to the network.',
        'Refresh manually with `pixi run update-deps`, then commit.',
        '',
        '## Snapshot',
        '',
        '- Upstream: https://github.com/ThirteenAG/Ultimate-ASI-Loader',
        "- Tag: ``$($meta.Tag)``",
        "- Commit: ``$($meta.CommitSha)``",
        "- Asset: ``$($meta.AssetName)``",
        "- Upstream URL: $($meta.AssetUrl)",
        "- Upstream dinput8.dll SHA-256: ``$upstreamSha``",
        "- Vendored dinput8.dll SHA-256: ``$dllSha`` (after the strip below)",
        "- Fetched at: $($meta.FetchedAt)",
        '',
        'install.cmd copies `dinput8.dll` to the Dishonored Binaries/Win32/ directory as',
        '`dinput8.dll` (the proxy slot UE3 loads ASI plugins through).',
        '',
        '## Modified: third-party payload stripped',
        '',
        'The upstream x86 loader carries three complete third-party DLLs as RCDATA resources,',
        'so that a user who renames it over one of those libraries still gets the original',
        'exports, plus the ini template one of them reads:',
        '',
        '- `binkw32.dll` - RAD Game Tools, Inc., Bink and Smacker 1.994i. Proprietary',
        '  middleware licensed per title; we have no right to redistribute it.',
        '- `wndmode.dll` - DirectX Windower Embedded v2.3, (C) 2008 VEG, (C) 2004 menopem.',
        '  No licence accompanies it.',
        '- `vorbisfile.dll` - Xiph.Org, BSD-3-Clause. Redistributable only with its notice.',
        '',
        '`scripts/strip-loader-payload.ps1` zeroes all three, and the windower ini template,',
        'before the file is committed. Only the `.rsrc` section changes: the loader code, its',
        'imports, relocations and appended PDB are byte-identical to upstream. Nothing in this',
        'mod can reach the stripped resources - the two library payloads are keyed off the',
        "loader's own filename, and we deploy it as `dinput8.dll`, while the windower needs a",
        '`wndmode.ini` we never ship. MIT permits the modification; it is recorded here and in',
        'THIRD-PARTY-NOTICES.md so this copy is not mistaken for stock upstream.'
    ) -join "`n"
    Set-Content -Path $vendorAsiReadme -Value $readme -Encoding UTF8

    Write-Host "  tag=$($meta.Tag) sha256=$($dllSha.Substring(0,12))..." -ForegroundColor DarkGray
} finally {
    Remove-Item $tempDir -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host ""
Write-Host "vendor/ultimate-asi-loader refreshed. Review and commit." -ForegroundColor Green
