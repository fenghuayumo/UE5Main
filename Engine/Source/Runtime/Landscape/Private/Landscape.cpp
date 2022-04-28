// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
Landscape.cpp: Terrain rendering
=============================================================================*/

#include "Landscape.h"

#include "Serialization/MemoryWriter.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/MemoryReader.h"
#include "UObject/RenderingObjectVersion.h"
#include "UObject/UObjectIterator.h"
#include "UObject/PropertyPortFlags.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/DevObjectVersion.h"
#include "UObject/LinkerLoad.h"
#include "LandscapeStreamingProxy.h"
#include "LandscapeInfo.h"
#include "LightMap.h"
#include "Engine/MapBuildDataRegistry.h"
#include "ShadowMap.h"
#include "LandscapeComponent.h"
#include "LandscapeLayerInfoObject.h"
#include "LandscapeInfoMap.h"
#include "EditorSupportDelegates.h"
#include "LandscapeMeshProxyComponent.h"
#include "LandscapeRender.h"
#include "LandscapeRenderMobile.h"
#include "Logging/TokenizedMessage.h"
#include "Logging/MessageLog.h"
#include "Misc/UObjectToken.h"
#include "Misc/MapErrors.h"
#include "Misc/PackageSegment.h"
#include "DerivedDataCacheInterface.h"
#include "Interfaces/ITargetPlatform.h"
#include "LandscapeMeshCollisionComponent.h"
#include "Materials/Material.h"
#include "LandscapeMaterialInstanceConstant.h"
#include "Engine/CollisionProfile.h"
#include "LandscapeMeshProxyActor.h"
#include "Materials/MaterialExpressionLandscapeLayerWeight.h"
#include "Materials/MaterialExpressionLandscapeLayerSwitch.h"
#include "Materials/MaterialExpressionLandscapeLayerSample.h"
#include "Materials/MaterialExpressionLandscapeLayerBlend.h"
#include "Materials/MaterialExpressionLandscapeVisibilityMask.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProfilingDebugging/CookStats.h"
#include "ILandscapeSplineInterface.h"
#include "LandscapeSplineActor.h"
#include "LandscapeSplinesComponent.h"
#include "EngineGlobals.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "ComponentRecreateRenderStateContext.h"
#include "LandscapeWeightmapUsage.h"
#include "LandscapeSubsystem.h"
#include "Streaming/LandscapeMeshMobileUpdate.h"
#include "ContentStreaming.h"
#include "UObject/ObjectSaveContext.h"

#if WITH_EDITOR
#include "LandscapeEdit.h"
#include "MaterialUtilities.h"
#include "Editor.h"
#include "Algo/Transform.h"
#include "Algo/BinarySearch.h"
#include "Engine/Texture2D.h"
#endif
#include "LandscapeVersion.h"
#include "UObject/FortniteMainBranchObjectVersion.h"
#include "LandscapeDataAccess.h"
#include "UObject/EditorObjectVersion.h"
#include "Algo/BinarySearch.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionHelpers.h"
#include "WorldPartition/WorldPartitionHandle.h"
#include "WorldPartition/Landscape/LandscapeActorDesc.h"
#include "WorldPartition/Landscape/LandscapeSplineActorDesc.h"
#if WITH_EDITOR
#include "Rendering/StaticLightingSystemInterface.h"
#include "Misc/ScopedSlowTask.h"
#endif

/** Landscape stats */

DEFINE_STAT(STAT_LandscapeDynamicDrawTime);
DEFINE_STAT(STAT_LandscapeStaticDrawLODTime);
DEFINE_STAT(STAT_LandscapeVFDrawTimeVS);
DEFINE_STAT(STAT_LandscapeInitViewCustomData);
DEFINE_STAT(STAT_LandscapePostInitViewCustomData);
DEFINE_STAT(STAT_LandscapeComputeCustomMeshBatchLOD);
DEFINE_STAT(STAT_LandscapeComputeCustomShadowMeshBatchLOD);
DEFINE_STAT(STAT_LandscapeVFDrawTimePS);
DEFINE_STAT(STAT_LandscapeComponentRenderPasses);
DEFINE_STAT(STAT_LandscapeTessellatedShadowCascade);
DEFINE_STAT(STAT_LandscapeTessellatedComponents);
DEFINE_STAT(STAT_LandscapeComponentUsingSubSectionDrawCalls);
DEFINE_STAT(STAT_LandscapeDrawCalls);
DEFINE_STAT(STAT_LandscapeTriangles);

DEFINE_STAT(STAT_LandscapeLayersRegenerate_RenderThread);
DEFINE_STAT(STAT_LandscapeLayersRegenerateDrawCalls);

DEFINE_STAT(STAT_LandscapeLayersRegenerateHeightmaps);
DEFINE_STAT(STAT_LandscapeLayersResolveHeightmaps);
DEFINE_STAT(STAT_LandscapeLayersResolveTexture);

DEFINE_STAT(STAT_LandscapeLayersUpdateMaterialInstance);
DEFINE_STAT(STAT_LandscapeLayersReallocateWeightmaps);

DEFINE_STAT(STAT_LandscapeLayersResolveWeightmaps);
DEFINE_STAT(STAT_LandscapeLayersRegenerateWeightmaps);

DEFINE_STAT(STAT_LandscapeVertexMem);
DEFINE_STAT(STAT_LandscapeHoleMem);
DEFINE_STAT(STAT_LandscapeComponentMem);

#if ENABLE_COOK_STATS
namespace LandscapeCookStats
{
	static FCookStats::FDDCResourceUsageStats UsageStats;
	static FCookStatsManager::FAutoRegisterCallback RegisterCookStats([](FCookStatsManager::AddStatFuncRef AddStat)
	{
		UsageStats.LogStats(AddStat, TEXT("Landscape.Usage"), TEXT(""));
	});
}
#endif

// Set this to 0 to disable landscape cooking and thus disable it on device.
#define ENABLE_LANDSCAPE_COOKING 1

static bool UseMobileLandscapeMesh(const ITargetPlatform* TargetPlatform)
{
	return TargetPlatform->SupportsFeature(ETargetPlatformFeatures::MobileLandscapeMesh);
}

#define LOCTEXT_NAMESPACE "Landscape"

static void PrintNumLandscapeShadows()
{
	int32 NumComponents = 0;
	int32 NumShadowCasters = 0;
	for (TObjectIterator<ULandscapeComponent> It; It; ++It)
	{
		ULandscapeComponent* LC = *It;
		NumComponents++;
		if (LC->CastShadow && LC->bCastDynamicShadow)
		{
			NumShadowCasters++;
		}
	}
	UE_LOG(LogConsoleResponse, Display, TEXT("%d/%d landscape components cast shadows"), NumShadowCasters, NumComponents);
}

FAutoConsoleCommand CmdPrintNumLandscapeShadows(
	TEXT("ls.PrintNumLandscapeShadows"),
	TEXT("Prints the number of landscape components that cast shadows."),
	FConsoleCommandDelegate::CreateStatic(PrintNumLandscapeShadows)
	);

ULandscapeComponent::ULandscapeComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
#if WITH_EDITORONLY_DATA
	, CachedEditingLayerData(nullptr)
	, LayerUpdateFlagPerMode(0)
	, bPendingCollisionDataUpdate(false)
	, bPendingLayerCollisionDataUpdate(false)
	, WeightmapsHash(0)
	, SplineHash(0)
	, PhysicalMaterialHash(0)
#endif
	, GrassData(MakeShareable(new FLandscapeComponentGrassData()))
	, ChangeTag(0)
{
	SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	SetGenerateOverlapEvents(false);

	bUseAsOccluder = true;
	bAllowCullDistanceVolume = false;
	CollisionMipLevel = 0;
	StaticLightingResolution = 0.f; // Default value 0 means no overriding

	MaterialInstances.AddDefaulted(); // make sure we always have a MaterialInstances[0]	
	LODIndexToMaterialIndex.AddDefaulted(); // make sure we always have a MaterialInstances[0]	

	HeightmapScaleBias = FVector4(0.0f, 0.0f, 0.0f, 1.0f);
	WeightmapScaleBias = FVector4(0.0f, 0.0f, 0.0f, 1.0f);

	bBoundsChangeTriggersStreamingDataRebuild = true;
	ForcedLOD = -1;
	LODBias = 0;
#if WITH_EDITORONLY_DATA
	LightingLODBias = -1; // -1 Means automatic LOD calculation based on ForcedLOD + LODBias
#endif

	Mobility = EComponentMobility::Static;

#if WITH_EDITORONLY_DATA
	EditToolRenderData = FLandscapeEditToolRenderData();
#endif

	// We don't want to load this on the server, this component is for graphical purposes only
	AlwaysLoadOnServer = false;

	// Default sort priority of landscape to -1 so that it will default to the first thing rendered in any runtime virtual texture
	TranslucencySortPriority = -1;

	LODStreamingProxy = CreateDefaultSubobject<ULandscapeLODStreamingProxy>(TEXT("LandscapeLODStreamingProxy"));
}

int32 ULandscapeComponent::GetMaterialInstanceCount(bool InDynamic) const
{
	ALandscapeProxy* Actor = GetLandscapeProxy();

	if (Actor != nullptr && Actor->bUseDynamicMaterialInstance && InDynamic)
	{
		return MaterialInstancesDynamic.Num();
	}

	return MaterialInstances.Num();
}

UMaterialInstance* ULandscapeComponent::GetMaterialInstance(int32 InIndex, bool InDynamic) const
{
	ALandscapeProxy* Actor = GetLandscapeProxy();

	if (Actor != nullptr && Actor->bUseDynamicMaterialInstance && InDynamic)
	{
		check(MaterialInstancesDynamic.IsValidIndex(InIndex));
		return MaterialInstancesDynamic[InIndex];
	}

	check(MaterialInstances.IsValidIndex(InIndex));
	return MaterialInstances[InIndex];
}

UMaterialInstanceDynamic* ULandscapeComponent::GetMaterialInstanceDynamic(int32 InIndex) const
{
	ALandscapeProxy* Actor = GetLandscapeProxy();

	if (Actor != nullptr && Actor->bUseDynamicMaterialInstance)
	{
		if (MaterialInstancesDynamic.IsValidIndex(InIndex))
		{
			return MaterialInstancesDynamic[InIndex];
		}
	}

	return nullptr;
}


#if WITH_EDITOR
void ULandscapeComponent::BeginCacheForCookedPlatformData(const ITargetPlatform* TargetPlatform)
{
	Super::BeginCacheForCookedPlatformData(TargetPlatform);

	if (UseMobileLandscapeMesh(TargetPlatform) && !HasAnyFlags(RF_ClassDefaultObject))
	{
		CheckGenerateLandscapePlatformData(true, TargetPlatform);
	}
}

void ALandscapeProxy::CheckGenerateLandscapePlatformData(bool bIsCooking, const ITargetPlatform* TargetPlatform)
{
	for (ULandscapeComponent* Component : LandscapeComponents)
	{
		Component->CheckGenerateLandscapePlatformData(bIsCooking, TargetPlatform);
	}
}

void ULandscapeComponent::CheckGenerateLandscapePlatformData(bool bIsCooking, const ITargetPlatform* TargetPlatform)
{
#if ENABLE_LANDSCAPE_COOKING

	// Regenerate platform data only when it's missing or there is a valid hash-mismatch.

	FBufferArchive ComponentStateAr;
	SerializeStateHashes(ComponentStateAr);

	if (bIsCooking && TargetPlatform && TargetPlatform->SupportsFeature(ETargetPlatformFeatures::LandscapeMeshLODStreaming))
	{
		int32 MaxLODClamp = GetLandscapeProxy()->MaxLODLevel;
		MaxLODClamp = MaxLODClamp < 0 ? INT32_MAX : MaxLODClamp;
		ComponentStateAr << MaxLODClamp;
	}
	else
	{
		int32 DummyMaxLODClamp = INDEX_NONE;
		ComponentStateAr << DummyMaxLODClamp;
	}

	// Serialize the version guid as part of the hash so we can invalidate DDC data if needed
	FString Version = FDevSystemGuids::GetSystemGuid(FDevSystemGuids::Get().LANDSCAPE_MOBILE_COOK_VERSION).ToString();
	ComponentStateAr << Version;

	uint32 Hash[5];
	FSHA1::HashBuffer(ComponentStateAr.GetData(), ComponentStateAr.Num(), (uint8*)Hash);
	FGuid NewSourceHash = FGuid(Hash[0] ^ Hash[4], Hash[1], Hash[2], Hash[3]);

	bool bHashMismatch = MobileDataSourceHash != NewSourceHash;
	bool bMissingVertexData = !PlatformData.HasValidPlatformData();
	bool bMissingPixelData = MobileMaterialInterfaces.Num() == 0 || MobileWeightmapTextures.Num() == 0 || MaterialPerLOD.Num() == 0;

	bool bRegenerateVertexData = bMissingVertexData || bMissingPixelData || bHashMismatch;

	if (bRegenerateVertexData)
	{
		if (bIsCooking)
		{
			// The DDC is only useful when cooking (see else).

			COOK_STAT(auto Timer = LandscapeCookStats::UsageStats.TimeSyncWork());
// Temporarily disabling DDC use. See FORT-317076.
// 			if (PlatformData.LoadFromDDC(NewSourceHash, this))
// 			{
// 				COOK_STAT(Timer.AddHit(PlatformData.GetPlatformDataSize()));
// 			}
// 			else
			{
				GeneratePlatformVertexData(TargetPlatform);
// 				PlatformData.SaveToDDC(NewSourceHash, this);
				COOK_STAT(Timer.AddMiss(PlatformData.GetPlatformDataSize()));
			}
		}
		else
		{
			// When not cooking (e.g. mobile preview) DDC data isn't sufficient to 
			// display correctly, so the platform vertex data must be regenerated.

			GeneratePlatformVertexData(TargetPlatform);
		}
	}

	bool bRegeneratePixelData = bMissingPixelData || bHashMismatch;

	if (bRegeneratePixelData)
	{
		GeneratePlatformPixelData(bIsCooking, TargetPlatform);
	}

	MobileDataSourceHash = NewSourceHash;

#endif
}
#endif

void ULandscapeComponent::SetForcedLOD(int32 InForcedLOD)
{
	SetLOD(/*bForced = */true, InForcedLOD);
}

void ULandscapeComponent::SetLODBias(int32 InLODBias)
{
	SetLOD(/*bForced = */false, InLODBias);
}

void ULandscapeComponent::SetLOD(bool bForcedLODChanged, int32 InLODValue)
{
	if (bForcedLODChanged)
	{
		ForcedLOD = InLODValue;
		if (ForcedLOD >= 0)
		{
			ForcedLOD = FMath::Clamp<int32>(ForcedLOD, 0, FMath::CeilLogTwo(SubsectionSizeQuads + 1) - 1);
		}
		else
		{
			ForcedLOD = -1;
		}
	}
	else
	{
		int32 MaxLOD = FMath::CeilLogTwo(SubsectionSizeQuads + 1) - 1;
		LODBias = FMath::Clamp<int32>(InLODValue, -MaxLOD, MaxLOD);
	}

	InvalidateLightingCache();
	MarkRenderStateDirty();

#if WITH_EDITOR
	// Update neighbor components for lighting cache (only relevant in the editor ATM) : 
	ULandscapeInfo* Info = GetLandscapeInfo();
	if (Info)
	{
		FIntPoint ComponentBase = GetSectionBase() / ComponentSizeQuads;
		FIntPoint LandscapeKey[8] =
		{
			ComponentBase + FIntPoint(-1, -1),
			ComponentBase + FIntPoint(+0, -1),
			ComponentBase + FIntPoint(+1, -1),
			ComponentBase + FIntPoint(-1, +0),
			ComponentBase + FIntPoint(+1, +0),
			ComponentBase + FIntPoint(-1, +1),
			ComponentBase + FIntPoint(+0, +1),
			ComponentBase + FIntPoint(+1, +1)
		};

		for (int32 Idx = 0; Idx < 8; ++Idx)
		{
			ULandscapeComponent* Comp = Info->XYtoComponentMap.FindRef(LandscapeKey[Idx]);
			if (Comp)
			{
				Comp->Modify();
				Comp->InvalidateLightingCache();
				Comp->MarkRenderStateDirty();
			}
		}
	}
#endif // WITH_EDITOR
}

void ULandscapeComponent::Serialize(FArchive& Ar)
{
	LLM_SCOPE(ELLMTag::Landscape);
	Ar.UsingCustomVersion(FRenderingObjectVersion::GUID);
	Ar.UsingCustomVersion(FFortniteMainBranchObjectVersion::GUID);
	Ar.UsingCustomVersion(FEditorObjectVersion::GUID);

#if WITH_EDITOR
	if (Ar.IsCooking() && !HasAnyFlags(RF_ClassDefaultObject) && UseMobileLandscapeMesh(Ar.CookingTarget()))
	{
		// for -oldcook:
		// the old cooker calls BeginCacheForCookedPlatformData after the package export set is tagged, so the mobile material doesn't get saved, so we have to do CheckGenerateLandscapePlatformData in serialize
		// the new cooker clears the texture source data before calling serialize, causing GeneratePlatformVertexData to crash, so we have to do CheckGenerateLandscapePlatformData in BeginCacheForCookedPlatformData
		CheckGenerateLandscapePlatformData(true, Ar.CookingTarget());
	}

	// Avoid the archiver in the PIE duplicate writer case because we want to share landscape textures & materials
	if (Ar.GetPortFlags() & PPF_DuplicateForPIE)
	{
		if (Ar.IsLoading())
		{
			Super::Serialize(Ar);
		}

		TArray<UObject**> TexturesAndMaterials;
		TexturesAndMaterials.Add((UObject**)&HeightmapTexture);
		TexturesAndMaterials.Add((UObject**)&XYOffsetmapTexture);
		for (TObjectPtr<UTexture2D>& WeightmapTexture : WeightmapTextures)
		{
			TexturesAndMaterials.Add((UObject**)&static_cast<UTexture2D*&>(WeightmapTexture));
		}
		for (TObjectPtr<UTexture2D>& MobileWeightmapTexture : MobileWeightmapTextures)
		{
			TexturesAndMaterials.Add((UObject**)&static_cast<UTexture2D*&>(MobileWeightmapTexture));
		}
		for (auto& ItPair : LayersData)
		{
			FLandscapeLayerComponentData& LayerComponentData = ItPair.Value;
			TexturesAndMaterials.Add((UObject**)&LayerComponentData.HeightmapData.Texture);
			for (UE_TRANSITIONAL_OBJECT_PTR(UTexture2D)& WeightmapTexture : LayerComponentData.WeightmapData.Textures)
			{
				TexturesAndMaterials.Add((UObject**)&static_cast<UTexture2D*&>(WeightmapTexture));
			}
		}
		for (TObjectPtr<UMaterialInstanceConstant>& MaterialInstance : MaterialInstances)
		{
			TexturesAndMaterials.Add((UObject**)&static_cast<UMaterialInstanceConstant*&>(MaterialInstance));
		}
		for (TObjectPtr<UMaterialInterface>& MobileMaterialInterface : MobileMaterialInterfaces)
		{
			TexturesAndMaterials.Add((UObject**)(&static_cast<UMaterialInterface*&>(MobileMaterialInterface)));
		}
		for (TObjectPtr<UMaterialInstanceConstant>& MobileCombinationMaterialInstance : MobileCombinationMaterialInstances)
		{
			TexturesAndMaterials.Add((UObject**)&static_cast<UMaterialInstanceConstant*&>(MobileCombinationMaterialInstance));
		}

		if (Ar.IsSaving())
		{
			TArray<UObject*> BackupTexturesAndMaterials;
			BackupTexturesAndMaterials.AddZeroed(TexturesAndMaterials.Num());
			for (int i = 0; i < TexturesAndMaterials.Num(); ++i)
			{
				Exchange(*TexturesAndMaterials[i], BackupTexturesAndMaterials[i]);
			}

			Super::Serialize(Ar);

			for (int i = 0; i < TexturesAndMaterials.Num(); ++i)
			{
				Exchange(*TexturesAndMaterials[i], BackupTexturesAndMaterials[i]);
			}
		}
		// Manually serialize pointers
		for (UObject** Object : TexturesAndMaterials)
		{
			Ar.Serialize(Object, sizeof(UObject*));
		}
	}
	else if (Ar.IsCooking() && !HasAnyFlags(RF_ClassDefaultObject))
	{
		const bool bUseMobileLandscapeMesh = UseMobileLandscapeMesh(Ar.CookingTarget());
		
		if (bUseMobileLandscapeMesh && !Ar.CookingTarget()->SupportsFeature(ETargetPlatformFeatures::DeferredRendering))
		{
			// These are used for SM5 rendering or if MobileLandscapeMesh is disabled 
			UTexture2D* BackupHeightmapTexture = nullptr;
			UTexture2D* BackupXYOffsetmapTexture = nullptr;
			TArray<UMaterialInstanceConstant*> BackupMaterialInstances;
			TArray<UTexture2D*> BackupWeightmapTextures;

			Exchange(HeightmapTexture, BackupHeightmapTexture);
			Exchange(BackupXYOffsetmapTexture, XYOffsetmapTexture);
			Exchange(BackupMaterialInstances, MaterialInstances);
			Exchange(BackupWeightmapTextures, WeightmapTextures);

			Super::Serialize(Ar);

			Exchange(HeightmapTexture, BackupHeightmapTexture);
			Exchange(BackupXYOffsetmapTexture, XYOffsetmapTexture);
			Exchange(BackupMaterialInstances, MaterialInstances);
			Exchange(BackupWeightmapTextures, WeightmapTextures);
		}
		else if (!bUseMobileLandscapeMesh)
		{
			// These properties are only when MobileLandscapeMesh is enabled so we back them up and clear them before serializing them.
			TArray<UMaterialInterface*> BackupMobileMaterialInterfaces;
			TArray<UTexture2D*> BackupMobileWeightmapTextures;

			Exchange(MobileMaterialInterfaces, BackupMobileMaterialInterfaces);
			Exchange(MobileWeightmapTextures, BackupMobileWeightmapTextures);

			Super::Serialize(Ar);

			Exchange(MobileMaterialInterfaces, BackupMobileMaterialInterfaces);
			Exchange(MobileWeightmapTextures, BackupMobileWeightmapTextures);
		}
		else
		{
			// Serialize both mobile landscape mesh and heightmap properties
			Super::Serialize(Ar);
		}
	}
	else
#endif
	{
		Super::Serialize(Ar);
	}

	if (Ar.IsLoading() && Ar.CustomVer(FRenderingObjectVersion::GUID) < FRenderingObjectVersion::MapBuildDataSeparatePackage)
	{
		FMeshMapBuildData* LegacyMapBuildData = new FMeshMapBuildData();
		Ar << LegacyMapBuildData->LightMap;
		Ar << LegacyMapBuildData->ShadowMap;
		LegacyMapBuildData->IrrelevantLights = IrrelevantLights_DEPRECATED;

		FMeshMapBuildLegacyData LegacyComponentData;
		LegacyComponentData.Data.Emplace(MapBuildDataId, LegacyMapBuildData);
		GComponentsWithLegacyLightmaps.AddAnnotation(this, MoveTemp(LegacyComponentData));
	}

	if (Ar.IsLoading() && Ar.CustomVer(FFortniteMainBranchObjectVersion::GUID) < FFortniteMainBranchObjectVersion::NewLandscapeMaterialPerLOD)
	{
		if (MobileMaterialInterface_DEPRECATED != nullptr)
		{
			MobileMaterialInterfaces.AddUnique(MobileMaterialInterface_DEPRECATED);
		}

#if WITH_EDITORONLY_DATA
		if (MobileCombinationMaterialInstance_DEPRECATED != nullptr)
		{
			MobileCombinationMaterialInstances.AddUnique(MobileCombinationMaterialInstance_DEPRECATED);
		}
#endif
	}

	if (Ar.UEVer() >= VER_UE4_SERIALIZE_LANDSCAPE_GRASS_DATA)
	{
		// Share the shared ref so PIE can share this data
		if (Ar.GetPortFlags() & PPF_DuplicateForPIE)
		{
			if (Ar.IsSaving())
			{
				PTRINT GrassDataPointer = (PTRINT)&GrassData;
				Ar << GrassDataPointer;
			}
			else
			{
				PTRINT GrassDataPointer;
				Ar << GrassDataPointer;
				// Duplicate shared reference
				GrassData = *(TSharedRef<FLandscapeComponentGrassData, ESPMode::ThreadSafe>*)GrassDataPointer;
			}
		}
		else
		{
			Ar << GrassData.Get();
		}

		// When loading or saving a component, validate that grass data is valid : 
		checkf(IsTemplate() || !Ar.IsLoading() || !Ar.IsSaving() || GrassData->HasValidData(), TEXT("If this asserts, then serialization occurred on grass data that wasn't properly loaded/computed. It's a problem"));
	}

#if WITH_EDITOR
	if (Ar.IsTransacting())
	{
		Ar << EditToolRenderData.SelectedType;
	}
#endif

	bool bCooked = false;

	if (Ar.UEVer() >= VER_UE4_LANDSCAPE_PLATFORMDATA_COOKING && !HasAnyFlags(RF_ClassDefaultObject))
	{
		bCooked = Ar.IsCooking() || (FPlatformProperties::RequiresCookedData() && Ar.IsSaving());
		// This is needed when loading cooked data, to know to serialize differently
		Ar << bCooked;
	}

	if (FPlatformProperties::RequiresCookedData() && !bCooked && Ar.IsLoading())
	{
		UE_LOG(LogLandscape, Fatal, TEXT("This platform requires cooked packages, and this landscape does not contain cooked data %s."), *GetName());
	}

#if ENABLE_LANDSCAPE_COOKING
	if (bCooked)
	{
		bool bCookedMobileData = Ar.IsCooking() && UseMobileLandscapeMesh(Ar.CookingTarget());
		Ar << bCookedMobileData;

		// Saving for cooking path
		if (bCookedMobileData)
		{
			if (Ar.IsCooking())
			{
				check(PlatformData.HasValidPlatformData());
			}
			PlatformData.Serialize(Ar, this);
		}
	}
#endif

#if WITH_EDITOR
	if (Ar.GetPortFlags() & PPF_DuplicateForPIE)
	{
		PlatformData.Serialize(Ar, this);
	}
#endif

#if WITH_EDITOR
	if (Ar.IsSaving() && Ar.IsPersistent())
	{
		//Update the last saved Guid for GI texture
		LastBakedTextureMaterialGuid = BakedTextureMaterialGuid;
		//Update the last saved Hash for physical material
		LastSavedPhysicalMaterialHash = PhysicalMaterialHash;
	}
#endif
}

