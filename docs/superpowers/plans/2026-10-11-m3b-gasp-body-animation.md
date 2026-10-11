# M3b: GASP Body Animation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The runner's body plays Epic's GASP motion-capture clips, picked and timed by the core's state; the first-person arms keep copying the body; a legs mesh shows her hips and legs when she looks down.

**Architecture:**
- **Import (scripted, once per machine):** a Python pass inside the user's GASP project retargets the listed clips to the UE5 Manny and exports FBX; a RunlineEditor commandlet imports them into the git-ignored `/Game/ThirdParty/GASP` and fills the committed anim set data asset.
- **`FRunlineBodyChooser`** (plain C++, no UObject): core state, timers and events in; weighted clip layers out (slot, time, weight) plus a wallrun lean. All the timing rules live here and are unit-tested with no clips.
- **`URunlineBodyAnimInstance`** (native proxy, no anim graph): samples the layers' sequences and blends them.
- **`ARunlineRunner`** feeds it each tick, adds the legs mesh, and falls back to `ABP_Unarmed` when the clips aren't there.
- **`?poses=1`** screenshots every core state in first and third person for review.

**Tech Stack:** UE 5.8 C++ (UAnimInstance + FAnimInstanceProxy, UAnimSequence sampling, FAnimationRuntime blending, commandlets, automation tests), Unreal Python (IKRetargetBatchOperation, AssetExportTask), PowerShell.

**Spec:** `docs/superpowers/specs/2026-10-11-m3b-gasp-body-animation-design.md`

## Global Constraints

- GASP content never enters git: `Unreal/Runline/Content/ThirdParty/` and `build/gasp-fbx/` are git-ignored. Only the clip list, scripts, code and the anim set asset (soft references and numbers) are committed. (spec §2, §3)
- RunlineCore is not modified; animation only reads the core; every trace still replays bit-exact. (spec §6.5)
- Without the clips the game works exactly as today (`ABP_Unarmed`). (spec §3)
- No Mirror's Edge files. Conversions only through `RunlineUnits.h`. (CLAUDE.md)
- The user's GASP project at `D:\GameAnimationSample` is read, plus a temporary `/Game/_RunlineExport` folder the Python pass creates and deletes. Python is enabled with `-EnablePlugins=PythonScriptPlugin` on the command line: its `.uproject` is not edited.
- Unreal at `D:\UE_5.8`. Tests: `scripts/test-unreal.ps1 [-Filter X]` (52 tests on master before this plan) and `scripts/test.ps1` (80 cases); both stay green. Game-target builds have no editor-only APIs (guard with `WITH_EDITOR`); bone hiding must happen after registration (`PostInitializeComponents`), as M3a found.
- Visual work is not done until every pose-capture screenshot has been looked at (user feedback, 2026-10-11).
- Edit scripts go in the scratchpad as files. Merging is the user's decision.

## Review Focus

1. **A fresh clone without the clips:** the game runs on `ABP_Unarmed`, no errors, no T-pose. Test: Task 6 `Runline.Body.FallsBackWithoutClips`.
2. **A move the core ends early** (a vault cut short, a slide ended by a jump): the clip doesn't run on after the state; it cross-fades out. Test: Task 4 `Runline.Body.ClipEndsWithTheMove`.
3. **Respawn mid-move:** no blend from the old pose. Test: Task 4 `Runline.Body.SnapCuts`.
4. **Hitch frames and game speed:** a one-shot's position comes from the core's timer, never summed frame times. Test: Task 4 `Runline.Body.OneShotsFollowTheCoreTimer`.
5. **A slot whose clip is missing:** the locomotion loop instead, one warning per slot. Test: Task 4 `Runline.Body.MissingSlotUsesLocomotion`.

## File Structure

```
.gitignore                                      modify: Content/ThirdParty/, build/gasp-fbx/
Data/Animation/gasp-clips.txt                   create: Slot,GaspPath[,HoldAt][,standin]
tools/gasp/export_clips.py                      create: pass 1 (inside the GASP project)
scripts/import-gasp.ps1                         create: runs both passes
Unreal/Runline/Source/Runline/Runner/RunlineMovementComponent.h/.cpp   cherry-pick: GetFrameEvents/GetStepAlpha/GetSnapCount (m3a a998e03)
RunlineCore/tests/Replay.h                      cherry-pick: onStep hook (same commit)
Unreal/Runline/Source/Runline/Runner/Body/
  RunlineAnimSet.h/.cpp                         create: ERunlineAnimSlot, FRunlineAnimSlotDef, URunlineAnimSet (UDataAsset)
  RunlineBodyChooser.h/.cpp                     create: FRunlineBodyInput, FRunlineBodyPose, FRunlineBodyChooser
  RunlineBodyAnimInstance.h/.cpp                create: URunlineBodyAnimInstance + proxy
Unreal/Runline/Source/RunlineEditor/RunlineImportGaspCommandlet.h/.cpp  create: pass 2
Unreal/Runline/Source/Runline/Runner/RunlineRunner.h/.cpp   modify: body anim, legs mesh, pose capture
Unreal/Runline/Source/Runline/Runner/RunlineRunGameMode.cpp modify: ?poses=1
Unreal/Runline/Content/Runline/Animation/DA_RunlineAnimSet.uasset   created by the commandlet (LFS)
Unreal/Runline/Source/Runline/Tests/BodyChooserTests.cpp, BodyAnimTests.cpp   create
CLAUDE.md                                       modify: the import step, ?poses=1
```

`Runline.Build.cs` adds `PublicIncludePaths.Add("Runline/Runner/Body")`.

---

### Task 1: The movement feed (from the shelved branch)

**Files:** cherry-pick `a998e03` (RunlineMovementComponent.h/.cpp, MovementComponentTests.cpp, RunlineCore/tests/Replay.h).

**Interfaces:**
- Produces: `GetFrameEvents()`, `GetStepAlpha()`, `GetSnapCount()` on `URunlineMovementComponent`; `replay::Replay(..., onStep)`.

- [ ] **Step 1: Cherry-pick**

```bash
git cherry-pick a998e03
```
Expected: applies cleanly (master hasn't touched those files since 38a36db).

- [ ] **Step 2: Run the tests**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test-unreal.ps1 -Filter Runline.Movement` → Expected: 19 passed (ArmsFeed among them).
Run: `powershell -ExecutionPolicy Bypass -File scripts/test.ps1` → Expected: 80/80.

The test keeps its name `Runline.Movement.ArmsFeed`; amend nothing. (The commit message stays the original's.)

---

### Task 2: The import pipeline, proven on one clip

**Files:**
- Create: `Data/Animation/gasp-clips.txt` (one line for now), `tools/gasp/export_clips.py`, `scripts/import-gasp.ps1`
- Create: `Unreal/Runline/Source/RunlineEditor/RunlineImportGaspCommandlet.h/.cpp`
- Modify: `.gitignore`, `Unreal/Runline/Source/RunlineEditor/RunlineEditor.Build.cs` (add `"AssetTools"`, `"UnrealEd"` if absent)
- Test: `Unreal/Runline/Source/Runline/Tests/BodyAnimTests.cpp` (create)

**Interfaces:**
- Produces: imported sequences at `/Game/ThirdParty/GASP/A_<Slot>` (root locked, root motion off); the commandlet logs `Runline GASP: <Slot> length <s> speed <m/s>` per clip; `RunlineGasp::ClipPath(Slot)` (header-only in the test for now) = `/Game/ThirdParty/GASP/A_<Slot>.A_<Slot>`.

- [ ] **Step 1: Write the failing test** (`Tests/BodyAnimTests.cpp`)

```cpp
#include "Misc/AutomationTest.h"
#include "Animation/AnimSequence.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	UAnimSequence* Clip(const TCHAR* Slot)
	{
		return LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("/Game/ThirdParty/GASP/A_%s.A_%s"), Slot, Slot), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
}

