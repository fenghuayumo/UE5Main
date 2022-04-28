// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "UObject/NameTypes.h"
#include "UObject/GCObject.h"
#include "Folder.h"
#include "LevelInstance/LevelInstanceTypes.h"

#include "LevelInstanceSubsystem.generated.h"

class ILevelInstanceInterface;
class ULevelInstanceEditorObject;
class ULevelStreamingLevelInstance;
class ULevelStreamingLevelInstanceEditor;
class UWorldPartitionSubsystem;
class UBlueprint;

/**
 * ULevelInstanceSubsystem
 */
UCLASS()
class ENGINE_API ULevelInstanceSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	ULevelInstanceSubsystem();
	virtual ~ULevelInstanceSubsystem();
		
	//~ Begin USubsystem Interface.
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
	//~ End USubsystem Interface.
	
	//~ Begin UWorldSubsystem Interface.
	virtual void UpdateStreamingState() override;
	//~ End UWorldSubsystem Interface.

	ILevelInstanceInterface* GetLevelInstance(FLevelInstanceID LevelInstanceID) const;
	FLevelInstanceID RegisterLevelInstance(ILevelInstanceInterface* LevelInstance);
	void UnregisterLevelInstance(ILevelInstanceInterface* LevelInstance);
	void RequestLoadLevelInstance(ILevelInstanceInterface* LevelInstance, bool bUpdate);
	void RequestUnloadLevelInstance(ILevelInstanceInterface* LevelInstance);
	bool IsLoaded(const ILevelInstanceInterface* LevelInstance) const;
	void ForEachLevelInstanceAncestorsAndSelf(AActor* Actor, TFunctionRef<bool(ILevelInstanceInterface*)> Operation) const;