void ULandscapeComponent::GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize)
{
	Super::GetResourceSizeEx(CumulativeResourceSize);

	CumulativeResourceSize.AddDedicatedSystemMemoryBytes(GrassData->GetAllocatedSize());
}

#if WITH_EDITOR
UMaterialInterface* ULandscapeComponent::GetLandscapeMaterial(int8 InLODIndex) const
{
	if (InLODIndex != INDEX_NONE)
	{
		UWorld* World = GetWorld();

		if (World != nullptr)
		{
			if (const FLandscapePerLODMaterialOverride* LocalMaterialOverride = PerLODOverrideMaterials.FindByPredicate(
				[InLODIndex](const FLandscapePerLODMaterialOverride& InOverride) { return (InOverride.LODIndex == InLODIndex) && (InOverride.Material != nullptr); }))
			{
				return LocalMaterialOverride->Material;
			}
		}
	}

	if (OverrideMaterial != nullptr)
	{
		return OverrideMaterial;
	}

	ALandscapeProxy* Proxy = GetLandscapeProxy();
	if (Proxy)
	{
		return Proxy->GetLandscapeMaterial(InLODIndex);
	}

	return UMaterial::GetDefaultMaterial(MD_Surface);
}

UMaterialInterface* ULandscapeComponent::GetLandscapeHoleMaterial() const
{
	if (OverrideHoleMaterial)
	{
		return OverrideHoleMaterial;
	}
	ALandscapeProxy* Proxy = GetLandscapeProxy();
	if (Proxy)
	{
		return Proxy->GetLandscapeHoleMaterial();
	}
	return nullptr;
}

bool ULandscapeComponent::IsLandscapeHoleMaterialValid() const
{
	UMaterialInterface* HoleMaterial = GetLandscapeHoleMaterial();
	if (!HoleMaterial)
	{
		HoleMaterial = GetLandscapeMaterial();
	}

	return HoleMaterial ? HoleMaterial->GetMaterial()->HasAnyExpressionsInMaterialAndFunctionsOfType<UMaterialExpressionLandscapeVisibilityMask>() : false;
}

bool ULandscapeComponent::ComponentHasVisibilityPainted() const
{
	for (const FWeightmapLayerAllocationInfo& Allocation : WeightmapLayerAllocations)
	{
		if (Allocation.LayerInfo == ALandscapeProxy::VisibilityLayer)
		{
			return true;
		}
	}

	return false;
}

void ULandscapeComponent::GetLayerDebugColorKey(int32& R, int32& G, int32& B) const
{
	ULandscapeInfo* Info = GetLandscapeInfo();
	if (Info != nullptr)
	{
		R = INDEX_NONE, G = INDEX_NONE, B = INDEX_NONE;

		for (auto It = Info->Layers.CreateConstIterator(); It; It++)
		{
			const FLandscapeInfoLayerSettings& LayerStruct = *It;
			if (LayerStruct.DebugColorChannel > 0
				&& LayerStruct.LayerInfoObj)
			{
				const TArray<FWeightmapLayerAllocationInfo>& ComponentWeightmapLayerAllocations = GetWeightmapLayerAllocations();

				for (int32 LayerIdx = 0; LayerIdx < ComponentWeightmapLayerAllocations.Num(); LayerIdx++)
				{
					if (ComponentWeightmapLayerAllocations[LayerIdx].LayerInfo == LayerStruct.LayerInfoObj)
					{
						if (LayerStruct.DebugColorChannel & 1) // R
						{
							R = (ComponentWeightmapLayerAllocations[LayerIdx].WeightmapTextureIndex * 4 + ComponentWeightmapLayerAllocations[LayerIdx].WeightmapTextureChannel);
						}
						if (LayerStruct.DebugColorChannel & 2) // G
						{
							G = (ComponentWeightmapLayerAllocations[LayerIdx].WeightmapTextureIndex * 4 + ComponentWeightmapLayerAllocations[LayerIdx].WeightmapTextureChannel);
						}
						if (LayerStruct.DebugColorChannel & 4) // B
						{
							B = (ComponentWeightmapLayerAllocations[LayerIdx].WeightmapTextureIndex * 4 + ComponentWeightmapLayerAllocations[LayerIdx].WeightmapTextureChannel);
						}
						break;
					}
				}
			}
		}
	}
}
#endif	//WITH_EDITOR

ULandscapeInfo::ULandscapeInfo(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

#if WITH_EDITOR
void ULandscapeInfo::UpdateDebugColorMaterial()
{
	FlushRenderingCommands();
	//GWarn->BeginSlowTask( *FString::Printf(TEXT("Compiling layer color combinations for %s"), *GetName()), true);

	for (auto It = XYtoComponentMap.CreateIterator(); It; ++It)
	{
		ULandscapeComponent* Comp = It.Value();
		if (Comp)
		{
			Comp->EditToolRenderData.UpdateDebugColorMaterial(Comp);
			Comp->UpdateEditToolRenderData();
		}
	}
	FlushRenderingCommands();
	//GWarn->EndSlowTask();
}

void ULandscapeComponent::UpdatedSharedPropertiesFromActor()
{
	ALandscapeProxy* LandscapeProxy = GetLandscapeProxy();

	CastShadow = LandscapeProxy->CastShadow;
	bCastDynamicShadow = LandscapeProxy->bCastDynamicShadow;
	bCastStaticShadow = LandscapeProxy->bCastStaticShadow;
	bCastContactShadow = LandscapeProxy->bCastContactShadow;
	bCastFarShadow = LandscapeProxy->bCastFarShadow;
	bCastHiddenShadow = LandscapeProxy->bCastHiddenShadow;
	bCastShadowAsTwoSided = LandscapeProxy->bCastShadowAsTwoSided;
	bAffectDistanceFieldLighting = LandscapeProxy->bAffectDistanceFieldLighting;
	bRenderCustomDepth = LandscapeProxy->bRenderCustomDepth;
	CustomDepthStencilWriteMask = LandscapeProxy->CustomDepthStencilWriteMask;
	CustomDepthStencilValue = LandscapeProxy->CustomDepthStencilValue;
	SetCullDistance(LandscapeProxy->LDMaxDrawDistance);
	LightingChannels = LandscapeProxy->LightingChannels;
	UpdateNavigationRelevance();
	UpdateRejectNavmeshUnderneath();
}

void ULandscapeComponent::PostLoad()
{
	Super::PostLoad();

	ALandscapeProxy* LandscapeProxy = GetLandscapeProxy();
	if (ensure(LandscapeProxy))
	{
		// Ensure that the component's lighting settings matches the actor's.
		UpdatedSharedPropertiesFromActor();

		// check SectionBaseX/Y are correct
		const FVector LocalRelativeLocation = GetRelativeLocation();
		int32 CheckSectionBaseX = FMath::RoundToInt(LocalRelativeLocation.X) + LandscapeProxy->LandscapeSectionOffset.X;
		int32 CheckSectionBaseY = FMath::RoundToInt(LocalRelativeLocation.Y) + LandscapeProxy->LandscapeSectionOffset.Y;
		if (CheckSectionBaseX != SectionBaseX ||
			CheckSectionBaseY != SectionBaseY)
		{
			UE_LOG(LogLandscape, Warning, TEXT("LandscapeComponent SectionBaseX disagrees with its location, attempted automated fix: '%s', %d,%d vs %d,%d."),
				*GetFullName(), SectionBaseX, SectionBaseY, CheckSectionBaseX, CheckSectionBaseY);
			SectionBaseX = CheckSectionBaseX;
			SectionBaseY = CheckSectionBaseY;
		}
	}

#if WITH_EDITOR
	if (GIsEditor && !HasAnyFlags(RF_ClassDefaultObject))
	{
		// This is to ensure that component relative location is exact section base offset value
		FVector LocalRelativeLocation = GetRelativeLocation();
		float CheckRelativeLocationX = float(SectionBaseX - LandscapeProxy->LandscapeSectionOffset.X);
		float CheckRelativeLocationY = float(SectionBaseY - LandscapeProxy->LandscapeSectionOffset.Y);
		if (CheckRelativeLocationX != LocalRelativeLocation.X ||
			CheckRelativeLocationY != LocalRelativeLocation.Y)
		{
			UE_LOG(LogLandscape, Warning, TEXT("LandscapeComponent RelativeLocation disagrees with its section base, attempted automated fix: '%s', %f,%f vs %f,%f."),
				*GetFullName(), LocalRelativeLocation.X, LocalRelativeLocation.Y, CheckRelativeLocationX, CheckRelativeLocationY);
			LocalRelativeLocation.X = CheckRelativeLocationX;
			LocalRelativeLocation.Y = CheckRelativeLocationY;

			SetRelativeLocation_Direct(LocalRelativeLocation);
		}

		// Remove standalone flags from data textures to ensure data is unloaded in the editor when reverting an unsaved level.
		// Previous version of landscape set these flags on creation.
		if (HeightmapTexture && HeightmapTexture->HasAnyFlags(RF_Standalone))
		{
			HeightmapTexture->ClearFlags(RF_Standalone);
		}
		for (int32 Idx = 0; Idx < WeightmapTextures.Num(); Idx++)
		{
			if (WeightmapTextures[Idx] && WeightmapTextures[Idx]->HasAnyFlags(RF_Standalone))
			{
				WeightmapTextures[Idx]->ClearFlags(RF_Standalone);
			}
		}

		if (GIBakedBaseColorTexture)
		{
			if (GIBakedBaseColorTexture->GetOutermost() != GetOutermost())
			{
				// The GIBakedBaseColorTexture property was never intended to be reassigned, but it was previously editable so we need to null any invalid values
				// it will get recreated by ALandscapeProxy::UpdateBakedTextures()
				GIBakedBaseColorTexture = nullptr;
				BakedTextureMaterialGuid = FGuid();
			}
			else
			{
				// Remove public flag from GI textures to stop them being visible in the content browser.
				// Previous version of landscape set these flags on creation.
				if (GIBakedBaseColorTexture->HasAnyFlags(RF_Public))
				{
					GIBakedBaseColorTexture->ClearFlags(RF_Public);
				}
			}
		}
		LastBakedTextureMaterialGuid = BakedTextureMaterialGuid;
		LastSavedPhysicalMaterialHash = PhysicalMaterialHash;

		PRAGMA_DISABLE_DEPRECATION_WARNINGS;
		if (!OverrideMaterials_DEPRECATED.IsEmpty())
		{
			PerLODOverrideMaterials.Reserve(OverrideMaterials_DEPRECATED.Num());
			for (const FLandscapeComponentMaterialOverride& LocalMaterialOverride : OverrideMaterials_DEPRECATED)
			{
				PerLODOverrideMaterials.Add({ LocalMaterialOverride.LODIndex.Default, LocalMaterialOverride.Material });
			}
			OverrideMaterials_DEPRECATED.Reset();
		}
		PRAGMA_ENABLE_DEPRECATION_WARNINGS;
	}
#endif // WITH_EDITOR

#if WITH_EDITORONLY_DATA
	// Handle old MaterialInstance
	if (MaterialInstance_DEPRECATED)
	{
		MaterialInstances.Empty(1);
		MaterialInstances.Add(MaterialInstance_DEPRECATED);
		MaterialInstance_DEPRECATED = nullptr;

#if WITH_EDITOR
		if (GIsEditor && MaterialInstances.Num() > 0 && MaterialInstances[0] != nullptr)
		{
			MaterialInstances[0]->ConditionalPostLoad();
			UpdateMaterialInstances();
		}
#endif // WITH_EDITOR
	}
#endif

#if WITH_EDITOR
	auto ReparentObject = [this](UObject* Object)
	{
		if (Object && !Object->HasAllFlags(RF_Public | RF_Standalone) && (Object->GetOuter() != GetOuter()) && (Object->GetOutermost() == GetOutermost()))
		{
			Object->Rename(nullptr, GetOuter(), REN_ForceNoResetLoaders);
			return true;
		}
		return false;
	};

	ReparentObject(HeightmapTexture);
	ReparentObject(XYOffsetmapTexture);

	for (UTexture2D* WeightmapTexture : WeightmapTextures)
	{
		ReparentObject(WeightmapTexture);
	}

	for (UTexture2D* MobileWeightmapTexture : MobileWeightmapTextures)
	{
		ReparentObject(MobileWeightmapTexture);
	}

	for (auto& ItPair : LayersData)
	{
		FLandscapeLayerComponentData& LayerComponentData = ItPair.Value;
		ReparentObject(LayerComponentData.HeightmapData.Texture);
		for (UTexture2D* WeightmapTexture : LayerComponentData.WeightmapData.Textures)
		{
			ReparentObject(WeightmapTexture);
		}

		// Fixup missing/mismatching edit layer names :
		if (const FLandscapeLayer* EditLayer = GetLandscapeActor() ? GetLandscapeActor()->GetLayer(ItPair.Key) : nullptr)
		{
			if (LayerComponentData.DebugName != EditLayer->Name)
			{
				LayerComponentData.DebugName = EditLayer->Name;
			}
		}
	}

	for (UMaterialInstance* MaterialInstance : MaterialInstances)
	{
		ULandscapeMaterialInstanceConstant* CurrentMIC = Cast<ULandscapeMaterialInstanceConstant>(MaterialInstance);
		while (ReparentObject(CurrentMIC))
		{
			CurrentMIC = Cast<ULandscapeMaterialInstanceConstant>(MaterialInstance->Parent);
		}
	}

	for (UMaterialInterface* MobileMaterialInterface : MobileMaterialInterfaces)
	{
		while (ReparentObject(MobileMaterialInterface))
		{
			MobileMaterialInterface = Cast<UMaterialInstance>(MobileMaterialInterface) ? Cast<UMaterialInstance>(((UMaterialInstance*)MobileMaterialInterface)->Parent) : nullptr;
		}
	}

	for (UMaterialInstance* MobileCombinationMaterialInstance : MobileCombinationMaterialInstances)
	{
		while (ReparentObject(MobileCombinationMaterialInstance))
		{
			MobileCombinationMaterialInstance = Cast<UMaterialInstance>(MobileCombinationMaterialInstance->Parent);
		}
	}
#endif

#if !UE_BUILD_SHIPPING
	// This will fix the data in case there is mismatch between save of asset/maps
	int8 MaxLOD = FMath::CeilLogTwo(SubsectionSizeQuads + 1) - 1;

	TArray<ULandscapeMaterialInstanceConstant*> ResolvedMaterials;

	if (LODIndexToMaterialIndex.Num() != MaxLOD+1)
	{
		if (GIsEditor)
		{
			UpdateMaterialInstances();
		}
		else
		{
			// Correct in-place differences by applying the highest LOD value we have to the newly added items as most case will be missing items added at the end
			LODIndexToMaterialIndex.SetNumZeroed(MaxLOD + 1);

			int8 LastLODIndex = 0;

			for (int32 i = 0; i < LODIndexToMaterialIndex.Num(); ++i)
			{
				if (LODIndexToMaterialIndex[i] > LastLODIndex)
				{
					LastLODIndex = LODIndexToMaterialIndex[i];
				}

				if (LODIndexToMaterialIndex[i] == 0 && LastLODIndex != 0)
				{
					LODIndexToMaterialIndex[i] = LastLODIndex;
				}
			}
		}
	}
#endif // UE_BUILD_SHIPPING

#if WITH_EDITOR
	if (GIsEditor && !HasAnyFlags(RF_ClassDefaultObject))
	{
		// Move the MICs and Textures back to the Package if they're currently in the level
		// Moving them into the level caused them to be duplicated when running PIE, which is *very very slow*, so we've reverted that change
		// Also clear the public flag to avoid various issues, e.g. generating and saving thumbnails that can never be seen
		if (ULevel* Level = GetLevel())
		{
			TArray<UObject*> ObjectsToMoveFromLevelToPackage;
			GetGeneratedTexturesAndMaterialInstances(ObjectsToMoveFromLevelToPackage);

			UPackage* MyPackage = GetOutermost();
			for (auto* Obj : ObjectsToMoveFromLevelToPackage)
			{
				Obj->ClearFlags(RF_Public);
				if (Obj->GetOuter() == Level)
				{
					Obj->Rename(nullptr, MyPackage, REN_DoNotDirty | REN_DontCreateRedirectors | REN_ForceNoResetLoaders | REN_NonTransactional);
				}
			}
		}
	}
#endif

#if !UE_BUILD_SHIPPING
	if (MobileCombinationMaterialInstances.Num() == 0)
	{
		if (GIsEditor)
		{
			UpdateMaterialInstances();
		}
		else
		{
			if (UseMobileLandscapeMesh(GMaxRHIShaderPlatform))
			{
				UE_LOG(LogLandscape, Error, TEXT("Landscape component (%d, %d) Does not have a valid mobile combination material. To correct this issue, open the map in the editor and resave the map."), SectionBaseX, SectionBaseY);
			}
		}
	}
#endif // UE_BUILD_SHIPPING

	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		UWorld* World = GetWorld();
		ERHIFeatureLevel::Type FeatureLevel = ((GEngine->GetDefaultWorldFeatureLevel() == ERHIFeatureLevel::ES3_1) || (World && (World->FeatureLevel <= ERHIFeatureLevel::ES3_1))) 
			? ERHIFeatureLevel::ES3_1 : GMaxRHIFeatureLevel;
				
		// If we're loading on a platform that doesn't require cooked data, but defaults to a mobile feature level, generate or preload data from the DDC
		if (!FPlatformProperties::RequiresCookedData() && UseMobileLandscapeMesh(GShaderPlatformForFeatureLevel[FeatureLevel]))
		{
			CheckGenerateLandscapePlatformData(false, nullptr);
		}
	}

	GrassData->ConditionalDiscardDataOnLoad();
}

