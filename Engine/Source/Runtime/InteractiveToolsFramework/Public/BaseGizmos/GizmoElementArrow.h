// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BaseGizmos/GizmoElementBase.h"
#include "InputState.h"
#include "UObject/ObjectMacros.h"
#include "GizmoElementArrow.generated.h"

class UGizmoElementBox;
class UGizmoElementCone;
class UGizmoElementCylinder;

/**
 * Simple object intended to be used as part of 3D Gizmos.
 * Draws a 3D arrow based on parameters.
 */

UENUM()
enum class EGizmoElementArrowHeadType
{
	Cone,
	Cube
};


UCLASS(Transient)
class INTERACTIVETOOLSFRAMEWORK_API UGizmoElementArrow : public UGizmoElementBase
{
	GENERATED_BODY()

public:
	UGizmoElementArrow();

	//~ Begin UGizmoElementBase Interface.
	virtual void Render(IToolsContextRenderAPI* RenderAPI, const FRenderTraversalState& RenderState) override;
	virtual FInputRayHit LineTrace(const FVector Start, const FVector Direction) override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	//~ End UGizmoElementBase Interface.

	// Location of base of arrow cylinder
	virtual void SetBase(const FVector& InBase);
	virtual FVector GetBase() const;

	// Arrow direction.
	virtual void SetDirection(const FVector& InDirection);
	virtual FVector GetDirection() const;

	// Arrow side direction for box head.
	virtual void SetSideDirection(const FVector& InSideDirection);
	virtual FVector GetSideDirection() const;

	// Arrow body length.
	virtual void SetBodyLength(float InBodyLength);
	virtual float GetBodyLength() const;

	// Arrow body cylinder radius.
	virtual void SetBodyRadius(float InBodyRadius);
	virtual float GetBodyRadius() const;

	// Arrow head length, used for both cone and cube head.
	virtual void SetHeadLength(float InHeadLength);
	virtual float GetHeadLength() const;

	// Arrow head radius, if cone.
	virtual void SetHeadRadius(float InHeadRadius);
	virtual float GetHeadRadius() const;

	// Number of sides for cylinder and cone, if relevant.
	virtual void SetNumSides(int32 InNumSides);
	virtual int32 GetNumSides() const;

	// Head type cone or cube.
	virtual void SetHeadType(EGizmoElementArrowHeadType InHeadType);
	virtual EGizmoElementArrowHeadType GetHeadType() const;

protected:

	// Update arrow cylinder based on parameters
	virtual void UpdateArrowBody();

	// Update arrow cone or box based on parameters
	virtual void UpdateArrowHead();

	// Arrow cylinder body
	UPROPERTY()
	TObjectPtr<UGizmoElementCylinder> CylinderElement;

	// Arrow cone head
	UPROPERTY()
	TObjectPtr<UGizmoElementCone> ConeElement;

	// Arrow box head
	UPROPERTY()
	TObjectPtr<UGizmoElementBox> BoxElement;

	// Location of center of arrow base circle.
	UPROPERTY()
	FVector Base = FVector::ZeroVector;

	// Direction of arrow axis
	UPROPERTY()
	FVector Direction = FVector(0.0f, 0.0f, 1.0f);

	// Side direction for box head
	UPROPERTY()
	FVector SideDirection = FVector(0.0f, 1.0f, 0.0f);

	// Arrow body length
	UPROPERTY()
	float BodyLength = 1.5f;

	// Radius of arrow cylinder
	UPROPERTY()
	float BodyRadius = 0.5f;

	// Length of head, cone or box
	UPROPERTY()
	float HeadLength = 0.5f;

	// Radius of head cone
	UPROPERTY()
	float HeadRadius = 0.75f;

	// Number of sides for tessellating cone and cylinder
	UPROPERTY()
	int32 NumSides = 32;

	// Head type
	UPROPERTY()
	EGizmoElementArrowHeadType HeadType = EGizmoElementArrowHeadType::Cone;
};
