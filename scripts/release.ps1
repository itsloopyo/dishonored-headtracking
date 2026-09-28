#!/usr/bin/env pwsh
#Requires -Version 5.1
# Fully unattended release workflow for DishonoredHeadTracking.
# Usage: pixi run release <major|minor|patch|nightly|X.Y.Z>
#
# Running this command IS the authorization. There is no second gate: the
# release runs end to end with zero prompts. The preconditions below (clean
# tree, on main, tag absent, valid semver) are the safety net in place of
# any interactive confirmation - each fails fast with a non-zero exit.

[CmdletBinding()]
param(
    [Parameter(Position=0)]
    [string]$Version,
    [switch]$AllowDirty,
    # Ship a release even when there are no user-facing commits since the
    # last tag (writes a maintenance changelog entry instead of aborting).
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# THIRD-PARTY-NOTICES.md names the cameraunlock-core commit compiled into the
# release ZIPs, and bumping the submodule does not touch it. Packaging refuses
# to ship that mismatch, so a bump with no notices edit stopped the release
# here, or in CI once the tag had already been pushed. Re-sync it and let this
# release carry the correction.
$noticesRoot = Split-Path -Parent $PSScriptRoot
& git -C $noticesRoot diff --quiet -- THIRD-PARTY-NOTICES.md
if ($LASTEXITCODE -ne 0) { throw "THIRD-PARTY-NOTICES.md has uncommitted edits. Commit or discard them, then re-run." }
& (Join-Path $noticesRoot 'cameraunlock-core\scripts\sync-core-notices.ps1') -Repo $noticesRoot
if ($LASTEXITCODE -ne 0) { throw "sync-core-notices.ps1 exited $LASTEXITCODE - fix THIRD-PARTY-NOTICES.md before releasing." }
& git -C $noticesRoot diff --quiet -- THIRD-PARTY-NOTICES.md
if ($LASTEXITCODE -ne 0) {
    & git -C $noticesRoot commit -q -m 'chore: record the cameraunlock-core commit this build compiles' -- THIRD-PARTY-NOTICES.md
    if ($LASTEXITCODE -ne 0) { throw "Could not commit the re-synced THIRD-PARTY-NOTICES.md." }
    Write-Host 'THIRD-PARTY-NOTICES.md re-synced to the pinned cameraunlock-core commit.' -ForegroundColor Yellow
}

$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

if (-not $Version) {
    Write-Error "Usage: pixi run release <major|minor|patch|nightly|X.Y.Z>"
    exit 1
}

if ($Version -eq 'nightly') {
    & (Join-Path $PSScriptRoot 'release-nightly.ps1') -AllowDirty:$AllowDirty
    exit $LASTEXITCODE
}

Import-Module (Join-Path $ProjectRoot 'cameraunlock-core/powershell/ReleaseWorkflow.psm1') -Force

function Write-NoBom {
    param([string]$Path, [string]$Text)
    [System.IO.File]::WriteAllText($Path, $Text, (New-Object System.Text.UTF8Encoding $false))
}

# --- 1. Resolve and validate the target version ------------------------
$cmakePath = Join-Path $ProjectRoot 'CMakeLists.txt'
$cmakeText = Get-Content $cmakePath -Raw
if ($cmakeText -notmatch 'project\(DishonoredHeadTracking VERSION (\d+\.\d+\.\d+)') {
    Write-Error "Could not parse current version from CMakeLists.txt"
    exit 1
}
$current = $Matches[1]

try {
    $target = Resolve-ReleaseVersion -Argument $Version -CurrentVersion $current
} catch {
    Write-Error "Error: $($_.Exception.Message)"
    exit 1
}

try {
    Assert-ReleaseNotBelowCanonicalSince -RepoRoot $ProjectRoot -Version $target
} catch {
    Write-Error "Error: $($_.Exception.Message)"
    exit 1
}

$tag = "v$target"
$changelogPath = Join-Path $ProjectRoot 'CHANGELOG.md'

# --- 2. Preconditions (these stand in for interactive confirmation) ----
$branch = (git -C $ProjectRoot rev-parse --abbrev-ref HEAD).Trim()
if ($branch -ne 'main') {
    Write-Error "Releases must run on 'main' (currently on '$branch')."
    exit 1
}

if (-not $AllowDirty) {
    $status = git -C $ProjectRoot status --porcelain
    if ($status) {
        Write-Error "Working tree is not clean. Commit or stash changes before releasing."
        exit 1
    }
}

if (git -C $ProjectRoot tag --list $tag) {
    Write-Error "Tag $tag already exists."
    exit 1
}

Write-Host "Running the full test suite..." -ForegroundColor Cyan
Push-Location $ProjectRoot
try {
    pixi run test
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Error: pixi run test failed. Nothing was changed." -ForegroundColor Red
        exit 1
    }
} finally {
    Pop-Location
}

Write-Host "Releasing $current -> $target" -ForegroundColor Cyan