#endif // WITH_EDITOR

#if WITH_EDITORONLY_DATA
TArray<ALandscapeProxy*> ALandscapeProxy::LandscapeProxies;
#endif

ALandscapeProxy::ALandscapeProxy(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
#if WITH_EDITORONLY_DATA
	, TargetDisplayOrder(ELandscapeLayerDisplayMode::Default)
#endif // WITH_EDITORONLY_DATA
#if !WITH_EDITORONLY_DATA
	, LandscapeMaterialCached(nullptr)
	, LandscapeGrassTypes()
	, GrassMaxDiscardDistance(0.0f)
#endif
	, bHasLandscapeGrass(true)
{
	bReplicates = false;
	NetUpdateFrequency = 10.0f;
	SetHidden(false);
	SetReplicatingMovement(false);
	SetCanBeDamaged(false);

	CastShadow = true;
	bCastDynamicShadow = true;
	bCastStaticShadow = true;
	bCastContactShadow = true;
	bCastFarShadow = true;
	bCastHiddenShadow = false;
	bCastShadowAsTwoSided = false;
	bAffectDistanceFieldLighting = true;

	RootComponent->SetRelativeScale3D(FVector(128.0f, 128.0f, 256.0f)); // Old default scale, preserved for compatibility. See ULandscapeEditorObject::NewLandscape_Scale
	RootComponent->Mobility = EComponentMobility::Static;
	LandscapeSectionOffset = FIntPoint::ZeroValue;

	StaticLightingResolution = 1.0f;
	StreamingDistanceMultiplier = 1.0f;
	MaxLODLevel = -1;
	bUseDynamicMaterialInstance = false;
#if WITH_EDITORONLY_DATA
	bLockLocation = true;
	bIsMovingToLevel = false;
#endif // WITH_EDITORONLY_DATA
	ComponentScreenSizeToUseSubSections = 0.65f;
	LOD0ScreenSize = 0.5f;
	LOD0DistributionSetting = 1.25f;
	LODDistributionSetting = 3.0f;
	bCastStaticShadow = true;
	bUsedForNavigation = true;
	bFillCollisionUnderLandscapeForNavmesh = false;
	CollisionThickness = 16;
	BodyInstance.SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	bGenerateOverlapEvents = false;
#if WITH_EDITORONLY_DATA
	MaxPaintedLayersPerComponent = 0;
	bHasLayersContent = false;
#endif

#if WITH_EDITOR
	NumComponentsNeedingGrassMapRender = 0;
	NumTexturesToStreamForVisibleGrassMapRender = 0;
	NumComponentsNeedingTextureBaking = 0;

	if (VisibilityLayer == nullptr)
	{
		// Structure to hold one-time initialization
		struct FConstructorStatics
		{
			ConstructorHelpers::FObjectFinderOptional<ULandscapeLayerInfoObject> DataLayer;
			FConstructorStatics()
				: DataLayer(TEXT("LandscapeLayerInfoObject'/Engine/EditorLandscapeResources/DataLayer.DataLayer'"))
			{
			}
		};
		static FConstructorStatics ConstructorStatics;

		VisibilityLayer = ConstructorStatics.DataLayer.Get();
		check(VisibilityLayer);
#if WITH_EDITORONLY_DATA
		// This layer should be no weight blending
		VisibilityLayer->bNoWeightBlend = true;
#endif
		VisibilityLayer->LayerName = UMaterialExpressionLandscapeVisibilityMask::ParameterName;
		VisibilityLayer->LayerUsageDebugColor = FLinearColor(0, 0, 0, 0);
		VisibilityLayer->AddToRoot();
	}

	if (!HasAnyFlags(RF_ArchetypeObject | RF_ClassDefaultObject) && GetWorld() != nullptr)
	{
		FOnFeatureLevelChanged::FDelegate FeatureLevelChangedDelegate = FOnFeatureLevelChanged::FDelegate::CreateUObject(this, &ALandscapeProxy::OnFeatureLevelChanged);
		FeatureLevelChangedDelegateHandle = GetWorld()->AddOnFeatureLevelChangedHandler(FeatureLevelChangedDelegate);
	}
#endif

	static uint32 FrameOffsetForTickIntervalInc = 0;
	FrameOffsetForTickInterval = FrameOffsetForTickIntervalInc++;

#if WITH_EDITORONLY_DATA
	LandscapeProxies.Add(this);
#endif
}

#if WITH_EDITORONLY_DATA
ALandscape::FLandscapeEdModeInfo::FLandscapeEdModeInfo()
	: ViewMode(ELandscapeViewMode::Invalid)
	, ToolTarget(ELandscapeToolTargetType::Invalid)
{
}
#endif

ALandscape::ALandscape(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	bLockLocation = false;
	WasCompilingShaders = false;
	LayerContentUpdateModes = 0;
	bSplineLayerUpdateRequested = false;
	CombinedLayersWeightmapAllMaterialLayersResource = nullptr;
	CurrentLayersWeightmapAllMaterialLayersResource = nullptr;
	WeightmapScratchExtractLayerTextureResource = nullptr;
	WeightmapScratchPackLayerTextureResource = nullptr;
	bLandscapeLayersAreInitialized = false;
	bLandscapeLayersAreUsingLocalMerge = false;
	LandscapeEdMode = nullptr;
	bGrassUpdateEnabled = true;
	bIsSpatiallyLoaded = false;
	bDefaultOutlinerExpansionState = false;
#endif // WITH_EDITORONLY_DATA
}

ALandscapeStreamingProxy::ALandscapeStreamingProxy(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	bLockLocation = true;
#endif // WITH_EDITORONLY_DATA
}

const ALandscape* ALandscape::GetLandscapeActor() const
{
	return this;
}

ALandscape* ALandscape::GetLandscapeActor()
{
	return this;
}

const ALandscape* ALandscapeStreamingProxy::GetLandscapeActor() const
{
	return LandscapeActor.Get();
}

ALandscape* ALandscapeStreamingProxy::GetLandscapeActor()
{
	return LandscapeActor.Get();
}

ULandscapeInfo* ALandscapeProxy::CreateLandscapeInfo(bool bMapCheck)
{
	ULandscapeInfo* LandscapeInfo = ULandscapeInfo::FindOrCreate(GetWorld(), LandscapeGuid);
	LandscapeInfo->RegisterActor(this, bMapCheck);
	return LandscapeInfo;
}

ULandscapeInfo* ALandscapeProxy::GetLandscapeInfo() const
{
	return ULandscapeInfo::Find(GetWorld(), LandscapeGuid);
}

FTransform ALandscapeProxy::LandscapeActorToWorld() const
{
	FTransform TM = ActorToWorld();
	// Add this proxy landscape section offset to obtain landscape actor transform
	TM.AddToTranslation(TM.TransformVector(-FVector(LandscapeSectionOffset)));
	return TM;
}

static TArray<float> GetLODScreenSizeArray(const ALandscapeProxy* InLandscapeProxy, const int32 InNumLODLevels)
{
	static TConsoleVariableData<float>* CVarSMLODDistanceScale = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("r.StaticMeshLODDistanceScale"));
	static IConsoleVariable* CVarLSLOD0DistributionScale = IConsoleManager::Get().FindConsoleVariable(TEXT("r.LandscapeLOD0DistributionScale"));
	float CurrentScreenSize = InLandscapeProxy->LOD0ScreenSize / CVarSMLODDistanceScale->GetValueOnGameThread();
	const float ScreenSizeMult = 1.f / FMath::Max(InLandscapeProxy->LOD0DistributionSetting * CVarLSLOD0DistributionScale->GetFloat(), 1.01f);

	TArray<float> Result;
	Result.Empty(InNumLODLevels);
	for (int32 Idx = 0; Idx < InNumLODLevels; ++Idx)
	{
		Result.Add(CurrentScreenSize);
		CurrentScreenSize *= ScreenSizeMult;
	}
	return Result;
}

TArray<float> ALandscapeProxy::GetLODScreenSizeArray() const
{
	const int32 NumLODLevels = FMath::Clamp<int32>(MaxLODLevel, 0, FMath::CeilLogTwo(SubsectionSizeQuads + 1) - 1);
	return ::GetLODScreenSizeArray(this, NumLODLevels);
}


ALandscape* ULandscapeComponent::GetLandscapeActor() const
{
	ALandscapeProxy* Landscape = GetLandscapeProxy();
	if (Landscape)
	{
		return Landscape->GetLandscapeActor();
	}
	return nullptr;
}

ULevel* ULandscapeComponent::GetLevel() const
{
	AActor* MyOwner = GetOwner();
	return MyOwner ? MyOwner->GetLevel() : nullptr;
}

#if WITH_EDITOR
TArray<UTexture*> ULandscapeComponent::GetGeneratedTextures() const
{
	TArray<UTexture*> OutTextures;
	if (HeightmapTexture)
	{
		OutTextures.Add(HeightmapTexture);
	}

	for (const auto& ItPair : LayersData)
	{
		const FLandscapeLayerComponentData& LayerComponentData = ItPair.Value;

		OutTextures.Add(LayerComponentData.HeightmapData.Texture);
		OutTextures.Append(LayerComponentData.WeightmapData.Textures);
	}

	OutTextures.Append(WeightmapTextures);

	if (XYOffsetmapTexture)
	{
		OutTextures.Add(XYOffsetmapTexture);
	}

	TArray<UMaterialInstance*> OutMaterials;
	for (UMaterialInstance* MaterialInstance : MaterialInstances)
	{
		for (ULandscapeMaterialInstanceConstant* CurrentMIC = Cast<ULandscapeMaterialInstanceConstant>(MaterialInstance); CurrentMIC; CurrentMIC = Cast<ULandscapeMaterialInstanceConstant>(CurrentMIC->Parent))
		{
			// Sometimes weight map is not registered in the WeightmapTextures, so
			// we need to get it from here.
			FTextureParameterValue* WeightmapPtr = CurrentMIC->TextureParameterValues.FindByPredicate(
				[](const FTextureParameterValue& ParamValue)
			{
				static const FName WeightmapParamName("Weightmap0");
				return ParamValue.ParameterInfo.Name == WeightmapParamName;
			});

			if (WeightmapPtr != nullptr)
			{
				OutTextures.AddUnique(WeightmapPtr->ParameterValue);
			}
		}
	}

	OutTextures.Remove(nullptr);

	return OutTextures;
}

TArray<UMaterialInstance*> ULandscapeComponent::GetGeneratedMaterialInstances() const
{
	TArray<UMaterialInstance*> OutMaterials;
	for (UMaterialInstance* MaterialInstance : MaterialInstances)
	{
		for (ULandscapeMaterialInstanceConstant* CurrentMIC = Cast<ULandscapeMaterialInstanceConstant>(MaterialInstance); CurrentMIC; CurrentMIC = Cast<ULandscapeMaterialInstanceConstant>(CurrentMIC->Parent))
		{
			OutMaterials.Add(CurrentMIC);
		}
	}

	for (UMaterialInstanceConstant* MaterialInstance : MobileCombinationMaterialInstances)
	{
		for (ULandscapeMaterialInstanceConstant* CurrentMIC = Cast<ULandscapeMaterialInstanceConstant>(MaterialInstance); CurrentMIC; CurrentMIC = Cast<ULandscapeMaterialInstanceConstant>(CurrentMIC->Parent))
		{
			OutMaterials.Add(CurrentMIC);
		}
	}

	return OutMaterials;
}

void ULandscapeComponent::GetGeneratedTexturesAndMaterialInstances(TArray<UObject*>& OutTexturesAndMaterials) const
{
	TArray<UTexture*> LocalTextures = GetGeneratedTextures();
	TArray<UMaterialInstance*> LocalMaterialInstances = GetGeneratedMaterialInstances();
	OutTexturesAndMaterials.Reserve(LocalTextures.Num() + LocalMaterialInstances.Num());
	OutTexturesAndMaterials.Append(LocalTextures);
	OutTexturesAndMaterials.Append(LocalMaterialInstances);
}
#endif

ALandscapeProxy* ULandscapeComponent::GetLandscapeProxy() const
{
	return CastChecked<ALandscapeProxy>(GetOuter());
}

const FMeshMapBuildData* ULandscapeComponent::GetMeshMapBuildData() const
{
	AActor* Owner = GetOwner();

	if (Owner)
	{
		ULevel* OwnerLevel = Owner->GetLevel();

#if WITH_EDITOR
		if (FStaticLightingSystemInterface::GetPrimitiveMeshMapBuildData(this))
		{
			return FStaticLightingSystemInterface::GetPrimitiveMeshMapBuildData(this);
		}
#endif

		if (OwnerLevel && OwnerLevel->OwningWorld)
		{
			ULevel* ActiveLightingScenario = OwnerLevel->OwningWorld->GetActiveLightingScenario();
			UMapBuildDataRegistry* MapBuildData = NULL;

			if (ActiveLightingScenario && ActiveLightingScenario->MapBuildData)
			{
				MapBuildData = ActiveLightingScenario->MapBuildData;
			}
			else if (OwnerLevel->MapBuildData)
			{
				MapBuildData = OwnerLevel->MapBuildData;
			}

			if (MapBuildData)
			{
				return MapBuildData->GetMeshBuildData(MapBuildDataId);
			}
		}
	}

	return NULL;
}

bool ULandscapeComponent::IsPrecomputedLightingValid() const
{
	return GetMeshMapBuildData() != NULL;
}

void ULandscapeComponent::PropagateLightingScenarioChange()
{
	FComponentRecreateRenderStateContext Context(this);
}

TArray<URuntimeVirtualTexture*> const& ULandscapeComponent::GetRuntimeVirtualTextures() const
{
	return GetLandscapeProxy()->RuntimeVirtualTextures;
}

ERuntimeVirtualTextureMainPassType ULandscapeComponent::GetVirtualTextureRenderPassType() const
{
	return GetLandscapeProxy()->VirtualTextureRenderPassType;
}

ULandscapeInfo* ULandscapeComponent::GetLandscapeInfo() const
{
	return GetLandscapeProxy()->GetLandscapeInfo();
}

void ULandscapeComponent::BeginDestroy()
{
	Super::BeginDestroy();

	if (LODStreamingProxy != nullptr)
	{
		LODStreamingProxy->UnlinkStreaming();
	}

#if WITH_EDITOR
	// Ask render thread to destroy EditToolRenderData
	EditToolRenderData = FLandscapeEditToolRenderData();
	UpdateEditToolRenderData();

	if (GIsEditor && !HasAnyFlags(RF_ClassDefaultObject))
	{
		ALandscapeProxy* Proxy = GetLandscapeProxy();

		// Remove any weightmap allocations from the Landscape Actor's map
		for (int32 LayerIdx = 0; LayerIdx < WeightmapLayerAllocations.Num(); LayerIdx++)
		{
			int32 WeightmapIndex = WeightmapLayerAllocations[LayerIdx].WeightmapTextureIndex;
			if (WeightmapTextures.IsValidIndex(WeightmapIndex))
			{
				UTexture2D* WeightmapTexture = WeightmapTextures[WeightmapIndex];
				TObjectPtr<ULandscapeWeightmapUsage>* Usage = Proxy->WeightmapUsageMap.Find(WeightmapTexture);
				if (Usage != nullptr && (*Usage) != nullptr)
				{
					(*Usage)->ChannelUsage[WeightmapLayerAllocations[LayerIdx].WeightmapTextureChannel] = nullptr;

					if ((*Usage)->IsEmpty())
					{
						Proxy->WeightmapUsageMap.Remove(WeightmapTexture);
					}
				}
			}
		}

		WeightmapTexturesUsage.Reset();
	}
#endif
}

FPrimitiveSceneProxy* ULandscapeComponent::CreateSceneProxy()
{
	check(LODStreamingProxy);
	LODStreamingProxy->ClearStreamingResourceState();
	LODStreamingProxy->UnlinkStreaming();

	const auto FeatureLevel = GetWorld()->FeatureLevel;
	FPrimitiveSceneProxy* Proxy = nullptr;
	if (FeatureLevel >= ERHIFeatureLevel::SM5 || !UseMobileLandscapeMesh(GShaderPlatformForFeatureLevel[FeatureLevel]))
	{
		Proxy = new FLandscapeComponentSceneProxy(this);
	}
	else // i.e. (FeatureLevel <= ERHIFeatureLevel::ES3_1)
	{
		if (PlatformData.HasValidRuntimeData())
		{
			Proxy = new FLandscapeComponentSceneProxyMobile(this);
			LODStreamingProxy->InitResourceStateForMobileStreaming();
			LODStreamingProxy->LinkStreaming();
		}
	}

	return Proxy;
}

bool ULandscapeComponent::IsShown(const FEngineShowFlags& ShowFlags) const
{
	return ShowFlags.Landscape;
}

void ULandscapeComponent::DestroyComponent(bool bPromoteChildren/*= false*/)
{
	ALandscapeProxy* Proxy = GetLandscapeProxy();
	if (Proxy)
	{
		Proxy->LandscapeComponents.Remove(this);
	}

	Super::DestroyComponent(bPromoteChildren);
}

FBoxSphereBounds ULandscapeComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	FBox MyBounds = CachedLocalBox.TransformBy(LocalToWorld);
	MyBounds = MyBounds.ExpandBy({ 0, 0, NegativeZBoundsExtension }, { 0, 0, PositiveZBoundsExtension });

	ALandscapeProxy* Proxy = GetLandscapeProxy();
	if (Proxy)
	{
		MyBounds = MyBounds.ExpandBy({ 0, 0, Proxy->NegativeZBoundsExtension }, { 0, 0, Proxy->PositiveZBoundsExtension });
	}

	return FBoxSphereBounds(MyBounds);
}

static void OnStaticMeshLODDistanceScaleChanged()
{
	extern RENDERER_API TAutoConsoleVariable<float> CVarStaticMeshLODDistanceScale;

	static float LastValue = 1.0f;

	if (LastValue != CVarStaticMeshLODDistanceScale.GetValueOnAnyThread())
	{
		LastValue = CVarStaticMeshLODDistanceScale.GetValueOnAnyThread();

		for (auto* LandscapeComponent : TObjectRange<ULandscapeComponent>(RF_ClassDefaultObject | RF_ArchetypeObject, true, EInternalObjectFlags::Garbage))
		{
			LandscapeComponent->MarkRenderStateDirty();
		}
	}
}

FAutoConsoleVariableSink OnStaticMeshLODDistanceScaleChangedSink(FConsoleCommandDelegate::CreateStatic(&OnStaticMeshLODDistanceScaleChanged));

void ULandscapeComponent::OnRegister()
{
	Super::OnRegister();

	if (GetLandscapeProxy())
	{
		// Generate MID representing the MIC
		if (GetLandscapeProxy()->bUseDynamicMaterialInstance)
		{
			MaterialInstancesDynamic.Reserve(MaterialInstances.Num());

			for (int32 i = 0; i < MaterialInstances.Num(); ++i)
			{
				MaterialInstancesDynamic.Add(UMaterialInstanceDynamic::Create(MaterialInstances[i], this));
			}
		}

		// AActor::GetWorld checks for Unreachable and BeginDestroyed
		UWorld* World = GetLandscapeProxy()->GetWorld();
		if (World)
		{
			ULandscapeInfo* Info = GetLandscapeInfo();
			if (Info)
			{
				Info->RegisterActorComponent(this);
			}
		}
	}
}

void ULandscapeComponent::OnUnregister()
{
	Super::OnUnregister();

#if WITH_EDITOR
	PhysicalMaterialTask.Release();
#endif

	if (GetLandscapeProxy())
	{
		// Generate MID representing the MIC
		if (GetLandscapeProxy()->bUseDynamicMaterialInstance)
		{
			MaterialInstancesDynamic.Empty();
		}

		// AActor::GetWorld checks for Unreachable and BeginDestroyed
		UWorld* World = GetLandscapeProxy()->GetWorld();

		// Game worlds don't have landscape infos
		if (World && !World->IsGameWorld())
		{
			ULandscapeInfo* Info = GetLandscapeInfo();
			if (Info)
			{
				Info->UnregisterActorComponent(this);
			}
		}
	}
}

UTexture2D* ULandscapeComponent::GetHeightmap(bool InReturnEditingHeightmap) const
{
#if WITH_EDITORONLY_DATA
	if (InReturnEditingHeightmap)
	{
		if (const FLandscapeLayerComponentData* EditingLayer = GetEditingLayer())
		{
			return EditingLayer->HeightmapData.Texture;
		}
	}
#endif

	return HeightmapTexture;
}

UTexture2D* ULandscapeComponent::GetHeightmap(const FGuid& InLayerGuid) const
{
#if WITH_EDITORONLY_DATA
	if (InLayerGuid.IsValid())
	{
		if (const FLandscapeLayerComponentData* LayerData = GetLayerData(InLayerGuid))
		{
			return LayerData->HeightmapData.Texture;
		}
	}
#endif

	return HeightmapTexture;
}

const TArray<UTexture2D*>& ULandscapeComponent::GetWeightmapTextures(bool InReturnEditingWeightmap) const
{
#if WITH_EDITORONLY_DATA
	if (InReturnEditingWeightmap)
	{
		if (const FLandscapeLayerComponentData* EditingLayer = GetEditingLayer())
		{
			return EditingLayer->WeightmapData.Textures;
		}
	}
#endif

	return WeightmapTextures;
}

TArray<UTexture2D*>& ULandscapeComponent::GetWeightmapTextures(bool InReturnEditingWeightmap)
{
#if WITH_EDITORONLY_DATA
	if (InReturnEditingWeightmap)
	{
		if (FLandscapeLayerComponentData* EditingLayer = GetEditingLayer())
		{
			return EditingLayer->WeightmapData.Textures;
		}
	}
#endif

	return WeightmapTextures;
}

const TArray<UTexture2D*>& ULandscapeComponent::GetWeightmapTextures(const FGuid& InLayerGuid) const
{
#if WITH_EDITORONLY_DATA
	if (InLayerGuid.IsValid())
	{
		if (const FLandscapeLayerComponentData* LayerData = GetLayerData(InLayerGuid))
		{
			return LayerData->WeightmapData.Textures;
		}
	}
#endif

	return WeightmapTextures;
}

