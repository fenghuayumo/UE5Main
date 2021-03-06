// Copyright Epic Games, Inc. All Rights Reserved.

#include "Conditions/StateTreeCondition_Common.h"

#include "StateTreeExecutionContext.h"
#include "StateTreePropertyBindings.h"
#include "StateTreeLinker.h"

#define LOCTEXT_NAMESPACE "StateTreeEditor"

namespace UE::StateTree::Conditions
{

#if WITH_EDITOR
FText GetOperatorText(const EGenericAICheck Operator)
{
	switch (Operator)
	{
	case EGenericAICheck::Equal:
		return FText::FromString(TEXT("=="));
		break;
	case EGenericAICheck::NotEqual:
		return FText::FromString(TEXT("!="));
		break;
	case EGenericAICheck::Less:
		return FText::FromString(TEXT("&lt;"));
		break;
	case EGenericAICheck::LessOrEqual:
		return FText::FromString(TEXT("&lt;="));
		break;
	case EGenericAICheck::Greater:
		return FText::FromString(TEXT("&gt;"));
		break;
	case EGenericAICheck::GreaterOrEqual:
		return FText::FromString(TEXT("&gt;="));
		break;
	default:
		return FText::FromString(TEXT("??"));
		break;
	}
	return FText::GetEmpty();
}
#endif // WITH_EDITOR

template<typename T>
bool CompareNumbers(const T Left, const T Right, const EGenericAICheck Operator)
{
	switch (Operator)
	{
	case EGenericAICheck::Equal:
		return Left == Right;
		break;
	case EGenericAICheck::NotEqual:
		return Left != Right;
		break;
	case EGenericAICheck::Less:
		return Left < Right;
		break;
	case EGenericAICheck::LessOrEqual:
		return Left <= Right;
		break;
	case EGenericAICheck::Greater:
		return Left > Right;
		break;
	case EGenericAICheck::GreaterOrEqual:
		return Left >= Right;
		break;
	default:
		ensureMsgf(false, TEXT("Unhandled operator %d"), Operator);
		return false;
		break;
	}
	return false;
}

} // UE::StateTree::Conditions


//----------------------------------------------------------------------//
//  FStateTreeCondition_CompareInt
//----------------------------------------------------------------------//

bool FStateTreeCondition_CompareInt::Link(FStateTreeLinker& Linker)
{
	Linker.LinkInstanceDataProperty(LeftHandle, STATETREE_INSTANCEDATA_PROPERTY(FStateTreeCondition_CompareIntInstanceData, Left));
	Linker.LinkInstanceDataProperty(RightHandle, STATETREE_INSTANCEDATA_PROPERTY(FStateTreeCondition_CompareIntInstanceData, Right));

	return true;
}

bool FStateTreeCondition_CompareInt::TestCondition(FStateTreeExecutionContext& Context) const
{
	const int32 Left = Context.GetInstanceData(LeftHandle);
	const int32 Right = Context.GetInstanceData(RightHandle);
	const bool bResult = UE::StateTree::Conditions::CompareNumbers<int32>(Left, Right, Operator);
	return bResult ^ bInvert;
}


//----------------------------------------------------------------------//
//  FStateTreeCondition_CompareFloat
//----------------------------------------------------------------------//

bool FStateTreeCondition_CompareFloat::Link(FStateTreeLinker& Linker)
{
	Linker.LinkInstanceDataProperty(LeftHandle, STATETREE_INSTANCEDATA_PROPERTY(FStateTreeCondition_CompareFloatInstanceData, Left));
	Linker.LinkInstanceDataProperty(RightHandle, STATETREE_INSTANCEDATA_PROPERTY(FStateTreeCondition_CompareFloatInstanceData, Right));

	return true;
}

bool FStateTreeCondition_CompareFloat::TestCondition(FStateTreeExecutionContext& Context) const
{
	const float Left = Context.GetInstanceData(LeftHandle);
	const float Right = Context.GetInstanceData(RightHandle);
	const bool bResult = UE::StateTree::Conditions::CompareNumbers<float>(Left, Right, Operator);
	return bResult ^ bInvert;
}


//----------------------------------------------------------------------//
//  FStateTreeCondition_CompareBool
//----------------------------------------------------------------------//

bool FStateTreeCondition_CompareBool::Link(FStateTreeLinker& Linker)
{
	Linker.LinkInstanceDataProperty(LeftHandle, STATETREE_INSTANCEDATA_PROPERTY(FStateTreeCondition_CompareBoolInstanceData, bLeft));
	Linker.LinkInstanceDataProperty(RightHandle, STATETREE_INSTANCEDATA_PROPERTY(FStateTreeCondition_CompareBoolInstanceData, bRight));

	return true;
}

