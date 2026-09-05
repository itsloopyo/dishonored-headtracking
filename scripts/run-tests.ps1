[CmdletBinding()]
param([string]$Config = 'Debug')
$ErrorActionPreference = 'Stop'
$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
$BuildDir = Join-Path $ProjectRoot 'build-tests'

cmake -S $ProjectRoot -B $BuildDir -A Win32 -DDISHONORED_BUILD_TESTS=ON
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed ($LASTEXITCODE)" }

cmake --build $BuildDir --config $Config --target dishonored_tests
if ($LASTEXITCODE -ne 0) { throw "Test build failed ($LASTEXITCODE)" }

ctest --test-dir $BuildDir -C $Config --output-on-failure --no-tests=error
if ($LASTEXITCODE -ne 0) { throw "Tests failed ($LASTEXITCODE)" }

Write-Host 'All tests passed' -ForegroundColor Green
