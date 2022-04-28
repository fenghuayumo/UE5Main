// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PostProcessAmbientOcclusionMobile.h
=============================================================================*/

#pragma once
#include "CoreMinimal.h"
#include "RendererInterface.h"

FRDGTextureRef CreateMobileScreenSpaceAOTexture(FRDGBuilder& GraphBuilder, const struct FSceneTexturesConfig& Config);

bool IsMobileAmbientOcclusionEnabled(EShaderPlatform ShaderPlatform);
bool IsUsingMobileAmbientOcclusion(EShaderPlatform ShaderPlatform);