// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Class.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#if WITH_ENGINE
#include "Curves/RichCurve.h"
#endif

#include "InterchangeCommonAnimationPayload.generated.h"

UENUM()
enum class EInterchangeTransformCurveChannel
{
	TranslationX = 0,
	TranslationY = 1,
	TranslationZ = 2,
	EulerX = 3,
	EulerY = 4,
	EulerZ = 5,
	ScaleX = 6,
	ScaleY = 7,
	ScaleZ = 8,
	TransformChannelCount = 9,
	None
};

/** If using Cubic, this enum describes how the tangents should be controlled. */
UENUM()
enum class EInterchangeCurveInterpMode : uint8
{
	/** Use linear interpolation between values. */
	Linear,
	/** Use a constant value. Represents stepped values. */
	Constant,
	/** Cubic interpolation. See TangentMode for different cubic interpolation options. */
	Cubic,
	/** No interpolation. */
	None
};

/** If using Cubic interpolation mode, this enum describes how the tangents should be controlled. */
UENUM()
enum class EInterchangeCurveTangentMode : uint8
{
	/** Automatically calculates tangents to create smooth curves between values. */
	Auto,
	/** User specifies the tangent as a unified tangent where the two tangents are locked to each other, presenting a consistent curve before and after. */
	User,
	/** User specifies the tangent as two separate broken tangents on each side of the key which can allow a sharp change in evaluation before or after. */
	Break,
	/** No tangents. */
	None
};


/** Enumerates tangent weight modes. */
UENUM()
enum class EInterchangeCurveTangentWeightMode : uint8
{
	/** Don't take tangent weights into account. */
	WeightedNone,
	/** Only take the arrival tangent weight into account for evaluation. */
	WeightedArrive,
	/** Only take the leaving tangent weight into account for evaluation. */
	WeightedLeave,
	/** Take both the arrival and leaving tangent weights into account for evaluation. */
	WeightedBoth
};

/**
* This struct contains only the key data, this is only used to pass animation data from translators to factories
*/
USTRUCT()
struct INTERCHANGECOMMONPARSER_API FInterchangeCurveKey
{
	GENERATED_BODY()

	/** Interpolation mode between this key and the next */
	UPROPERTY()
	EInterchangeCurveInterpMode InterpMode = EInterchangeCurveInterpMode::None;

	/** Mode for tangents at this key */
	UPROPERTY()
	EInterchangeCurveTangentMode TangentMode = EInterchangeCurveTangentMode::None;

	/** If either tangent at this key is 'weighted' */
	UPROPERTY()
	EInterchangeCurveTangentWeightMode TangentWeightMode = EInterchangeCurveTangentWeightMode::WeightedNone;

	/** Time at this key */
	UPROPERTY()
	float Time = 0.0f;

	/** Value at this key */
	UPROPERTY()
	float Value = 0.0f;

	/** If RCIM_Cubic, the arriving tangent at this key */
	UPROPERTY()
	float ArriveTangent = 0.0f;

	/** If RCTWM_WeightedArrive or RCTWM_WeightedBoth, the weight of the left tangent */
	UPROPERTY()
	float ArriveTangentWeight = 0.0f;

	/** If RCIM_Cubic, the leaving tangent at this key */
	UPROPERTY()
	float LeaveTangent = 0.0f;

	/** If RCTWM_WeightedLeave or RCTWM_WeightedBoth, the weight of the right tangent */
	UPROPERTY()
	float LeaveTangentWeight = 0.0f;

#if WITH_ENGINE
	/** Conversion to FRichCurve */
	void ToRichCurveKey(FRichCurveKey& OutKey) const;
#endif

	void Serialize(FArchive& Ar)
	{
		Ar << InterpMode;
		Ar << TangentMode;
		Ar << TangentWeightMode;
		Ar << Time;
		Ar << Value;
		Ar << ArriveTangent;
		Ar << ArriveTangentWeight;
		Ar << LeaveTangent;
		Ar << LeaveTangentWeight;
	}

	friend FArchive& operator<<(FArchive& Ar, FInterchangeCurveKey& P)
	{
		P.Serialize(Ar);
		return Ar;
	}
};

/**
* This struct contains only the key data, this is only used to pass animation data from translators to factories.
* You cannot evaluate a curve with this struct.
*/
USTRUCT()
struct INTERCHANGECOMMONPARSER_API FInterchangeCurve
{
	GENERATED_BODY()

	UPROPERTY()
	EInterchangeTransformCurveChannel TransformChannel = EInterchangeTransformCurveChannel::None;
	
	UPROPERTY()
	TArray<FInterchangeCurveKey> Keys;

#if WITH_ENGINE
	/** Conversion to FRichCurve */
	void ToRichCurve(FRichCurve& OutKey) const;
#endif

	void Serialize(FArchive& Ar)
	{
		Ar << TransformChannel;
		Ar << Keys;
	}

	friend FArchive& operator<<(FArchive& Ar, FInterchangeCurve& P)
	{
		P.Serialize(Ar);
		return Ar;
	}
};