// The pipeline: a GASP clip, retargeted to our Manny, imported in place. Skips (with a warning)
// on a machine without the clips: run scripts/import-gasp.ps1.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineBodyImported, "Runline.Body.ImportedRunClip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineBodyImported::RunTest(const FString&)
{
	UAnimSequence* Run = Clip(TEXT("Run"));
	if (!Run)
	{
		AddWarning(TEXT("No GASP clips imported on this machine (scripts/import-gasp.ps1): skipped"));
		return true;
	}
	TestTrue(TEXT("on our Manny's skeleton"), Run->GetSkeleton() && Run->GetSkeleton()->GetPathName() == TEXT("/Game/Characters/Mannequins/Meshes/SK_Mannequin.SK_Mannequin"));
	TestTrue(TEXT("plays in place"), Run->bForceRootLock && !Run->bEnableRootMotion);
	const double Length = Run->GetPlayLength();
	const FTransform Moved = Run->ExtractRootMotionFromRange(0.0, Length, FAnimExtractContext());
	const double Speed = Moved.GetTranslation().Size2D() / 100.0 / Length;
	AddInfo(FString::Printf(TEXT("Run loop: %.3f s, %.2f m/s"), Length, Speed));
	TestTrue(TEXT("a run's authored speed (2 to 7 m/s)"), Speed > 2.0 && Speed < 7.0);
	return true;
}

#endif
```

`ExtractRootMotionFromRange` reads the root track whatever `bForceRootLock` says. If it returns zero for a root-locked clip, measure before locking in the commandlet instead, store it on the anim set (Task 3 adds the field), and ledger a ruling.

- [ ] **Step 2: Run it**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test-unreal.ps1 -Filter Runline.Body` → Expected: 1 passed with the "skipped" warning (no clips yet). That's the pre-pipeline baseline; the RED for this task is Step 6's first real run.

- [ ] **Step 3: The clip list and .gitignore**

`Data/Animation/gasp-clips.txt`:

```
# Slot,GASP asset (on the UEFN mannequin),hold fraction,standin
# The clips Runline uses (spec M3b §4). scripts/import-gasp.ps1 retargets each to the UE5 Manny and
# imports it as /Game/ThirdParty/GASP/A_<Slot> (git-ignored: GASP content stays out of the repo).
Run,/Game/Characters/UEFN_Mannequin/Animations/Run/M_Neutral_Run_Loop_F
```

`.gitignore` add:

```
# GASP clips (Epic's Game Animation Sample; UE-only content): made by scripts/import-gasp.ps1
Unreal/Runline/Content/ThirdParty/
build/gasp-fbx/
```

- [ ] **Step 4: Pass 1, inside the GASP project** (`tools/gasp/export_clips.py`)

```python
"""Pass 1 of scripts/import-gasp.ps1, run inside the GASP project (UnrealEditor-Cmd -run=pythonscript).

Retargets each clip in the list from the UEFN mannequin to the UE5 Manny with GASP's own retargeter,
exports the results as FBX, and deletes the temporary retargeted assets again.
Environment: RUNLINE_GASP_LIST (the clip list), RUNLINE_GASP_OUT (the FBX folder).
"""
import os
import unreal

LIST = os.environ["RUNLINE_GASP_LIST"]
OUT = os.environ["RUNLINE_GASP_OUT"]
TEMP = "/Game/_RunlineExport"
SOURCE_MESH = "/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin"
TARGET_MESH = "/Game/Characters/UE5_Mannequins/Meshes/SKM_Manny_Simple"
RETARGETER = "/Game/Characters/UE5_Mannequins/Rigs/RTG_UEFN_to_UE5_Mannequin"


def clips():
    for line in open(LIST, encoding="utf8"):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        fields = line.split(",")
        yield fields[0], fields[1]


def main():
    os.makedirs(OUT, exist_ok=True)
    lib = unreal.EditorAssetLibrary
    source = unreal.load_asset(SOURCE_MESH)
    target = unreal.load_asset(TARGET_MESH)
    rtg = unreal.load_asset(RETARGETER)
    if not (source and target and rtg):
        raise RuntimeError("Runline GASP: missing mesh or retargeter in this project")
    for slot, path in clips():
        data = unreal.EditorAssetLibrary.find_asset_data(path)
        if not data.is_valid():
            raise RuntimeError("Runline GASP: no asset " + path)
        inputs = unreal.IKRetargetBatchOperationInputs()
        inputs.assets_to_retarget = [data]
        inputs.source_mesh = source
        inputs.target_mesh = target
        inputs.ik_retarget_asset = rtg
        inputs.target_path = TEMP
        inputs.prefix = "R_"
        inputs.include_referenced_assets = False
        inputs.overwrite_existing_files = True
        made = unreal.IKRetargetBatchOperation.run_batch_retarget(inputs)
        seqs = [a.get_asset() for a in made if isinstance(a.get_asset(), unreal.AnimSequence)]
        if len(seqs) != 1:
            raise RuntimeError("Runline GASP: retargeting %s made %d sequences" % (path, len(seqs)))
        task = unreal.AssetExportTask()
        task.object = seqs[0]
        task.filename = os.path.join(OUT, slot + ".fbx")
        task.automated = True
        task.prompt = False
        task.replace_identical = True
        task.exporter = unreal.AnimSequenceExporterFBX()
        if not unreal.Exporter.run_asset_export_task(task):
            raise RuntimeError("Runline GASP: FBX export failed for " + slot)
        unreal.log("Runline GASP: exported %s -> %s" % (path, task.filename))
    lib.delete_directory(TEMP)


main()
```

The Python property names follow Unreal's snake_case of `FIKRetargetBatchOperationInputs` (`AssetsToRetarget` → `assets_to_retarget`, `IKRetargetAsset` → `ik_retarget_asset`). If one is named differently in 5.8, print `dir(unreal.IKRetargetBatchOperationInputs())` once and correct it (ledger it).

- [ ] **Step 5: Pass 2, the commandlet** (RunlineEditor)

`RunlineImportGaspCommandlet.h`:

```cpp
#pragma once
#include "Commandlets/Commandlet.h"
#include "RunlineImportGaspCommandlet.generated.h"

/// Pass 2 of scripts/import-gasp.ps1: imports build/gasp-fbx/<Slot>.fbx onto our Manny skeleton as
/// /Game/ThirdParty/GASP/A_<Slot> (in place: root locked, no root motion) for every slot in
/// Data/Animation/gasp-clips.txt. -List=<file> -Fbx=<folder>.
UCLASS()
class URunlineImportGaspCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	URunlineImportGaspCommandlet();
	virtual int32 Main(const FString& Params) override;
};
```

`RunlineImportGaspCommandlet.cpp`:

