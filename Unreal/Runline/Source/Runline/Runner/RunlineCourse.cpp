#include "RunlineCourse.h"
#include "Components/BoxComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RunlineUnits.h"

DEFINE_LOG_CATEGORY_STATIC(LogRunlineCourse, Log, All);

namespace
{
	/// Mirror's Edge-ish greybox palette: pale roofs, red "runner vision" for things you use.
	FLinearColor ColourOf(const FString& Look)
	{
		if (Look == TEXT("Roof")) return FLinearColor(0.75f, 0.75f, 0.72f);
		if (Look == TEXT("Wall")) return FLinearColor(0.45f, 0.45f, 0.45f);
		if (Look == TEXT("Runner")) return FLinearColor(0.80f, 0.06f, 0.04f);
		if (Look == TEXT("Prop")) return FLinearColor(0.22f, 0.22f, 0.24f);
		if (Look == TEXT("Finish")) return FLinearColor(1.00f, 0.45f, 0.05f);
		if (Look == TEXT("Skyline")) return FLinearColor(0.55f, 0.60f, 0.66f);
		return FLinearColor(1.0f, 0.0f, 1.0f);   // unknown look: loud on purpose
	}

	bool ParseBox(const TArray<FString>& F, int32 At, runline::Aabb& Out)
	{
		if (F.Num() < At + 6) return false;
		Out = {{FCString::Atof(*F[At]), FCString::Atof(*F[At + 1]), FCString::Atof(*F[At + 2])},
		       {FCString::Atof(*F[At + 3]), FCString::Atof(*F[At + 4]), FCString::Atof(*F[At + 5])}};
		return true;
	}
}

ARunlineCourse::ARunlineCourse()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

bool ARunlineCourse::Load(const FString& LevelName)
{
	const FString Path = FPaths::ProjectDir() / TEXT("Data/Levels") / LevelName + TEXT(".csv");
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *Path))
	{
		UE_LOG(LogRunlineCourse, Error, TEXT("Runline course: no level file %s"), *Path);
		return false;
	}
	Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	// The template's prototype grid (a world-aligned 1 m grid: gaps and heights read at a glance),
	// tinted per look; the flat colour material if the template content is missing.
	ShapeMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/LevelPrototyping/Materials/M_PrototypeGrid.M_PrototypeGrid"));
	if (!ShapeMaterial) ShapeMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/LevelPrototyping/Materials/M_FlatCol.M_FlatCol"));
	for (const FString& Line : Lines)
	{
		TArray<FString> F;
		Line.ParseIntoArray(F, TEXT(","), false);
		if (F.Num() == 0) continue;
		runline::Aabb Box;
		if ((F[0] == TEXT("solid") || F[0] == TEXT("decor")) && ParseBox(F, 2, Box))
		{
			AddBox(Box, F[1], F[0] == TEXT("solid"));
		}
		else if (F[0] == TEXT("checkpoint") && F.Num() == 12 && ParseBox(F, 2, Box))
		{
			CheckpointList.Add({F[1], Box, {FCString::Atof(*F[8]), FCString::Atof(*F[9]), FCString::Atof(*F[10])}, FCString::Atof(*F[11])});
		}
		else if (F[0] == TEXT("skipped_fixtures") && F.Num() == 2 && FCString::Atoi(*F[1]) > 0)
		{
			UE_LOG(LogRunlineCourse, Warning, TEXT("Runline course %s: %s fixtures (ziplines, poles, beams) skipped until M4"), *LevelName, *F[1]);
		}
		else if (F[0] != TEXT("level") && F[0] != TEXT("finish") && F[0] != TEXT("skipped_fixtures"))
		{
			UE_LOG(LogRunlineCourse, Error, TEXT("Runline course %s: can't read line '%s'"), *LevelName, *Line);
			return false;
		}
	}
	if (Solids == 0 || CheckpointList.Num() == 0)
	{
		UE_LOG(LogRunlineCourse, Error, TEXT("Runline course %s: no solids or no checkpoints"), *LevelName);
		return false;
	}
	AddLights();
	return true;
}

void ARunlineCourse::AddBox(const runline::Aabb& Box, const FString& Look, bool bSolid)
{
	const FBox U = RunlineUnits::ToUnrealBox(Box);
	if (bSolid)
	{
		UBoxComponent* Collider = NewObject<UBoxComponent>(this);
		Collider->SetBoxExtent(U.GetExtent(), false);
		Collider->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Collider->ComponentTags.Add(FName(*FString::Printf(TEXT("RunlineOrder%d"), Solids)));
		Collider->SetupAttachment(GetRootComponent());
		Collider->RegisterComponent();
		Collider->SetWorldLocation(U.GetCenter());
		++Solids;
	}
	if (!Cube) return;
	UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(this);
	Mesh->SetStaticMesh(Cube);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetupAttachment(GetRootComponent());
	Mesh->RegisterComponent();
	Mesh->SetWorldLocation(U.GetCenter());
	Mesh->SetWorldScale3D(U.GetSize() / 100.0);   // the engine cube is 100 cm
	if (ShapeMaterial)
	{
		TObjectPtr<UMaterialInstanceDynamic>& Mat = LookMaterials.FindOrAdd(Look);
		if (!Mat)
		{
			Mat = UMaterialInstanceDynamic::Create(ShapeMaterial, this);
			const FLinearColor C = ColourOf(Look);
			for (const TCHAR* P : {TEXT("SurfaceColor"), TEXT("Base Color")}) Mat->SetVectorParameterValue(P, C);
			Mat->SetVectorParameterValue(TEXT("TopSurfaceColor"), C * 1.1f);
			for (const TCHAR* P : {TEXT("GridColor"), TEXT("TopGridColor")}) Mat->SetVectorParameterValue(P, C * 0.6f);
			for (const TCHAR* P : {TEXT("SubGridColor"), TEXT("TopSubGridGridColor")}) Mat->SetVectorParameterValue(P, C * 0.8f);
		}
		Mesh->SetMaterial(0, Mat);
	}
}

void ARunlineCourse::AddLights()
{
	UWorld* W = GetWorld();
	ADirectionalLight* Sun = W->SpawnActor<ADirectionalLight>(FVector::ZeroVector, FRotator(-40.0, 20.0, 0.0));
	if (Sun) Sun->GetLightComponent()->SetMobility(EComponentMobility::Movable);
	if (ASkyLight* Sky = W->SpawnActor<ASkyLight>())
	{
		Sky->GetLightComponent()->SetMobility(EComponentMobility::Movable);
		Sky->GetLightComponent()->bRealTimeCapture = true;
		Sky->GetLightComponent()->RecaptureSky();
	}
	AActor* Atmosphere = W->SpawnActor<AActor>();
	USkyAtmosphereComponent* Air = NewObject<USkyAtmosphereComponent>(Atmosphere);
	Atmosphere->SetRootComponent(Air);
	Air->RegisterComponent();
}

int32 ARunlineCourse::CheckpointAt(runline::Vec3 Feet) const
{
	for (int32 I = 0; I < CheckpointList.Num(); ++I)
	{
		const runline::Aabb& R = CheckpointList[I].Region;
		if (Feet.x >= R.min.x && Feet.x <= R.max.x && Feet.y >= R.min.y && Feet.y <= R.max.y && Feet.z >= R.min.z && Feet.z <= R.max.z) return I;
	}
	return INDEX_NONE;
}