# Exact contents of every file steps 3 and 4 rewrite, captured before either runs.
# Restoring from this rather than `git checkout --` matters: checkout discards ANY
# unstaged edit to those paths, and -AllowDirty means the tree is not guaranteed clean,
# so a failed build would have destroyed the user's own uncommitted work.
$rollback = @{}
foreach ($rp in @('CHANGELOG.md', 'CMakeLists.txt', 'pixi.toml', 'src/dllmain.cpp',
                  'scripts/install.cmd', 'launcher-manifest.json')) {
    $full = Join-Path $ProjectRoot $rp
    if (Test-Path $full) { $rollback[$full] = Get-Content $full -Raw -Encoding UTF8 }
}
function Restore-Rollback {
    foreach ($entry in $rollback.GetEnumerator()) {
        Write-NoBom -Path $entry.Key -Text $entry.Value
    }
}

# --- 3. Changelog from commits since the last tag ----------------------
# This is the gate that aborts when there are no user-facing commits, so run
# it BEFORE mutating any version files or building - a failure here then
# leaves a clean tree instead of stranding a half-applied version bump with
# no tag.
Write-Host "Generating CHANGELOG from commits..." -ForegroundColor Cyan
try {
    New-ChangelogFromCommits -ChangelogPath $changelogPath -Version $target -ArtifactPaths @(
        'src/',
        'cameraunlock-core',
        'scripts/install.cmd',
        'scripts/uninstall.cmd'
    ) -Maintenance:$Force | Out-Null
} catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
    if (-not $Force) {
        Write-Host "No user-facing changes to release. Re-run with -Force for a maintenance release." -ForegroundColor Yellow
    }
    exit 1
}

# --- 4. Bump the canonical version (CMakeLists.txt) + derived copies ---
# kModVersion is what the .asi logs at attach; MOD_VERSION is what
# install.cmd writes into .headtracking-state.json; launcher-manifest.json's
# mod_info.version is what the launcher reads. All five must move together or
# shipped artifacts report a stale version. The launcher-manifest replace is
# targeted: mod_info.version is the only semver in the file.
$versionFiles = @(
    @{ Path = 'CMakeLists.txt';         Pattern = 'project\(DishonoredHeadTracking VERSION \d+\.\d+\.\d+'; Replacement = "project(DishonoredHeadTracking VERSION $target" },
    @{ Path = 'pixi.toml';              Pattern = '(?m)^version = "\d+\.\d+\.\d+"';                        Replacement = "version = `"$target`"" },
    @{ Path = 'src/dllmain.cpp';        Pattern = 'kModVersion = "\d+\.\d+\.\d+"';                         Replacement = "kModVersion = `"$target`"" },
    @{ Path = 'scripts/install.cmd';    Pattern = '(?m)^set "MOD_VERSION=\d+\.\d+\.\d+"';                  Replacement = "set `"MOD_VERSION=$target`"" },
    @{ Path = 'launcher-manifest.json'; Pattern = '("version":\s*")\d+\.\d+\.\d+(")';                      Replacement = "`${1}$target`$2" }
)
foreach ($vf in $versionFiles) {
    $vfPath = Join-Path $ProjectRoot $vf.Path
    $vfText = Get-Content $vfPath -Raw
    if ($vfText -notmatch $vf.Pattern) {
        Write-Error "Version pattern not found in $($vf.Path) - cannot bump."
        exit 1
    }
    Write-NoBom -Path $vfPath -Text ($vfText -replace $vf.Pattern, $vf.Replacement)
}

# --- 5. Release-config build -------------------------------------------
# A failure here has to undo steps 3 AND 4. Left applied, the changelog entry and the
# bump are six modified files with no commit and no tag: the next `pixi run release`
# aborts on the "working tree is not clean" precondition without saying why, and forcing
# past it writes a second changelog entry for the same version.
Write-Host "Building release configuration..." -ForegroundColor Cyan
pixi run build-release
if ($LASTEXITCODE -ne 0) {
    Write-Host "Release build failed - restoring the changelog and the version bump." -ForegroundColor Yellow
    Restore-Rollback
    Write-Error "Release build failed."
    exit 1
}

# --- 6. Commit the version bump + changelog ----------------------------
git -C $ProjectRoot add CMakeLists.txt pixi.toml src/dllmain.cpp scripts/install.cmd launcher-manifest.json CHANGELOG.md
git -C $ProjectRoot commit -m "Release v$target"
if ($LASTEXITCODE -ne 0) { Write-Error "git commit failed."; exit 1 }

# --- 7. Annotated tag --------------------------------------------------
git -C $ProjectRoot tag -a $tag -m "Release v$target"
if ($LASTEXITCODE -ne 0) { Write-Error "git tag failed."; exit 1 }

# --- 8. Push commits + tag (triggers .github/workflows/release.yml) ----
git -C $ProjectRoot push origin HEAD
if ($LASTEXITCODE -ne 0) { Write-Error "git push (commits) failed."; exit 1 }
git -C $ProjectRoot push origin $tag
if ($LASTEXITCODE -ne 0) { Write-Error "git push (tag) failed."; exit 1 }

Write-Host "Released $tag" -ForegroundColor Green
