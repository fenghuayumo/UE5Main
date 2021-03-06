// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <Metal/Metal.h>
#include "device.hpp"
#include "command_queue.hpp"
#include "command_buffer.hpp"

#include "Containers/LockFreeList.h"

class FAGXCommandList;

/**
 * Enumeration of features which are present only on some OS/device combinations.
 * These have to be checked at runtime as well as compile time to ensure backward compatibility.
 */
typedef NS_OPTIONS(uint64, EAGXFeatures)
{
	/** Support for specifying an update to the buffer offset only */
	EAGXFeaturesSetBufferOffset = 1 << 0,
	/** Supports NSUInteger counting visibility queries */
	EAGXFeaturesCountingQueries = 1 << 1,
	/** Supports base vertex/instance for draw calls */
	EAGXFeaturesBaseVertexInstance = 1 << 2,
	/** Supports indirect buffers for draw calls */
	EAGXFeaturesIndirectBuffer = 1 << 3,
	/** Supports layered rendering */
	EAGXFeaturesLayeredRendering = 1 << 4,
	/** Support for specifying small buffers as byte arrays */
	EAGXFeaturesSetBytes = 1 << 5,
	/** Unused Reserved Bit */
	EAGXFeaturesUnusedReservedBit6 = 1 << 6, // was EAGXFeaturesTessellation
	/** Supports framework-level validation */
	EAGXFeaturesValidation = 1 << 7,
	/** Supports detailed statistics */
	EAGXFeaturesStatistics = 1 << 8,
	/** Supports the explicit MTLHeap APIs */
	EAGXFeaturesHeaps_REMOVED = 1 << 9,
	/** Supports the explicit MTLFence APIs */
	EAGXFeaturesFences_REMOVED = 1 << 10,
	/** Supports MSAA Depth Resolves */
	EAGXFeaturesMSAADepthResolve = 1 << 11,
	/** Supports Store & Resolve in a single store action */
	EAGXFeaturesMSAAStoreAndResolve = 1 << 12,
	/** Supports framework GPU frame capture */
	EAGXFeaturesGPUTrace = 1 << 13,
	/** Supports the use of cubemap arrays */
	EAGXFeaturesCubemapArrays = 1 << 14,
	/** Supports the specification of multiple viewports and scissor rects */
	EAGXFeaturesMultipleViewports = 1 << 15,
    /** Supports minimum on-glass duration for drawables */
    EAGXFeaturesPresentMinDuration = 1llu << 16llu,
    /** Supports programmatic frame capture API */
    EAGXFeaturesGPUCaptureManager = 1llu << 17llu,
	/** Supports efficient buffer-blits */
	EAGXFeaturesEfficientBufferBlits = 1llu << 18llu,
	/** Supports any kind of buffer sub-allocation */
	EAGXFeaturesBufferSubAllocation = 1llu << 19llu,
	/** Supports private buffer sub-allocation */
	EAGXFeaturesPrivateBufferSubAllocation = 1llu << 20llu,
	/** Supports texture buffers */
	EAGXFeaturesTextureBuffers = 1llu << 21llu,
	/** Supports max. compute threads per threadgroup */
	EAGXFeaturesMaxThreadsPerThreadgroup = 1llu << 22llu,
	/** Supports parallel render encoders */
	EAGXFeaturesParallelRenderEncoders = 1llu << 23llu,
	/** Supports indirect argument buffers */
	EAGXFeaturesIABs_REMOVED = 1llu << 24llu,
	/** Supports specifying the mutability of buffers bound to PSOs */
    EAGXFeaturesPipelineBufferMutability = 1llu << 25llu,
    /** Supports tile shaders */
    EAGXFeaturesTileShaders = 1llu << 26llu,
	/** Unused Reserved Bit */
	EAGXFeaturesUnusedReservedBit27 = 1llu << 27llu, // was EAGXFeaturesSeparateTessellation
	/** Supports indirect argument buffers Tier 2 */
	EAGXFeaturesTier2IABs_REMOVED = 1llu << 28llu,
};

/**
 * FAGXCommandQueue:
 */
class FAGXCommandQueue
{
public:
#pragma mark - Public C++ Boilerplate -

	/**
	 * Constructor
	 * @param Device The Metal device to create on.
	 * @param MaxNumCommandBuffers The maximum number of incomplete command-buffers, defaults to 0 which implies the system default.
	 */
	FAGXCommandQueue(uint32 const MaxNumCommandBuffers = 0);
	
	/** Destructor */
	~FAGXCommandQueue(void);
	
#pragma mark - Public Command Buffer Mutators -

	/**
	 * Start encoding to CommandBuffer. It is an error to call this with any outstanding command encoders or current command buffer.
	 * Instead call EndEncoding & CommitCommandBuffer before calling this.
	 * @param CommandBuffer The new command buffer to begin encoding to.
	 */
	mtlpp::CommandBuffer CreateCommandBuffer(void);
	
	/**
	 * Commit the supplied command buffer immediately.
	 * @param CommandBuffer The command buffer to commit, must be non-nil.
 	 */
	void CommitCommandBuffer(mtlpp::CommandBuffer& CommandBuffer);
	
	/**
	 * Deferred contexts submit their internal lists of command-buffers out of order, the command-queue takes ownership and handles reordering them & lazily commits them once all command-buffer lists are submitted.
	 * @param BufferList The list of buffers to enqueue into the command-queue at the given index.
	 * @param Index The 0-based index to commit BufferList's contents into relative to other active deferred contexts.
	 * @param Count The total number of deferred contexts that will submit - only once all are submitted can any command-buffer be committed.
	 */
	void SubmitCommandBuffers(TArray<mtlpp::CommandBuffer> BufferList, uint32 Index, uint32 Count);

	/** @params Fences An array of command-buffer fences for the committed command-buffers */
	void GetCommittedCommandBufferFences(TArray<mtlpp::CommandBufferFence>& Fences);
	
#pragma mark - Public Command Queue Accessors -
	
	/** Converts a Metal v1.1+ resource option to something valid on the current version. */
	static mtlpp::ResourceOptions GetCompatibleResourceOptions(mtlpp::ResourceOptions Options);
	
	/**
	 * @param InFeature A specific Metal feature to check for.
	 * @returns True if the requested feature is supported, else false.
	 */
	static inline bool SupportsFeature(EAGXFeatures InFeature) { return ((Features & InFeature) != 0); }

	/**
	* @param InFeature A specific Metal feature to check for.
	* @returns True if RHISupportsSeparateMSAAAndResolveTextures will be true.  
	* Currently Mac only.
	*/
	static inline bool SupportsSeparateMSAAAndResolveTarget() { return (PLATFORM_MAC != 0 || GMaxRHIFeatureLevel >= ERHIFeatureLevel::SM5); }

#pragma mark - Public Debug Support -

	/** Inserts a boundary that marks the end of a frame for the debug capture tool. */
	void InsertDebugCaptureBoundary(void);
	
	/** Enable or disable runtime debugging features. */
	void SetRuntimeDebuggingLevel(int32 const Level);
	
	/** @returns The level of runtime debugging features enabled. */
	int32 GetRuntimeDebuggingLevel(void) const;

private:
#pragma mark - Private Member Variables -
	id<MTLCommandQueue> CommandQueue;
	TArray<TArray<mtlpp::CommandBuffer>> CommandBuffers;
	TLockFreePointerListLIFO<mtlpp::CommandBufferFence> CommandBufferFences;
	uint64 ParallelCommandLists;
	int32 RuntimeDebuggingLevel;
	static NSUInteger PermittedOptions;
	static uint64 Features;
};
