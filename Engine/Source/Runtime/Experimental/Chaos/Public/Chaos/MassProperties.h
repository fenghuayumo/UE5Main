// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Core.h"
#include "Chaos/Defines.h"
#include "Chaos/Matrix.h"
#include "Chaos/Rotation.h"
#include "Chaos/Vector.h"
#include "Containers/ArrayView.h"

#if !COMPILE_WITHOUT_UNREAL_SUPPORT
namespace Chaos
{
	class FTriangleMesh;

	template<class T, int d>
	class TParticles;
	using FParticles = TParticles<FReal, 3>;

	struct CHAOS_API FMassProperties
	{
		FMassProperties()
			: Mass(0)
			, Volume(0)
			, CenterOfMass(0)
			, RotationOfMass(FRotation3::FromElements(FVec3(0), 1))
			, InertiaTensor(0)
		{}
		FReal Mass;
		FReal Volume;
		FVec3 CenterOfMass;
		FRotation3 RotationOfMass;
		FMatrix33 InertiaTensor;
	};

	FRotation3 CHAOS_API TransformToLocalSpace(FMatrix33& Inertia);
	void CHAOS_API TransformToLocalSpace(FMassProperties& MassProperties);

	template<typename T, typename TSurfaces>
	void CHAOS_API CalculateVolumeAndCenterOfMass(const TParticles<T,3>& Vertices, const TSurfaces& Surfaces, T& OutVolume, TVec3<T>& OutCenterOfMass);

	template<typename T, typename TSurfaces>
	void CHAOS_API CalculateVolumeAndCenterOfMass(const TArray<TVec3<T>>& Vertices, const TSurfaces& Surfaces, T& OutVolume, TVec3<T>& OutCenterOfMass);

	template<typename TSurfaces>
	FMassProperties CHAOS_API CalculateMassProperties(const FParticles& Vertices, const TSurfaces& Surfaces, const FReal Mass);

	template<typename TSurfaces>
	void CHAOS_API CalculateInertiaAndRotationOfMass(const FParticles& Vertices, const TSurfaces& Surfaces, const FReal Density, const FVec3& CenterOfMass,
		FMatrix33& OutInertiaTensor, FRotation3& OutRotationOfMass);

	void CHAOS_API CalculateVolumeAndCenterOfMass(const FBox& BoundingBox, FVector::FReal& OutVolume, FVector& OutCenterOfMass);
	void CHAOS_API CalculateInertiaAndRotationOfMass(const FBox& BoundingBox, const FVector::FReal Density, FMatrix33& OutInertiaTensor, FRotation3& OutRotationOfMass);

	// Combine a list of transformed inertia tensors into a single inertia. Also diagonalize the inertia and set the rotation of mass accordingly. 
	// This is equivalent to a call to CombineWorldSpace followed by TransformToLocalSpace.
	// @see CombineWorldSpace()
	FMassProperties CHAOS_API Combine(const TArray<FMassProperties>& MPArray);

	// Combine a list of transformed inertia tensors into a single inertia tensor.
	// @note The inertia matrix is not diagonalized, and any rotation will be built into the matrix (RotationOfMass will always be Identity)
	// @see Combine()
	FMassProperties CHAOS_API CombineWorldSpace(const TArray<FMassProperties>& MPArray);


	template <typename T, int d>
	using TMassProperties UE_DEPRECATED(4.27, "Deprecated. this class is to be deleted, use FMassProperties instead") = FMassProperties;
}

#endif
