# Configure, build and run every RunlineCore test. Usage: powershell -ExecutionPolicy Bypass -File scripts/test.ps1
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
    $vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
    $cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
}
$build = Join-Path $root 'build\RunlineCore'
& $cmake -S (Join-Path $root 'RunlineCore') -B $build
if ($LASTEXITCODE) { exit $LASTEXITCODE }
& $cmake --build $build --config Debug
if ($LASTEXITCODE) { exit $LASTEXITCODE }
& (Join-Path $build 'Debug\RunlineCoreTests.exe')
exit $LASTEXITCODE