TArray<UTexture2D*>& ULandscapeComponent::GetWeightmapTextures(const FGuid& InLayerGuid)
{
#if WITH_EDITORONLY_DATA
	if (InLayerGuid.IsValid())
	{
		if (FLandscapeLayerComponentData* LayerData = GetLayerData(InLayerGuid))
		{
			return LayerData->WeightmapData.Textures;
		}
	}
#endif

	return WeightmapTextures;
}

const TArray<FWeightmapLayerAllocationInfo>& ULandscapeComponent::GetWeightmapLayerAllocations(bool InReturnEditingWeightmap) const
{
#if WITH_EDITORONLY_DATA
	if (InReturnEditingWeightmap)
	{
		if (const FLandscapeLayerComponentData* EditingLayer = GetEditingLayer())
		{
			return EditingLayer->WeightmapData.LayerAllocations;
		}
	}
#endif

	return WeightmapLayerAllocations;
}

TArray<FWeightmapLayerAllocationInfo>& ULandscapeComponent::GetWeightmapLayerAllocations(const FGuid& InLayerGuid)
{
#if WITH_EDITORONLY_DATA
	if (InLayerGuid.IsValid())
	{
		if (FLandscapeLayerComponentData* LayerData = GetLayerData(InLayerGuid))
		{
			return LayerData->WeightmapData.LayerAllocations;
		}
	}
#endif

	return WeightmapLayerAllocations;
}

const TArray<FWeightmapLayerAllocationInfo>& ULandscapeComponent::GetWeightmapLayerAllocations(const FGuid& InLayerGuid) const
{
#if WITH_EDITORONLY_DATA
	if (InLayerGuid.IsValid())
	{
		if (const FLandscapeLayerComponentData* LayerData = GetLayerData(InLayerGuid))
		{
			return LayerData->WeightmapData.LayerAllocations;
		}
	}
#endif

	return WeightmapLayerAllocations;
}

TArray<FWeightmapLayerAllocationInfo>& ULandscapeComponent::GetWeightmapLayerAllocations(bool InReturnEditingWeightmap)
{
#if WITH_EDITORONLY_DATA
	if (InReturnEditingWeightmap)
	{
		if (FLandscapeLayerComponentData* EditingLayer = GetEditingLayer())
		{
			return EditingLayer->WeightmapData.LayerAllocations;
		}
	}
#endif

	return WeightmapLayerAllocations;
}

#if WITH_EDITOR

void ULandscapeComponent::SetEditingLayer(const FGuid& InEditingLayer)
{
	LandscapeEditingLayer = InEditingLayer;
}

FLandscapeLayerComponentData* ULandscapeComponent::GetEditingLayer()
{
	if (CachedEditingLayer != LandscapeEditingLayer)
	{
		CachedEditingLayer = LandscapeEditingLayer;
		CachedEditingLayerData = CachedEditingLayer.IsValid() ? LayersData.Find(CachedEditingLayer) : nullptr;
	}
	return CachedEditingLayerData;
}

const FLandscapeLayerComponentData* ULandscapeComponent::GetEditingLayer() const
{
	if (CachedEditingLayer != LandscapeEditingLayer)
	{
		CachedEditingLayer = LandscapeEditingLayer;
		CachedEditingLayerData = CachedEditingLayer.IsValid() ? const_cast<TMap<FGuid, FLandscapeLayerComponentData>&>(LayersData).Find(CachedEditingLayer) : nullptr;
	}
	return CachedEditingLayerData;
}

void ULandscapeComponent::CopyFinalLayerIntoEditingLayer(FLandscapeEditDataInterface& DataInterface, TSet<UTexture2D*>& ProcessedHeightmaps)
{
	Modify();
	GetLandscapeProxy()->Modify();

	// Heightmap	
	UTexture2D* EditingTexture = GetHeightmap(true);
	if (!ProcessedHeightmaps.Contains(EditingTexture))
	{
		DataInterface.CopyTextureFromHeightmap(EditingTexture, this, 0);
		ProcessedHeightmaps.Add(EditingTexture);
	}

	// Weightmap
	const TArray<FWeightmapLayerAllocationInfo>& FinalWeightmapLayerAllocations = GetWeightmapLayerAllocations();
	TArray<FWeightmapLayerAllocationInfo>& EditingLayerWeightmapLayerAllocations = GetWeightmapLayerAllocations(GetEditingLayerGUID());

	// Add missing Alloc Infos
	for (const FWeightmapLayerAllocationInfo& FinalAllocInfo : FinalWeightmapLayerAllocations)
	{
		int32 Index = EditingLayerWeightmapLayerAllocations.IndexOfByPredicate([&FinalAllocInfo](const FWeightmapLayerAllocationInfo& EditingAllocInfo) { return EditingAllocInfo.LayerInfo == FinalAllocInfo.LayerInfo; });
		if (Index == INDEX_NONE)
		{
			new (EditingLayerWeightmapLayerAllocations) FWeightmapLayerAllocationInfo(FinalAllocInfo.LayerInfo);
		}
	}

	const bool bEditingWeighmaps = true;
	const bool bSaveToTransactionBuffer = true;
	ReallocateWeightmaps(&DataInterface, bEditingWeighmaps, bSaveToTransactionBuffer);

	const TArray<UTexture2D*>& EditingWeightmapTextures = GetWeightmapTextures(true);
	for (const FWeightmapLayerAllocationInfo& AllocInfo : EditingLayerWeightmapLayerAllocations)
	{
		DataInterface.CopyTextureFromWeightmap(EditingWeightmapTextures[AllocInfo.WeightmapTextureIndex], AllocInfo.WeightmapTextureChannel, this, AllocInfo.LayerInfo, 0);
	}
}

FGuid ULandscapeComponent::GetEditingLayerGUID() const
{
	ALandscape* Landscape = GetLandscapeActor();
	return Landscape != nullptr ? Landscape->GetEditingLayer() : FGuid();
}

bool ULandscapeComponent::HasLayersData() const
{
	return LayersData.Num() > 0;
}

const FLandscapeLayerComponentData* ULandscapeComponent::GetLayerData(const FGuid& InLayerGuid) const
{
	return LayersData.Find(InLayerGuid);
}

FLandscapeLayerComponentData* ULandscapeComponent::GetLayerData(const FGuid& InLayerGuid)
{
	return LayersData.Find(InLayerGuid);
}

void ULandscapeComponent::ForEachLayer(TFunctionRef<void(const FGuid&, struct FLandscapeLayerComponentData&)> Fn)
{
	for (auto& Pair : LayersData)
	{
		Fn(Pair.Key, Pair.Value);
	}
}

void ULandscapeComponent::AddLayerData(const FGuid& InLayerGuid, const FLandscapeLayerComponentData& InData)
{
	Modify();
	check(!LandscapeEditingLayer.IsValid());
	FLandscapeLayerComponentData& Data = LayersData.FindOrAdd(InLayerGuid);
	Data = InData;
	CachedEditingLayer.Invalidate();
	CachedEditingLayerData = nullptr;
}

void ULandscapeComponent::AddDefaultLayerData(const FGuid& InLayerGuid, const TArray<ULandscapeComponent*>& InComponentsUsingHeightmap, TMap<UTexture2D*, UTexture2D*>& InOutCreatedHeightmapTextures)
{
	Modify();

	UTexture2D* ComponentHeightmap = GetHeightmap();

	// Compute per layer data
	FLandscapeLayerComponentData* LayerData = GetLayerData(InLayerGuid);

	if (LayerData == nullptr || !LayerData->IsInitialized())
	{
		const FLandscapeLayer* EditLayer = GetLandscapeActor() ? GetLandscapeActor()->GetLayer(InLayerGuid) : nullptr;
		FLandscapeLayerComponentData NewData(EditLayer ? EditLayer->Name : FName());

		// Setup Heightmap data
		UTexture2D** LayerHeightmap = InOutCreatedHeightmapTextures.Find(ComponentHeightmap);

		if (LayerHeightmap == nullptr)
		{
			UTexture2D* NewLayerHeightmap = GetLandscapeProxy()->CreateLandscapeTexture(ComponentHeightmap->Source.GetSizeX(), ComponentHeightmap->Source.GetSizeY(), TEXTUREGROUP_Terrain_Heightmap, ComponentHeightmap->Source.GetFormat());
			LayerHeightmap = &InOutCreatedHeightmapTextures.Add(ComponentHeightmap, NewLayerHeightmap);

			ULandscapeComponent::CreateEmptyTextureMips(NewLayerHeightmap, true);

			// Init Mip0 to be at 32768 which is equal to "0"
			FColor* Mip0Data = (FColor*)NewLayerHeightmap->Source.LockMip(0);

			for (ULandscapeComponent* ComponentUsingHeightmap : InComponentsUsingHeightmap)
			{
				int32 HeightmapComponentOffsetX = FMath::RoundToInt((float)NewLayerHeightmap->Source.GetSizeX() * ComponentUsingHeightmap->HeightmapScaleBias.Z);
				int32 HeightmapComponentOffsetY = FMath::RoundToInt((float)NewLayerHeightmap->Source.GetSizeY() * ComponentUsingHeightmap->HeightmapScaleBias.W);

				for (int32 SubsectionY = 0; SubsectionY < NumSubsections; SubsectionY++)
				{
					for (int32 SubsectionX = 0; SubsectionX < NumSubsections; SubsectionX++)
					{
						for (int32 SubY = 0; SubY <= SubsectionSizeQuads; SubY++)
						{
							for (int32 SubX = 0; SubX <= SubsectionSizeQuads; SubX++)
							{
								// X/Y of the vertex we're looking at in component's coordinates.
								const int32 CompX = SubsectionSizeQuads * SubsectionX + SubX;
								const int32 CompY = SubsectionSizeQuads * SubsectionY + SubY;

								// X/Y of the vertex we're looking indexed into the texture data
								const int32 TexX = (SubsectionSizeQuads + 1) * SubsectionX + SubX;
								const int32 TexY = (SubsectionSizeQuads + 1) * SubsectionY + SubY;

								const int32 HeightTexDataIdx = (HeightmapComponentOffsetX + TexX) + (HeightmapComponentOffsetY + TexY) * NewLayerHeightmap->Source.GetSizeX();

								// copy height and normal data
								const uint16 HeightValue = LandscapeDataAccess::GetTexHeight(0.f);

								Mip0Data[HeightTexDataIdx].R = HeightValue >> 8;
								Mip0Data[HeightTexDataIdx].G = HeightValue & 255;

								// Normal with get calculated later
								Mip0Data[HeightTexDataIdx].B = 0.0f;
								Mip0Data[HeightTexDataIdx].A = 0.0f;
							}
						}
					}
				}
			}

			NewLayerHeightmap->Source.UnlockMip(0);

			NewLayerHeightmap->UpdateResource();
		}

		NewData.HeightmapData.Texture = *LayerHeightmap;

		// Nothing to do for Weightmap by default

		AddLayerData(InLayerGuid, MoveTemp(NewData));
	}
}

void ULandscapeComponent::RemoveLayerData(const FGuid& InLayerGuid)
{
	Modify();
	check(!LandscapeEditingLayer.IsValid());
	LayersData.Remove(InLayerGuid);
	CachedEditingLayer.Invalidate();
	CachedEditingLayerData = nullptr;
}

void ULandscapeComponent::SetHeightmap(UTexture2D* NewHeightmap)
{
	check(NewHeightmap != nullptr);
	HeightmapTexture = NewHeightmap;
}

void ULandscapeComponent::SetWeightmapTextures(const TArray<UTexture2D*>& InNewWeightmapTextures, bool InApplyToEditingWeightmap)
{
#if WITH_EDITORONLY_DATA
	FLandscapeLayerComponentData* EditingLayer = GetEditingLayer();

	if (InApplyToEditingWeightmap && EditingLayer != nullptr)
	{
		EditingLayer->WeightmapData.Textures.Reset(InNewWeightmapTextures.Num());
		EditingLayer->WeightmapData.Textures.Append(InNewWeightmapTextures);
	}
	else
#endif
	{
		WeightmapTextures = InNewWeightmapTextures;
	}
}

void ULandscapeComponent::SetWeightmapLayerAllocations(const TArray<FWeightmapLayerAllocationInfo>& InNewWeightmapLayerAllocations)
{
	WeightmapLayerAllocations = InNewWeightmapLayerAllocations;
}

TArray<ULandscapeWeightmapUsage*>& ULandscapeComponent::GetWeightmapTexturesUsage(bool InReturnEditingWeightmap)
{
#if WITH_EDITORONLY_DATA
	if (InReturnEditingWeightmap)
	{
		if (FLandscapeLayerComponentData* EditingLayer = GetEditingLayer())
		{
			return EditingLayer->WeightmapData.TextureUsages;
		}
	}
#endif

	return WeightmapTexturesUsage;
}

const TArray<ULandscapeWeightmapUsage*>& ULandscapeComponent::GetWeightmapTexturesUsage(bool InReturnEditingWeightmap) const
{
#if WITH_EDITORONLY_DATA
	if (InReturnEditingWeightmap)
	{
		if (const FLandscapeLayerComponentData* EditingLayer = GetEditingLayer())
		{
			return EditingLayer->WeightmapData.TextureUsages;
		}
	}
#endif

	return WeightmapTexturesUsage;
}

TArray<ULandscapeWeightmapUsage*>& ULandscapeComponent::GetWeightmapTexturesUsage(const FGuid& InLayerGuid)
{
#if WITH_EDITORONLY_DATA
	if (InLayerGuid.IsValid())
	{
		if (FLandscapeLayerComponentData* LayerData = GetLayerData(InLayerGuid))
		{
			return LayerData->WeightmapData.TextureUsages;
		}
	}
#endif

	return WeightmapTexturesUsage;
}

const TArray<ULandscapeWeightmapUsage*>& ULandscapeComponent::GetWeightmapTexturesUsage(const FGuid& InLayerGuid) const
{
#if WITH_EDITORONLY_DATA
	if (InLayerGuid.IsValid())
	{
		if (const FLandscapeLayerComponentData* LayerData = GetLayerData(InLayerGuid))
		{
			return LayerData->WeightmapData.TextureUsages;
		}
	}
#endif

	return WeightmapTexturesUsage;
}

void ULandscapeComponent::SetWeightmapTexturesUsage(const TArray<ULandscapeWeightmapUsage*>& InNewWeightmapTexturesUsage, bool InApplyToEditingWeightmap)
{
#if WITH_EDITORONLY_DATA
	FLandscapeLayerComponentData* EditingLayer = GetEditingLayer();

	if (InApplyToEditingWeightmap && EditingLayer != nullptr)
	{
		EditingLayer->WeightmapData.TextureUsages.Reset(InNewWeightmapTexturesUsage.Num());
		EditingLayer->WeightmapData.TextureUsages.Append(InNewWeightmapTexturesUsage);
	}
	else
#endif
	{
		WeightmapTexturesUsage = InNewWeightmapTexturesUsage;
	}
}

#endif

void ALandscapeProxy::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
	ULandscapeInfo* LandscapeInfo = nullptr;
	if (!IsPendingKillPending())
	{
		// Duplicated Landscapes don't have a valid guid until PostEditImport is called, we'll register then
		if (LandscapeGuid.IsValid())
		{
#if WITH_EDITOR
			if (GIsEditor && !GetWorld()->IsGameWorld())
			{
				// Note: This can happen when loading certain cooked assets in an editor
				// Todo: Determine the root cause of this and fix it at a higher level!
				if (LandscapeComponents.Num() > 0 && LandscapeComponents[0] == nullptr)
				{
					LandscapeComponents.Empty();
				}

				UpdateCachedHasLayersContent(true);

				// Cache the value at this point as CreateLandscapeInfo (-> RegisterActor) might create/destroy layers content if there was a mismatch between landscape & proxy
				// Check the actual flag here not HasLayersContent() which could return true if the LandscapeActor is valid.
				bool bHasLayersContentBefore = bHasLayersContent;

				LandscapeInfo = CreateLandscapeInfo(true);

				FixupWeightmaps();

				const bool bNeedOldDataMigration = !bHasLayersContentBefore && CanHaveLayersContent();
				if (bNeedOldDataMigration && LandscapeInfo->LandscapeActor.IsValid() && LandscapeInfo->LandscapeActor.Get()->HasLayersContent())
				{
					LandscapeInfo->LandscapeActor.Get()->CopyOldDataToDefaultLayer(this);
				}
			}
			else
#endif
			{
				LandscapeInfo = CreateLandscapeInfo(true);
			}
		}

		if (UWorld* OwningWorld = GetWorld())
		{
			if (ULandscapeSubsystem* LandscapeSubsystem = OwningWorld->GetSubsystem<ULandscapeSubsystem>())
			{
				LandscapeSubsystem->RegisterActor(this);
			}
		}
	}
#if WITH_EDITOR
	// Game worlds don't have landscape infos
	if (!GetWorld()->IsGameWorld() && !IsPendingKillPending())
	{
		if (LandscapeGuid.IsValid())
		{
			LandscapeInfo->FixupProxiesTransform();
		}
	}
#endif
}

void ALandscapeProxy::UnregisterAllComponents(const bool bForReregister)
{
	// Game worlds don't have landscape infos
	// On shutdown the world will be unreachable
	if (GetWorld() && IsValidChecked(GetWorld()) && !GetWorld()->IsUnreachable() &&
		// When redoing the creation of a landscape we may get UnregisterAllComponents called when
		// we are in a "pre-initialized" state (empty guid, etc)
		LandscapeGuid.IsValid())
	{
		ULandscapeInfo* LandscapeInfo = GetLandscapeInfo();
		if (LandscapeInfo)
		{
			LandscapeInfo->UnregisterActor(this);
		}

		if (ULandscapeSubsystem* LandscapeSubsystem = GetWorld()->GetSubsystem<ULandscapeSubsystem>())
		{
			LandscapeSubsystem->UnregisterActor(this);
		}
	}

	Super::UnregisterAllComponents(bForReregister);
}


FArchive& operator<<(FArchive& Ar, FWeightmapLayerAllocationInfo& U)
{
	return Ar << U.LayerInfo << U.WeightmapTextureChannel << U.WeightmapTextureIndex;
}

#if WITH_EDITORONLY_DATA
FArchive& operator<<(FArchive& Ar, FLandscapeAddCollision& U)
{
	return Ar << U.Corners[0] << U.Corners[1] << U.Corners[2] << U.Corners[3];
}
#endif // WITH_EDITORONLY_DATA

FArchive& operator<<(FArchive& Ar, FLandscapeLayerStruct*& L)
{
	if (L)
	{
		Ar << L->LayerInfoObj;
#if WITH_EDITORONLY_DATA
		return Ar << L->ThumbnailMIC;
#else
		return Ar;
#endif // WITH_EDITORONLY_DATA
	}
	return Ar;
}

void ULandscapeInfo::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	if (Ar.IsTransacting())
	{
		Ar << XYtoComponentMap;
#if WITH_EDITORONLY_DATA
		Ar << XYtoAddCollisionMap;
#endif
		Ar << SelectedComponents;
		Ar << SelectedRegion;
		Ar << SelectedRegionComponents;
	}
}

void ALandscape::PostLoad()
{
	if (!LandscapeGuid.IsValid())
	{
		LandscapeGuid = FGuid::NewGuid();
	}
	else
	{
#if WITH_EDITOR
		UWorld* CurrentWorld = GetWorld();
		for (ALandscape* Landscape : TObjectRange<ALandscape>(RF_ClassDefaultObject | RF_BeginDestroyed))
		{
			if (Landscape && Landscape != this && Landscape->LandscapeGuid == LandscapeGuid && Landscape->GetWorld() == CurrentWorld)
			{
				// Duplicated landscape level, need to generate new GUID. This can happen during PIE or gameplay when streaming the same landscape actor.
				Modify();
				LandscapeGuid = FGuid::NewGuid();
				break;
			}
		}
#endif
	}

#if WITH_EDITOR
	for (FLandscapeLayer& Layer : LandscapeLayers)
	{
		// For now, only Layer reserved for Landscape Spline uses AlphaBlend
		Layer.BlendMode = (Layer.Guid == LandscapeSplinesTargetLayerGuid) ? LSBM_AlphaBlend : LSBM_AdditiveBlend;
		for (FLandscapeLayerBrush& Brush : Layer.Brushes)
		{
			Brush.SetOwner(this);
		}
	}
#endif

	Super::PostLoad();
}

FBox ALandscape::GetLoadedBounds() const
{
	return GetLandscapeInfo()->GetLoadedBounds();
}

#if WITH_EDITOR
FBox ALandscape::GetCompleteBounds() const
{
	return GetLandscapeInfo()->GetCompleteBounds();
}
#endif

#if WITH_EDITOR
void ALandscapeProxy::OnFeatureLevelChanged(ERHIFeatureLevel::Type NewFeatureLevel)
{
	FlushGrassComponents();

	UpdateAllComponentMaterialInstances();

	if (UseMobileLandscapeMesh(GShaderPlatformForFeatureLevel[NewFeatureLevel]))
	{
		for (ULandscapeComponent* Component : LandscapeComponents)
		{
			if (Component != nullptr)
			{
				Component->CheckGenerateLandscapePlatformData(false, nullptr);
			}
		}
	}
}
#endif

void ALandscapeProxy::PreSave(const class ITargetPlatform* TargetPlatform)
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS;
	Super::PreSave(TargetPlatform);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS;
}

void ALandscapeProxy::PreSave(FObjectPreSaveContext ObjectSaveContext)
{
	Super::PreSave(ObjectSaveContext);

#if WITH_EDITOR
	// Work out whether we have grass or not for the next game run
	BuildGrassMaps();
	//Update the baked textures before saving
	BuildGIBakedTextures();

	for (ULandscapeComponent* Component : LandscapeComponents)
	{
		// Reset flag
		Component->GrassData->bIsDirty = false;
	}

	if (ALandscape* Landscape = GetLandscapeActor())
	{
		for (ULandscapeComponent* LandscapeComponent : LandscapeComponents)
		{
			Landscape->ClearDirtyData(LandscapeComponent);

			// Make sure edit layer debug names are synchronized upon save :
			LandscapeComponent->ForEachLayer([&](const FGuid& LayerGuid, FLandscapeLayerComponentData& LayerData)
			{
				if (const FLandscapeLayer* EditLayer = Landscape->GetLayer(LayerGuid))
				{
					LayerData.DebugName = EditLayer->Name;
				}
			});
		}
	}
#endif // WITH_EDITOR
}

