<#
.SYNOPSIS
    Build, test and run Sanctum without needing cmake on your PATH.

.DESCRIPTION
    Visual Studio ships its own copy of CMake but does not add it to PATH, so
    a bare `cmake --build build` fails on a stock VS install. This script finds
    that copy (or a standalone install) and drives it for you.

.EXAMPLE
    .\build.ps1              # build everything in Release
    .\build.ps1 -Run         # build, then launch the game
    .\build.ps1 -Test        # build, then run the unit tests
    .\build.ps1 -Configure   # re-run CMake configure (after adding a new .cpp)
    .\build.ps1 -Clean       # delete build/ and configure from scratch
#>

[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string]$Config = 'Release',
    [switch]$Configure,
    [switch]$Test,
    [switch]$Run,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

function Find-Tool([string]$exe) {
    # 1. Already on PATH?
    $onPath = Get-Command $exe -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }

    # 2. Visual Studio's bundled copy, any edition or year.
    $roots = @("${env:ProgramFiles}\Microsoft Visual Studio",
               "${env:ProgramFiles(x86)}\Microsoft Visual Studio")
    foreach ($root in $roots) {
        if (-not (Test-Path $root)) { continue }
        $hit = Get-ChildItem -Path $root -Filter $exe -Recurse -File -ErrorAction SilentlyContinue |
               Where-Object { $_.FullName -like '*CommonExtensions\Microsoft\CMake\CMake\bin*' } |
               Select-Object -First 1
        if ($hit) { return $hit.FullName }
    }

    # 3. A standalone CMake install.
    foreach ($candidate in @("${env:ProgramFiles}\CMake\bin\$exe",
                             "${env:ProgramFiles(x86)}\CMake\bin\$exe")) {
        if (Test-Path $candidate) { return $candidate }
    }
    return $null
}

$cmake = Find-Tool 'cmake.exe'
if (-not $cmake) {
    Write-Host "Could not find cmake.exe." -ForegroundColor Red
    Write-Host "Install the 'Desktop development with C++' workload in the Visual Studio"
    Write-Host "Installer, or install CMake from https://cmake.org/download/"
    exit 1
}
$ctest = Find-Tool 'ctest.exe'
Write-Host "cmake: $cmake" -ForegroundColor DarkGray

if ($Clean -and (Test-Path 'build')) {
    Write-Host "Removing build/ ..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force 'build'
}

# Configure when asked, or whenever there is no cache to build against.
if ($Configure -or $Clean -or -not (Test-Path 'build\CMakeCache.txt')) {
    Write-Host "`nConfiguring ..." -ForegroundColor Cyan
    & $cmake -B build -S .
    if ($LASTEXITCODE -ne 0) { Write-Host "Configure failed." -ForegroundColor Red; exit 1 }
}

Write-Host "`nBuilding ($Config) ..." -ForegroundColor Cyan
& $cmake --build build --config $Config
if ($LASTEXITCODE -ne 0) { Write-Host "`nBuild failed." -ForegroundColor Red; exit 1 }

$exePath = Join-Path $PSScriptRoot "build\bin\$Config\SanctumGame.exe"
Write-Host "Build OK." -ForegroundColor Green
Write-Host "  $exePath" -ForegroundColor DarkGray

# Building does not launch anything, which is easy to mistake for a failure.
if (-not $Run -and -not $Test) {
    Write-Host "`nThis only built the game. To play it:" -ForegroundColor Yellow
    Write-Host "  .\build.ps1 -Run" -ForegroundColor White
}

if ($Test) {
    if (-not $ctest) { Write-Host "ctest.exe not found." -ForegroundColor Red; exit 1 }
    Write-Host "`nRunning tests ..." -ForegroundColor Cyan
    & $ctest --test-dir build -C $Config --output-on-failure
    if ($LASTEXITCODE -ne 0) { exit 1 }
}

if ($Run) {
    $exe = Join-Path $PSScriptRoot "build\bin\$Config\SanctumGame.exe"
    if (-not (Test-Path $exe)) { Write-Host "Not built: $exe" -ForegroundColor Red; exit 1 }
    Write-Host "`nLaunching ..." -ForegroundColor Cyan
    # Run from the executable's folder so assets/ and settings.json resolve.
    Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe)
}
