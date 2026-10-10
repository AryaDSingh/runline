# Bakes faith-runner's courses (Unreal/Runline/Data/Levels) into maps under /Game/Runline/Maps.
# Usage: powershell -ExecutionPolicy Bypass -File scripts/bake-courses.ps1   (builds the editor first)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$ue = if ($env:UE_ROOT) { $env:UE_ROOT } else { 'D:\UE_5.8' }
$project = Join-Path $root 'Unreal\Runline\Runline.uproject'
& "$ue\Engine\Build\BatchFiles\Build.bat" RunlineEditor Win64 Development "-Project=$project" -WaitMutex -NoHotReload
if ($LASTEXITCODE) { exit $LASTEXITCODE }
foreach ($course in @(@{ Name = 'rooftops'; Map = '/Game/Runline/Maps/Rooftops' }, @{ Name = 'greybox'; Map = '/Game/Runline/Maps/Greybox' })) {
    & "$ue\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" $project -run=RunlineBakeCourse "-course=$($course.Name)" "-map=$($course.Map)" -unattended -nopause -nullrhi -NoSound
    if ($LASTEXITCODE) { Write-Host "Baking $($course.Name) failed"; exit $LASTEXITCODE }
}
Write-Host 'Baked Rooftops and Greybox.'