void ALandscapeProxy::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FLandscapeCustomVersion::GUID);
	Ar.UsingCustomVersion(FEditorObjectVersion::GUID);

#if WITH_EDITORONLY_DATA
	if (Ar.IsLoading() && Ar.CustomVer(FLandscapeCustomVersion::GUID) < FLandscapeCustomVersion::MigrateOldPropertiesToNewRenderingProperties)
	{
		if (LODDistanceFactor_DEPRECATED > 0)
		{
			const float LOD0LinearDistributionSettingMigrationTable[11] = { 1.75f, 1.75f, 1.75f, 1.75f, 1.75f, 1.68f, 1.55f, 1.4f, 1.25f, 1.25f, 1.25f };
			const float LODDLinearDistributionSettingMigrationTable[11] = { 2.0f, 2.0f, 2.0f, 1.65f, 1.35f, 1.25f, 1.25f, 1.25f, 1.25f, 1.25f, 1.25f };
			const float LOD0SquareRootDistributionSettingMigrationTable[11] = { 1.75f, 1.6f, 1.25f, 1.25f, 1.25f, 1.25f, 1.25f, 1.25f, 1.25f, 1.25f, 1.25f };
			const float LODDSquareRootDistributionSettingMigrationTable[11] = { 2.0f, 1.8f, 1.55f, 1.3f, 1.25f, 1.25f, 1.25f, 1.25f, 1.25f, 1.25f, 1.25f };

			if (LODFalloff_DEPRECATED == ELandscapeLODFalloff::Type::Linear)
			{
				LOD0DistributionSetting = LOD0LinearDistributionSettingMigrationTable[FMath::RoundToInt(LODDistanceFactor_DEPRECATED)];
				LODDistributionSetting = LODDLinearDistributionSettingMigrationTable[FMath::RoundToInt(LODDistanceFactor_DEPRECATED)];
			}
			else if (LODFalloff_DEPRECATED == ELandscapeLODFalloff::Type::SquareRoot)
			{
				LOD0DistributionSetting = LOD0SquareRootDistributionSettingMigrationTable[FMath::RoundToInt(LODDistanceFactor_DEPRECATED)];
				LODDistributionSetting = LODDSquareRootDistributionSettingMigrationTable[FMath::RoundToInt(LODDistanceFactor_DEPRECATED)];
			}
		}
	}
#endif // WITH_EDITORONLY_DATA
}

void ALandscapeProxy::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	ALandscapeProxy* This = CastChecked<ALandscapeProxy>(InThis);

	Super::AddReferencedObjects(InThis, Collector);

#if WITH_EDITORONLY_DATA
	Collector.AddReferencedObjects(This->MaterialInstanceConstantMap, This);
#endif
}

#if WITH_EDITOR

FName FLandscapeInfoLayerSettings::GetLayerName() const
{
	checkSlow(LayerInfoObj == nullptr || LayerInfoObj->LayerName == LayerName);

	return LayerName;
}

FLandscapeEditorLayerSettings& FLandscapeInfoLayerSettings::GetEditorSettings() const
{
	check(LayerInfoObj);

	ULandscapeInfo* LandscapeInfo = Owner->GetLandscapeInfo();
	return LandscapeInfo->GetLayerEditorSettings(LayerInfoObj);
}

FLandscapeEditorLayerSettings& ULandscapeInfo::GetLayerEditorSettings(ULandscapeLayerInfoObject* LayerInfo) const
{
	ALandscapeProxy* Proxy = GetLandscapeProxy();
	FLandscapeEditorLayerSettings* EditorLayerSettings = Proxy->EditorLayerSettings.FindByKey(LayerInfo);
	if (EditorLayerSettings)
	{
		return *EditorLayerSettings;
	}
	else
	{
		int32 Index = Proxy->EditorLayerSettings.Add(FLandscapeEditorLayerSettings(LayerInfo));
		return Proxy->EditorLayerSettings[Index];
	}
}

void ULandscapeInfo::CreateLayerEditorSettingsFor(ULandscapeLayerInfoObject* LayerInfo)
{
	ForAllLandscapeProxies([LayerInfo](ALandscapeProxy* Proxy)
	{
		FLandscapeEditorLayerSettings* EditorLayerSettings = Proxy->EditorLayerSettings.FindByKey(LayerInfo);
		if (!EditorLayerSettings)
		{
			Proxy->Modify();
			Proxy->EditorLayerSettings.Add(FLandscapeEditorLayerSettings(LayerInfo));
		}
	});
}

ULandscapeLayerInfoObject* ULandscapeInfo::GetLayerInfoByName(FName LayerName, ALandscapeProxy* Owner /*= nullptr*/) const
{
	ULandscapeLayerInfoObject* LayerInfo = nullptr;
	for (int32 j = 0; j < Layers.Num(); j++)
	{
		if (Layers[j].LayerInfoObj && Layers[j].LayerInfoObj->LayerName == LayerName
			&& (Owner == nullptr || Layers[j].Owner == Owner))
		{
			LayerInfo = Layers[j].LayerInfoObj;
		}
	}
	return LayerInfo;
}

int32 ULandscapeInfo::GetLayerInfoIndex(ULandscapeLayerInfoObject* LayerInfo, ALandscapeProxy* Owner /*= nullptr*/) const
{
	for (int32 j = 0; j < Layers.Num(); j++)
	{
		if (Layers[j].LayerInfoObj && Layers[j].LayerInfoObj == LayerInfo
			&& (Owner == nullptr || Layers[j].Owner == Owner))
		{
			return j;
		}
	}

	return INDEX_NONE;
}

int32 ULandscapeInfo::GetLayerInfoIndex(FName LayerName, ALandscapeProxy* Owner /*= nullptr*/) const
{
	for (int32 j = 0; j < Layers.Num(); j++)
	{
		if (Layers[j].GetLayerName() == LayerName
			&& (Owner == nullptr || Layers[j].Owner == Owner))
		{
			return j;
		}
	}

	return INDEX_NONE;
}


bool ULandscapeInfo::UpdateLayerInfoMapInternal(ALandscapeProxy* Proxy, bool bInvalidate)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ULandscapeInfo::UpdateLayerInfoMapInternal);

	bool bHasCollision = false;
	if (GIsEditor)
	{
		if (Proxy)
		{
			if (bInvalidate)
			{
				// this is a horribly dangerous combination of parameters...

				for (int32 i = 0; i < Layers.Num(); i++)
				{
					if (Layers[i].Owner == Proxy)
					{
						Layers.RemoveAt(i--);
					}
				}
			}
			else // Proxy && !bInvalidate
			{
				TArray<FName> LayerNames = Proxy->GetLayersFromMaterial();

				// Validate any existing layer infos owned by this proxy
				for (int32 i = 0; i < Layers.Num(); i++)
				{
					if (Layers[i].Owner == Proxy)
					{
						Layers[i].bValid = LayerNames.Contains(Layers[i].GetLayerName());
					}
				}

				// Add placeholders for any unused material layers
				for (int32 i = 0; i < LayerNames.Num(); i++)
				{
					int32 LayerInfoIndex = GetLayerInfoIndex(LayerNames[i]);
					if (LayerInfoIndex == INDEX_NONE)
					{
						FLandscapeInfoLayerSettings LayerSettings(LayerNames[i], Proxy);
						LayerSettings.bValid = true;
						Layers.Add(LayerSettings);
					}
				}

				// Populate from layers used in components
				for (int32 ComponentIndex = 0; ComponentIndex < Proxy->LandscapeComponents.Num(); ComponentIndex++)
				{
					ULandscapeComponent* Component = Proxy->LandscapeComponents[ComponentIndex];

					// Add layers from per-component override materials
					if (Component->OverrideMaterial != nullptr)
					{
						TArray<FName> ComponentLayerNames = Proxy->GetLayersFromMaterial(Component->OverrideMaterial);
						for (int32 i = 0; i < ComponentLayerNames.Num(); i++)
						{
							int32 LayerInfoIndex = GetLayerInfoIndex(ComponentLayerNames[i]);
							if (LayerInfoIndex == INDEX_NONE)
							{
								FLandscapeInfoLayerSettings LayerSettings(ComponentLayerNames[i], Proxy);
								LayerSettings.bValid = true;
								Layers.Add(LayerSettings);
							}
						}
					}

					const TArray<FWeightmapLayerAllocationInfo>& ComponentWeightmapLayerAllocations = Component->GetWeightmapLayerAllocations();

					for (int32 AllocationIndex = 0; AllocationIndex < ComponentWeightmapLayerAllocations.Num(); AllocationIndex++)
					{
						ULandscapeLayerInfoObject* LayerInfo = ComponentWeightmapLayerAllocations[AllocationIndex].LayerInfo;
						if (LayerInfo)
						{
							int32 LayerInfoIndex = GetLayerInfoIndex(LayerInfo);
							bool bValid = LayerNames.Contains(LayerInfo->LayerName);

							if (bValid)
							{
								//LayerInfo->IsReferencedFromLoadedData = true;
							}

							if (LayerInfoIndex != INDEX_NONE)
							{
								FLandscapeInfoLayerSettings& LayerSettings = Layers[LayerInfoIndex];

								// Valid layer infos take precedence over invalid ones
								// Landscape Actors take precedence over Proxies
								if ((bValid && !LayerSettings.bValid)
									|| (bValid == LayerSettings.bValid && Proxy->IsA<ALandscape>()))
								{
									LayerSettings.Owner = Proxy;
									LayerSettings.bValid = bValid;
									LayerSettings.ThumbnailMIC = nullptr;
								}
							}
							else
							{
								// handle existing placeholder layers
								LayerInfoIndex = GetLayerInfoIndex(LayerInfo->LayerName);
								if (LayerInfoIndex != INDEX_NONE)
								{
									FLandscapeInfoLayerSettings& LayerSettings = Layers[LayerInfoIndex];

									//if (LayerSettings.Owner == Proxy)
									{
										LayerSettings.Owner = Proxy;
										LayerSettings.LayerInfoObj = LayerInfo;
										LayerSettings.bValid = bValid;
										LayerSettings.ThumbnailMIC = nullptr;
									}
								}
								else
								{
									FLandscapeInfoLayerSettings LayerSettings(LayerInfo, Proxy);
									LayerSettings.bValid = bValid;
									Layers.Add(LayerSettings);
								}
							}
						}
					}
				}

				// Add any layer infos cached in the actor
				Proxy->EditorLayerSettings.RemoveAll([](const FLandscapeEditorLayerSettings& Settings) { return Settings.LayerInfoObj == nullptr; });
				for (int32 i = 0; i < Proxy->EditorLayerSettings.Num(); i++)
				{
					FLandscapeEditorLayerSettings& EditorLayerSettings = Proxy->EditorLayerSettings[i];
					if (LayerNames.Contains(EditorLayerSettings.LayerInfoObj->LayerName))
					{
						// intentionally using the layer name here so we don't add layer infos from
						// the cache that have the same name as an actual assignment from a component above
						int32 LayerInfoIndex = GetLayerInfoIndex(EditorLayerSettings.LayerInfoObj->LayerName);
						if (LayerInfoIndex != INDEX_NONE)
						{
							FLandscapeInfoLayerSettings& LayerSettings = Layers[LayerInfoIndex];
							if (LayerSettings.LayerInfoObj == nullptr)
							{
								LayerSettings.Owner = Proxy;
								LayerSettings.LayerInfoObj = EditorLayerSettings.LayerInfoObj;
								LayerSettings.bValid = true;
							}
						}
					}
					else
					{
						Proxy->Modify();
						Proxy->EditorLayerSettings.RemoveAt(i--);
					}
				}
			}
		}
		else // !Proxy
		{
			Layers.Empty();

			if (!bInvalidate)
			{
				ForAllLandscapeProxies([this](ALandscapeProxy* EachProxy)
				{
					if (!EachProxy->IsPendingKillPending())
					{
						checkSlow(EachProxy->GetLandscapeInfo() == this);
						UpdateLayerInfoMapInternal(EachProxy, false);
					}
				});
			}
		}

		//if (GCallbackEvent)
		//{
		//	GCallbackEvent->Send( CALLBACK_EditorPostModal );
		//}
	}
	return bHasCollision;
}

bool ULandscapeInfo::UpdateLayerInfoMap(ALandscapeProxy* Proxy /*= nullptr*/, bool bInvalidate /*= false*/)
{
	bool bResult = UpdateLayerInfoMapInternal(Proxy, bInvalidate);
	if (GIsEditor)
	{
		ALandscape* Landscape = LandscapeActor.Get();
		if (Landscape && Landscape->HasLayersContent())
		{
			Landscape->RequestLayersInitialization(/*bInRequestContentUpdate*/false);
		}
	}
	return bResult;
}

#endif // WITH_EDITOR

void ALandscapeProxy::PostLoad()
{
	Super::PostLoad();

	// Temporary
	if (ComponentSizeQuads == 0 && LandscapeComponents.Num() > 0)
	{
		ULandscapeComponent* Comp = LandscapeComponents[0];
		if (Comp)
		{
			ComponentSizeQuads = Comp->ComponentSizeQuads;
			SubsectionSizeQuads = Comp->SubsectionSizeQuads;
			NumSubsections = Comp->NumSubsections;
		}
	}

	if (IsTemplate() == false)
	{
		BodyInstance.FixupData(this);
	}

	if ((GetLinker() && (GetLinker()->UEVer() < VER_UE4_LANDSCAPE_COMPONENT_LAZY_REFERENCES)) ||
		LandscapeComponents.Num() != CollisionComponents.Num() ||
		LandscapeComponents.ContainsByPredicate([](ULandscapeComponent* Comp) { return ((Comp != nullptr) && !Comp->CollisionComponent.IsValid()); }))
	{
		CreateLandscapeInfo();
	}
#if WITH_EDITOR
	PRAGMA_DISABLE_DEPRECATION_WARNINGS;
	if (!LandscapeMaterialsOverride_DEPRECATED.IsEmpty())
	{
		PerLODOverrideMaterials.Reserve(LandscapeMaterialsOverride_DEPRECATED.Num());
		for (const FLandscapeProxyMaterialOverride& LocalMaterialOverride : LandscapeMaterialsOverride_DEPRECATED)
		{
			PerLODOverrideMaterials.Add({ LocalMaterialOverride.LODIndex.Default, LocalMaterialOverride.Material });
		}
		LandscapeMaterialsOverride_DEPRECATED.Reset();
	}
	PRAGMA_ENABLE_DEPRECATION_WARNINGS;

	if (GIsEditor && GetWorld() && !GetWorld()->IsGameWorld())
	{
		if ((GetLinker() && (GetLinker()->UEVer() < VER_UE4_LANDSCAPE_COMPONENT_LAZY_REFERENCES)) ||
			LandscapeComponents.Num() != CollisionComponents.Num() ||
			LandscapeComponents.ContainsByPredicate([](ULandscapeComponent* Comp) { return ((Comp != nullptr) && !Comp->CollisionComponent.IsValid()); }))
		{
			// Need to clean up invalid collision components
			RecreateCollisionComponents();
		}
	}

	EditorLayerSettings.RemoveAll([](const FLandscapeEditorLayerSettings& Settings) { return Settings.LayerInfoObj == nullptr; });

	if (EditorCachedLayerInfos_DEPRECATED.Num() > 0)
	{
		for (int32 i = 0; i < EditorCachedLayerInfos_DEPRECATED.Num(); i++)
		{
			EditorLayerSettings.Add(FLandscapeEditorLayerSettings(EditorCachedLayerInfos_DEPRECATED[i]));
		}
		EditorCachedLayerInfos_DEPRECATED.Empty();
	}

	bool bFixedUpInvalidMaterialInstances = false;

	for (ULandscapeComponent* Comp : LandscapeComponents)
	{
		if (Comp)
		{
			// Validate the layer combination and store it in the MaterialInstanceConstantMap
			if (UMaterialInstance* MaterialInstance = Comp->GetMaterialInstance(0, false))
			{
				UMaterialInstanceConstant* CombinationMaterialInstance = Cast<UMaterialInstanceConstant>(MaterialInstance->Parent);
				// Only validate if uncooked and in the editor/commandlet mode (we cannot re-build material instance constants if this is not the case : see UMaterialInstance::CacheResourceShadersForRendering, which is only called if FApp::CanEverRender() returns true) 
				if (!Comp->GetOutermost()->HasAnyPackageFlags(PKG_FilterEditorOnly) && (GIsEditor && FApp::CanEverRender()))
				{
					if (Comp->ValidateCombinationMaterial(CombinationMaterialInstance))
					{
						MaterialInstanceConstantMap.Add(*ULandscapeComponent::GetLayerAllocationKey(Comp->GetWeightmapLayerAllocations(), CombinationMaterialInstance->Parent), CombinationMaterialInstance);
					}
					else
					{
						// There was a problem with the loaded material : it doesn't match the expected material combination, we need to regenerate the material instances : 
						Comp->UpdateMaterialInstances();
						bFixedUpInvalidMaterialInstances = true;
					}
				}
				else if (CombinationMaterialInstance)
				{
					// Skip ValidateCombinationMaterial
					MaterialInstanceConstantMap.Add(*ULandscapeComponent::GetLayerAllocationKey(Comp->GetWeightmapLayerAllocations(), CombinationMaterialInstance->Parent), CombinationMaterialInstance);
				}
			}
		}
	}

	if (bFixedUpInvalidMaterialInstances)
	{
		FFormatNamedArguments Arguments;
		Arguments.Add(TEXT("LandscapeName"), FText::FromString(GetPathName()));
		Arguments.Add(TEXT("ProxyPackage"), FText::FromString(GetOutermost()->GetName()));
		FMessageLog("MapCheck").Info()
			->AddToken(FTextToken::Create(FText::Format(LOCTEXT("MapCheck_Message_FixedUpInvalidLandscapeMaterialInstances", "{LandscapeName} : Fixed up invalid landscape material instances. Please re-save {ProxyPackage}."), Arguments)))
			->AddToken(FMapErrorToken::Create(FMapErrors::FixedUpInvalidLandscapeMaterialInstances));
	}

	// track feature level change to flush grass cache
	if (GetWorld())
	{
		FOnFeatureLevelChanged::FDelegate FeatureLevelChangedDelegate = FOnFeatureLevelChanged::FDelegate::CreateUObject(this, &ALandscapeProxy::OnFeatureLevelChanged);
		FeatureLevelChangedDelegateHandle = GetWorld()->AddOnFeatureLevelChangedHandler(FeatureLevelChangedDelegate);
	}
	RepairInvalidTextures();
#endif
}

FIntPoint ALandscapeProxy::GetSectionBaseOffset() const
{
	return LandscapeSectionOffset;
}

#if WITH_EDITOR
void ALandscapeProxy::Destroyed()
{
	Super::Destroyed();

	UWorld* World = GetWorld();

	if (GIsEditor && !World->IsGameWorld())
	{
		ULandscapeInfo::RecreateLandscapeInfo(World, false);

		if (SplineComponent)
		{
			SplineComponent->ModifySplines();
		}

		TotalComponentsNeedingGrassMapRender -= NumComponentsNeedingGrassMapRender;
		NumComponentsNeedingGrassMapRender = 0;
		TotalTexturesToStreamForVisibleGrassMapRender -= NumTexturesToStreamForVisibleGrassMapRender;
		NumTexturesToStreamForVisibleGrassMapRender = 0;
	}

	// unregister feature level changed handler for grass
	if (FeatureLevelChangedDelegateHandle.IsValid())
	{
		World->RemoveOnFeatureLevelChangedHandler(FeatureLevelChangedDelegateHandle);
		FeatureLevelChangedDelegateHandle.Reset();
	}
}

void ALandscapeProxy::GetSharedProperties(ALandscapeProxy* Landscape)
{
	if (GIsEditor && Landscape)
	{
		Modify();

		LandscapeGuid = Landscape->LandscapeGuid;

		//@todo UE merge, landscape, this needs work
		RootComponent->SetRelativeScale3D(Landscape->GetRootComponent()->GetComponentToWorld().GetScale3D());

		//PrePivot = Landscape->PrePivot;
		StaticLightingResolution = Landscape->StaticLightingResolution;
		CastShadow = Landscape->CastShadow;
		bCastDynamicShadow = Landscape->bCastDynamicShadow;
		bCastStaticShadow = Landscape->bCastStaticShadow;
		bCastContactShadow = Landscape->bCastContactShadow;
		bCastFarShadow = Landscape->bCastFarShadow;
		bCastHiddenShadow = Landscape->bCastHiddenShadow;
		bCastShadowAsTwoSided = Landscape->bCastShadowAsTwoSided;
		bAffectDistanceFieldLighting = Landscape->bAffectDistanceFieldLighting;
		LightingChannels = Landscape->LightingChannels;
		bRenderCustomDepth = Landscape->bRenderCustomDepth;
		CustomDepthStencilWriteMask = Landscape->CustomDepthStencilWriteMask;
		CustomDepthStencilValue = Landscape->CustomDepthStencilValue;
		LDMaxDrawDistance = Landscape->LDMaxDrawDistance;
		ComponentSizeQuads = Landscape->ComponentSizeQuads;
		NumSubsections = Landscape->NumSubsections;
		SubsectionSizeQuads = Landscape->SubsectionSizeQuads;
		MaxLODLevel = Landscape->MaxLODLevel;
		LODDistanceFactor_DEPRECATED = Landscape->LODDistanceFactor_DEPRECATED;
		LODFalloff_DEPRECATED = Landscape->LODFalloff_DEPRECATED;
		ComponentScreenSizeToUseSubSections = Landscape->ComponentScreenSizeToUseSubSections;
		LODDistributionSetting = Landscape->LODDistributionSetting;
		LOD0DistributionSetting = Landscape->LOD0DistributionSetting;
		LOD0ScreenSize = Landscape->LOD0ScreenSize;
		NegativeZBoundsExtension = Landscape->NegativeZBoundsExtension;
		PositiveZBoundsExtension = Landscape->PositiveZBoundsExtension;
		CollisionMipLevel = Landscape->CollisionMipLevel;
		bBakeMaterialPositionOffsetIntoCollision = Landscape->bBakeMaterialPositionOffsetIntoCollision;
		RuntimeVirtualTextures = Landscape->RuntimeVirtualTextures;
		VirtualTextureLodBias = Landscape->VirtualTextureLodBias;
		VirtualTextureNumLods = Landscape->VirtualTextureNumLods;
		VirtualTextureRenderPassType = Landscape->VirtualTextureRenderPassType;

		if (!LandscapeMaterial)
		{
			LandscapeMaterial = Landscape->LandscapeMaterial;
			PerLODOverrideMaterials = Landscape->PerLODOverrideMaterials;
		}
		if (!LandscapeHoleMaterial)
		{
			LandscapeHoleMaterial = Landscape->LandscapeHoleMaterial;
		}
		if (LandscapeMaterial == Landscape->LandscapeMaterial)
		{
			EditorLayerSettings = Landscape->EditorLayerSettings;
		}
		if (!DefaultPhysMaterial)
		{
			DefaultPhysMaterial = Landscape->DefaultPhysMaterial;
		}
		LightmassSettings = Landscape->LightmassSettings;
	}
}