```cpp
#include "RunlineImportGaspCommandlet.h"
#include "AssetToolsModule.h"
#include "AssetImportTask.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Factories/FbxImportUI.h"
#include "Factories/FbxAnimSequenceImportData.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/SavePackage.h"

DEFINE_LOG_CATEGORY_STATIC(LogRunlineGasp, Log, All);

URunlineImportGaspCommandlet::URunlineImportGaspCommandlet()
{
	IsClient = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 URunlineImportGaspCommandlet::Main(const FString& Params)
{
	FString List, Fbx;
	if (!FParse::Value(*Params, TEXT("List="), List) || !FParse::Value(*Params, TEXT("Fbx="), Fbx))
	{
		UE_LOG(LogRunlineGasp, Error, TEXT("usage: -run=RunlineImportGasp -List=<gasp-clips.txt> -Fbx=<folder>"));
		return 1;
	}
	// The classic FBX importer (5.8 sends FBX through Interchange by default): UFbxImportUI's
	// animation options are the well-trodden path for "animation only, onto this skeleton".
	if (IConsoleVariable* Cv = IConsoleManager::Get().FindConsoleVariable(TEXT("Interchange.FeatureFlags.Import.FBX"))) Cv->Set(false);
	USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, TEXT("/Game/Characters/Mannequins/Meshes/SK_Mannequin.SK_Mannequin"));
	if (!Skeleton) { UE_LOG(LogRunlineGasp, Error, TEXT("no SK_Mannequin")); return 1; }
	TArray<FString> Lines;
	FFileHelper::LoadFileToStringArray(Lines, *List);
	IAssetTools& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	int32 Failed = 0;
	for (const FString& Line : Lines)
	{
		if (Line.IsEmpty() || Line.StartsWith(TEXT("#"))) continue;
		TArray<FString> F;
		Line.ParseIntoArray(F, TEXT(","), false);
		const FString Slot = F[0];
		const FString File = FPaths::Combine(Fbx, Slot + TEXT(".fbx"));
		UFbxImportUI* Ui = NewObject<UFbxImportUI>();
		Ui->bImportMesh = false;
		Ui->bImportAnimations = true;
		Ui->bImportMaterials = false;
		Ui->bImportTextures = false;
		Ui->Skeleton = Skeleton;
		Ui->MeshTypeToImport = FBXIT_Animation;
		Ui->bAutomatedImportShouldDetectType = false;
		Ui->AnimSequenceImportData->bImportBoneTracks = true;
		UAssetImportTask* Task = NewObject<UAssetImportTask>();
		Task->Filename = File;
		Task->DestinationPath = TEXT("/Game/ThirdParty/GASP");
		Task->DestinationName = TEXT("A_") + Slot;
		Task->bAutomated = true;
		Task->bReplaceExisting = true;
		Task->bSave = false;
		Task->Options = Ui;
		Tools.ImportAssetTasks({Task});
		UAnimSequence* Seq = nullptr;
		for (UObject* O : Task->GetObjects()) if (UAnimSequence* S = Cast<UAnimSequence>(O)) Seq = S;
		if (!Seq) { UE_LOG(LogRunlineGasp, Error, TEXT("import failed: %s"), *File); ++Failed; continue; }
		// Measure the authored speed before locking the root: the core moves her.
		const double Length = Seq->GetPlayLength();
		const double Speed = Length > 0.0 ? Seq->ExtractRootMotionFromRange(0.0, Length, FAnimExtractContext()).GetTranslation().Size2D() / 100.0 / Length : 0.0;
		Seq->bEnableRootMotion = false;
		Seq->bForceRootLock = true;
		Seq->MarkPackageDirty();
		UPackage* Package = Seq->GetOutermost();
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		UPackage::SavePackage(Package, Seq, *FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension()), Args);
		UE_LOG(LogRunlineGasp, Display, TEXT("Runline GASP: %s length %.3f speed %.3f"), *Slot, Length, Speed);
	}
	return Failed == 0 ? 0 : 1;
}
```

- [ ] **Step 6: The driver script** (`scripts/import-gasp.ps1`) and the first real run

```powershell
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
$GaspProject = Get-ChildItem $Gasp -Filter *.uproject | Select-Object -First 1
if (-not $GaspProject) { throw "No GASP project in $Gasp" }
$env:RUNLINE_GASP_LIST = $List
$env:RUNLINE_GASP_OUT = $Out
& $Cmd $GaspProject.FullName -run=pythonscript "-script=$(Join-Path $Root 'tools\gasp\export_clips.py')" -EnablePlugins=PythonScriptPlugin -unattended -nop4 -nosplash -stdout | Tee-Object (Join-Path $Root 'build\gasp-export.log') | Select-String 'Runline GASP|Error'
if ($LASTEXITCODE -ne 0) { throw "GASP export failed (build/gasp-export.log)" }
& $Cmd (Join-Path $Root 'Unreal\Runline\Runline.uproject') -run=RunlineImportGasp "-List=$List" "-Fbx=$Out" -unattended -nop4 -nosplash -stdout | Tee-Object (Join-Path $Root 'build\gasp-import.log') | Select-String 'Runline GASP|Error'
if ($LASTEXITCODE -ne 0) { throw "GASP import failed (build/gasp-import.log)" }
```

Run: build the editor first (`scripts/test-unreal.ps1 -Filter Runline.Body` builds it), then `powershell -ExecutionPolicy Bypass -File scripts/import-gasp.ps1`.
Expected: `Runline GASP: exported ... Run.fbx` and `Runline GASP: Run length 0.9xx speed 3.x`; `Unreal/Runline/Content/ThirdParty/GASP/A_Run.uasset` exists; `git status` doesn't list it.
Then: `powershell -ExecutionPolicy Bypass -File scripts/test-unreal.ps1 -Filter Runline.Body` → Expected: `ImportedRunClip` passes **without** the skip warning (the real GREEN). If the run clip's speed is 0, apply the Step 1 note.

This step is the spike the spec asks for: if the pipeline can't be made to work, stop and report to the user before Task 3.

- [ ] **Step 7: Commit**

```bash
git add .gitignore Data/Animation/gasp-clips.txt tools/gasp/export_clips.py scripts/import-gasp.ps1 Unreal/Runline/Source/RunlineEditor Unreal/Runline/Source/Runline/Tests/BodyAnimTests.cpp
git commit -m "M3b: GASP import pipeline (retarget + FBX in the GASP project, import here), proven on the run loop"
```

---

### Task 3: All the clips, and the anim set

**Files:**
- Modify: `Data/Animation/gasp-clips.txt` (every slot)
- Create: `Unreal/Runline/Source/Runline/Runner/Body/RunlineAnimSet.h/.cpp`
- Modify: `RunlineImportGaspCommandlet.cpp` (fill the anim set), `Runline.Build.cs` (include path)
- Test: `BodyAnimTests.cpp`

**Interfaces:**
- Produces: `enum class ERunlineAnimSlot : uint8` (order below); `FRunlineAnimSlotDef { TSoftObjectPtr<UAnimSequence> Clip; float AuthoredSpeed; float HoldAt; bool bStandIn; }`; `URunlineAnimSet : UDataAsset` with `TArray<FRunlineAnimSlotDef> Slots` (indexed by slot), `static URunlineAnimSet* Get()` (loads `/Game/Runline/Animation/DA_RunlineAnimSet`), `const TCHAR* RunlineAnimSlotName(ERunlineAnimSlot)`, `bool RunlineAnimSlotFromName(const FString&, ERunlineAnimSlot&)`.

Slots, in this order: `Idle, Walk, Run, Sprint, CrouchIdle, CrouchWalk, JumpStand, JumpRun, JumpSprint, Fall, LandStandLight, LandStandHeavy, LandRunLight, LandRunHeavy, LandSprintLight, LandSprintHeavy, Stumble, Roll, SlideInto, SlideLoop, SlideOut, Vault, Mantle, StepUp, PullUp, Springboard, Hang, Count`.

- [ ] **Step 1: Write the failing test** (append to BodyAnimTests.cpp; include `RunlineAnimSet.h`)