#if WITH_EDITOR
	void Tick();
	void OnExitEditorMode();
	void OnTryExitEditorMode();
	bool OnExitEditorModeInternal(bool bForceExit);
	void PackAllLoadedActors();
	bool CanPackAllLoadedActors() const;

	ILevelInstanceInterface* GetEditingLevelInstance() const;
	bool CanEditLevelInstance(const ILevelInstanceInterface* LevelInstance, FText* OutReason = nullptr) const;
	bool CanCommitLevelInstance(const ILevelInstanceInterface* LevelInstance, bool bDiscardEdits = false, FText* OutReason = nullptr) const;
	void EditLevelInstance(ILevelInstanceInterface* LevelInstance, TWeakObjectPtr<AActor> ContextActorPtr = nullptr);
	bool CommitLevelInstance(ILevelInstanceInterface* LevelInstance, bool bDiscardEdits = false, TSet<FName>* DirtyPackages = nullptr);
	bool IsEditingLevelInstanceDirty(const ILevelInstanceInterface* LevelInstance) const;
	bool IsEditingLevelInstance(const ILevelInstanceInterface* LevelInstance) const { return GetLevelInstanceEdit(LevelInstance) != nullptr; }
	
	bool GetLevelInstanceBounds(const ILevelInstanceInterface* LevelInstance, FBox& OutBounds) const;
	static bool GetLevelInstanceBoundsFromPackage(const FTransform& InstanceTransform, FName LevelPackage, FBox& OutBounds);
	
	void ForEachActorInLevelInstance(const ILevelInstanceInterface* LevelInstance, TFunctionRef<bool(AActor * LevelActor)> Operation) const;
	void ForEachLevelInstanceAncestorsAndSelf(const AActor* Actor, TFunctionRef<bool(const ILevelInstanceInterface*)> Operation) const;
	void ForEachLevelInstanceAncestors(const AActor* Actor, TFunctionRef<bool(const ILevelInstanceInterface*)> Operation) const;
	void ForEachLevelInstanceChild(const ILevelInstanceInterface* LevelInstance, bool bRecursive, TFunctionRef<bool(const ILevelInstanceInterface*)> Operation) const;
	void ForEachLevelInstanceChild(ILevelInstanceInterface* LevelInstance, bool bRecursive, TFunctionRef<bool(ILevelInstanceInterface*)> Operation) const;
	bool HasDirtyChildrenLevelInstances(const ILevelInstanceInterface* LevelInstance) const;
	
	void SetIsHiddenEdLayer(ILevelInstanceInterface* LevelInstance, bool bIsHiddenEdLayer);
	void SetIsTemporarilyHiddenInEditor(ILevelInstanceInterface* LevelInstance, bool bIsHidden);

	bool SetCurrent(ILevelInstanceInterface* LevelInstance) const;
	bool IsCurrent(const ILevelInstanceInterface* LevelInstance) const;
	ILevelInstanceInterface* CreateLevelInstanceFrom(const TArray<AActor*>& ActorsToMove, const FNewLevelInstanceParams& CreationParams);
	bool MoveActorsToLevel(const TArray<AActor*>& ActorsToRemove, ULevel* DestinationLevel, TArray<AActor*>* OutActors = nullptr) const;
	bool MoveActorsTo(ILevelInstanceInterface* LevelInstance, const TArray<AActor*>& ActorsToMove, TArray<AActor*>* OutActors = nullptr);
	bool BreakLevelInstance(ILevelInstanceInterface* LevelInstance, uint32 Levels = 1, TArray<AActor*>* OutMovedActors = nullptr);

	bool CanMoveActorToLevel(const AActor* Actor, FText* OutReason = nullptr) const;
	void OnActorDeleted(AActor* Actor);
	ULevel* GetLevelInstanceLevel(const ILevelInstanceInterface* LevelInstance) const;

	bool LevelInstanceHasLevelScriptBlueprint(const ILevelInstanceInterface* LevelInstance) const;

	ILevelInstanceInterface* GetParentLevelInstance(const AActor* Actor) const;
	void BlockLoadLevelInstance(ILevelInstanceInterface* LevelInstance);
	void BlockUnloadLevelInstance(ILevelInstanceInterface* LevelInstance);
		
	bool HasChildEdit(const ILevelInstanceInterface* LevelInstance) const;

	TArray<ILevelInstanceInterface*> GetLevelInstances(const FString& WorldAssetPackage);
	
	static bool CheckForLoop(const ILevelInstanceInterface* LevelInstance, TArray<TPair<FText, TSoftObjectPtr<UWorld>>>* LoopInfo = nullptr, const ILevelInstanceInterface** LoopStart = nullptr);
	static bool CheckForLoop(const ILevelInstanceInterface* LevelInstance, TSoftObjectPtr<UWorld> WorldAsset, TArray<TPair<FText, TSoftObjectPtr<UWorld>>>* LoopInfo = nullptr, const ILevelInstanceInterface** LoopStart = nullptr);
	static bool CanUseWorldAsset(const ILevelInstanceInterface* LevelInstance, TSoftObjectPtr<UWorld> WorldAsset, FString* OutReason);
	static bool CanUsePackage(FName InPackageName);
#endif

private:
	void BlockOnLoading();
	void LoadLevelInstance(ILevelInstanceInterface* LevelInstance);
	void UnloadLevelInstance(const FLevelInstanceID& LevelInstanceID);
	void ForEachActorInLevel(ULevel* Level, TFunctionRef<bool(AActor * LevelActor)> Operation) const;
	void ForEachLevelInstanceAncestors(AActor* Actor, TFunctionRef<bool(ILevelInstanceInterface*)> Operation) const;
	ILevelInstanceInterface* GetOwningLevelInstance(const ULevel* Level) const;
	
	void RegisterLoadedLevelStreamingLevelInstance(ULevelStreamingLevelInstance* LevelStreaming);

