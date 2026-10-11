# Imports the GASP clips Runline uses (Data/Animation/gasp-clips.txt) into the git-ignored
# Unreal/Runline/Content/ThirdParty/GASP. Pass 1 runs inside your GASP project (Python, enabled on
# the command line only); pass 2 imports the FBX into Runline. Safe to re-run.
param([string]$Gasp = 'D:\GameAnimationSample')
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$Ue = if ($env:UE_ROOT) { $env:UE_ROOT } else { 'D:\UE_5.8' }
$Cmd = Join-Path $Ue 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$List = Join-Path $Root 'Data\Animation\gasp-clips.txt'
$Out = Join-Path $Root 'build\gasp-fbx'
# Unreal reads backslashes in -script= as escapes ("\t" is a tab): pass it with forward slashes.
$Script = (Join-Path $Root 'tools\gasp\export_clips.py').Replace('\', '/')
$GaspProject = Get-ChildItem $Gasp -Filter *.uproject | Select-Object -First 1
if (-not $GaspProject) { throw "No GASP project in $Gasp" }
$env:RUNLINE_GASP_LIST = $List
$env:RUNLINE_GASP_OUT = $Out
& $Cmd $GaspProject.FullName -run=pythonscript "-script=$Script" -EnablePlugins=PythonScriptPlugin -unattended -nop4 -nosplash -stdout | Tee-Object (Join-Path $Root 'build\gasp-export.log') | Select-String 'Runline GASP|Error'
if ($LASTEXITCODE -ne 0) { throw "GASP export failed (build/gasp-export.log)" }
& $Cmd (Join-Path $Root 'Unreal\Runline\Runline.uproject') -run=RunlineImportGasp "-List=$List" "-Fbx=$Out" -unattended -nop4 -nosplash -stdout | Tee-Object (Join-Path $Root 'build\gasp-import.log') | Select-String 'Runline GASP|Error'
if ($LASTEXITCODE -ne 0) { throw "GASP import failed (build/gasp-import.log)" }