```cpp
// The list and the anim set agree: every slot has a line, a clip, and (after import) the clip
// loads; stand-ins are marked.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineBodyAnimSet, "Runline.Body.AnimSetMatchesTheList",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineBodyAnimSet::RunTest(const FString&)
{
	TArray<FString> Lines;
	TestTrue(TEXT("clip list"), FFileHelper::LoadFileToStringArray(Lines, *(FPaths::ProjectDir() / TEXT("../../Data/Animation/gasp-clips.txt"))));
	TSet<int32> Listed;
	for (const FString& L : Lines)
	{
		if (L.IsEmpty() || L.StartsWith(TEXT("#"))) continue;
		ERunlineAnimSlot S;
		TestTrue(*FString::Printf(TEXT("known slot in '%s'"), *L), RunlineAnimSlotFromName(L.Left(L.Find(TEXT(","))), S));
		Listed.Add(int32(S));
	}
	TestEqual(TEXT("every slot listed once"), Listed.Num(), int32(ERunlineAnimSlot::Count));
	const URunlineAnimSet* Set = URunlineAnimSet::Get();
	TestNotNull(TEXT("the anim set asset"), Set);
	if (!Set) return false;
	TestEqual(TEXT("one entry per slot"), Set->Slots.Num(), int32(ERunlineAnimSlot::Count));
	TestTrue(TEXT("Hang is a stand-in"), Set->Slots[int32(ERunlineAnimSlot::Hang)].bStandIn);
	if (!Clip(TEXT("Run"))) { AddWarning(TEXT("No GASP clips imported: clip loading not checked")); return true; }
	for (int32 I = 0; I < Set->Slots.Num(); ++I)
		TestNotNull(*FString::Printf(TEXT("%s loads"), RunlineAnimSlotName(ERunlineAnimSlot(I))), Set->Slots[I].Clip.LoadSynchronous());
	TestTrue(TEXT("the run's authored speed was measured"), Set->Slots[int32(ERunlineAnimSlot::Run)].AuthoredSpeed > 2.0f);
	return true;
}
```

- [ ] **Step 2: Run it** → Expected: build fails (`RunlineAnimSet.h` not found).

- [ ] **Step 3: Implement**

`Data/Animation/gasp-clips.txt` (replacing the Run-only body; `A` = `/Game/Characters/UEFN_Mannequin/Animations`, written out in full in the file):

```
Idle,A/Idle/M_Neutral_Stand_Idle_Loop
Walk,A/Walk/M_Neutral_Walk_Loop_F
Run,A/Run/M_Neutral_Run_Loop_F
Sprint,A/Sprint/M_Neutral_Sprint_Loop_F
CrouchIdle,A/Idle/M_Neutral_Crouch_Idle_Loop
CrouchWalk,A/Crouch/M_Neutral_Crouch_Loop_F
JumpStand,A/Jump/M_Neutral_Jump_F_Start_Stand_Lfoot
JumpRun,A/Jump/M_Neutral_Jump_F_Start_Run_Lfoot
JumpSprint,A/Jump/M_Neutral_Jump_F_Start_Sprint_Lfoot
Fall,A/Jump/M_Neutral_Jump_Loop_Fall
LandStandLight,A/Jump/M_Neutral_Jump_F_Land_Stand_Light_Lfoot
LandStandHeavy,A/Jump/M_Neutral_Jump_F_Land_Stand_Heavy_Lfoot
LandRunLight,A/Jump/M_Neutral_Jump_F_Land_Run_Light_Lfoot
LandRunHeavy,A/Jump/M_Neutral_Jump_F_Land_Run_Heavy_Lfoot
LandSprintLight,A/Jump/M_Neutral_Jump_F_Land_Sprint_Light_Lfoot
LandSprintHeavy,A/Jump/M_Neutral_Jump_F_Land_Sprint_Heavy_Lfoot
Stumble,A/Jump/M_Neutral_Jump_F_Land_Stumble_Lfoot
Roll,A/Jump/M_Neutral_Jump_F_Land_Roll_Lfoot
SlideInto,A/Slide/M_Neutral_Slide_FootOut_Into_Lfoot
SlideLoop,A/Slide/M_Neutral_Slide_FootOut_Loop
SlideOut,A/Slide/M_Neutral_Slide_FootOut_Out_Moving_Run
Vault,A/Traversal/Vault/M_Neutral_Traversal_Vault_1_0_run_F_Lfoot
Mantle,A/Traversal/Mantle/M_Neutral_Traversal_Mantle_1_0_run_F_Lfoot
StepUp,A/Traversal/Mantle/M_Neutral_Traversal_Mantle_1_0_stand_F_Lfoot
PullUp,A/Traversal/Climb/M_Neutral_Traversal_Climb_Start_2_5_stand_F_Lfoot
Springboard,A/Jump/M_Neutral_Jump_F_Start_Run_Rfoot
Hang,A/Traversal/Catch/Cliff/M_Neutral_Traversal_Catch_Cliff_high_stand,0.35,standin
```

`RunlineAnimSet.h`:

```cpp
#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "RunlineAnimSet.generated.h"

class UAnimSequence;

/// The clips the body plays (spec M3b §4). Order matters: it indexes URunlineAnimSet::Slots.
UENUM()
enum class ERunlineAnimSlot : uint8
{
	Idle, Walk, Run, Sprint, CrouchIdle, CrouchWalk, JumpStand, JumpRun, JumpSprint, Fall,
	LandStandLight, LandStandHeavy, LandRunLight, LandRunHeavy, LandSprintLight, LandSprintHeavy,
	Stumble, Roll, SlideInto, SlideLoop, SlideOut, Vault, Mantle, StepUp, PullUp, Springboard, Hang,
	Count
};

const TCHAR* RunlineAnimSlotName(ERunlineAnimSlot Slot);
bool RunlineAnimSlotFromName(const FString& Name, ERunlineAnimSlot& Out);

USTRUCT()
struct FRunlineAnimSlotDef
{
	GENERATED_BODY()
	/// The clip (soft: the GASP clips are git-ignored, so this asset loads without them).
	UPROPERTY(EditAnywhere, Category = "Anim") TSoftObjectPtr<UAnimSequence> Clip;
	/// Metres per second the clip was captured at (loops; measured at import). 0: not a loop.
	UPROPERTY(EditAnywhere, Category = "Anim") float AuthoredSpeed = 0.0f;
	/// For a held stand-in: where in the clip (0..1) to hold.
	UPROPERTY(EditAnywhere, Category = "Anim") float HoldAt = 0.0f;
	/// A stand-in for a move GASP has no clip for (M3c replaces these).
	UPROPERTY(EditAnywhere, Category = "Anim") bool bStandIn = false;
};

/// The body's clips, one per slot (made by scripts/import-gasp.ps1; edit in the editor to try
/// another clip).
UCLASS()
class RUNLINE_API URunlineAnimSet : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "Anim") TArray<FRunlineAnimSlotDef> Slots;
	/// /Game/Runline/Animation/DA_RunlineAnimSet, or null.
	static URunlineAnimSet* Get();
};
```

`RunlineAnimSet.cpp`:

```cpp
#include "RunlineAnimSet.h"

const TCHAR* RunlineAnimSlotName(ERunlineAnimSlot Slot)
{
	static const TCHAR* Names[] = {TEXT("Idle"), TEXT("Walk"), TEXT("Run"), TEXT("Sprint"), TEXT("CrouchIdle"), TEXT("CrouchWalk"),
		TEXT("JumpStand"), TEXT("JumpRun"), TEXT("JumpSprint"), TEXT("Fall"), TEXT("LandStandLight"), TEXT("LandStandHeavy"),
		TEXT("LandRunLight"), TEXT("LandRunHeavy"), TEXT("LandSprintLight"), TEXT("LandSprintHeavy"), TEXT("Stumble"), TEXT("Roll"),
		TEXT("SlideInto"), TEXT("SlideLoop"), TEXT("SlideOut"), TEXT("Vault"), TEXT("Mantle"), TEXT("StepUp"), TEXT("PullUp"),
		TEXT("Springboard"), TEXT("Hang")};
	static_assert(UE_ARRAY_COUNT(Names) == int32(ERunlineAnimSlot::Count));
	return int32(Slot) < int32(ERunlineAnimSlot::Count) ? Names[int32(Slot)] : TEXT("?");
}

bool RunlineAnimSlotFromName(const FString& Name, ERunlineAnimSlot& Out)
{
	for (int32 I = 0; I < int32(ERunlineAnimSlot::Count); ++I)
		if (Name == RunlineAnimSlotName(ERunlineAnimSlot(I))) { Out = ERunlineAnimSlot(I); return true; }
	return false;
}

URunlineAnimSet* URunlineAnimSet::Get()
{
	return LoadObject<URunlineAnimSet>(nullptr, TEXT("/Game/Runline/Animation/DA_RunlineAnimSet.DA_RunlineAnimSet"), nullptr, LOAD_NoWarn | LOAD_Quiet);
}
```

