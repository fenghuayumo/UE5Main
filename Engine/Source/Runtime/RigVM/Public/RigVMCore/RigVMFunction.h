// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigVMExecuteContext.h"
#include "RigVMMemory.h"
#include "Blueprint/BlueprintSupport.h"

typedef TArrayView<FRigVMMemoryHandle> FRigVMMemoryHandleArray;
typedef TArrayView<void*> FRigVMUserDataArray;

typedef void (*FRigVMFunctionPtr)(FRigVMExtendedExecuteContext& RigVMExecuteContext, FRigVMMemoryHandleArray RigVMMemoryHandles);

struct FRigVMTemplate;

/**
 * The Pin Direction is used to differentiate different kinds of 
 * pins in the data flow graph - inputs, outputs etc.
 */
UENUM(BlueprintType)
enum class ERigVMPinDirection : uint8
{
	Input, // A const input value
	Output, // A mutable output value
	IO, // A mutable input and output value
	Visible, // A const value that cannot be connected to
	Hidden, // A mutable hidden value (used for interal state)
	Invalid // The max value for this enum - used for guarding.
};

/**
 * The FRigVMFunctionArgument describes an argument necessary for the C++ invocation of the RIGVM_METHOD backed function
 */
struct RIGVM_API FRigVMFunctionArgument
{
	const TCHAR* Name;
	const TCHAR* Type;

	FRigVMFunctionArgument()
		: Name(nullptr)
		, Type(nullptr)
	{
	}

	FRigVMFunctionArgument(const TCHAR* InName, const TCHAR* InType)
		: Name(InName)
		, Type(InType)
	{
	}
};

/**
 * The FRigVMFunction is used to represent a function pointer generated by UHT
 * for a given name. The name might be something like "FMyStruct::MyVirtualMethod"
 */
struct RIGVM_API FRigVMFunction
{
	const TCHAR* Name;
	UScriptStruct* Struct;
	FRigVMFunctionPtr FunctionPtr;
	int32 Index;
	int32 TemplateIndex;
	TArray<FRigVMFunctionArgument> Arguments;

	FRigVMFunction()
		: Name(nullptr)
		, Struct(nullptr)
		, FunctionPtr(nullptr)
		, Index(INDEX_NONE)
		, TemplateIndex(INDEX_NONE)
		, Arguments()
	{
	}

	FRigVMFunction(const TCHAR* InName, FRigVMFunctionPtr InFunctionPtr, UScriptStruct* InStruct = nullptr, int32 InIndex = INDEX_NONE, const TArray<FRigVMFunctionArgument>& InArguments = TArray<FRigVMFunctionArgument>())
		: Name(InName)
		, Struct(InStruct)
		, FunctionPtr(InFunctionPtr)
		, Index(InIndex)
		, TemplateIndex(INDEX_NONE)
		, Arguments(InArguments)
	{
	}

	FORCEINLINE bool IsValid() const { return Name != nullptr && FunctionPtr != nullptr; }
	FString GetName() const;
	FName GetMethodName() const;
	FString GetModuleName() const;
	FString GetModuleRelativeHeaderPath() const;
	const TArray<FRigVMFunctionArgument>& GetArguments() const { return Arguments; }
	bool IsAdditionalArgument(const FRigVMFunctionArgument& InArgument) const;
	const FRigVMTemplate* GetTemplate() const;
};