void ALandscapeProxy::FixupSharedData(ALandscape* Landscape)
{
	if (Landscape == nullptr)
	{
		return;
	}

	bool bUpdated = false;

	if (MaxLODLevel != Landscape->MaxLODLevel)
	{
		MaxLODLevel = Landscape->MaxLODLevel;
		bUpdated = true;
	}
	
	if (ComponentScreenSizeToUseSubSections != Landscape->ComponentScreenSizeToUseSubSections)
	{
		ComponentScreenSizeToUseSubSections = Landscape->ComponentScreenSizeToUseSubSections;
		bUpdated = true;
	}

	if (LODDistributionSetting != Landscape->LODDistributionSetting)
	{
		LODDistributionSetting = Landscape->LODDistributionSetting;
		bUpdated = true;
	}

	if (LOD0DistributionSetting != Landscape->LOD0DistributionSetting)
	{
		LOD0DistributionSetting = Landscape->LOD0DistributionSetting;
		bUpdated = true;
	}

	if (LOD0ScreenSize != Landscape->LOD0ScreenSize)
	{
		LOD0ScreenSize = Landscape->LOD0ScreenSize;
		bUpdated = true;
	}

	if (TargetDisplayOrder != Landscape->TargetDisplayOrder)
	{
		TargetDisplayOrder = Landscape->TargetDisplayOrder;
		bUpdated = true;
	}

	if (TargetDisplayOrderList != Landscape->TargetDisplayOrderList)
	{
		TargetDisplayOrderList = Landscape->TargetDisplayOrderList;
		bUpdated = true;
	}


	TSet<FGuid> LayerGuids;
	Algo::Transform(Landscape->LandscapeLayers, LayerGuids, [](const FLandscapeLayer& Layer) { return Layer.Guid; });
	bUpdated |= RemoveObsoleteLayers(LayerGuids);

	for (const FLandscapeLayer& Layer : Landscape->LandscapeLayers)
	{
		bUpdated |= AddLayer(Layer.Guid);
	}

	if (bUpdated)
	{
		MarkPackageDirty();
	}
}

void ALandscapeProxy::SetAbsoluteSectionBase(FIntPoint InSectionBase)
{
	FIntPoint Difference = InSectionBase - LandscapeSectionOffset;
	LandscapeSectionOffset = InSectionBase;

	RecreateComponentsRenderState([Difference](ULandscapeComponent* Comp)
	{
		FIntPoint AbsoluteSectionBase = Comp->GetSectionBase() + Difference;
		Comp->SetSectionBase(AbsoluteSectionBase);
	});

	for (int32 CompIdx = 0; CompIdx < CollisionComponents.Num(); CompIdx++)
	{
		ULandscapeHeightfieldCollisionComponent* Comp = CollisionComponents[CompIdx];
		if (Comp)
		{
			FIntPoint AbsoluteSectionBase = Comp->GetSectionBase() + Difference;
			Comp->SetSectionBase(AbsoluteSectionBase);
		}
	}
}

void ALandscapeProxy::RecreateComponentsState()
{
	RecreateComponentsRenderState([](ULandscapeComponent* Comp)
	{
		Comp->UpdateComponentToWorld();
		Comp->UpdateCachedBounds();
		Comp->UpdateBounds();
	});

	for (int32 ComponentIndex = 0; ComponentIndex < CollisionComponents.Num(); ComponentIndex++)
	{
		ULandscapeHeightfieldCollisionComponent* Comp = CollisionComponents[ComponentIndex];
		if (Comp)
		{
			Comp->UpdateComponentToWorld();
			Comp->RecreatePhysicsState();
		}
	}
}

void ALandscapeProxy::RecreateComponentsRenderState(TFunctionRef<void(ULandscapeComponent*)> Fn)
{
	// Batch component render state recreation
	TArray<FComponentRecreateRenderStateContext> ComponentRecreateRenderStates;
	ComponentRecreateRenderStates.Reserve(LandscapeComponents.Num());

	for (int32 ComponentIndex = 0; ComponentIndex < LandscapeComponents.Num(); ComponentIndex++)
	{
		ULandscapeComponent* Comp = LandscapeComponents[ComponentIndex];
		if (Comp)
		{
			Fn(Comp);
			ComponentRecreateRenderStates.Emplace(Comp);
		}
	}
}

UMaterialInterface* ALandscapeProxy::GetLandscapeMaterial(int8 InLODIndex) const
{
	if (InLODIndex != INDEX_NONE)
	{
		UWorld* World = GetWorld();

		if (World != nullptr)
		{
			if (const FLandscapePerLODMaterialOverride* LocalMaterialOverride = PerLODOverrideMaterials.FindByPredicate(
				[InLODIndex](const FLandscapePerLODMaterialOverride& InOverride) { return (InOverride.LODIndex == InLODIndex) && (InOverride.Material != nullptr); }))
			{
				return LocalMaterialOverride->Material;
			}
		}
	}

	return LandscapeMaterial != nullptr ? LandscapeMaterial : UMaterial::GetDefaultMaterial(MD_Surface);
}

UMaterialInterface* ALandscapeProxy::GetLandscapeHoleMaterial() const
{
	return LandscapeHoleMaterial;
}

UMaterialInterface* ALandscapeStreamingProxy::GetLandscapeMaterial(int8 InLODIndex) const
{
	if (InLODIndex != INDEX_NONE)
	{
		UWorld* World = GetWorld();

		if (World != nullptr)
		{
			if (const FLandscapePerLODMaterialOverride* LocalMaterialOverride = PerLODOverrideMaterials.FindByPredicate(
				[InLODIndex](const FLandscapePerLODMaterialOverride& InOverride) { return (InOverride.LODIndex == InLODIndex) && (InOverride.Material != nullptr); }))
			{
				return LocalMaterialOverride->Material;
			}
		}
	}

	if (LandscapeMaterial != nullptr)
	{
		return LandscapeMaterial;
	}

	if (LandscapeActor != nullptr)
	{
		return LandscapeActor->GetLandscapeMaterial(InLODIndex);
	}

	return UMaterial::GetDefaultMaterial(MD_Surface);
}

UMaterialInterface* ALandscapeStreamingProxy::GetLandscapeHoleMaterial() const
{
	if (LandscapeHoleMaterial)
	{
		return LandscapeHoleMaterial;
	}
	else if (ALandscape* Landscape = LandscapeActor.Get())
	{
		return Landscape->GetLandscapeHoleMaterial();
	}
	return nullptr;
}

void ALandscape::PreSave(const class ITargetPlatform* TargetPlatform)
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS;
	Super::PreSave(TargetPlatform);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS;
}

void ALandscape::PreSave(FObjectPreSaveContext ObjectSaveContext)
{
	Super::PreSave(ObjectSaveContext);
	//ULandscapeInfo* Info = GetLandscapeInfo();
	//if (GIsEditor && Info && !ObjectSaveContext.IsProceduralSave())
	//{
	//	for (TSet<ALandscapeProxy*>::TIterator It(Info->Proxies); It; ++It)
	//	{
	//		ALandscapeProxy* Proxy = *It;
	//		if (!ensure(Proxy->LandscapeActor == this))
	//		{
	//			Proxy->LandscapeActor = this;
	//			Proxy->GetSharedProperties(this);
	//		}
	//	}
	//}
}

ALandscapeProxy* ULandscapeInfo::GetLandscapeProxyForLevel(ULevel* Level) const
{
	ALandscapeProxy* LandscapeProxy = nullptr;
	ForAllLandscapeProxies([&LandscapeProxy, Level](ALandscapeProxy* Proxy)
	{
		if (Proxy->GetLevel() == Level)
		{
			LandscapeProxy = Proxy;
		}
	});
	return LandscapeProxy;
}

ALandscapeProxy* ULandscapeInfo::GetCurrentLevelLandscapeProxy(bool bRegistered) const
{
	ALandscapeProxy* LandscapeProxy = nullptr;
	ForAllLandscapeProxies([&LandscapeProxy, bRegistered](ALandscapeProxy* Proxy)
	{
		if (!bRegistered || Proxy->GetRootComponent()->IsRegistered())
		{
			UWorld* ProxyWorld = Proxy->GetWorld();
			if (ProxyWorld &&
				ProxyWorld->GetCurrentLevel() == Proxy->GetOuter())
			{
				LandscapeProxy = Proxy;
			}
		}
	});
	return LandscapeProxy;
}

ALandscapeProxy* ULandscapeInfo::GetLandscapeProxy() const
{
	// Mostly this Proxy used to calculate transformations
	// in Editor all proxies of same landscape actor have root components in same locations
	// so it doesn't really matter which proxy we return here

	// prefer LandscapeActor in case it is loaded
	if (LandscapeActor.IsValid())
	{
		ALandscape* Landscape = LandscapeActor.Get();
		if (Landscape != nullptr &&
			Landscape->GetRootComponent()->IsRegistered())
		{
			return Landscape;
		}
	}

	// prefer current level proxy 
	ALandscapeProxy* Proxy = GetCurrentLevelLandscapeProxy(true);
	if (Proxy != nullptr)
	{
		return Proxy;
	}

	// any proxy in the world
	for (auto It = Proxies.CreateConstIterator(); It; ++It)
	{
		Proxy = (*It);
		if (Proxy != nullptr &&
			Proxy->GetRootComponent()->IsRegistered())
		{
			return Proxy;
		}
	}

	return nullptr;
}

void ULandscapeInfo::Reset()
{
	LandscapeActor.Reset();

	Proxies.Empty();
	XYtoComponentMap.Empty();
	XYtoAddCollisionMap.Empty();

	//SelectedComponents.Empty();
	//SelectedRegionComponents.Empty();
	//SelectedRegion.Empty();
}

void ULandscapeInfo::FixupProxiesTransform(bool bDirty)
{
	ALandscape* Landscape = LandscapeActor.Get();

	if (Landscape == nullptr ||
		Landscape->GetRootComponent()->IsRegistered() == false)
	{
		return;
	}

	// Make sure section offset of all proxies is multiple of ALandscapeProxy::ComponentSizeQuads
	for (auto It = Proxies.CreateConstIterator(); It; ++It)
	{
		ALandscapeProxy* Proxy = *It;

		if (bDirty)
		{
			Proxy->Modify();
		}

		FIntPoint LandscapeSectionOffset = Proxy->LandscapeSectionOffset - Landscape->LandscapeSectionOffset;
		FIntPoint LandscapeSectionOffsetRem(
			LandscapeSectionOffset.X % Proxy->ComponentSizeQuads,
			LandscapeSectionOffset.Y % Proxy->ComponentSizeQuads);

		if (LandscapeSectionOffsetRem.X != 0 || LandscapeSectionOffsetRem.Y != 0)
		{
			FIntPoint NewLandscapeSectionOffset = Proxy->LandscapeSectionOffset - LandscapeSectionOffsetRem;

			UE_LOG(LogLandscape, Warning, TEXT("Landscape section base is not multiple of component size, attempted automated fix: '%s', %d,%d vs %d,%d."),
				*Proxy->GetFullName(), Proxy->LandscapeSectionOffset.X, Proxy->LandscapeSectionOffset.Y, NewLandscapeSectionOffset.X, NewLandscapeSectionOffset.Y);

			Proxy->SetAbsoluteSectionBase(NewLandscapeSectionOffset);
		}
	}

	FTransform LandscapeTM = Landscape->LandscapeActorToWorld();
	// Update transformations of all linked landscape proxies
	for (auto It = Proxies.CreateConstIterator(); It; ++It)
	{
		ALandscapeProxy* Proxy = *It;
		FTransform ProxyRelativeTM(FVector(Proxy->LandscapeSectionOffset));
		FTransform ProxyTransform = ProxyRelativeTM * LandscapeTM;

		if (!Proxy->GetTransform().Equals(ProxyTransform))
		{
			Proxy->SetActorTransform(ProxyTransform);

			// Let other systems know that an actor was moved
			GEngine->BroadcastOnActorMoved(Proxy);
		}
	}
}

void ULandscapeInfo::UpdateComponentLayerAllowList()
{
	ForAllLandscapeProxies([](ALandscapeProxy* Proxy)
	{
		for (ULandscapeComponent* Comp : Proxy->LandscapeComponents)
		{
			Comp->UpdateLayerAllowListFromPaintedLayers();
		}
	});
}

void ULandscapeInfo::RecreateLandscapeInfo(UWorld* InWorld, bool bMapCheck)
{
	check(InWorld);

	ULandscapeInfoMap& LandscapeInfoMap = ULandscapeInfoMap::GetLandscapeInfoMap(InWorld);
	LandscapeInfoMap.Modify();

	// reset all LandscapeInfo objects
	for (auto& LandscapeInfoPair : LandscapeInfoMap.Map)
	{
		ULandscapeInfo* LandscapeInfo = LandscapeInfoPair.Value;

		if (LandscapeInfo != nullptr)
		{
			LandscapeInfo->Modify();
			LandscapeInfo->Reset();
		}
	}

	TMap<FGuid, TArray<ALandscapeProxy*>> ValidLandscapesMap;
	// Gather all valid landscapes in the world
	for (ALandscapeProxy* Proxy : TActorRange<ALandscapeProxy>(InWorld))
	{
		if (Proxy->GetLevel() &&
			Proxy->GetLevel()->bIsVisible &&
			!Proxy->HasAnyFlags(RF_BeginDestroyed) &&
			IsValid(Proxy) &&
			!Proxy->IsPendingKillPending())
		{
			ValidLandscapesMap.FindOrAdd(Proxy->GetLandscapeGuid()).Add(Proxy);
		}
	}

	// Register landscapes in global landscape map
	for (auto& ValidLandscapesPair : ValidLandscapesMap)
	{
		auto& LandscapeList = ValidLandscapesPair.Value;
		for (ALandscapeProxy* Proxy : LandscapeList)
		{
			Proxy->CreateLandscapeInfo()->RegisterActor(Proxy, bMapCheck);
		}
	}

	// Remove empty entries from global LandscapeInfo map
	for (auto It = LandscapeInfoMap.Map.CreateIterator(); It; ++It)
	{
		ULandscapeInfo* Info = It.Value();

		if (Info != nullptr && Info->GetLandscapeProxy() == nullptr)
		{
			Info->MarkAsGarbage();
			It.RemoveCurrent();
		}
		else if (Info == nullptr) // remove invalid entry
		{
			It.RemoveCurrent();
		}
	}

	// We need to inform Landscape editor tools about LandscapeInfo updates
	FEditorSupportDelegates::WorldChange.Broadcast();
}


#endif

ULandscapeInfo* ULandscapeInfo::Find(UWorld* InWorld, const FGuid& LandscapeGuid)
{
	ULandscapeInfo* LandscapeInfo = nullptr;

	check(LandscapeGuid.IsValid());
	if (InWorld != nullptr)
	{
		auto& LandscapeInfoMap = ULandscapeInfoMap::GetLandscapeInfoMap(InWorld);
		LandscapeInfo = LandscapeInfoMap.Map.FindRef(LandscapeGuid);
	}
	return LandscapeInfo;
}

ULandscapeInfo* ULandscapeInfo::FindOrCreate(UWorld* InWorld, const FGuid& LandscapeGuid)
{
	ULandscapeInfo* LandscapeInfo = nullptr;

	check(LandscapeGuid.IsValid());
	check(InWorld);

	auto& LandscapeInfoMap = ULandscapeInfoMap::GetLandscapeInfoMap(InWorld);
	LandscapeInfo = LandscapeInfoMap.Map.FindRef(LandscapeGuid);

	if (!LandscapeInfo)
	{
		LandscapeInfo = NewObject<ULandscapeInfo>(GetTransientPackage(), NAME_None, RF_Transactional | RF_Transient);
		LandscapeInfoMap.Modify(false);
		LandscapeInfo->Initialize(InWorld, LandscapeGuid);
		LandscapeInfoMap.Map.Add(LandscapeGuid, LandscapeInfo);
	}
	check(LandscapeInfo);
	return LandscapeInfo;
}

void ULandscapeInfo::Initialize(UWorld* InWorld, const FGuid& InLandscapeGuid)
{
	LandscapeGuid = InLandscapeGuid;
}

void ULandscapeInfo::ForAllLandscapeProxies(TFunctionRef<void(ALandscapeProxy*)> Fn) const
{
	ALandscape* Landscape = LandscapeActor.Get();
	if (Landscape)
	{
		Fn(Landscape);
	}

	for (ALandscapeProxy* LandscapeProxy : Proxies)
	{
		Fn(LandscapeProxy);
	}
}

void ULandscapeInfo::RegisterActor(ALandscapeProxy* Proxy, bool bMapCheck)
{
	UWorld* OwningWorld = Proxy->GetWorld();
	// do not pass here invalid actors
	checkSlow(Proxy);
	check(Proxy->GetLandscapeGuid().IsValid());
	check(LandscapeGuid.IsValid());
	
#if WITH_EDITOR
	if (!OwningWorld->IsGameWorld())
	{
		// in case this Info object is not initialized yet
		// initialized it with properties from passed actor
		if (GetLandscapeProxy() == nullptr)
		{
			ComponentSizeQuads = Proxy->ComponentSizeQuads;
			ComponentNumSubsections = Proxy->NumSubsections;
			SubsectionSizeQuads = Proxy->SubsectionSizeQuads;
			DrawScale = Proxy->GetRootComponent() != nullptr ? Proxy->GetRootComponent()->GetRelativeScale3D() : FVector(100.0f);
		}

		// check that passed actor matches all shared parameters
		check(LandscapeGuid == Proxy->GetLandscapeGuid());
		check(ComponentSizeQuads == Proxy->ComponentSizeQuads);
		check(ComponentNumSubsections == Proxy->NumSubsections);
		check(SubsectionSizeQuads == Proxy->SubsectionSizeQuads);

		if (Proxy->GetRootComponent() != nullptr && !DrawScale.Equals(Proxy->GetRootComponent()->GetRelativeScale3D()))
		{
			UE_LOG(LogLandscape, Warning, TEXT("Landscape proxy (%s) scale (%s) does not match to main actor scale (%s)."),
				*Proxy->GetName(), *Proxy->GetRootComponent()->GetRelativeScale3D().ToCompactString(), *DrawScale.ToCompactString());
		}

		// register
		if (ALandscape* Landscape = Cast<ALandscape>(Proxy))
		{
			checkf(!LandscapeActor || LandscapeActor == Landscape, TEXT("Multiple landscapes with the same GUID detected: %s vs %s"), *LandscapeActor->GetPathName(), *Landscape->GetPathName());
			LandscapeActor = Landscape;
			// In world composition user is not allowed to move landscape in editor, only through WorldBrowser 
			bool bIsLockLocation = LandscapeActor->IsLockLocation();
			bIsLockLocation |= OwningWorld != nullptr ? OwningWorld->WorldComposition != nullptr : false;
			LandscapeActor->SetLockLocation(bIsLockLocation);

			// update proxies reference actor
			for (ALandscapeStreamingProxy* StreamingProxy : Proxies)
			{
				StreamingProxy->LandscapeActor = LandscapeActor;
				StreamingProxy->FixupSharedData(Landscape);
			}
		}
		else
		{
			auto LamdbdaLowerBound = [](ALandscapeProxy* A, ALandscapeProxy* B)
			{
				FIntPoint SectionBaseA = A->GetSectionBaseOffset();
				FIntPoint SectionBaseB = B->GetSectionBaseOffset();

				if (SectionBaseA.X != SectionBaseB.X)
				{
					return SectionBaseA.X < SectionBaseB.X;
				}

				return SectionBaseA.Y < SectionBaseB.Y;
			};

			// Insert Proxies in a sorted fashion for generating deterministic results in the Layer system
			ALandscapeStreamingProxy* StreamingProxy = CastChecked<ALandscapeStreamingProxy>(Proxy);
			if (!Proxies.Contains(Proxy))
			{
				uint32 InsertIndex = Algo::LowerBound(Proxies, Proxy, LamdbdaLowerBound);
				Proxies.Insert(StreamingProxy, InsertIndex);
			}
			StreamingProxy->LandscapeActor = LandscapeActor;
			StreamingProxy->FixupSharedData(LandscapeActor.Get());
		}

		UpdateLayerInfoMap(Proxy);
		UpdateAllAddCollisions();

		RegisterSplineActor(Proxy);
	}
#endif

	//
	// add proxy components to the XY map
	//
	for (int32 CompIdx = 0; CompIdx < Proxy->LandscapeComponents.Num(); ++CompIdx)
	{
		RegisterActorComponent(Proxy->LandscapeComponents[CompIdx], bMapCheck);
	}

	for (ULandscapeHeightfieldCollisionComponent* CollComp : Proxy->CollisionComponents)
	{
		RegisterCollisionComponent(CollComp);
	}
}