bool FStateTreeCondition_CompareBool::TestCondition(FStateTreeExecutionContext& Context) const
{
	const bool bLeft = Context.GetInstanceData(LeftHandle);
	const bool bRight = Context.GetInstanceData(RightHandle);
	return (bLeft == bRight) ^ bInvert;
}


//----------------------------------------------------------------------//
//  FStateTreeCondition_CompareEnum
//----------------------------------------------------------------------//

bool FStateTreeCondition_CompareEnum::Link(FStateTreeLinker& Linker)
{
	Linker.LinkInstanceDataProperty(LeftHandle, STATETREE_INSTANCEDATA_PROPERTY(FStateTreeCondition_CompareEnumInstanceData, Left));
	Linker.LinkInstanceDataProperty(RightHandle, STATETREE_INSTANCEDATA_PROPERTY(FStateTreeCondition_CompareEnumInstanceData, Right));

	return true;
}

bool FStateTreeCondition_CompareEnum::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FStateTreeAnyEnum Left = Context.GetInstanceData(LeftHandle);
	const FStateTreeAnyEnum Right = Context.GetInstanceData(RightHandle);
	return (Left == Right) ^ bInvert;
}

#if WITH_EDITOR
void FStateTreeCondition_CompareEnum::OnBindingChanged(const FGuid& ID, FStateTreeDataView InstanceData, const FStateTreeEditorPropertyPath& SourcePath, const FStateTreeEditorPropertyPath& TargetPath, const IStateTreeBindingLookup& BindingLookup)
{
	if (!TargetPath.IsValid())
	{
		return;
	}

	FStateTreeCondition_CompareEnumInstanceData& Instance = InstanceData.GetMutable<FStateTreeCondition_CompareEnumInstanceData>();

	// Left has changed, update enums from the leaf property.
	if (TargetPath.Path.Last() == GET_MEMBER_NAME_STRING_CHECKED(FStateTreeCondition_CompareEnumInstanceData, Left))
	{
		if (const FProperty* LeafProperty = BindingLookup.GetPropertyPathLeafProperty(SourcePath))
		{
			// Handle both old stype namespace enums and new class enum properties.
			UEnum* NewEnum = nullptr;
			if (const FByteProperty* ByteProperty = CastField<FByteProperty>(LeafProperty))
			{
				NewEnum = ByteProperty->GetIntPropertyEnum();
			}
			else if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(LeafProperty))
			{
				NewEnum = EnumProperty->GetEnum();
			}

			if (Instance.Left.Enum != NewEnum)
			{
				Instance.Left.Initialize(NewEnum);
			}
		}
		else
		{
			Instance.Left.Initialize(nullptr);
		}

		if (Instance.Right.Enum != Instance.Left.Enum)
		{
			Instance.Right.Initialize(Instance.Left.Enum);
		}
	}
}

#endif


//----------------------------------------------------------------------//
//  FStateTreeCondition_CompareDistance
//----------------------------------------------------------------------//

bool FStateTreeCondition_CompareDistance::Link(FStateTreeLinker& Linker)
{
	Linker.LinkInstanceDataProperty(SourceHandle, STATETREE_INSTANCEDATA_PROPERTY(FStateTreeCondition_CompareDistanceInstanceData, Source));
	Linker.LinkInstanceDataProperty(TargetHandle, STATETREE_INSTANCEDATA_PROPERTY(FStateTreeCondition_CompareDistanceInstanceData, Target));
	Linker.LinkInstanceDataProperty(DistanceHandle, STATETREE_INSTANCEDATA_PROPERTY(FStateTreeCondition_CompareDistanceInstanceData, Distance));

	return true;
}

bool FStateTreeCondition_CompareDistance::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FVector& Source = Context.GetInstanceData(SourceHandle);
	const FVector& Target = Context.GetInstanceData(TargetHandle);
	const float Distance = Context.GetInstanceData(DistanceHandle);

	const float Left = FVector::DistSquared(Source, Target);
	const float Right = FMath::Square(Distance);
	const bool bResult = UE::StateTree::Conditions::CompareNumbers<float>(Left, Right, Operator);
	return bResult ^ bInvert;
}


//----------------------------------------------------------------------//
//  FStateTreeCondition_Random
//----------------------------------------------------------------------//

bool FStateTreeCondition_Random::Link(FStateTreeLinker& Linker)
{
	Linker.LinkInstanceDataProperty(ThresholdHandle, STATETREE_INSTANCEDATA_PROPERTY(FStateTreeCondition_RandomInstanceData, Threshold));

	return true;
}

bool FStateTreeCondition_Random::TestCondition(FStateTreeExecutionContext& Context) const
{
	const float Threshold = Context.GetInstanceData(ThresholdHandle);
	return FMath::FRandRange(0.0f, 1.0f) < Threshold;
}


#undef LOCTEXT_NAMESPACE
