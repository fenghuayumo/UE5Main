// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "RHIDefinitions.h"
#include "RenderGraphDefinitions.h"

class FRHITexture;
class UTexture;
class FViewInfo;

namespace RectLightAtlas
{
// Atlas slot description in terms of UV coordinates
struct FAtlasSlotDesc
{
	FVector2f UVOffset;
	FVector2f UVScale;
	float MaxMipLevel;
};
	
// Add a rect light source texture to the texture atlas
uint32 AddRectLightTexture(UTexture* Texture);

// Remove a rect light source texture to the texture atlas
void RemoveRectLightTexture(uint32 InSlotId);

// Return the atlas texture coordinate for a	 particular slot
FAtlasSlotDesc GetRectLightAtlasSlot(uint32 InSlotId);

// Return the atlas texture
FRHITexture* GetRectLightAtlasTexture();

// Update the rect light atlas texture
void UpdateRectLightAtlasTexture(FRDGBuilder& GraphBuilder, const ERHIFeatureLevel::Type FeatureLevel);

// Return the rect light atlas debug pass
void AddRectLightAtlasDebugPass(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGTextureRef OutputTexture);

// Scope for invalidating a particular texture 
// This ensures the atlas contains the latest version of the texture and filter it
struct FAtlasTextureInvalidationScope
{
	FAtlasTextureInvalidationScope(UTexture* In);
	~FAtlasTextureInvalidationScope();
	FAtlasTextureInvalidationScope(const FAtlasTextureInvalidationScope&) = delete;
	FAtlasTextureInvalidationScope& operator=(const FAtlasTextureInvalidationScope&) = delete;
	UTexture* Texture = nullptr;
};

} 