// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "ComputeFramework/ComputeGraphInstance.h"
#include "ComputeGraphComponent.generated.h"

class UComputeGraph;

/** 
 * Component which holds a context for a UComputeGraph.
 * This object binds the graph to its data providers, and queues the execution. 
 */
UCLASS(Blueprintable, meta = (BlueprintSpawnableComponent))
class COMPUTEFRAMEWORK_API UComputeGraphComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UComputeGraphComponent();
	~UComputeGraphComponent();

	/** The Compute Graph asset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Compute")
	TObjectPtr<UComputeGraph> ComputeGraph = nullptr;

	/**
	 * Create the Data Provider objects for the current ComputeGraph.
	 * @param bSetDefaultBindings Attempt to automate setup of the Data Provider objects based on the current Actor.
	 */
	UFUNCTION(BlueprintCallable, Category = "Compute")
	void CreateDataProviders(bool bSetDefaultBindings);

	/** Destroy all associated DataProvider objects. */
	UFUNCTION(BlueprintCallable, Category = "Compute")
	void DestroyDataProviders();

	/** Queue the graph for execution at the next render update. */
	UFUNCTION(BlueprintCallable, Category = "Compute")
	void QueueExecute();

protected:
	//~ Begin UActorComponent Interface
	void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	void SendRenderDynamicData_Concurrent() override;
	bool ShouldCreateRenderState() const override { return true; }
	//~ End UActorComponent Interface

private:
	UPROPERTY()
	FComputeGraphInstance ComputeGraphInstance;

	bool bValidProviders = false;
};
