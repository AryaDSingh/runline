# Builds RunlineCore as a Release static library for the Unreal module (/MD, /fp:strict).
# Run by UBT as a pre-build step (Runline*.Target.cs); safe to run by hand.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
    $vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
    $cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
}
$build = Join-Path $root 'build\RunlineCore-ue'
& $cmake -S (Join-Path $root 'RunlineCore') -B $build -DRUNLINE_TESTS=OFF | Out-Null
if ($LASTEXITCODE) { exit $LASTEXITCODE }
& $cmake --build $build --config Release --target RunlineCore
exit $LASTEXITCODE
