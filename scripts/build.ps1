<#
    CorForge build script.

    Imports the MSVC developer environment (vcvars64) into this session, puts the
    Visual Studio-bundled CMake + Ninja on PATH, then configures and builds.

    Usage:
        powershell -ExecutionPolicy Bypass -File scripts\build.ps1            # configure + build (Release)
        powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Run       # ...then launch the app
        powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Config Debug
        powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Clean     # wipe build dir first
#>
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Config = 'Release',
    [switch]$Run,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$root      = Split-Path -Parent $PSScriptRoot
$buildDir  = Join-Path $root 'build'

# --- Locate Visual Studio (Build Tools) via vswhere -------------------------
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "vswhere not found - is Visual Studio (Build Tools) installed?" }
$vsPath = & $vswhere -latest -products * -property installationPath | Select-Object -First 1
if (-not $vsPath) { throw "No Visual Studio installation found." }

$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) { throw "vcvars64.bat not found at $vcvars" }

# --- Import the dev environment into this PowerShell session ----------------
Write-Host "==> Importing MSVC environment..." -ForegroundColor Cyan
cmd /c "`"$vcvars`" >nul 2>&1 && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') {
        Set-Item -Path "Env:$($matches[1])" -Value $matches[2]
    }
}

# --- Make the bundled CMake + Ninja reachable -------------------------------
$cmakeBin = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
$ninjaBin = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'
$env:PATH = "$cmakeBin;$ninjaBin;$env:PATH"

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) { throw "cmake not on PATH after setup." }
Write-Host "==> cmake: $((Get-Command cmake).Source)" -ForegroundColor DarkGray

# --- Ensure GLAD's Python generator has jinja2 ------------------------------
# glad2's CMake integration runs `python -m glad`, which imports jinja2.
$py = (Get-Command python -ErrorAction SilentlyContinue).Source
if ($py) {
    & $py -c "import jinja2" 2>$null
    if ($LASTEXITCODE -ne 0) {
        Write-Host "==> Installing jinja2 for GLAD generator..." -ForegroundColor Yellow
        & $py -m pip install --user --quiet jinja2
    }
} else {
    Write-Host "==> WARNING: no 'python' on PATH; GLAD generation may fail." -ForegroundColor Yellow
}

# --- Clean (optional) -------------------------------------------------------
if ($Clean -and (Test-Path $buildDir)) {
    Write-Host "==> Cleaning $buildDir" -ForegroundColor Yellow
    Remove-Item -Recurse -Force $buildDir
}

# --- Configure --------------------------------------------------------------
# -Wno-dev silences CMake developer warnings from the fetched third-party libs
# (glm/assimp), which we don't control, keeping our build output clean.
Write-Host "==> Configuring ($Config)..." -ForegroundColor Cyan
cmake -S $root -B $buildDir -G Ninja -Wno-dev "-DCMAKE_BUILD_TYPE=$Config"
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }

# --- Build ------------------------------------------------------------------
Write-Host "==> Building..." -ForegroundColor Cyan
cmake --build $buildDir
if ($LASTEXITCODE -ne 0) { throw "Build failed." }

$exe = Join-Path $buildDir 'bin\corforge.exe'
Write-Host "==> Build OK: $exe" -ForegroundColor Green

if ($Run) {
    Write-Host "==> Running..." -ForegroundColor Cyan
    & $exe
}
