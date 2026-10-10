#include "RunlineCourseActors.h"
#include "Components/BoxComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "RunlineCourseData.h"
#include "RunlineUnits.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/// Mirror's Edge-ish greybox palette: pale roofs, red "runner vision" for things you use.
	FLinearColor ColourOf(ERunlineLook Look)
	{
		switch (Look)
		{
			case ERunlineLook::Roof: return FLinearColor(0.75f, 0.75f, 0.72f);
			case ERunlineLook::Wall: return FLinearColor(0.45f, 0.45f, 0.45f);
			case ERunlineLook::Runner: return FLinearColor(0.80f, 0.06f, 0.04f);
			case ERunlineLook::Prop: return FLinearColor(0.22f, 0.22f, 0.24f);
			case ERunlineLook::Finish: return FLinearColor(1.00f, 0.45f, 0.05f);
			case ERunlineLook::Skyline: return FLinearColor(0.55f, 0.60f, 0.66f);
		}
		return FLinearColor(1.0f, 0.0f, 1.0f);
	}

	UMaterialInterface* GridMaterial()
	{
		// The template's prototype grid (a world-aligned 1 m grid: gaps and heights read at a glance),
		// tinted per look; the flat colour material if the template content is missing.
		static TWeakObjectPtr<UMaterialInterface> Cached;
		if (!Cached.IsValid())
		{
			UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/LevelPrototyping/Materials/M_PrototypeGrid.M_PrototypeGrid"));
			if (!M) M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/LevelPrototyping/Materials/M_FlatCol.M_FlatCol"));
			Cached = M;
		}
		return Cached.Get();
	}

	UBoxComponent* MakeRegion(AActor* Owner)
	{
		UBoxComponent* R = Owner->CreateDefaultSubobject<UBoxComponent>(TEXT("Region"));
		R->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		R->SetHiddenInGame(true);
		R->ShapeColor = FColor(255, 140, 20);
		return R;
	}

	runline::Aabb CoreBoxOf(const UBoxComponent& B)
	{
		const FVector C = B.GetComponentLocation();
		const FVector E = B.GetScaledBoxExtent().GetAbs();
		return RunlineUnits::ToCoreBox(FBox(C - E, C + E));
	}

	void PlaceBox(AActor& Actor, UBoxComponent& Box, const runline::Aabb& Core)
	{
		const FBox U = RunlineUnits::ToUnrealBox(Core);
		Box.SetBoxExtent(U.GetExtent(), false);
		Actor.SetActorLocation(U.GetCenter());
	}
}

// ---------------------------------------------------------------- ARunlineBox

ARunlineBox::ARunlineBox()
{
	PrimaryActorTick.bCanEverTick = false;
	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Box->SetBoxExtent(FVector(50.0));
	SetRootComponent(Box);
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Box);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	Mesh->SetStaticMesh(Cube.Object);
}

ERunlineLook ARunlineBox::LookFromName(const FString& Name)
{
	if (Name == TEXT("Wall")) return ERunlineLook::Wall;
	if (Name == TEXT("Runner")) return ERunlineLook::Runner;
	if (Name == TEXT("Prop")) return ERunlineLook::Prop;
	if (Name == TEXT("Finish")) return ERunlineLook::Finish;
	if (Name == TEXT("Skyline")) return ERunlineLook::Skyline;
	return ERunlineLook::Roof;
}

void ARunlineBox::SetFromCore(const runline::Aabb& B)
{
	PlaceBox(*this, *Box, B);
	UpdateVisual();
}

runline::Aabb ARunlineBox::CoreBox() const { return CoreBoxOf(*Box); }

void ARunlineBox::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UpdateVisual();
}

