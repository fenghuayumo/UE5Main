// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderGraph.h"
#include "Renderer/Private/SceneRendering.h"

void RenderPhysicsField(
	FRDGBuilder& GraphBuilder,
	TArrayView<FViewInfo> Views,
	const class FPhysicsFieldSceneProxy* PhysicsFieldProxy,
	FRDGTextureRef SceneColorTexture);