void ULandscapeInfo::UnregisterActor(ALandscapeProxy* Proxy)
{
	UWorld* OwningWorld = Proxy->GetWorld();
#if WITH_EDITOR
	if (!OwningWorld->IsGameWorld())
	{
		if (ALandscape* Landscape = Cast<ALandscape>(Proxy))
		{
			// Note: UnregisterActor sometimes gets triggered twice, e.g. it has been observed to happen during redo
			// Note: In some cases LandscapeActor could be updated to a new landscape actor before the old landscape is unregistered/destroyed
			// e.g. this has been observed when merging levels in the editor

			if (LandscapeActor.Get() == Landscape)
			{
				LandscapeActor = nullptr;
			}

			// update proxies reference to landscape actor
			for (ALandscapeStreamingProxy* StreamingProxy : Proxies)
			{
				StreamingProxy->LandscapeActor = LandscapeActor;
			}
		}
		else
		{
			ALandscapeStreamingProxy* StreamingProxy = CastChecked<ALandscapeStreamingProxy>(Proxy);
			Proxies.Remove(StreamingProxy);
			StreamingProxy->LandscapeActor = nullptr;
		}

		UnregisterSplineActor(Proxy);
	}
#endif

	// remove proxy components from the XY map
	for (int32 CompIdx = 0; CompIdx < Proxy->LandscapeComponents.Num(); ++CompIdx)
	{
		ULandscapeComponent* Component = Proxy->LandscapeComponents[CompIdx];
		if (Component) // When a landscape actor is being GC'd it's possible the components were already GC'd and are null
		{
			UnregisterActorComponent(Component);
		}
	}
	XYtoComponentMap.Compact();

	for (ULandscapeHeightfieldCollisionComponent* CollComp : Proxy->CollisionComponents)
	{
		if (CollComp)
		{
			UnregisterCollisionComponent(CollComp);
		}
	}
	XYtoCollisionComponentMap.Compact();

#if WITH_EDITOR
	if (!OwningWorld->IsGameWorld())
	{
		UpdateLayerInfoMap();
		UpdateAllAddCollisions();
	}
#endif
}

#if WITH_EDITOR
ALandscapeSplineActor* ULandscapeInfo::CreateSplineActor(const FVector& Location)
{
	check(LandscapeActor.Get());
	UWorld* World = LandscapeActor->GetWorld();
	check(World);
	FActorSpawnParameters SpawnParams;
	SpawnParams.OverrideLevel = World->PersistentLevel;
	SpawnParams.bNoFail = true;
	SpawnParams.ObjectFlags |= RF_Transactional;
	ALandscapeSplineActor* SplineActor = World->SpawnActor<ALandscapeSplineActor>(Location, FRotator::ZeroRotator, SpawnParams);
	SplineActor->GetSharedProperties(this);
	SplineActor->GetSplinesComponent()->ShowSplineEditorMesh(true);
	RegisterSplineActor(SplineActor);
	return SplineActor;
}

void ULandscapeInfo::ForAllSplineActors(TFunctionRef<void(TScriptInterface<ILandscapeSplineInterface>)> Fn) const
{
	for (const TScriptInterface<ILandscapeSplineInterface>& SplineActor : SplineActors)
	{
		Fn(SplineActor);
	}
}

TArray<TScriptInterface<ILandscapeSplineInterface>> ULandscapeInfo::GetSplineActors() const
{
	TArray<TScriptInterface<ILandscapeSplineInterface>> CopySplineActors(SplineActors);
	return MoveTemp(CopySplineActors);
}

void ULandscapeInfo::RegisterSplineActor(TScriptInterface<ILandscapeSplineInterface> SplineActor)
{
	Modify();

	// Sort on insert to ensure spline actors are always processed in the same order, regardless of variation in the
	// sub level streaming/registration sequence.
	auto SortPredicate = [](const TScriptInterface<ILandscapeSplineInterface>& A, const TScriptInterface<ILandscapeSplineInterface>& B)
	{
		return Cast<UObject>(A.GetInterface())->GetPathName() < Cast<UObject>(B.GetInterface())->GetPathName();
	};

	// Add a unique entry, sorted
	const int32 LBoundIdx = Algo::LowerBound(SplineActors, SplineActor, SortPredicate);
	if (LBoundIdx == SplineActors.Num() || SplineActors[LBoundIdx] != SplineActor)
	{
		SplineActors.Insert(SplineActor, LBoundIdx);
	}

	if (SplineActor->GetSplinesComponent())
	{
		RequestSplineLayerUpdate();
	}
}

void ULandscapeInfo::UnregisterSplineActor(TScriptInterface<ILandscapeSplineInterface> SplineActor)
{
	Modify();
	SplineActors.Remove(SplineActor);

	if (SplineActor->GetSplinesComponent())
	{
		RequestSplineLayerUpdate();
	}
}

void ULandscapeInfo::RequestSplineLayerUpdate()
{
	if (LandscapeActor)
	{
		LandscapeActor->RequestSplineLayerUpdate();
	}
}

void ULandscapeInfo::ForceLayersFullUpdate()
{
	if (LandscapeActor)
	{
		LandscapeActor->ForceLayersFullUpdate();
	}
}
#endif

void ULandscapeInfo::RegisterCollisionComponent(ULandscapeHeightfieldCollisionComponent* Component)
{
	if (Component == nullptr || !Component->IsRegistered())
	{
		return;
	}

	FIntPoint ComponentKey = Component->GetSectionBase() / Component->CollisionSizeQuads;
	auto RegisteredComponent = XYtoCollisionComponentMap.FindRef(ComponentKey);

	if (RegisteredComponent != Component)
	{
		if (RegisteredComponent == nullptr)
		{
			XYtoCollisionComponentMap.Add(ComponentKey, Component);
		}
	}
}

void ULandscapeInfo::UnregisterCollisionComponent(ULandscapeHeightfieldCollisionComponent* Component)
{
	if (ensure(Component))
	{
		FIntPoint ComponentKey = Component->GetSectionBase() / Component->CollisionSizeQuads;
		auto RegisteredComponent = XYtoCollisionComponentMap.FindRef(ComponentKey);

		if (RegisteredComponent == Component)
		{
			XYtoCollisionComponentMap.Remove(ComponentKey);
		}
	}
}

void ULandscapeInfo::RegisterActorComponent(ULandscapeComponent* Component, bool bMapCheck)
{
	// Do not register components which are not part of the world
	if (Component == nullptr ||
		Component->IsRegistered() == false)
	{
		return;
	}

	check(Component);

	FIntPoint ComponentKey = Component->GetSectionBase() / Component->ComponentSizeQuads;
	auto RegisteredComponent = XYtoComponentMap.FindRef(ComponentKey);

	if (RegisteredComponent != Component)
	{
		if (RegisteredComponent == nullptr)
		{
			XYtoComponentMap.Add(ComponentKey, Component);
		}
		else if (bMapCheck)
		{
#if WITH_EDITOR
			ALandscapeProxy* OurProxy = Component->GetLandscapeProxy();
			ALandscapeProxy* ExistingProxy = RegisteredComponent->GetLandscapeProxy();
			FFormatNamedArguments Arguments;
			Arguments.Add(TEXT("ProxyName1"), FText::FromString(OurProxy->GetName()));
			Arguments.Add(TEXT("LevelName1"), FText::FromString(OurProxy->GetLevel()->GetOutermost()->GetName()));
			Arguments.Add(TEXT("ProxyName2"), FText::FromString(ExistingProxy->GetName()));
			Arguments.Add(TEXT("LevelName2"), FText::FromString(ExistingProxy->GetLevel()->GetOutermost()->GetName()));
			Arguments.Add(TEXT("XLocation"), Component->GetSectionBase().X);
			Arguments.Add(TEXT("YLocation"), Component->GetSectionBase().Y);
			FMessageLog("MapCheck").Warning()
				->AddToken(FUObjectToken::Create(OurProxy))
				->AddToken(FTextToken::Create(FText::Format(LOCTEXT("MapCheck_Message_LandscapeComponentPostLoad_Warning", "Landscape {ProxyName1} of {LevelName1} has overlapping render components with {ProxyName2} of {LevelName2} at location ({XLocation}, {YLocation})."), Arguments)))
				->AddToken(FActionToken::Create(LOCTEXT("MapCheck_RemoveDuplicateLandscapeComponent", "Delete Duplicate"), LOCTEXT("MapCheck_RemoveDuplicateLandscapeComponentDesc", "Deletes the duplicate landscape component."), FOnActionTokenExecuted::CreateUObject(OurProxy, &ALandscapeProxy::RemoveOverlappingComponent, Component), true))
				->AddToken(FMapErrorToken::Create(FMapErrors::LandscapeComponentPostLoad_Warning));

			// Show MapCheck window
			FMessageLog("MapCheck").Open(EMessageSeverity::Warning);
#endif
		}
	}


#if WITH_EDITOR
	// Update Selected Components/Regions
	if (Component->EditToolRenderData.SelectedType)
	{
		if (Component->EditToolRenderData.SelectedType & FLandscapeEditToolRenderData::ST_COMPONENT)
		{
			SelectedComponents.Add(Component);
		}
		else if (Component->EditToolRenderData.SelectedType & FLandscapeEditToolRenderData::ST_REGION)
		{
			SelectedRegionComponents.Add(Component);
		}
	}
#endif
}

void ULandscapeInfo::UnregisterActorComponent(ULandscapeComponent* Component)
{
	if (ensure(Component))
	{
		FIntPoint ComponentKey = Component->GetSectionBase() / Component->ComponentSizeQuads;
		auto RegisteredComponent = XYtoComponentMap.FindRef(ComponentKey);

		if (RegisteredComponent == Component)
		{
			XYtoComponentMap.Remove(ComponentKey);
		}

		SelectedComponents.Remove(Component);
		SelectedRegionComponents.Remove(Component);
	}
}

namespace LandscapeInfoBoundsHelper
{
	void AccumulateBounds(ALandscapeProxy* Proxy, FBox& Bounds)
	{
		const bool bOnlyCollidingComponents = false;
		const bool bIncludeChildActors = false;
		FVector Origin;
		FVector BoxExtents;

		Proxy->GetActorBounds(bOnlyCollidingComponents, Origin, BoxExtents, bIncludeChildActors);

		// Reject invalid bounds
		if (BoxExtents != FVector::Zero())
		{
			Bounds += FBox::BuildAABB(Origin, BoxExtents);
		}
	}
}

FBox ULandscapeInfo::GetLoadedBounds() const
{
	FBox Bounds(EForceInit::ForceInit);

	if (LandscapeActor.IsValid())
	{
		LandscapeInfoBoundsHelper::AccumulateBounds(LandscapeActor.Get(), Bounds);
	}

	// Since in PIE/in-game the Proxies aren't populated, we must iterate through the loaded components
	// but this is functionally equivalent to calling ForAllLandscapeProxies
	TSet<ALandscapeProxy*> LoadedProxies;
	for (auto It = XYtoComponentMap.CreateConstIterator(); It; ++It)
	{
		if (!It.Value())
		{
			continue;
		}

		if (ALandscapeProxy* Proxy = Cast<ALandscapeProxy>(It.Value()->GetOwner()))
		{
			LoadedProxies.Add(Proxy);
		}
	}

	for (ALandscapeProxy* Proxy : LoadedProxies)
	{
		LandscapeInfoBoundsHelper::AccumulateBounds(Proxy, Bounds);
	}

	return Bounds;
}

#if WITH_EDITOR
FBox ULandscapeInfo::GetCompleteBounds() const
{
	ALandscape* Landscape = LandscapeActor.Get();

	// In a non-WP situation, the current actor's bounds will do.
	if(!Landscape || !Landscape->GetWorld() || !Landscape->GetWorld()->GetWorldPartition())
	{
		return GetLoadedBounds();
	}

	FBox Bounds(EForceInit::ForceInit);

	if (UWorldPartition* WorldPartition = Landscape->GetWorld()->GetWorldPartition())
	{
		FWorldPartitionHelpers::ForEachActorDesc<ALandscapeProxy>(WorldPartition, [this, &Bounds, Landscape](const FWorldPartitionActorDesc* ActorDesc)
		{
			FLandscapeActorDesc* LandscapeActorDesc = (FLandscapeActorDesc*)ActorDesc;

			if (LandscapeActorDesc->GridGuid == LandscapeGuid)
			{
				ALandscapeProxy* LandscapeProxy = Cast<ALandscapeProxy>(ActorDesc->GetActor());

				// Skip owning landscape actor
				if (LandscapeProxy != Landscape)
				{
					if (LandscapeProxy)
					{
						// Prioritize loaded bounds, as the bounds in the actor desc might not be up-to-date
						LandscapeInfoBoundsHelper::AccumulateBounds(LandscapeProxy, Bounds);
					}
					else
					{
						Bounds += ActorDesc->GetBounds();
					}
				}
			}

			return true;
		});
	}

	return Bounds;
}
#endif

void ULandscapeComponent::PostInitProperties()
{
	Super::PostInitProperties();

	// Create a new guid in case this is a newly created component
	// If not, this guid will be overwritten when serialized
	FPlatformMisc::CreateGuid(StateId);

	// Initialize MapBuildDataId to something unique, in case this is a new ULandscapeComponent
	MapBuildDataId = FGuid::NewGuid();
}

void ULandscapeComponent::PostDuplicate(bool bDuplicateForPIE)
{
	if (!bDuplicateForPIE)
	{
		// Reset the StateId on duplication since it needs to be unique for each capture.
		// PostDuplicate covers direct calls to StaticDuplicateObject, but not actor duplication (see PostEditImport)
		FPlatformMisc::CreateGuid(StateId);
	}
}

ULandscapeWeightmapUsage::ULandscapeWeightmapUsage(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ClearUsage();
}

// Generate a new guid to force a recache of all landscape derived data
#define LANDSCAPE_FULL_DERIVEDDATA_VER			TEXT("3000901CF3B24F028854C2DB986E5B3B")

FString FLandscapeComponentDerivedData::GetDDCKeyString(const FGuid& StateId)
{
	return FDerivedDataCacheInterface::BuildCacheKey(TEXT("LS_FULL"), LANDSCAPE_FULL_DERIVEDDATA_VER, *StateId.ToString());
}

void FLandscapeComponentDerivedData::InitializeFromUncompressedData(const TArray<uint8>& UncompressedData, const TArray<TArray<uint8>>& StreamingLODs)
{
	int32 UncompressedSize = UncompressedData.Num() * UncompressedData.GetTypeSize();

	TArray<uint8> TempCompressedMemory;
	// Compressed can be slightly larger than uncompressed
	TempCompressedMemory.Empty(UncompressedSize * 4 / 3);
	TempCompressedMemory.AddUninitialized(UncompressedSize * 4 / 3);
	int32 CompressedSize = TempCompressedMemory.Num() * TempCompressedMemory.GetTypeSize();

	verify(FCompression::CompressMemory(
		NAME_Zlib,
		TempCompressedMemory.GetData(),
		CompressedSize,
		UncompressedData.GetData(),
		UncompressedSize,
		COMPRESS_BiasMemory));

	// Note: change LANDSCAPE_FULL_DERIVEDDATA_VER when modifying the serialization layout
	FMemoryWriter FinalArchive(CompressedLandscapeData, true);
	FinalArchive << UncompressedSize;
	FinalArchive << CompressedSize;
	FinalArchive.Serialize(TempCompressedMemory.GetData(), CompressedSize);

	const int32 NumStreamingLODs = StreamingLODs.Num();
	StreamingLODDataArray.Empty(NumStreamingLODs);
	for (int32 Idx = 0; Idx < NumStreamingLODs; ++Idx)
	{
		const TArray<uint8>& SrcData = StreamingLODs[Idx];
		const int32 NumSrcBytes = SrcData.Num();
		FByteBulkData& LODData = StreamingLODDataArray[StreamingLODDataArray.AddDefaulted()];
		if (NumSrcBytes > 0)
		{
			LODData.ResetBulkDataFlags(BULKDATA_Force_NOT_InlinePayload);
			LODData.Lock(LOCK_READ_WRITE);
			void* Dest = LODData.Realloc(NumSrcBytes);
			FMemory::Memcpy(Dest, SrcData.GetData(), NumSrcBytes);
			LODData.Unlock();
		}
	}
}

void FLandscapeComponentDerivedData::Serialize(FArchive& Ar, UObject* Owner)
{
	Ar << CompressedLandscapeData;

	int32 NumStreamingLODs = StreamingLODDataArray.Num();
	Ar << NumStreamingLODs;
	if (Ar.IsLoading())
	{
		StreamingLODDataArray.Empty(NumStreamingLODs);
		StreamingLODDataArray.AddDefaulted(NumStreamingLODs);
	}

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	CachedLODDataPackagePath.Empty();
	CachedLODDataPackageSegment = EPackageSegment::Header;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	for (int32 Idx = 0; Idx < NumStreamingLODs; ++Idx)
	{
		FByteBulkData& LODData = StreamingLODDataArray[Idx];
		LODData.Serialize(Ar, Owner, Idx);

		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		if (CachedLODDataPackagePath.IsEmpty() && 
			!!(LODData.GetBulkDataFlags() & BULKDATA_Force_NOT_InlinePayload) &&
			LODData.IsUsingIODispatcher() == false)
		{
			CachedLODDataPackagePath = LODData.GetPackagePath();
			CachedLODDataPackageSegment = LODData.GetPackageSegment();
		}
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}
}

bool FLandscapeComponentDerivedData::LoadFromDDC(const FGuid& StateId, UObject* Component)
{
	TArray<uint8> Bytes;
	if (GetDerivedDataCacheRef().GetSynchronous(*GetDDCKeyString(StateId), Bytes, Component->GetPathName()))
	{
		FMemoryReader Ar(Bytes, true);
		Serialize(Ar, Component);
		return true;
	}
	return false;
}

void FLandscapeComponentDerivedData::SaveToDDC(const FGuid& StateId, UObject* Component)
{
	check(CompressedLandscapeData.Num() > 0);
	TArray<uint8> Bytes;
	FMemoryWriter Ar(Bytes, true);
	Serialize(Ar, Component);
	GetDerivedDataCacheRef().Put(*GetDDCKeyString(StateId), Bytes, Component->GetPathName());
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
ALandscapeProxy::~ALandscapeProxy()
{
	for (int32 Index = 0; Index < AsyncFoliageTasks.Num(); Index++)
	{
		FAsyncTask<FAsyncGrassTask>* Task = AsyncFoliageTasks[Index];
		Task->EnsureCompletion(true);
		FAsyncGrassTask& Inner = Task->GetTask();
		delete Task;
	}
	AsyncFoliageTasks.Empty();

#if WITH_EDITOR
	TotalComponentsNeedingGrassMapRender -= NumComponentsNeedingGrassMapRender;
	NumComponentsNeedingGrassMapRender = 0;
	TotalTexturesToStreamForVisibleGrassMapRender -= NumTexturesToStreamForVisibleGrassMapRender;
	NumTexturesToStreamForVisibleGrassMapRender = 0;
#endif

#if WITH_EDITORONLY_DATA
	LandscapeProxies.Remove(this);
#endif
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

//
// ALandscapeMeshProxyActor
//
ALandscapeMeshProxyActor::ALandscapeMeshProxyActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetCanBeDamaged(false);

	LandscapeMeshProxyComponent = CreateDefaultSubobject<ULandscapeMeshProxyComponent>(TEXT("LandscapeMeshProxyComponent0"));
	LandscapeMeshProxyComponent->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	LandscapeMeshProxyComponent->Mobility = EComponentMobility::Static;
	LandscapeMeshProxyComponent->SetGenerateOverlapEvents(false);

	RootComponent = LandscapeMeshProxyComponent;
}

//
// ULandscapeMeshProxyComponent
//
ULandscapeMeshProxyComponent::ULandscapeMeshProxyComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void ULandscapeMeshProxyComponent::InitializeForLandscape(ALandscapeProxy* Landscape, int8 InProxyLOD)
{
	LandscapeGuid = Landscape->GetLandscapeGuid();

	for (ULandscapeComponent* Component : Landscape->LandscapeComponents)
	{
		if (Component)
		{
			ProxyComponentBases.Add(Component->GetSectionBase() / Component->ComponentSizeQuads);
		}
	}

	if (InProxyLOD != INDEX_NONE)
	{
		ProxyLOD = FMath::Clamp<int32>(InProxyLOD, 0, FMath::CeilLogTwo(Landscape->SubsectionSizeQuads + 1) - 1);
	}
}

#if WITH_EDITOR
void ALandscapeProxy::SerializeStateHashes(FArchive& Ar)
{
	for (FLandscapePerLODMaterialOverride& MaterialOverride : PerLODOverrideMaterials)
	{
		if (MaterialOverride.Material != nullptr)
		{
			FGuid LocalStateId = MaterialOverride.Material->GetMaterial_Concurrent()->StateId;
			Ar << LocalStateId;
			Ar << MaterialOverride.LODIndex;
		}
	}
}

void ULandscapeComponent::SerializeStateHashes(FArchive& Ar)
{
	FGuid HeightmapGuid = HeightmapTexture->Source.GetId();
	Ar << HeightmapGuid;
	for (auto WeightmapTexture : WeightmapTextures)
	{
		FGuid WeightmapGuid = WeightmapTexture->Source.GetId();
		Ar << WeightmapGuid;
	}

	bool bMeshHoles = GetLandscapeProxy()->bMeshHoles;
	uint8 MeshHolesMaxLod = GetLandscapeProxy()->MeshHolesMaxLod;
	Ar << bMeshHoles << MeshHolesMaxLod;

	// Take into account the Heightmap offset per component
	Ar << HeightmapScaleBias.Z;
	Ar << HeightmapScaleBias.W;

	if (OverrideMaterial != nullptr)
	{
		FGuid LocalStateId = OverrideMaterial->GetMaterial_Concurrent()->StateId;
		Ar << LocalStateId;
	}

	for (FLandscapePerLODMaterialOverride& MaterialOverride : PerLODOverrideMaterials)
	{
		if (MaterialOverride.Material != nullptr)
		{
			FGuid LocalStateId = MaterialOverride.Material->GetMaterial_Concurrent()->StateId;
			Ar << LocalStateId;
			Ar << MaterialOverride.LODIndex;
		}
	}

	ALandscapeProxy* Proxy = GetLandscapeProxy();

	if (Proxy->LandscapeMaterial != nullptr)
	{
		FGuid LocalStateId = Proxy->LandscapeMaterial->GetMaterial_Concurrent()->StateId;
		Ar << LocalStateId;
	}

	Proxy->SerializeStateHashes(Ar);
}

FLandscapeGIBakedTextureBuilder::FLandscapeGIBakedTextureBuilder(UWorld* InWorld)
	:World(InWorld)
	,OutdatedGIBakedTextureComponentsCount(0)
	,GIBakedTexturesLastCheckTime(0)
{

}

void FLandscapeGIBakedTextureBuilder::Build()
{
	if (World)
	{
		for (TActorIterator<ALandscapeProxy> ProxyIt(World); ProxyIt; ++ProxyIt)
		{
			ProxyIt->BuildGIBakedTextures();
		}
		//Force update the outdated count when using the build menu option.
		OutdatedGIBakedTextureComponentsCount = 0;
		GIBakedTexturesLastCheckTime = FPlatformTime::Seconds();

	}
}

int32 FLandscapeGIBakedTextureBuilder::GetOutdatedGIBakedTextureComponentsCount(bool bInForceUpdate) const
{
	if (World)
	{
		bool bUpdate = bInForceUpdate;
		double GIBakedTexturesTimeNow = FPlatformTime::Seconds();
		if (!bUpdate)
		{
			// Recheck every 20 secs
			if ((GIBakedTexturesTimeNow - GIBakedTexturesLastCheckTime) > 20)
			{
				bUpdate = true;
			}
		}
		if (bUpdate)
		{
			GIBakedTexturesLastCheckTime = GIBakedTexturesTimeNow;
			OutdatedGIBakedTextureComponentsCount = 0;
			for (TActorIterator<ALandscapeProxy> ProxyIt(World); ProxyIt; ++ProxyIt)
			{
				OutdatedGIBakedTextureComponentsCount += ProxyIt->GetOutdatedGIBakedTextureComponentsCount();
			}
		}
	}
	return OutdatedGIBakedTextureComponentsCount;
}

void ALandscapeProxy::BuildGIBakedTextures(struct FScopedSlowTask* InSlowTask /* = nullptr */)
{
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		const bool bShouldMarkDirty = true;
		UpdateGIBakedTextureData(bShouldMarkDirty);
	}
	}