Commandlet: after the loop, load or create `/Game/Runline/Animation/DA_RunlineAnimSet` (`CreatePackage` + `NewObject<URunlineAnimSet>(Package, "DA_RunlineAnimSet", RF_Public | RF_Standalone)` if `URunlineAnimSet::Get()` is null), size `Slots` to `Count`, and for each listed slot set `Clip = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/ThirdParty/GASP/A_<Slot>.A_<Slot>")))`, `AuthoredSpeed` (the speed measured above when the slot is one of the loops: Walk, Run, Sprint, CrouchWalk; 0 otherwise), `HoldAt` (field 3 or 0) and `bStandIn` (field 4 == "standin"); save it like the sequences. The commandlet needs `#include "RunlineAnimSet.h"` (RunlineEditor already depends on Runline).

Re-run `scripts/import-gasp.ps1` (all 27 clips); then commit `DA_RunlineAnimSet.uasset` (LFS, under `Content/Runline/Animation`).

- [ ] **Step 4: Run the test** → Expected: `Runline.Body.AnimSetMatchesTheList` and `ImportedRunClip` pass, no skip warnings.

- [ ] **Step 5: Commit**

```bash
git add Data/Animation/gasp-clips.txt Unreal/Runline/Source Unreal/Runline/Content/Runline/Animation/DA_RunlineAnimSet.uasset
git commit -m "M3b: every GASP clip the body uses, and the anim set"
```

---

### Task 4: The chooser: which clips, where in them, how much

**Files:**
- Create: `Unreal/Runline/Source/Runline/Runner/Body/RunlineBodyChooser.h/.cpp`
- Test: `Unreal/Runline/Source/Runline/Tests/BodyChooserTests.cpp` (create)

**Interfaces:**
- Consumes: Task 3's `ERunlineAnimSlot`; Task 1's movement feed (through the input).
- Produces:
  - `struct FRunlineClipInfo { float Length = 0; float AuthoredSpeed = 0; float HoldAt = 0; bool bLoaded = false; };`
  - `struct FRunlineBodyInput { runline::State State; bool bCrouched; runline::Vec3 Vel; runline::Vec3 WallNormal (WallRun only); float Yaw; runline::Tuning CoreTuning; float StepSeconds; float StepAlpha; float Dt; TArray<runline::Event> Events; int32 SnapCount; static FRunlineBodyInput From(const URunlineMovementComponent&, float Dt); };`
  - `struct FRunlineBodyLayer { ERunlineAnimSlot Slot; float Time; float Weight; };` `struct FRunlineBodyPose { TArray<FRunlineBodyLayer, TInlineAllocator<8>> Layers; float LeanDegrees = 0; };` (weights sum to 1)
  - `class FRunlineBodyChooser { public: TFunction<FRunlineClipInfo(ERunlineAnimSlot)> Clips; FRunlineBodyPose Choose(const FRunlineBodyInput&); void Reset(); int32 Warnings() const; }`

