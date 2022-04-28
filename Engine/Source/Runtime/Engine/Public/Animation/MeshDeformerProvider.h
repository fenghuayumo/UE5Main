// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "CoreMinimal.h"
#include "Features/IModularFeature.h"
#include "RHIDefinitions.h"
#include "UObject/SoftObjectPtr.h"

/**
 * Modular feature interface for mesh deformer providers. 
 * Modules that inherit from this need to be loaded before shader compilation starts (PostConfigInit)
 * so that the correct vertex factories can be created.
 */
class ENGINE_API IMeshDeformerProvider : public IModularFeature
{
public:
	virtual ~IMeshDeformerProvider() {}

	static const FName ModularFeatureName; // "MeshDeformer"
	static bool IsAvailable();
	static IMeshDeformerProvider* Get();

	/** 
	 * Returns a default mesh deformer. 
	 * This can allow a mesh deformer plugin to automatically replace the UE fixed function animation path.
	 * todo: Extend this to take requested features (lbs, morph, cloth etc.)
	 */
	virtual TSoftObjectPtr<class UMeshDeformer> GetDefaultMeshDeformer() = 0;
};
