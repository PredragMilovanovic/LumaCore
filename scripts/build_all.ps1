<#
.SYNOPSIS
    Builds and verifies all of LumaCore: native build and tests, managed build and tests,
    the end-to-end C++ vs C# comparison, and the result plots.

.DESCRIPTION
    Stops at the first failing step. Requires VS 2022 Build Tools (C++), CMake >= 3.24,
    the .NET SDK (8 or later) and Python 3 with numpy and matplotlib.

.PARAMETER Clean
    Delete all build outputs first (a from-scratch build, as on a fresh clone).

.PARAMETER Bench
    Also run the multithreading benchmark (about 10 s) and plot it.

.PARAMETER Web
    Also build the WebAssembly version (needs the Emscripten SDK, see docs/web.md), check it
    in Node.js and confirm its results equal the native ones.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\build_all.ps1 -Clean
#>
[CmdletBinding()]
param(
    [switch]$Clean,
    [switch]$Bench,
    [switch]$Web
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

# CMake installed by its MSI is not on PATH in shells opened before the install.
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    $cmakeBin = Join-Path $env:ProgramFiles 'CMake\bin'
    if (Test-Path $cmakeBin) { $env:Path = "$cmakeBin;$env:Path" }
}
foreach ($tool in 'cmake', 'ctest', 'dotnet', 'python') {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        throw "$tool not found on PATH. See README.md, 'Requirements'."
    }
}

$steps = New-Object System.Collections.Generic.List[string]
$timer = [System.Diagnostics.Stopwatch]::StartNew()

function Step([string]$name, [scriptblock]$body) {
    Write-Host ""
    Write-Host "== $name ==" -ForegroundColor Cyan
    $global:LASTEXITCODE = 0
    & $body
    if ($LASTEXITCODE -ne 0) {
        throw "FAILED: $name (exit code $LASTEXITCODE)"
    }
    $steps.Add($name)
}

if ($Clean) {
    Step 'Clean build outputs' {
        foreach ($dir in @('build') + (Get-ChildItem managed -Directory | ForEach-Object {
                    @("$($_.FullName)\bin", "$($_.FullName)\obj") })) {
            if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }
        }
    }
}

Step 'Configure native (CMake, MSVC x64)' { cmake --preset msvc }
Step 'Build native (Release)'             { cmake --build --preset msvc-release }
Step 'Native tests (GoogleTest)'          { ctest --preset msvc-release }
Step 'Build managed (.NET Framework 4.8)' { dotnet build managed\LumaCore.sln -c Release --nologo }
Step 'Managed tests (xUnit)'              { dotnet test managed\LumaCore.sln -c Release --no-build --nologo }

New-Item -ItemType Directory -Force results | Out-Null
Step 'Run C++ CLI: single (closed-form check)' { .\build\bin\Release\lumacore_cli.exe single }
Step 'Run C++ CLI: office'                     { .\build\bin\Release\lumacore_cli.exe office }
Step 'Run C# console through lumacore.dll' {
    .\managed\LumaCore.Console\bin\Release\net48\LumaCore.Console.exe --csv results\office_csharp.csv
}
Step 'Compare C++ and C# grids (must be exactly equal)' {
    python tools\compare_grids.py results\office.csv results\office_csharp.csv
}
Step 'Plot heatmaps' {
    python tools\heatmap.py results\office.csv
    if ($LASTEXITCODE -eq 0) { python tools\heatmap.py results\single.csv }
}

if ($Bench) {
    Step 'Benchmark: sequential vs threaded' { .\build\bin\Release\lumacore_bench.exe }
    Step 'Plot benchmark'                    { python tools\plot_bench.py results\bench.csv }
}

if ($Web) {
    Step 'Build WebAssembly (Emscripten)' {
        powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build_wasm.ps1
    }
    Step 'WebAssembly checks (Node.js)' {
        # Prefer the Node.js bundled with the Emscripten SDK; fall back to one on PATH.
        $nodeExe = Get-ChildItem (Join-Path $env:USERPROFILE 'emsdk\node') -Filter node.exe -Recurse -Depth 2 `
            -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName
        if (-not $nodeExe) { $nodeExe = 'node' }
        & $nodeExe tools\wasm_check.mjs
    }
    Step 'Compare C++ and WebAssembly grids (must be exactly equal)' {
        python tools\compare_grids.py results\office.csv results\office_wasm.csv
    }
    Step 'Web checks: energy conservation, surfaces, furniture rules, scenes (Node.js)' {
        $nodeExe = Get-ChildItem (Join-Path $env:USERPROFILE 'emsdk\node') -Filter node.exe -Recurse -Depth 2 `
            -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName
        if (-not $nodeExe) { $nodeExe = 'node' }
        & $nodeExe tools\web_check.mjs
    }
    Step 'Compare C++ and WebAssembly with EULUMDAT luminaires (last-bit tolerance)' {
        .\build\bin\Release\lumacore_cli.exe office --ldt samples\ldt\linear_batwing.ldt --out results\office_linear_batwing.csv | Out-Null
        if ($LASTEXITCODE -eq 0) {
            python tools\compare_grids.py results\office_linear_batwing.csv results\office_linear_batwing_wasm.csv --rel-tol 1e-12
        }
    }
}

$timer.Stop()
Write-Host ""
Write-Host ("ALL {0} STEPS PASSED in {1:N0} s" -f $steps.Count, $timer.Elapsed.TotalSeconds) -ForegroundColor Green
$steps | ForEach-Object { Write-Host "  [ok] $_" }
Write-Host ""
Write-Host "Results: results\office.png, results\single.png$(if ($Bench) { ', results\bench.png' })"
