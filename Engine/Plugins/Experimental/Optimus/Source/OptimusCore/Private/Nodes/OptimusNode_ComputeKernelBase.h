﻿// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IOptimusComputeKernelProvider.h"
#include "OptimusDataDomain.h"
#include "OptimusDataType.h"
#include "OptimusNode.h"

#include "OptimusNode_ComputeKernelBase.generated.h"

struct FOptimusCompilerDiagnostic;



UCLASS(Abstract)
class UOptimusNode_ComputeKernelBase :
	public UOptimusNode, 
	public IOptimusComputeKernelProvider
{
	GENERATED_BODY()
	
public:
	/** Implement this to return the HLSL kernel's function name */
	virtual FString GetKernelName() const PURE_VIRTUAL(UOptimusNode_ComputeKernelBase::GetKernelName, return FString();)

	/** Implement this to return the HLSL kernel's function name */
	virtual FIntVector GetGroupSize() const PURE_VIRTUAL(UOptimusNode_ComputeKernelBase::GetGroupSize, return FIntVector();)

	/** Implement this to return the complete HLSL code for this kernel */
	virtual FString GetKernelSourceText() const PURE_VIRTUAL(UOptimusNode_ComputeKernelBase::GetKernelSourceText, return FString();)
	
	// IOptimusComputeKernelProvider
	UOptimusKernelSource* CreateComputeKernel(
		UObject* InKernelSourceOuter,
		const FOptimusPinTraversalContext& InTraversalContext,
		const FOptimus_NodeToDataInterfaceMap& InNodeDataInterfaceMap,
		const FOptimus_PinToDataInterfaceMap& InLinkDataInterfaceMap,
		const TArray<const UOptimusNode*>& InValueNodes,
		const UComputeDataInterface* GraphDataInterface,
		FOptimus_InterfaceBindingMap& OutInputDataBindings, FOptimus_InterfaceBindingMap& OutOutputDataBindings
	) const override;

	void SetCompilationDiagnostics(
		const TArray<FOptimusCompilerDiagnostic>& InDiagnostics
	) override PURE_VIRTUAL(UOptimusNode_ComputeKernelBase::SetCompilationDiagnostics);

protected:
	static TArray<FString> GetIndexNamesFromDataDomainLevels(
		const TArray<FName> &InLevelNames
		)
	{
		TArray<FString> IndexNames;
	
		for (FName DomainName: InLevelNames)
		{
			IndexNames.Add(FString::Printf(TEXT("%sIndex"), *DomainName.ToString()));
		}
		return IndexNames;
	}

	static FString GetCookedKernelSource(
		const FString& InObjectPathName,
		const FString& InShaderSource,
		const FString& InKernelName,
		FIntVector InGroupSize
		);
	
	
private:
	void ProcessInputPinForComputeKernel(
		const UOptimusNodePin* InInputPin,
		const UOptimusNodePin* InOutputPin,
		const FOptimus_NodeToDataInterfaceMap& InNodeDataInterfaceMap,
		const FOptimus_PinToDataInterfaceMap& InLinkDataInterfaceMap,
		const TArray<const UOptimusNode*>& InValueNodes,
		const UComputeDataInterface* GraphDataInterface,
		UOptimusKernelSource* InKernelSource,
		TArray<FString>& OutGeneratedFunctions,
		FOptimus_InterfaceBindingMap& OutInputDataBindings
		) const;

	void ProcessOutputPinForComputeKernel(
		const UOptimusNodePin* InOutputPin,
		const TArray<UOptimusNodePin *>& InInputPins,
		const FOptimus_NodeToDataInterfaceMap& InNodeDataInterfaceMap,
		const FOptimus_PinToDataInterfaceMap& InLinkDataInterfaceMap,
		UOptimusKernelSource* InKernelSource,
		TArray<FString>& OutGeneratedFunctions,
		FOptimus_InterfaceBindingMap& OutOutputDataBindings
		) const;

};

