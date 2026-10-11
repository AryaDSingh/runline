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
