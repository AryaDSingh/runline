#pragma once
// Test-only: a transient game world with blocking boxes, for the adapter tests.
#include "Components/BoxComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "RunlineUnits.h"

/// A transient game world for adapter tests, destroyed on scope exit.
struct FRunlineTestWorld
{
	UWorld* World = nullptr;

	FRunlineTestWorld()
	{
		World = UWorld::CreateWorld(EWorldType::Game, false, MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("RunlineTestWorld")));
		FWorldContext& Ctx = GEngine->CreateNewWorldContext(EWorldType::Game);
		Ctx.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		World->BeginPlay();
	}
	~FRunlineTestWorld()
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}
	FRunlineTestWorld(const FRunlineTestWorld&) = delete;
	FRunlineTestWorld& operator=(const FRunlineTestWorld&) = delete;

	/// One blocking UBoxComponent exactly covering B (core metres), tagged with its level order.
	AActor* AddBox(const runline::Aabb& B, int32 Order)
	{
		AActor* A = World->SpawnActor<AActor>();
		UBoxComponent* Box = NewObject<UBoxComponent>(A);
		const FBox U = RunlineUnits::ToUnrealBox(B);
		Box->SetBoxExtent(U.GetExtent(), false);
		Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Box->ComponentTags.Add(FName(*FString::Printf(TEXT("RunlineOrder%d"), Order)));
		A->SetRootComponent(Box);
		Box->SetWorldLocation(U.GetCenter());
		Box->RegisterComponent();
		return A;
	}
};
