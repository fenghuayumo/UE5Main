// Copyright Epic Games, Inc. All Rights Reserved.
// Draw functions for debugging trace/sweeps/overlaps


#pragma once 

#include "CoreMinimal.h"
#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_1
#include "Engine/EngineTypes.h"
#endif
#include "EngineDefines.h"
#include "PhysicsPublic.h"
#include "PhysXIncludes.h"

struct FHitResult;

namespace Chaos
{
	class FImplicitObject;
}

#if PHYSICS_INTERFACE_PHYSX
/** Draw PhysX geom with overlaps */
void DrawGeomOverlaps(const UWorld* InWorld, const PxGeometry& PGeom, const PxTransform& PGeomPose, TArray<struct FOverlapResult>& Overlaps, float Lifetime);
/** Draw PhysX geom being swept with hits */
void DrawGeomSweeps(const UWorld* InWorld, const FVector& Start, const FVector& End, const PxGeometry& PGeom, const PxQuat& Rotation, const TArray<FHitResult>& Hits, float Lifetime);
#endif

void DrawGeomOverlaps(const UWorld* InWorld, const Chaos::FImplicitObject& Geom, const FTransform& GeomPose, TArray<struct FOverlapResult>& Overlaps, float Lifetime);
void DrawGeomSweeps(const UWorld* InWorld, const FVector& Start, const FVector& End, const Chaos::FImplicitObject& Geom, const FQuat& Rotation, const TArray<FHitResult>& Hits, float Lifetime);