int32 ALandscapeProxy::GetOutdatedGIBakedTextureComponentsCount() const
	{
	int32 OutdatedGITextureComponentsCount = 0;
	UpdateGIBakedTextureStatus(nullptr, nullptr, &OutdatedGITextureComponentsCount);
	return OutdatedGITextureComponentsCount;
	}

void ALandscapeProxy::UpdateGIBakedTextureStatus(bool* bOutGenerateLandscapeGIData, TMap<UTexture2D*, FGIBakedTextureState>* OutComponentsNeedBakingByHeightmap, int32* OutdatedComponentsCount) const
{
	int32 OutdatedComponents = 0;
	int32 ComponentsNeedToBeCleared = 0;
	int32 ComponentsNeedToBeBaked = 0;

	//@todo - remove Landscape GI Data
	if (true)
	{
		if (bOutGenerateLandscapeGIData)
		{
			*bOutGenerateLandscapeGIData = false;
		}

		for (ULandscapeComponent* Component : LandscapeComponents)
		{
			if (Component != nullptr && Component->GIBakedBaseColorTexture != nullptr)
			{
				ComponentsNeedToBeCleared++;
			}
		}

		OutdatedComponents += ComponentsNeedToBeCleared;
	}
	else
	{
	// Stores the components and their state hash data for a single atlas
		struct FGIBakeTextureStateBuilder
	{
		// pointer as FMemoryWriter caches the address of the FBufferArchive, and this struct could be relocated on a realloc.
		TUniquePtr<FBufferArchive> ComponentStateAr;
		TArray<ULandscapeComponent*> Components;

			FGIBakeTextureStateBuilder()
		{
			ComponentStateAr = MakeUnique<FBufferArchive>();
		}
	};

		TMap<UTexture2D*, FGIBakeTextureStateBuilder> ComponentsByHeightmap;
	for (ULandscapeComponent* Component : LandscapeComponents)
	{
		if (Component == nullptr)
		{
			continue;
		}

			FGIBakeTextureStateBuilder& Info = ComponentsByHeightmap.FindOrAdd(Component->GetHeightmap());
		Info.Components.Add(Component);
		Component->SerializeStateHashes(*Info.ComponentStateAr);
	}

		for (auto It = ComponentsByHeightmap.CreateIterator(); It; ++It)
		{
			FGIBakeTextureStateBuilder& Info =It.Value();

			// Calculate a combined Guid-like ID we can use for this component
			uint32 Hash[5];
			FSHA1::HashBuffer(Info.ComponentStateAr->GetData(), Info.ComponentStateAr->Num(), (uint8*)Hash);
			FGuid CombinedStateId = FGuid(Hash[0] ^ Hash[4], Hash[1], Hash[2], Hash[3]);

			if (Info.Components[0]->BakedTextureMaterialGuid != CombinedStateId)
			{
				ComponentsNeedToBeBaked += Info.Components.Num();
				if (OutComponentsNeedBakingByHeightmap)
				{
					FGIBakedTextureState& GIBakedTextureState = OutComponentsNeedBakingByHeightmap->FindOrAdd(It.Key());
					GIBakedTextureState.Components = MoveTemp(Info.Components);
					GIBakedTextureState.CombinedStateId = CombinedStateId;
				}
			}
		}
		OutdatedComponents += ComponentsNeedToBeBaked;
	}

	if (OutdatedComponentsCount)
	{
		if (OutdatedComponents == 0)
		{
			for (auto Component : LandscapeComponents)
			{
				const bool bIsDirty = Component->GetPackage()->IsDirty();
				if (Component->LastBakedTextureMaterialGuid != Component->BakedTextureMaterialGuid && !bIsDirty)
				{
					OutdatedComponents++;
				}
			}
		}
		*OutdatedComponentsCount = OutdatedComponents;
	}
}

void ALandscapeProxy::UpdateGIBakedTextureData(bool bInShouldMarkDirty)
{
	const bool bBakeAllGITextures = true;
	UpdateGIBakedTextures(bBakeAllGITextures);
	if (bInShouldMarkDirty && GetOutdatedGIBakedTextureComponentsCount() > 0)
	{
		MarkPackageDirty();
	}
}

void ALandscapeProxy::UpdateGIBakedTextures(bool bBakeAllGITextures)
{
	// See if we can render
	UWorld* World = GetWorld();
	if (!GIsEditor || GUsingNullRHI || !World || World->IsGameWorld() || World->FeatureLevel < ERHIFeatureLevel::SM5)
	{
		return;
	}

	if (!bBakeAllGITextures && UpdateBakedTexturesCountdown-- > 0)
	{
		return;
	}

	bool bGenerateLandscapeGIData = true;
	TMap<UTexture2D*, FGIBakedTextureState> ComponentsToBeBakedByHeightmap;
	UpdateGIBakedTextureStatus(&bGenerateLandscapeGIData, &ComponentsToBeBakedByHeightmap);
	
	if (!bGenerateLandscapeGIData)
	{
		// Clear out any existing GI textures
		for (ULandscapeComponent* Component : LandscapeComponents)
		{
			if (Component != nullptr && Component->GIBakedBaseColorTexture != nullptr)
			{
				Component->BakedTextureMaterialGuid.Invalidate();
				Component->GIBakedBaseColorTexture = nullptr;
				Component->MarkRenderStateDirty();
			}
		}

		// Don't check if we need to update anything for another 60 frames
		UpdateBakedTexturesCountdown = 60;

		return;
	}

	TotalComponentsNeedingTextureBaking -= NumComponentsNeedingTextureBaking;
	NumComponentsNeedingTextureBaking = 0;
	int32 NumGenerated = 0;

	for (auto It = ComponentsToBeBakedByHeightmap.CreateConstIterator(); It; ++It)
	{
		const FGIBakedTextureState& Info = It.Value();

		bool bCanBake = true;
		for (ULandscapeComponent* Component : Info.Components)
		{
			// not registered; ignore this component
			if (!Component->SceneProxy)
			{
				continue;
			}

			// Check we can render the material
			UMaterialInstance* MaterialInstance = Component->GetMaterialInstance(0, false);
			if (!MaterialInstance)
			{
				// Cannot render this component yet as it doesn't have a material; abandon the atlas for this heightmap
				bCanBake = false;
				break;
			}

			FMaterialResource* MaterialResource = MaterialInstance->GetMaterialResource(World->FeatureLevel);
			if (!MaterialResource || !MaterialResource->HasValidGameThreadShaderMap())
			{
				// Cannot render this component yet as its shaders aren't compiled; abandon the atlas for this heightmap
				bCanBake = false;
				break;
			}
		}

		if (bCanBake)
		{
			// We throttle, baking only one atlas per frame if bBakeAllGITextures is false.
			if (!bBakeAllGITextures && NumGenerated > 0)
				{
					NumComponentsNeedingTextureBaking += Info.Components.Num();
				}
				else
				{
					UTexture2D* HeightmapTexture = It.Key();
					// 1/8 the res of the heightmap
					FIntPoint AtlasSize(HeightmapTexture->GetSizeX() >> 3, HeightmapTexture->GetSizeY() >> 3);

					TArray<FColor> AtlasSamples;
					AtlasSamples.AddZeroed(AtlasSize.X * AtlasSize.Y);

					for (ULandscapeComponent* Component : Info.Components)
					{
						// not registered; ignore this component
						if (!Component->SceneProxy)
						{
							continue;
						}

						int32 ComponentSamples = (SubsectionSizeQuads + 1) * NumSubsections;
						check(FMath::IsPowerOfTwo(ComponentSamples));

						int32 BakeSize = ComponentSamples >> 3;
						TArray<FColor> Samples;
						if (FMaterialUtilities::ExportBaseColor(Component, BakeSize, Samples))
						{
							int32 AtlasOffsetX = FMath::RoundToInt(Component->HeightmapScaleBias.Z * (float)HeightmapTexture->GetSizeX()) >> 3;
							int32 AtlasOffsetY = FMath::RoundToInt(Component->HeightmapScaleBias.W * (float)HeightmapTexture->GetSizeY()) >> 3;
							for (int32 y = 0; y < BakeSize; y++)
							{
								FMemory::Memcpy(&AtlasSamples[(y + AtlasOffsetY) * AtlasSize.X + AtlasOffsetX], &Samples[y * BakeSize], sizeof(FColor) * BakeSize);
							}
							NumGenerated++;
						}
					}
				UTexture2D* AtlasTexture = FMaterialUtilities::CreateTexture(GetOutermost(), HeightmapTexture->GetName() + TEXT("_BaseColor"), AtlasSize, AtlasSamples, TC_Default, TEXTUREGROUP_World, RF_NoFlags, true, Info.CombinedStateId);

					for (ULandscapeComponent* Component : Info.Components)
					{
					Component->BakedTextureMaterialGuid = Info.CombinedStateId;
						Component->GIBakedBaseColorTexture = AtlasTexture;
						Component->MarkRenderStateDirty();
					}
				}
			}
		}

	TotalComponentsNeedingTextureBaking += NumComponentsNeedingTextureBaking;

	if (NumGenerated == 0)
	{
		// Don't check if we need to update anything for another 60 frames
		UpdateBakedTexturesCountdown = 60;
	}
}

FLandscapePhysicalMaterialBuilder::FLandscapePhysicalMaterialBuilder(UWorld* InWorld)
	:World(InWorld)
	,OudatedPhysicalMaterialComponentsCount(0)
{
}

void FLandscapePhysicalMaterialBuilder::Build()
{
	if (World)
	{
		for (TActorIterator<ALandscapeProxy> ProxyIt(World); ProxyIt; ++ProxyIt)
		{
			ProxyIt->BuildPhysicalMaterial();
		}
	}
}

int32 FLandscapePhysicalMaterialBuilder::GetOudatedPhysicalMaterialComponentsCount()
{
	if (World)
	{
		OudatedPhysicalMaterialComponentsCount = 0;
		for (TActorIterator<ALandscapeProxy> ProxyIt(World); ProxyIt; ++ProxyIt)
		{
			OudatedPhysicalMaterialComponentsCount += ProxyIt->GetOudatedPhysicalMaterialComponentsCount();
		}
	}
	return OudatedPhysicalMaterialComponentsCount;
}

int32 ALandscapeProxy::GetOudatedPhysicalMaterialComponentsCount() const
{
	int32 OudatedPhysicalMaterialComponentsCount = 0;
	UpdatePhysicalMaterialTasksStatus(nullptr, &OudatedPhysicalMaterialComponentsCount);
	return OudatedPhysicalMaterialComponentsCount;
}

void ALandscapeProxy::BuildPhysicalMaterial(struct FScopedSlowTask* InSlowTask)
{
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		const bool bShouldMarkDirty = true;
		UpdatePhysicalMaterialTasks(bShouldMarkDirty);
	}
}

void ALandscapeProxy::UpdatePhysicalMaterialTasksStatus(TSet<ULandscapeComponent*>* OutdatedComponents, int32* OutdatedComponentsCount) const
{
	int32 OutdatedCount = 0;
	for (auto Component : LandscapeComponents)
	{
		uint32 Hash = Component->CalculatePhysicalMaterialTaskHash();
		if (Component->PhysicalMaterialHash != Hash || Component->PhysicalMaterialTask.IsValid())
		{
			OutdatedCount++;
			if (OutdatedComponents)
			{
				OutdatedComponents->Add(Component);
			}
		}
	}

	if (OutdatedCount == 0)
	{
		for (auto Component : LandscapeComponents)
		{
			const bool bIsDirty = Component->GetPackage()->IsDirty();
			if (Component->LastSavedPhysicalMaterialHash != Component->PhysicalMaterialHash && !bIsDirty)
			{
				OutdatedCount++;
			}
		}
	}

	if (OutdatedComponentsCount)
{
		*OutdatedComponentsCount = OutdatedCount;
	}
}

void ALandscapeProxy::UpdatePhysicalMaterialTasks(bool bInShouldMarkDirty)
{
	TSet<ULandscapeComponent*> OutdatedComponents;
	int32 PendingComponentsToBeSaved = 0;
	UpdatePhysicalMaterialTasksStatus(&OutdatedComponents, &PendingComponentsToBeSaved);
	for (ULandscapeComponent* Component : OutdatedComponents)
	{
		Component->UpdatePhysicalMaterialTasks();
	}
	if (bInShouldMarkDirty && PendingComponentsToBeSaved >0)
	{
		MarkPackageDirty();
	}
}
#endif

template<class ContainerType>
void InvalidateGeneratedComponentDataImpl(const ContainerType& Components, bool bInvalidateLightingCache)
{
	TMap<ALandscapeProxy*, TSet<ULandscapeComponent*>> ByProxy;
	for (auto Iter = Components.CreateConstIterator(); Iter; ++Iter)
	{
		ULandscapeComponent* Component = *Iter;
		if (bInvalidateLightingCache)
		{
			Component->InvalidateLightingCache();
		}
		Component->BakedTextureMaterialGuid.Invalidate();
		ByProxy.FindOrAdd(Component->GetLandscapeProxy()).Add(Component);
	}

	for (auto Iter = ByProxy.CreateConstIterator(); Iter; ++Iter)
	{
		Iter.Key()->FlushGrassComponents(&Iter.Value());
	}
}

void ALandscapeProxy::InvalidateGeneratedComponentData(bool bInvalidateLightingCache)
{
	InvalidateGeneratedComponentDataImpl(LandscapeComponents, bInvalidateLightingCache);
}

void ALandscapeProxy::InvalidateGeneratedComponentData(const TArray<ULandscapeComponent*>& Components, bool bInvalidateLightingCache)
{
	InvalidateGeneratedComponentDataImpl(Components, bInvalidateLightingCache);
}

void ALandscapeProxy::InvalidateGeneratedComponentData(const TSet<ULandscapeComponent*>& Components, bool bInvalidateLightingCache)
{
	InvalidateGeneratedComponentDataImpl(Components, bInvalidateLightingCache);
}

ULandscapeLODStreamingProxy::ULandscapeLODStreamingProxy(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	LandscapeComponent = Cast<ULandscapeComponent>(GetOuter());
}

int32 ULandscapeLODStreamingProxy::CalcCumulativeLODSize(int32 NumLODs) const
{
	check(LandscapeComponent);
	const int32 NumStreamingLODs = LandscapeComponent->PlatformData.StreamingLODDataArray.Num();
	const int32 LastLODIdx = NumStreamingLODs - NumLODs + 1;
	int64 Result = 0;
	for (int32 Idx = NumStreamingLODs - 1; Idx >= LastLODIdx; --Idx)
	{
		Result += LandscapeComponent->PlatformData.StreamingLODDataArray[Idx].GetBulkDataSize();
	}
	return (int32)Result;
}

bool ULandscapeLODStreamingProxy::GetMipDataFilename(const int32 MipIndex, FString& OutBulkDataFilename) const
{
	FPackagePath PackagePath;
	EPackageSegment PackageSegment;

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	const bool bResult = GetMipDataPackagePath(MipIndex, PackagePath, PackageSegment);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	if (bResult)
	{
		OutBulkDataFilename = PackagePath.GetLocalFullPath(PackageSegment);
		return true;
	}
	return false;
}

bool ULandscapeLODStreamingProxy::GetMipDataPackagePath(const int32 MipIndex, FPackagePath& OutPackagePath, EPackageSegment& OutPackageSegment) const
{
	check(LandscapeComponent);
	const int32 NumStreamingLODs = LandscapeComponent->PlatformData.StreamingLODDataArray.Num();
	if (MipIndex >= 0 && MipIndex < NumStreamingLODs)
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		OutPackagePath = LandscapeComponent->PlatformData.CachedLODDataPackagePath;
		OutPackageSegment = LandscapeComponent->PlatformData.CachedLODDataPackageSegment;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS

		return true;
	}
	return false;
}

FIoFilenameHash ULandscapeLODStreamingProxy::GetMipIoFilenameHash(const int32 MipIndex) const
{
	if (LandscapeComponent && LandscapeComponent->PlatformData.StreamingLODDataArray.IsValidIndex(MipIndex))
	{
		return LandscapeComponent->PlatformData.StreamingLODDataArray[MipIndex].GetIoFilenameHash();
	}
	else
	{
		return INVALID_IO_FILENAME_HASH;
	}
}

bool ULandscapeLODStreamingProxy::StreamOut(int32 NewMipCount)
{
	check(IsInGameThread());

	if (!HasPendingInitOrStreaming() && CachedSRRState.StreamOut(NewMipCount))
	{
		PendingUpdate = new FLandscapeMeshMobileStreamOut(this);
		return !PendingUpdate->IsCancelled();
	}
	return false;
}

bool ULandscapeLODStreamingProxy::StreamIn(int32 NewMipCount, bool bHighPrio)
{
	check(IsInGameThread());

	if (!HasPendingInitOrStreaming() && CachedSRRState.StreamIn(NewMipCount))
	{
#if WITH_EDITOR
		if (FPlatformProperties::HasEditorOnlyData())
		{
			PendingUpdate = new FLandscapeMeshMobileStreamIn_GPUDataOnly(this);
		}
		else
#endif
		{
			PendingUpdate = new FLandscapeMeshMobileStreamIn_IO_AsyncReallocate(this, bHighPrio);
		}
		return !PendingUpdate->IsCancelled();
	}
	return false;
}

TArray<float> ULandscapeLODStreamingProxy::GetLODScreenSizeArray() const
{
	check(LandscapeComponent);
	return ::GetLODScreenSizeArray(LandscapeComponent->GetLandscapeProxy(), CachedSRRState.MaxNumLODs);
}

TSharedPtr<FLandscapeMobileRenderData, ESPMode::ThreadSafe> ULandscapeLODStreamingProxy::GetRenderData() const
{
	check(LandscapeComponent);
	return LandscapeComponent->PlatformData.CachedRenderData;
}

FByteBulkData& ULandscapeLODStreamingProxy::GetStreamingLODBulkData(int32 LODIdx) const
{
	check(LandscapeComponent);
	return LandscapeComponent->PlatformData.StreamingLODDataArray[LODIdx];
}

void ULandscapeLODStreamingProxy::CancelAllPendingStreamingActions()
{
	FlushRenderingCommands();

	for (TObjectIterator<ULandscapeLODStreamingProxy> It; It; ++It)
	{
		ULandscapeLODStreamingProxy* StaticMesh = *It;
		StaticMesh->CancelPendingStreamingRequest();
	}

	FlushRenderingCommands();
}

bool ULandscapeLODStreamingProxy::HasPendingRenderResourceInitialization() const
{
	return LandscapeComponent && LandscapeComponent->PlatformData.CachedRenderData && !LandscapeComponent->PlatformData.CachedRenderData->bReadyForStreaming;
}

void ULandscapeLODStreamingProxy::ClearStreamingResourceState()
{
	CachedSRRState.Clear();
}

void ULandscapeLODStreamingProxy::InitResourceStateForMobileStreaming()
{
	check(LandscapeComponent);

	const int32 NumLODs = LandscapeComponent->PlatformData.StreamingLODDataArray.Num() + 1;
	const bool bHasValidRenderData = LandscapeComponent->PlatformData.CachedRenderData.IsValid();

	CachedSRRState.Clear();
	CachedSRRState.bSupportsStreaming = !NeverStream && NumLODs > 1 && bHasValidRenderData;
	CachedSRRState.NumNonStreamingLODs = 1;
	CachedSRRState.NumNonOptionalLODs = NumLODs;
	CachedSRRState.MaxNumLODs = NumLODs;
	CachedSRRState.NumResidentLODs = bHasValidRenderData ? (NumLODs - LandscapeComponent->PlatformData.CachedRenderData->CurrentFirstLODIdx) : NumLODs;
	CachedSRRState.NumRequestedLODs = CachedSRRState.NumResidentLODs;

	// Set bHasPendingInitHint so that HasPendingRenderResourceInitialization() gets called.
	CachedSRRState.bHasPendingInitHint = true;
}

#undef LOCTEXT_NAMESPACE
