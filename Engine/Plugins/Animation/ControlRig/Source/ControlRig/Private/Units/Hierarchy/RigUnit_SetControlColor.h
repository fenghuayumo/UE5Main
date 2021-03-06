// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/RigUnit.h"
#include "RigUnit_SetControlColor.generated.h"

/**
 * SetControlColor is used to change the gizmo's color on a control at runtime
 */
USTRUCT(meta=(DisplayName="Set Control Color", Category="Controls", DocumentationPolicy="Strict", Keywords = "SetControlColor,SetGizmoColor", TemplateName="SetControlColor", NodeColor = "0.0 0.36470600962638855 1.0"))
struct CONTROLRIG_API FRigUnit_SetControlColor : public FRigUnitMutable
{
	GENERATED_BODY()

	FRigUnit_SetControlColor()
		: Control(NAME_None)
		, Color(FLinearColor::Black)
		, CachedControlIndex(FCachedRigElement())
	{}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/**
	 * The name of the Control to set the color for.
	 */
	UPROPERTY(meta = (Input, CustomWidget = "ControlName" ))
	FName Control;

	/**
	 * The color to set for the control
	 */
	UPROPERTY(meta = (Input))
	FLinearColor Color;

	// Used to cache the internally used bone index
	UPROPERTY()
	FCachedRigElement CachedControlIndex;
};
