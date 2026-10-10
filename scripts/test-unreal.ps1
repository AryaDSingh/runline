# Builds the Runline editor target and runs Runline.* automation tests headless.
# Usage: powershell -ExecutionPolicy Bypass -File scripts/test-unreal.ps1 [-Filter Runline.World]
param([string]$Filter = 'Runline.')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$ue = if ($env:UE_ROOT) { $env:UE_ROOT } else { 'D:\UE_5.8' }
$project = Join-Path $root 'Unreal\Runline\Runline.uproject'
& "$ue\Engine\Build\BatchFiles\Build.bat" RunlineEditor Win64 Development "-Project=$project" -WaitMutex -NoHotReload
if ($LASTEXITCODE) { exit $LASTEXITCODE }
$log = Join-Path $root 'build\unreal-tests.log'
New-Item -ItemType Directory -Force (Split-Path $log) | Out-Null
& "$ue\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" $project "-ExecCmds=Automation RunTests $Filter;Quit" `
    -unattended -nopause -nosplash -nullrhi -NoSound "-abslog=$log" '-testexit=Automation Test Queue Empty'
$text = Get-Content $log -Raw
$passed = ([regex]::Matches($text, 'Test Completed\. Result=\{Success\}')).Count
$failed = ([regex]::Matches($text, 'Test Completed\. Result=\{(Fail|Failed)\}')).Count
Select-String -Path $log -Pattern 'Result=\{Fail|Error: ' | ForEach-Object { $_.Line }
Write-Host "Unreal tests: $passed passed, $failed failed"
if ($text -match 'Critical error|Fatal error') { Write-Host 'Unreal crashed during the tests (see the log)'; exit 1 }
if ($failed -gt 0 -or $passed -eq 0) { exit 1 }
exit 0
