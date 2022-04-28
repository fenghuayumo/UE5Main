// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelInstance/LevelInstanceEditorInstanceActor.h"
#include "LevelInstance/LevelInstanceInterface.h"
#include "LevelInstance/LevelInstanceSubsystem.h"
#include "Engine/World.h"

ALevelInstanceEditorInstanceActor::ALevelInstanceEditorInstanceActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));
	RootComponent->Mobility = EComponentMobility::Static;
}

#if WITH_EDITOR
AActor* ALevelInstanceEditorInstanceActor::GetSelectionParent() const
{
	if (ULevelInstanceSubsystem* LevelInstanceSubsystem = GetWorld()->GetSubsystem<ULevelInstanceSubsystem>())
	{
		if (ILevelInstanceInterface* LevelInstance = LevelInstanceSubsystem->GetLevelInstance(LevelInstanceID))
		{
			return CastChecked<AActor>(LevelInstance);
		}
	}

	return nullptr;
}

ALevelInstanceEditorInstanceActor* ALevelInstanceEditorInstanceActor::Create(ILevelInstanceInterface* LevelInstance, ULevel* LoadedLevel)
{
	FActorSpawnParameters SpawnParams;
	SpawnParams.OverrideLevel = LoadedLevel;
	SpawnParams.bHideFromSceneOutliner = true;
	SpawnParams.bCreateActorPackage = false;
	SpawnParams.ObjectFlags = RF_Transient;
	SpawnParams.bNoFail = true;

	AActor* LevelInstanceActor = CastChecked<AActor>(LevelInstance);
	ALevelInstanceEditorInstanceActor* InstanceActor = LevelInstanceActor->GetWorld()->SpawnActor<ALevelInstanceEditorInstanceActor>(LevelInstanceActor->GetActorLocation(), LevelInstanceActor->GetActorRotation(), SpawnParams);
	InstanceActor->SetActorScale3D(LevelInstanceActor->GetActorScale3D());
	InstanceActor->SetLevelInstanceID(LevelInstance->GetLevelInstanceID());
	
	for (AActor* LevelActor : LoadedLevel->Actors)
	{
		if (LevelActor && LevelActor->GetAttachParentActor() == nullptr && !LevelActor->IsChildActor() && LevelActor != InstanceActor)
		{
			LevelActor->AttachToActor(InstanceActor, FAttachmentTransformRules::KeepWorldTransform);
		}
	}

	return InstanceActor;
}

#endif