#if WITH_EDITOR
	void RegisterLoadedLevelStreamingLevelInstanceEditor(ULevelStreamingLevelInstanceEditor* LevelStreaming);

	void OnEditChild(FLevelInstanceID LevelInstanceID);
	void OnCommitChild(FLevelInstanceID LevelInstanceID, bool bChildChanged);

	bool ForEachLevelInstanceChildImpl(const ILevelInstanceInterface* LevelInstance, bool bRecursive, TFunctionRef<bool(const ILevelInstanceInterface*)> Operation) const;
	bool ForEachLevelInstanceChildImpl(ILevelInstanceInterface* LevelInstance, bool bRecursive, TFunctionRef<bool(ILevelInstanceInterface*)> Operation) const;

	void BreakLevelInstance_Impl(ILevelInstanceInterface* LevelInstance, uint32 Levels, TArray<AActor*>& OutMovedActors);

	static bool ShouldIgnoreDirtyPackage(UPackage* DirtyPackage, const UWorld* EditingWorld);

	class FLevelInstanceEdit : public FGCObject
	{
	public:
		TObjectPtr<ULevelStreamingLevelInstanceEditor> LevelStreaming;
		TObjectPtr<ULevelInstanceEditorObject> EditorObject;

		FLevelInstanceEdit(ULevelStreamingLevelInstanceEditor* InLevelStreaming, FLevelInstanceID InLevelInstanceID);
		virtual ~FLevelInstanceEdit();

		UWorld* GetEditWorld() const;
		FLevelInstanceID GetLevelInstanceID() const;

		virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
		virtual FString GetReferencerName() const override;

		void GetPackagesToSave(TArray<UPackage*>& OutPackagesToSave) const;
		bool CanDiscard(FText* OutReason = nullptr) const;
		bool HasCommittedChanges() const;
		void MarkCommittedChanges();
	};

	void ResetEdit(TUniquePtr<FLevelInstanceEdit>& InLevelInstanceEdit);
	bool EditLevelInstanceInternal(ILevelInstanceInterface* LevelInstance, TWeakObjectPtr<AActor> ContextActorPtr, const FString& InActorNameToSelect, bool bRecursive);
	bool CommitLevelInstanceInternal(TUniquePtr<FLevelInstanceEdit>& InLevelInstanceEdit, bool bDiscardEdits = false, bool bDiscardOnFailure = false, TSet<FName>* DirtyPackages = nullptr);
	
	const FLevelInstanceEdit* GetLevelInstanceEdit(const ILevelInstanceInterface* LevelInstance) const;
	bool IsLevelInstanceEditDirty(const FLevelInstanceEdit* LevelInstanceEdit) const;
	
	struct FLevelsToRemoveScope
	{
		FLevelsToRemoveScope(ULevelInstanceSubsystem* InOwner);
		~FLevelsToRemoveScope();
		bool IsValid() const { return !bIsBeingDestroyed; }

		TArray<ULevel*> Levels;
		TWeakObjectPtr<ULevelInstanceSubsystem> Owner;
		bool bResetTrans = false;
		bool bIsBeingDestroyed = false;
	};
	
	void RemoveLevelsFromWorld(const TArray<ULevel*>& Levels, bool bResetTrans = true);
#endif
	friend ULevelStreamingLevelInstance;
	friend ULevelStreamingLevelInstanceEditor;

#if WITH_EDITOR
	bool bIsCreatingLevelInstance;
	bool bIsCommittingLevelInstance;
#endif

	struct FLevelInstance
	{
		ULevelStreamingLevelInstance* LevelStreaming = nullptr;
	};

	TMap<ILevelInstanceInterface*, bool> LevelInstancesToLoadOrUpdate;
	TSet<FLevelInstanceID> LevelInstancesToUnload;
	TMap<FLevelInstanceID, FLevelInstance> LevelInstances;
	TMap<FLevelInstanceID, ILevelInstanceInterface*> RegisteredLevelInstances;

#if WITH_EDITORONLY_DATA
	// Optional scope to accelerate level unload by batching them
	TUniquePtr<FLevelsToRemoveScope> LevelsToRemoveScope;

	TUniquePtr<FLevelInstanceEdit> LevelInstanceEdit;

	TMap<FLevelInstanceID, int32> ChildEdits;
#endif
};