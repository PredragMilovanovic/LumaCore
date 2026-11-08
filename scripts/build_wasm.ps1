<#
.SYNOPSIS
    Compiles the LumaCore C++ engine to WebAssembly (web/lumacore.js) with Emscripten.

.DESCRIPTION
    Uses the same engine sources as the native build, minus the threaded executor (see
    native/wasm/lumacore_wasm.cpp for why). The output is a single ES module with the
    WebAssembly binary embedded, so the web page needs no extra files.

    Emscripten is activated for this script only; your system settings are not changed.

.PARAMETER EmsdkDir
    Folder of the Emscripten SDK (default: %USERPROFILE%\emsdk).

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\build_wasm.ps1
#>
[CmdletBinding()]
param(
    [string]$EmsdkDir = (Join-Path $env:USERPROFILE 'emsdk')
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$envScript = Join-Path $EmsdkDir 'emsdk_env.ps1'
if (-not (Test-Path $envScript)) {
    throw "Emscripten SDK not found in $EmsdkDir. Install it first (see docs/web.md)."
}
$env:EMSDK_QUIET = '1'   # emsdk otherwise prints setup messages on stderr
. $envScript | Out-Null
if (-not (Get-Command em++ -ErrorAction SilentlyContinue)) {
    throw "em++ not found after activating $EmsdkDir. Run: emsdk install latest; emsdk activate latest"
}

# Engine version from the top-level CMakeLists.txt, so all builds report the same version.
$cmake = Get-Content CMakeLists.txt -Raw
if ($cmake -notmatch 'VERSION\s+(\d+)\.(\d+)\.(\d+)') { throw 'project version not found in CMakeLists.txt' }
$major, $minor, $patch = $Matches[1], $Matches[2], $Matches[3]

$sources = @(
    'native/src/version.cpp',
    'native/src/math/ray.cpp',
    'native/src/geometry/shapes.cpp',
    'native/src/geometry/intersection.cpp',
    'native/src/geometry/primitives.cpp',
    'native/src/lighting/lighting.cpp',
    'native/src/lighting/photometry.cpp',
    'native/src/scene/scene.cpp',
    'native/src/scene/measurement_grid.cpp',
    'native/src/simulation/simulation.cpp',
    'native/wasm/lumacore_wasm.cpp'
)

$flags = @(
    '-std=c++17', '-O3',
    '-Wall', '-Wextra', '-Wpedantic', '-Wshadow', '-Werror',
    '-fwasm-exceptions',                       # engine validation throws; native Wasm EH
    '-Inative/include',
    "-DLUMACORE_VERSION_MAJOR=$major", "-DLUMACORE_VERSION_MINOR=$minor", "-DLUMACORE_VERSION_PATCH=$patch",
    '-lembind',
    '-sMODULARIZE=1', '-sEXPORT_ES6=1', '-sEXPORT_NAME=createLumaCore',
    '-sALLOW_MEMORY_GROWTH=1',
    '-sENVIRONMENT=web,node',                  # node: for tools/wasm_check.mjs
    '-sSINGLE_FILE=1',                         # embed the .wasm in the .js
    '-o', 'web/lumacore.js'
)

Write-Host "em++ $(em++ --version | Select-Object -First 1)"
& em++ @flags @sources
if ($LASTEXITCODE -ne 0) { throw "em++ failed (exit code $LASTEXITCODE)" }

$size = (Get-Item web/lumacore.js).Length / 1KB
Write-Host ("wrote web/lumacore.js ({0:N0} KB)" -f $size) -ForegroundColor Green

# The web page offers the sample luminaires; the site is served from web/ only.
New-Item -ItemType Directory -Force web/ldt | Out-Null
Copy-Item samples/ldt/*.ldt web/ldt/ -Force
Write-Host "copied samples/ldt/*.ldt to web/ldt/"
