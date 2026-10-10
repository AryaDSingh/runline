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

	/// The core is axis-aligned: a rotation or scale a designer gives a course actor is folded into its
	/// box (the box's world bounds become its extent; exact for quarter turns), and a checkpoint's
	/// spawn point and yaw turn with it, so what the editor drew is what the core uses.
	void FoldTransform(AActor& Actor, UBoxComponent& Box, FVector* SpawnPoint = nullptr, float* SpawnYaw = nullptr)
	{
		const FTransform T = Actor.GetActorTransform();
		if (T.GetRotation().IsIdentity(1e-8) && T.GetScale3D().Equals(FVector::OneVector, 1e-9)) return;
		const FBox World = Box.Bounds.GetBox();
		const FVector Spawn = SpawnPoint ? T.TransformPosition(*SpawnPoint) : FVector::ZeroVector;
		if (SpawnYaw) *SpawnYaw = float(FRotator::NormalizeAxis(*SpawnYaw + T.Rotator().Yaw));
		Actor.SetActorTransform(FTransform(FQuat::Identity, World.GetCenter(), FVector::OneVector));
		Box.SetBoxExtent(World.GetExtent(), false);
		if (SpawnPoint) *SpawnPoint = Spawn - World.GetCenter();
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
	FoldTransform(*this, *Box);
	UpdateVisual();
}

void ARunlineBox::UpdateVisual()
{
	// The engine cube is 100 cm: scale it to the box (extent is half the size).
	Mesh->SetRelativeScale3D(Box->GetUnscaledBoxExtent() / 50.0);
	Box->ComponentTags.RemoveAll([](const FName& Tag) { return Tag.ToString().StartsWith(TEXT("RunlineOrder")); });
	if (bSolid)
	{
		Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		// Only level boxes carry an order; a placed one sorts after them (FUnrealWorld).
		if (Order >= 0) Box->ComponentTags.Add(FName(*FString::Printf(TEXT("RunlineOrder%d"), Order)));
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

void ARunlineCheckpoint::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	FoldTransform(*this, *Region, &SpawnPoint, &SpawnYawDegrees);
}

ARunlineFinish::ARunlineFinish()
{
	PrimaryActorTick.bCanEverTick = false;
	Region = MakeRegion(this);
	SetRootComponent(Region);
}

runline::Aabb ARunlineFinish::CoreRegion() const { return CoreBoxOf(*Region); }

void ARunlineFinish::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	FoldTransform(*this, *Region);
}

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
	// Numbered ones in order; ones a designer placed (Index -1) after them, by name; stable for ties.
	const auto Key = [](const ARunlineCheckpoint& C) { return C.Index < 0 ? MAX_int32 : C.Index; };
	Out.StableSort([&Key](const ARunlineCheckpoint& A, const ARunlineCheckpoint& B)
	{
		return Key(A) != Key(B) ? Key(A) < Key(B) : A.CheckpointName < B.CheckpointName;
	});
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