void ARunlineBox::UpdateVisual()
{
	// The engine cube is 100 cm: scale it to the box (extent is half the size).
	Mesh->SetRelativeScale3D(Box->GetUnscaledBoxExtent() / 50.0);
	Box->ComponentTags.Reset();
	if (bSolid)
	{
		Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Box->ComponentTags.Add(FName(*FString::Printf(TEXT("RunlineOrder%d"), Order)));
	}
	else
	{
		Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	if (UMaterialInterface* Base = GridMaterial())
	{
		UMaterialInstanceDynamic* Mat = UMaterialInstanceDynamic::Create(Base, this);
		const FLinearColor C = ColourOf(Look);
		for (const TCHAR* P : {TEXT("SurfaceColor"), TEXT("Base Color")}) Mat->SetVectorParameterValue(P, C);
		Mat->SetVectorParameterValue(TEXT("TopSurfaceColor"), C * 1.1f);
		for (const TCHAR* P : {TEXT("GridColor"), TEXT("TopGridColor")}) Mat->SetVectorParameterValue(P, C * 0.6f);
		for (const TCHAR* P : {TEXT("SubGridColor"), TEXT("TopSubGridGridColor")}) Mat->SetVectorParameterValue(P, C * 0.8f);
		Mesh->SetMaterial(0, Mat);
	}
}

// ---------------------------------------------------------------- checkpoints and the finish

ARunlineCheckpoint::ARunlineCheckpoint()
{
	PrimaryActorTick.bCanEverTick = false;
	Region = MakeRegion(this);
	SetRootComponent(Region);
}

runline::Aabb ARunlineCheckpoint::CoreRegion() const { return CoreBoxOf(*Region); }
runline::Vec3 ARunlineCheckpoint::CoreSpawn() const { return RunlineUnits::ToCorePosition(GetActorLocation() + SpawnPoint); }
float ARunlineCheckpoint::CoreYaw() const { return RunlineUnits::ToCoreYaw(SpawnYawDegrees); }

ARunlineFinish::ARunlineFinish()
{
	PrimaryActorTick.bCanEverTick = false;
	Region = MakeRegion(this);
	SetRootComponent(Region);
}

runline::Aabb ARunlineFinish::CoreRegion() const { return CoreBoxOf(*Region); }

// ---------------------------------------------------------------- spawning

void RunlineCourses::Spawn(UWorld* World, const FRunlineCourseData& Data, bool bLights)
{
	for (const FRunlineCourseData::FBoxEntry& E : Data.Boxes)
	{
		ARunlineBox* A = World->SpawnActorDeferred<ARunlineBox>(ARunlineBox::StaticClass(), FTransform::Identity);
		A->Look = ARunlineBox::LookFromName(E.Look);
		A->bSolid = E.bSolid;
		A->Order = E.Order;
		const FBox U = RunlineUnits::ToUnrealBox(E.Box);
		A->Box->SetBoxExtent(U.GetExtent(), false);
		A->FinishSpawning(FTransform(U.GetCenter()));
		A->UpdateVisual();
	}
	for (int32 I = 0; I < Data.Checkpoints.Num(); ++I)
	{
		const FRunlineCourseData::FCheckpointEntry& E = Data.Checkpoints[I];
		ARunlineCheckpoint* C = World->SpawnActor<ARunlineCheckpoint>();
		PlaceBox(*C, *C->Region, E.Region);
		C->CheckpointName = E.Name;
		C->Index = I;
		C->SpawnPoint = RunlineUnits::ToUnrealPosition(E.Spawn) - C->GetActorLocation();
		C->SpawnYawDegrees = float(RunlineUnits::ToUnrealRotator(E.Yaw, 0.0f).Yaw);
	}
	if (Data.Finish)
	{
		ARunlineFinish* F = World->SpawnActor<ARunlineFinish>();
		PlaceBox(*F, *F->Region, *Data.Finish);
	}
	if (!bLights) return;
	ADirectionalLight* Sun = World->SpawnActor<ADirectionalLight>(FVector::ZeroVector, FRotator(-40.0, 20.0, 0.0));
	if (Sun) Sun->GetLightComponent()->SetMobility(EComponentMobility::Movable);
	if (ASkyLight* Sky = World->SpawnActor<ASkyLight>())
	{
		Sky->GetLightComponent()->SetMobility(EComponentMobility::Movable);
		Sky->GetLightComponent()->bRealTimeCapture = true;
		Sky->GetLightComponent()->RecaptureSky();
	}
	AActor* Atmosphere = World->SpawnActor<AActor>();
	USkyAtmosphereComponent* Air = NewObject<USkyAtmosphereComponent>(Atmosphere, TEXT("SkyAtmosphere"));
	Atmosphere->SetRootComponent(Air);
	Atmosphere->AddInstanceComponent(Air);
	Air->RegisterComponent();
}

TArray<ARunlineCheckpoint*> RunlineCourses::Checkpoints(const UWorld* World)
{
	TArray<ARunlineCheckpoint*> Out;
	for (TActorIterator<ARunlineCheckpoint> It(const_cast<UWorld*>(World)); It; ++It) Out.Add(*It);
	Out.Sort([](const ARunlineCheckpoint& A, const ARunlineCheckpoint& B) { return A.Index < B.Index; });
	return Out;
}

ARunlineFinish* RunlineCourses::Finish(const UWorld* World)
{
	TActorIterator<ARunlineFinish> It(const_cast<UWorld*>(World));
	return It ? *It : nullptr;
}

bool RunlineCourses::Inside(const runline::Aabb& R, runline::Vec3 P)
{
	return P.x >= R.min.x && P.x <= R.max.x && P.y >= R.min.y && P.y <= R.max.y && P.z >= R.min.z && P.z <= R.max.z;
}

int32 RunlineCourses::CheckpointAt(const TArray<ARunlineCheckpoint*>& Cps, runline::Vec3 Feet)
{
	for (int32 I = 0; I < Cps.Num(); ++I)
		if (Inside(Cps[I]->CoreRegion(), Feet)) return I;
	return INDEX_NONE;
}