Rules (spec §4), with `Late = StepAlpha * StepSeconds` added to every core timer:
- **Main clip** per state (table in spec §4; stand-ins as listed). Locomotion = two loops by speed among Idle (0), Walk, Run, Sprint at their `AuthoredSpeed` (Crouch: CrouchIdle, CrouchWalk), sampled at one shared normalized phase; phase advances by `Dt * Rate / Length(dominant)` with `Rate = clamp(speed / AuthoredSpeed(dominant), 0.7, 1.4)` (Idle: rate 1).
- **One-shots timed by the core:** `Time = clamp((t + Late) / Duration, 0, 1) * Length` with Duration: Roll `rollTime`, Stunned `hardLandStun`, Vault `Duration()`, Traverse `dur`, LayOnGround getting up `layOnGroundGetUpTime` (played backwards from the end).
- **Slide:** `SlideInto` over the first 0.25 s of `Slide.t`, then `SlideLoop` looping; leaving a slide for Ground plays `SlideOut` as an overlay (below).
- **Air:** after a Jump event (or a SpringBoard's end), the matching `Jump*` start by speed (< 1.5 m/s Stand, < 5.5 Run, else Sprint) plays from the event at rate 1 until its end, then `Fall` loops.
- **Overlays** (Ground and SoftLand only): a Land/HardLand/SoftLand event starts `Land{Stand,Run,Sprint}{Light,Heavy}` (speed as above; Heavy when `impact > 7` m/s); `SlideOut` likewise when a slide ends. An overlay plays at rate 1 at full weight, fading out over its last 30%, and ends at once if the state leaves Ground/SoftLand.
- **Stand-ins:** WallRun = Sprint locomotion plus `LeanDegrees = 20` away from the wall; WallClimb = `PullUp` at `min(t + Late, Length)`; WallClimbTurned = `JumpRun` from `t`; LedgeHang (any shimmy) and GrabTransfer = `Hang` held at `HoldAt * Length`; LayOnGround lying = `Stumble` at its end.
- **Cross-fades:** when the main clip changes (slot or state), the old main fades out over 0.15 s (0.08 s into Slide or Roll) while the new one fades in. A snap (`SnapCount` changed) resets: no fade.
- **Missing clips:** a slot with `!bLoaded` becomes the locomotion loop for the current speed; the first time per slot, `UE_LOG(Warning)` and `++Warnings`.

- [ ] **Step 1: Write the failing tests** (`Tests/BodyChooserTests.cpp`)

```cpp
#include "Misc/AutomationTest.h"
#include "RunlineBodyChooser.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr float Frame = 1.0f / 60.0f;
	/// Every clip loaded, 1 s long; loops authored at walk 1.5, run 4, sprint 6, crouch 1 m/s.
	FRunlineBodyChooser Chooser(TSet<ERunlineAnimSlot> Missing = {})
	{
		FRunlineBodyChooser C;
		C.Clips = [Missing](ERunlineAnimSlot S)
		{
			FRunlineClipInfo I;
			I.Length = 1.0f;
			I.bLoaded = !Missing.Contains(S);
			I.AuthoredSpeed = S == ERunlineAnimSlot::Walk ? 1.5f : S == ERunlineAnimSlot::Run ? 4.0f : S == ERunlineAnimSlot::Sprint ? 6.0f : S == ERunlineAnimSlot::CrouchWalk ? 1.0f : 0.0f;
			I.HoldAt = S == ERunlineAnimSlot::Hang ? 0.35f : 0.0f;
			return I;
		};
		return C;
	}
	FRunlineBodyInput In(const runline::State& S, float Speed = 0.0f)
	{
		FRunlineBodyInput I;
		I.State = S;
		I.Vel = {0.0f, 0.0f, -Speed};
		I.Dt = Frame;
		return I;
	}
	FRunlineBodyPose Settle(FRunlineBodyChooser& C, const FRunlineBodyInput& I, int32 N = 60)
	{
		FRunlineBodyPose P;
		for (int32 K = 0; K < N; ++K) P = C.Choose(I);
		return P;
	}
	/// The layer of slot S (weight 0 if absent).
	FRunlineBodyLayer Layer(const FRunlineBodyPose& P, ERunlineAnimSlot S)
	{
		for (const FRunlineBodyLayer& L : P.Layers) if (L.Slot == S) return L;
		return {S, 0.0f, 0.0f};
	}
	float Total(const FRunlineBodyPose& P) { float W = 0; for (const FRunlineBodyLayer& L : P.Layers) W += L.Weight; return W; }
	runline::Event Land(float Impact) { runline::Event E{runline::EventKind::Land}; E.impact = Impact; return E; }
}

// Running at 4.5 m/s: between the run (4) and sprint (6) loops, at one shared phase; the cycle plays
// at 4.5 / 4 the run clip's rate (it dominates).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineBodyLocomotion, "Runline.Body.LocomotionBySpeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineBodyLocomotion::RunTest(const FString&)
{
	FRunlineBodyChooser C = Chooser();
	const FRunlineBodyPose P = Settle(C, In(runline::Ground{}, 4.5f));
	const FRunlineBodyLayer Run = Layer(P, ERunlineAnimSlot::Run), Sprint = Layer(P, ERunlineAnimSlot::Sprint);
	TestEqual(TEXT("weights sum to 1"), Total(P), 1.0f, 1e-4f);
	TestEqual(TEXT("run 75%"), Run.Weight, 0.75f, 1e-3f);
	TestEqual(TEXT("sprint 25%"), Sprint.Weight, 0.25f, 1e-3f);
	TestEqual(TEXT("one phase"), Run.Time, Sprint.Time, 1e-4f);
	const float Before = Run.Time;
	const FRunlineBodyPose Q = C.Choose(In(runline::Ground{}, 4.5f));
	TestEqual(TEXT("the cycle runs at 4.5 / 4"), FMath::Fmod(Layer(Q, ERunlineAnimSlot::Run).Time - Before + 1.0f, 1.0f), Frame * 4.5f / 4.0f, 1e-4f);
	TestEqual(TEXT("standing: idle"), Layer(Settle(C, In(runline::Ground{}, 0.0f)), ERunlineAnimSlot::Idle).Weight, 1.0f, 1e-4f);
	return true;
}

// Review Focus 4: a vault half way through its Duration() is half way through its clip, whatever
// the frame times; the step alpha counts.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineBodyOneShots, "Runline.Body.OneShotsFollowTheCoreTimer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineBodyOneShots::RunTest(const FString&)
{
	FRunlineBodyChooser C = Chooser();
	runline::Vault V;
	V.kind = 3;   // VaultOver: 0 + 0.35 + 0.3 s
	V.t = V.Duration() * 0.5f;
	FRunlineBodyInput I = In(V);
	I.Dt = 0.25f;   // a hitch
	TestEqual(TEXT("half way"), Layer(Settle(C, I, 3), ERunlineAnimSlot::Vault).Time, 0.5f, 1e-4f);
	I.StepAlpha = 0.5f;
	TestEqual(TEXT("plus half a step"), Layer(C.Choose(I), ERunlineAnimSlot::Vault).Time, (V.t + 0.5f * I.StepSeconds) / V.Duration(), 1e-4f);
	runline::Roll R;
	R.t = I.CoreTuning.rollTime * 0.25f;
	TestEqual(TEXT("a roll spans rollTime"), Layer(Settle(C, In(R)), ERunlineAnimSlot::Roll).Time, 0.25f, 1e-4f);
	V.kind = 1;   // a step-up
	V.t = 0.0f;
	TestTrue(TEXT("step-ups use StepUp"), Layer(Settle(C, In(V)), ERunlineAnimSlot::StepUp).Weight > 0.99f);
	return true;
}

// Landings pick by speed and impact, play over the locomotion, and fade out.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineBodyLandings, "Runline.Body.LandingsBySpeedAndImpact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineBodyLandings::RunTest(const FString&)
{
	FRunlineBodyChooser C = Chooser();
	Settle(C, In(runline::Ground{}, 6.5f));
	FRunlineBodyInput I = In(runline::Ground{}, 6.5f);
	I.Events = {Land(9.0f)};
	const FRunlineBodyPose P = C.Choose(I);
	TestTrue(TEXT("sprint, heavy"), Layer(P, ERunlineAnimSlot::LandSprintHeavy).Weight > 0.99f);
	I.Events.Reset();
	const FRunlineBodyPose After = Settle(C, I, 70);
	TestEqual(TEXT("faded out after its length"), Layer(After, ERunlineAnimSlot::LandSprintHeavy).Weight, 0.0f, 1e-4f);
	FRunlineBodyChooser D = Chooser();
	Settle(D, In(runline::Ground{}, 0.5f));
	FRunlineBodyInput J = In(runline::Ground{}, 0.5f);
	J.Events = {Land(3.0f)};
	TestTrue(TEXT("standing, light"), Layer(D.Choose(J), ERunlineAnimSlot::LandStandLight).Weight > 0.99f);
	return true;
}

// Review Focus 2: a move the core ends early doesn't run on: its clip cross-fades out.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineBodyEndsWithMove, "Runline.Body.ClipEndsWithTheMove",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineBodyEndsWithMove::RunTest(const FString&)
{
	FRunlineBodyChooser C = Chooser();
	runline::Vault V;
	V.kind = 3;
	V.t = 0.1f;
	Settle(C, In(V), 10);
	const FRunlineBodyPose First = C.Choose(In(runline::Ground{}, 4.0f));
	TestTrue(TEXT("still mostly the vault on the first frame (a cross-fade)"), Layer(First, ERunlineAnimSlot::Vault).Weight > 0.8f);
	const FRunlineBodyPose Later = Settle(C, In(runline::Ground{}, 4.0f), 12);
	TestEqual(TEXT("gone after 0.15 s"), Layer(Later, ERunlineAnimSlot::Vault).Weight, 0.0f, 1e-4f);
	TestEqual(TEXT("weights sum to 1 throughout"), Total(First), 1.0f, 1e-4f);
	return true;
}

// Review Focus 3: a respawn cuts: no fade from the old pose.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineBodySnap, "Runline.Body.SnapCuts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineBodySnap::RunTest(const FString&)
{
	FRunlineBodyChooser C = Chooser();
	Settle(C, In(runline::Roll{0.2f}));
	FRunlineBodyInput I = In(runline::Ground{});
	I.SnapCount = 1;
	const FRunlineBodyPose P = C.Choose(I);
	TestEqual(TEXT("no roll left"), Layer(P, ERunlineAnimSlot::Roll).Weight, 0.0f, 1e-6f);
	TestEqual(TEXT("idle at once"), Layer(P, ERunlineAnimSlot::Idle).Weight, 1.0f, 1e-6f);
	return true;
}

// Review Focus 5: a missing clip falls back to the locomotion loop, with one warning per slot.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineBodyMissing, "Runline.Body.MissingSlotUsesLocomotion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineBodyMissing::RunTest(const FString&)
{
	FRunlineBodyChooser C = Chooser({ERunlineAnimSlot::Roll});
	const FRunlineBodyPose P = Settle(C, In(runline::Roll{0.1f}, 4.0f));
	TestEqual(TEXT("no roll"), Layer(P, ERunlineAnimSlot::Roll).Weight, 0.0f, 1e-6f);
	TestEqual(TEXT("the run loop instead"), Layer(P, ERunlineAnimSlot::Run).Weight, 1.0f, 1e-4f);
	TestEqual(TEXT("one warning"), C.Warnings(), 1);
	return true;
}

// Stand-ins and the slide: hang held at HoldAt; wallrun leans away from the wall; the slide's
// into-then-loop.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineBodyStandIns, "Runline.Body.StandInsAndSlide",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineBodyStandIns::RunTest(const FString&)
{
	FRunlineBodyChooser C = Chooser();
	const FRunlineBodyPose Hang = Settle(C, In(runline::LedgeHang{{0.0f, 0.0f, 1.0f}, 2.0f, false}));
	TestEqual(TEXT("hang held at 35%"), Layer(Hang, ERunlineAnimSlot::Hang).Time, 0.35f, 1e-4f);
	FRunlineBodyInput W = In(runline::WallRun{{-1.0f, 0.0f, 0.0f}, 0.3f}, 6.0f);   // wall on her right (core yaw 0)
	W.WallNormal = {-1.0f, 0.0f, 0.0f};
	const FRunlineBodyPose Run = Settle(C, W);
	TestTrue(TEXT("sprinting"), Layer(Run, ERunlineAnimSlot::Sprint).Weight > 0.99f);
	TestEqual(TEXT("leaning 20 degrees, away from the wall (left)"), Run.LeanDegrees, -20.0f, 1e-3f);
	TestTrue(TEXT("slide: into first"), Layer(Settle(C, In(runline::Slide{0.1f})), ERunlineAnimSlot::SlideInto).Weight > 0.99f);
	TestTrue(TEXT("then the loop"), Layer(Settle(C, In(runline::Slide{0.6f})), ERunlineAnimSlot::SlideLoop).Weight > 0.99f);
	return true;
}

#endif
```

- [ ] **Step 2: Run them** → Expected: build fails (`RunlineBodyChooser.h` not found).

- [ ] **Step 3: Implement** `RunlineBodyChooser.h/.cpp` to the rules above. Structure:
  - `Choose()`: snap check (`Reset()` + no fade); `StepLocomotion(In)` (phase); `FMain Want = MainFor(In)` (slot or locomotion pair, time); if `Want.Key != Main.Key`, push `Main` onto `Fading` (each with its weight, fading at `1 / FadeTime(Want)` per second) and start `Main = Want` at weight 0 (1 after a snap or on the first call); events → overlays (landing, slide-out) and air starts; build `Layers`: fading mains, the main (locomotion splits into its two loops), overlays on top, then normalize so the weights sum to 1 (overlay weight w scales everything under it by 1 - w).
  - `MainFor()` holds the per-state table; `Resolve(Slot)` swaps a missing clip for the locomotion loop (warning once per slot via a `TSet`).
  - Lean: WallRun only: the wall's side from `WallNormal` against her facing `(-sin(Yaw), 0, -cos(Yaw))` (core): `LeanDegrees = (wall on her right) ? -20 : 20`.
  - `FRunlineBodyInput::From` fills from the component (state, crouched, vel, yaw, tuning, step, alpha, events, snap count; `WallNormal` from `WallRun::normal`).
  The exact core yaw convention: core yaw 0 faces -Z, and positive yaw turns left (RunlineUnits: Unreal yaw = -core yaw); check her right vector against `RunlineUnits::ToUnrealDirection` in the test's setup if the lean's sign fails, and ledger it.

- [ ] **Step 4: Run** → Expected: all 7 Runline.Body chooser tests pass (plus Task 2-3's 2).

- [ ] **Step 5: Commit** `git commit -m "M3b: the body chooser: clips by core state, timed by the core"`

---

### Task 5: The anim instance: sampling and blending the clips

**Files:**
- Create: `Unreal/Runline/Source/Runline/Runner/Body/RunlineBodyAnimInstance.h/.cpp`
- Test: `BodyAnimTests.cpp`

**Interfaces:**
- Consumes: Task 3's anim set; Task 4's chooser and `FRunlineBodyPose`.
- Produces: `URunlineBodyAnimInstance` with `void SetInput(const FRunlineBodyInput&)` (game thread; runs the chooser, keeps the pose for the proxy), `bool HasClips() const` (the set loaded with at least Idle and Run), `const FRunlineBodyChooser& GetChooser() const`.

The proxy (`FRunlineBodyProxy : FAnimInstanceProxy`, `USTRUCT`) copies the pose and the resolved `UAnimSequence*` per layer in `PreUpdate`; `Evaluate` samples each layer with `Seq->GetAnimationPose(FAnimationPoseData, FAnimExtractContext(double(Time)))` into its own `FCompactPose`/`FBlendedCurve`/`FStackAttributeContainer`, blends with `FAnimationRuntime::BlendPosesTogether`, then applies `LeanDegrees` as a rotation of `spine_01` in component space about the component's forward axis (Manny faces +Y), split half on `spine_01`, half on `spine_03`. The anim instance resolves clips once (`LoadSynchronous` in `NativeInitializeAnimation`), builds the chooser's `Clips` callback from them (Length = `GetPlayLength()`, `AuthoredSpeed`, `HoldAt` from the set), and holds the sequences in a `UPROPERTY() TArray<TObjectPtr<UAnimSequence>>` so they aren't collected.

- [ ] **Step 1: Write the failing tests** (append to BodyAnimTests.cpp)

```cpp
// The real clips on the real mesh: a vault half way through shows the vault clip's half-way pose,
// and with no input the body stands in the idle clip's first frame (never the reference T-pose).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineBodyOnMesh, "Runline.Body.ClipsOnTheMesh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineBodyOnMesh::RunTest(const FString&)
{
	if (!Clip(TEXT("Run"))) { AddWarning(TEXT("No GASP clips imported: skipped")); return true; }
	FRunlineTestWorld TW;
	AActor* A = TW.World->SpawnActor<AActor>();
	USkeletalMeshComponent* Mesh = NewObject<USkeletalMeshComponent>(A);
	A->SetRootComponent(Mesh);
	Mesh->SetSkeletalMesh(LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple")));
	Mesh->SetAnimInstanceClass(URunlineBodyAnimInstance::StaticClass());
	Mesh->RegisterComponent();
	URunlineBodyAnimInstance* Anim = Cast<URunlineBodyAnimInstance>(Mesh->GetAnimInstance());
	TestTrue(TEXT("clips found"), Anim && Anim->HasClips());
	if (!Anim) return false;
	const auto Evaluate = [&]() { Mesh->TickAnimation(0.0f, false); Mesh->RefreshBoneTransforms(); };
	runline::Vault V;
	V.kind = 3;
	V.t = V.Duration() * 0.5f;
	FRunlineBodyInput I;
	I.State = V;
	I.Dt = 1.0f / 60.0f;
	I.SnapCount = 1;   // cut straight to it
	Anim->SetInput(I);
	Evaluate();
	UAnimSequence* Vault = Clip(TEXT("Vault"));
	const int32 Hand = Mesh->GetBoneIndex(TEXT("hand_r"));
	FTransform Want = FTransform::Identity;
	for (int32 B = Hand; B != INDEX_NONE; B = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton().GetParentIndex(B))
	{
		FTransform Local;
		Vault->GetBoneTransform(Local, FSkeletonPoseBoneIndex(Mesh->GetSkeletalMeshAsset()->GetSkeleton()->GetReferenceSkeleton().FindBoneIndex(Mesh->GetBoneName(B))), double(Vault->GetPlayLength()) * 0.5, false);
		Want = Want * Local;
	}
	TestTrue(TEXT("the vault's half-way hand"), Mesh->GetBoneTransform(Hand, FTransform::Identity).GetLocation().Equals(Want.GetLocation(), 1.0));
	return true;
}
```

(`FRunlineTestWorld` from `RunlineTestWorld.h`; add the includes for the anim instance and the skeletal mesh.) If the root bone's lock makes the root differ from the raw track, compare from `pelvis` down instead and ledger it.

- [ ] **Step 2: Run** → Expected: build fails (`RunlineBodyAnimInstance.h` not found).
- [ ] **Step 3: Implement** as described.
- [ ] **Step 4: Run** `scripts/test-unreal.ps1 -Filter Runline.Body` → Expected: all Runline.Body tests pass.
- [ ] **Step 5: Commit** `git commit -m "M3b: the body anim instance samples and blends the chosen GASP clips"`

---

### Task 6: The runner: body animation, legs, fallback

**Files:**
- Modify: `Unreal/Runline/Source/Runline/Runner/RunlineRunner.h/.cpp`
- Test: `Unreal/Runline/Source/Runline/Tests/RunnerTests.cpp`

**Interfaces:**
- Consumes: Task 5's `URunlineBodyAnimInstance` (`SetInput`, `HasClips`).
- Produces: `USkeletalMeshComponent* GetLegsMesh() const`; the body runs `URunlineBodyAnimInstance` when `URunlineAnimSet::Get()` resolves Idle and Run, else `ABP_Unarmed`; `Tick` feeds `SetInput(FRunlineBodyInput::From(Runline(), DeltaSeconds * Runline().GameSpeed))` after `UpdateBodyPlacement()`; the body mesh's tick has the actor's tick as a prerequisite.

- [ ] **Step 1: Write the failing tests** (RunnerTests.cpp)

```cpp
// M3b: with the GASP clips the body runs the native anim instance; the legs mesh copies it, only
// she sees it, it casts no shadow, and everything from spine_03 up is hidden.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineRunnerBody, "Runline.Runner.BodyAndLegs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineRunnerBody::RunTest(const FString&)
{
	FRunlineTestWorld TW;
	ARunlineRunner* R = TW.World->SpawnActor<ARunlineRunner>();
	USkeletalMeshComponent* Legs = R->GetLegsMesh();
	TestNotNull(TEXT("legs"), Legs);
	if (!Legs) return false;
	TestTrue(TEXT("only she sees the legs"), Legs->bOnlyOwnerSee);
	TestFalse(TEXT("no shadow from the legs"), Legs->CastShadow);
	TestTrue(TEXT("the legs copy the body"), Legs->GetAttachParent() == R->GetMesh() && Legs->GetAnimClass() && Legs->GetAnimClass()->GetName().Contains(TEXT("ABP_FP_Copy")));
	TestTrue(TEXT("no torso, arms or head on the legs"), Legs->IsBoneHiddenByName(TEXT("spine_03")));
	const bool bClips = URunlineAnimSet::Get() && LoadObject<UAnimSequence>(nullptr, TEXT("/Game/ThirdParty/GASP/A_Run.A_Run"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!bClips) { AddWarning(TEXT("No GASP clips: the GASP half of this test skipped")); return true; }
	TestTrue(TEXT("the body runs the GASP anim instance"), R->GetMesh()->GetAnimInstance() && R->GetMesh()->GetAnimInstance()->IsA<URunlineBodyAnimInstance>());
	return true;
}

// Review Focus 1: no clips, no anim set: the body keeps ABP_Unarmed (a fresh clone).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineRunnerFallback, "Runline.Body.FallsBackWithoutClips",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineRunnerFallback::RunTest(const FString&)
{
	FRunlineTestWorld TW;
	ARunlineRunner::bIgnoreAnimSetForTests = true;
	ARunlineRunner* R = TW.World->SpawnActor<ARunlineRunner>();
	ARunlineRunner::bIgnoreAnimSetForTests = false;
	TestTrue(TEXT("ABP_Unarmed"), R->GetMesh()->GetAnimClass() && R->GetMesh()->GetAnimClass()->GetName().Contains(TEXT("ABP_Unarmed")));
	return true;
}
```

`ARunlineRunner::bIgnoreAnimSetForTests` is a `static bool` (test hook, documented as such) that makes the runner behave as if the anim set were missing.

- [ ] **Step 2: Run** → Expected: build fails (`GetLegsMesh`).
- [ ] **Step 3: Implement.** Constructor: `LegsMesh` (Manny, `ABP_FP_Copy`, attached to the body, `SetOnlyOwnerSee(true)`, `SetCastShadow(false)`, no collision). `PostInitializeComponents`: `LegsMesh->HideBoneByName("spine_03", PBO_None)`; choose the body's anim class (the GASP instance when the set resolves Idle and Run and `!bIgnoreAnimSetForTests`); `GetMesh()->PrimaryComponentTick.AddPrerequisite(this, PrimaryActorTick)` in `BeginPlay`. `Tick`: feed the body anim instance (if it is one).
- [ ] **Step 4: Run** the full Unreal suite and the core suite → Expected: all pass (52 + 1 Movement + Body tests + 1 Runner + fallback).
- [ ] **Step 5: Commit** `git commit -m "M3b: the runner's body plays GASP clips; legs mesh; ABP_Unarmed without the clips"`

---

### Task 7: Pose capture, the look check, parity

**Files:**
- Modify: `RunlineRunGameMode.cpp` (`?poses=1`), `RunlineRunner.h/.cpp` (`void ShowPose(const runline::State&, float Speed)`), `RunlineMovementComponent.h` (`bool bFrozen = false;`: `Advance` returns at once when set; dev only)
- Modify: `CLAUDE.md`
- Test: `Unreal/Runline/Source/Runline/Tests/UnrealWorldTests.cpp` (trace parity is already `Runline.World.EveryBoxTraceReplays`: rerun it)

**Interfaces:**
- Produces: `?poses=1` on any map writes `Saved/Screenshots/Poses/NN_<StateName>_fp.png` and `NN_<StateName>_side.png` for each sample state, then quits.

- [ ] **Step 1: Implement `?poses=1`.** The game mode, after 3 s (shaders), runs through these samples on the runner, 1 s each (the blends settle), freezing the movement component and setting `MutableCore().state` (and `vel` for the speed) for each:
  - idle;
  - run at 4 m/s;
  - sprint at 7 m/s;
  - crouched;
  - air (falling 5 m/s);
  - a heavy landing (a `Land` event at 9);
  - stunned at 30%;
  - roll at 50%;
  - slide at 0.1 s and at 0.6 s;
  - vault over at 50%;
  - vault onto at 50%;
  - step-up at 50%;
  - pull-up at 50%;
  - springboard at 50%;
  - wallrun with the wall on her left and on her right;
  - wallclimb;
  - ledge hang;
  - grab transfer;
  - lying down;
  - getting up at 50%.

  For each it requests two screenshots: her own view, then a side view from an `ACameraActor` 3 m to her right at hip height looking at her (`PlayerController->SetViewTarget`; her owner-no-see body shows from another view target), via `FScreenshotRequest::RequestScreenshot(Path, false, false)`.
- [ ] **Step 2: Run it**

Run: `& 'D:\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe' "$PWD\Unreal\Runline\Runline.uproject" '/Game/Runline/Maps/Rooftops?poses=1' -game -windowed -ResX=1600 -ResY=900`
Expected: 44 PNGs (22 states, two views each) under `Unreal/Runline/Saved/Screenshots/Poses/`.

- [ ] **Step 3: Look at every image.** Read each PNG. For each state, check:
  - **The side view** shows the intended clip: no T-pose, no sinking into or floating over the roof, no limb through the body.
  - **Her own view** shows arms in a plausible place and, looking down, legs.

  Fix what's wrong (a wrong clip in `gasp-clips.txt`, `HoldAt`, a lean sign, the legs' hiding), re-import or rebuild, and capture again until every image passes. Write a one-line verdict per state into the ledger.

- [ ] **Step 4: Parity and suites.** Run: `scripts/test-unreal.ps1` (all) and `scripts/test.ps1` → Expected: all pass, `Runline.World.EveryBoxTraceReplays` included.

- [ ] **Step 5: CLAUDE.md.** Under "Build and test" add:
  - `scripts/import-gasp.ps1 [-Gasp D:\GameAnimationSample]`: imports the GASP clips listed in `Data/Animation/gasp-clips.txt` into the git-ignored `Content/ThirdParty/GASP` and fills `DA_RunlineAnimSet`. Without them the body runs `ABP_Unarmed`.
  - `?poses=1`: screenshots every core state (her view and a side view) to `Saved/Screenshots/Poses`, then quits.

- [ ] **Step 6: Commit** `git commit -m "M3b: ?poses=1 pose capture; every state checked"`

- [ ] **Step 7: Hand the user the pose set and the playtest checklist** (not automated):
  1. Run, sprint, stop and start: do the legs and arms move like a runner?
  2. Jump, fall, light and heavy landings, the roll.
  3. Slide in and out.
  4. Vault, step-up, pull-up, springboard.
  5. Stand-ins: wallrun, wallclimb, ledge hang. These are placeholders until M3c; judge only that they aren't broken.
  6. Look down while running and sliding: legs, with no torso or head poking in.
  7. Compared with before: better or worse?
