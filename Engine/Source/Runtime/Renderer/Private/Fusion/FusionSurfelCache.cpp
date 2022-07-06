#include "Fusion.h"
#include "SurfelTypes.h"
#ifdef RHI_RAYTRACING

#include "ScenePrivate.h"
#include "SceneRenderTargets.h"
#include "RenderGraphBuilder.h"
#include "RenderTargetPool.h"
#include "RHIResources.h"
#include "UniformBuffer.h"
#include "RayGenShaderUtils.h"
#include "SceneTextureParameters.h"
#include "ScreenSpaceDenoise.h"
#include "ClearQuad.h"
#include "PostProcess/PostProcessing.h"
#include "PostProcess/SceneFilterRendering.h"
#include "RayTracing/RaytracingOptions.h"
#include "BlueNoise.h"
#include "SceneTextureParameters.h"
#include "RayTracingDefinitions.h"
#include "RayTracing/RayTracingDeferredMaterials.h"
#include "RayTracingTypes.h"
#include "PathTracingDefinitions.h"
#include "PathTracing.h"

const uint32 IRCACHE_CASCADE_SIZE = 32;

#ifndef IRCACHE_CASCADE_COUNT
#define     IRCACHE_CASCADE_COUNT 12
#endif
const int MAX_GRID_CELLS =  IRCACHE_CASCADE_SIZE * IRCACHE_CASCADE_SIZE * IRCACHE_CASCADE_SIZE * IRCACHE_CASCADE_COUNT;

const uint32 MAX_ENTRIES = 1024 * 64;

// Must match GPU side
const float UNIT_SCALE = 100.0f;
const float IRCACHE_GRID_CELL_DIAMETER = UNIT_SCALE * 0.16 * 0.125;

#define INDIRECTION_BUF_ELEM_COUNT 1024 * 1024

extern TAutoConsoleVariable<int32> CVarSurfelGIUseSurfel;
extern TAutoConsoleVariable<int32> CVarFusionSurfelAccumulateEmissive;

struct FSurfelVertexPacked
{
	FVector4f   data0;
	// FVector4   data1;
};

// struct SurfelCascadeParameter 
// {
//     FIntVector4 Origin;
//     FIntVector4 VoxelScrolled;
// };

// BEGIN_SHADER_PARAMETER_STRUCT(SurfelCascadeConstants,)
// 	SHADER_PARAMETER(FIntVector4, Origin)
// 	SHADER_PARAMETER(FIntVector4, VoxelScrolled)
// END_SHADER_PARAMETER_STRUCT()

// BEGIN_SHADER_PARAMETER_STRUCT(FSurfelCascades,)
//     SHADER_PARAMETER(FVector4f, SurfelGridCenter)
// 	SHADER_PARAMETER_ARRAY(FIntVector4, Origin, [IRCACHE_CASCADE_COUNT])
// 	SHADER_PARAMETER_ARRAY(FIntVector4, VoxelScrolled, [IRCACHE_CASCADE_COUNT])
// END_SHADER_PARAMETER_STRUCT()

// IMPLEMENT_STATIC_UNIFORM_BUFFER_SLOT(SurfelGridData);
// IMPLEMENT_STATIC_AND_SHADER_UNIFORM_BUFFER_STRUCT(FSurfelCascades, "SurfelGridData", SurfelGridData);

class FClearEntriesPoolCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FClearEntriesPoolCS)
	SHADER_USE_PARAMETER_STRUCT(FClearEntriesPoolCS, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		//OutEnvironment.CompilerFlags.Add(CFLAG_WarningsAsErrors);
		OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
		OutEnvironment.CompilerFlags.Add(CFLAG_ForceDXC);
		OutEnvironment.SetDefine(TEXT("THREAD_BLOCK_SIZE"), GetThreadBlockSize());
		OutEnvironment.SetDefine(TEXT("CLEAR_ENTRIES"), 1);
	}

	static uint32 GetThreadBlockSize()
	{
		return 64;
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint32>, SurfelLifeBuf)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint32>, SurfelPoolBuf)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FClearEntriesPoolCS, "/Engine/Private/SurfelCache/SurfelEntries.usf", "ClearEntriesPoolCS", SF_Compute);

class FCompactEntriesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FCompactEntriesCS)
	SHADER_USE_PARAMETER_STRUCT(FCompactEntriesCS, FGlobalShader)

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		//OutEnvironment.CompilerFlags.Add(CFLAG_WarningsAsErrors);
		OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
		OutEnvironment.CompilerFlags.Add(CFLAG_ForceDXC);
		OutEnvironment.SetDefine(TEXT("THREAD_BLOCK_SIZE"), GetThreadBlockSize());
		OutEnvironment.SetDefine(TEXT("COMPACT_ENTRIES"), 1);
	}

	static uint32 GetThreadBlockSize()
	{
		return 64;
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWByteAddressBuffer, SurfelMetaBuf)

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint32>, SurfelLifeBuf)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint32>, SurfelEntryIndirectionBuf)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint32>, SurfelEntryOccupancyBuf)


		RDG_BUFFER_ACCESS(IndirectDispatchArgs, ERHIAccess::IndirectArgs)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FCompactEntriesCS, "/Engine/Private/SurfelCache/SurfelEntries.usf", "CompactEntriesCS", SF_Compute);

class FAgeEntriesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FAgeEntriesCS)
	SHADER_USE_PARAMETER_STRUCT(FAgeEntriesCS, FGlobalShader)

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		//OutEnvironment.CompilerFlags.Add(CFLAG_WarningsAsErrors);
		OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
		OutEnvironment.CompilerFlags.Add(CFLAG_ForceDXC);
		OutEnvironment.SetDefine(TEXT("THREAD_BLOCK_SIZE"), GetThreadBlockSize());
		OutEnvironment.SetDefine(TEXT("AGE_ENTRIES"), 1);
	}

	static uint32 GetThreadBlockSize()
	{
		return 64;
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWByteAddressBuffer, SurfelMetaBuf)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWByteAddressBuffer, SurfelGridMetaBuf)

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint32>, SurfelLifeBuf)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint32>, SurfelPoolBuf)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FSurfelVertexPacked>, SurfelRePositionBuf)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint32>, SurfelRePositionCountBuf)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FSurfelVertexPacked>, SurfelVertexBuf)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, SurfelIrradianceBuf)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint32>, SurfelEntryOccupancyBuf)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint32>, SurfelEntryCellBuf)

		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		//SHADER_PARAMETER_RDG_BUFFER(ByteAddressBuffer, IndirectDispatchArgs)
		RDG_BUFFER_ACCESS(IndirectDispatchArgs, ERHIAccess::IndirectArgs)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FAgeEntriesCS, "/Engine/Private/SurfelCache/SurfelEntries.usf", "AgeEntriesCS", SF_Compute);

class FResetEntriesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FResetEntriesCS)
	SHADER_USE_PARAMETER_STRUCT(FResetEntriesCS, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
		OutEnvironment.CompilerFlags.Add(CFLAG_ForceDXC);
		OutEnvironment.SetDefine(TEXT("RESET_ENTRIES"), 1);
	}
	static uint32 GetThreadBlockSize()
	{
		return 64;
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint32>, SurfelLifeBuf)
		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, SurfelMetaBuf)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, SurfelIrradianceBuf)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, SurfelAuxiBuf)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint32>, SurfelEntryIndirectionBuf)
		RDG_BUFFER_ACCESS(IndirectDispatchArgs, ERHIAccess::IndirectArgs)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FResetEntriesCS, "/Engine/Private/SurfelCache/SurfelEntries.usf", "ResetEntriesCS", SF_Compute);

class FPrepareTraceArgsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FPrepareTraceArgsCS)
	SHADER_USE_PARAMETER_STRUCT(FPrepareTraceArgsCS, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
		OutEnvironment.CompilerFlags.Add(CFLAG_ForceDXC);
		OutEnvironment.SetDefine(TEXT("PRE_DISPATCH_SURFEL_ARGS"), 1);
	}


	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWByteAddressBuffer, RWDispatchArgs)
		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, SurfelMetaBuf)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FPrepareTraceArgsCS, "/Engine/Private/SurfelCache/PrepareIndirectArgs.usf", "PrepareTraceArgsCS", SF_Compute);

class FPrepareAgeArgsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FPrepareAgeArgsCS)
	SHADER_USE_PARAMETER_STRUCT(FPrepareAgeArgsCS, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
		OutEnvironment.CompilerFlags.Add(CFLAG_ForceDXC);
		OutEnvironment.SetDefine(TEXT("PRE_DISPATCH_SURFEL_ARGS"), 1);
	}


	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWByteAddressBuffer, RWDispatchArgs)
		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, SurfelMetaBuf)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FPrepareAgeArgsCS, "/Engine/Private/SurfelCache/PrepareIndirectArgs.usf", "PrepareAgeArgsCS", SF_Compute);

class FSumIrradianceCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSumIrradianceCS)
	SHADER_USE_PARAMETER_STRUCT(FSumIrradianceCS, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
		OutEnvironment.CompilerFlags.Add(CFLAG_ForceDXC);
	}
	static uint32 GetThreadBlockSize()
	{
		return 64;
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, SurfelLifeBuf)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWByteAddressBuffer, SurfelMetaBuf)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, SurfelIrradianceBuf)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, SurfelAuxiBuf)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, SurfelEntryIndirectionBuf)
        SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		RDG_BUFFER_ACCESS(IndirectDispatchArgs, ERHIAccess::IndirectArgs)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FSumIrradianceCS, "/Engine/Private/SurfelCache/SurfelSumIrradiance.usf", "SumIrradianceCS", SF_Compute);

class FScollCascadeCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FScollCascadeCS)
	SHADER_USE_PARAMETER_STRUCT(FScollCascadeCS, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
		OutEnvironment.CompilerFlags.Add(CFLAG_ForceDXC);
		OutEnvironment.SetDefine(TEXT("PRE_DISPATCH_SURFEL_ARGS"), 1);
	}
    static uint32 GetThreadBlockSize()
	{
		return 32;
	}


	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )

        SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, SurfelGridMetaBuf)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWByteAddressBuffer, SurfelGridMetaBuf2)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, SurfelEntryCellBuf)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, SurfelIrradianceBuf)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, SurfelLifeBuf)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, SurfelPoolBuf)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWByteAddressBuffer, SurfelMetaBuf)
        SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
        // SHADER_PARAMETER_STRUCT_INCLUDE(FSurfelCascades, SurfelGridData)
        SHADER_PARAMETER(FVector4f, SurfelGridCenter)
        SHADER_PARAMETER_ARRAY(FIntVector4, SurfelGridOrigin, [IRCACHE_CASCADE_COUNT])
        SHADER_PARAMETER_ARRAY(FIntVector4, SurfelGridVoxelScrolled, [IRCACHE_CASCADE_COUNT])
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FScollCascadeCS, "/Engine/Private/SurfelCache/ScrollCascade.usf", "ScrollCascadeCS", SF_Compute);

class FIrradianceVisualizeCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FIrradianceVisualizeCS)
	SHADER_USE_PARAMETER_STRUCT(FIrradianceVisualizeCS, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
		OutEnvironment.CompilerFlags.Add(CFLAG_ForceDXC);
		OutEnvironment.SetDefine(TEXT("PRE_DISPATCH_SURFEL_ARGS"), 1);
	}

	static uint32 GetThreadBlockSize()
	{
		return 8;
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )

        SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, SurfelGridMetaBuf)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWByteAddressBuffer, SurfelGridMetaBuf2)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, SurfelEntryCellBuf)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, SurfelIrradianceBuf)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, SurfelLifeBuf)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, SurfelPoolBuf)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWByteAddressBuffer, SurfelMetaBuf)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FSurfelVertexPacked>, SurfelVertexBuf)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FSurfelVertexPacked>, SurfelRePositionBuf)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, SurfelRePositionCountBuf)

        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, NormalTexture)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, DepthTexture)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWDebugOutTex)

		SHADER_PARAMETER_SAMPLER(SamplerState, PointClampSampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearClampSampler)
        SHADER_PARAMETER(FVector4f, TexBufferSize)
        SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FIrradianceVisualizeCS, "/Engine/Private/SurfelCache/IrradianceVis.usf", "VisIrradianceCS", SF_Compute);


    BEGIN_SHADER_PARAMETER_STRUCT(FSurfelTraceCommonParamerter, )
	    SHADER_PARAMETER(uint32, SamplesPerPixel)
		SHADER_PARAMETER(uint32, MaxBounces)
		SHADER_PARAMETER(uint32, UpscaleFactor)
		SHADER_PARAMETER(float, MaxRayDistanceForGI)
		SHADER_PARAMETER(float, MaxRayDistanceForAO)
		SHADER_PARAMETER(float, MaxShadowDistance)
		SHADER_PARAMETER(float, NextEventEstimationSamples)
		SHADER_PARAMETER(float, DiffuseThreshold)
		SHADER_PARAMETER(uint32, EvalSkyLight)
		SHADER_PARAMETER(uint32, UseRussianRoulette)
		SHADER_PARAMETER(uint32, UseFireflySuppression)
		SHADER_PARAMETER(float, MaxNormalBias)
		SHADER_PARAMETER(uint32, RenderTileOffsetX)
		SHADER_PARAMETER(uint32, RenderTileOffsetY)
		SHADER_PARAMETER(uint32, AccumulateEmissive)

		SHADER_PARAMETER_SRV(RaytracingAccelerationStructure, TLAS)

		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FPathTracingLight>, SceneLights)
		SHADER_PARAMETER(uint32, SceneLightCount)
		SHADER_PARAMETER_STRUCT_INCLUDE(FPathTracingLightGrid, LightGridParameters)

		SHADER_PARAMETER_STRUCT_INCLUDE(FPathTracingSkylight, SkylightParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SSProfilesTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, TransmissionProfilesLinearSampler)

        // SHADER_PARAMETER_STRUCT_INCLUDE(FSurfelCascades, SurfelGridData)
        SHADER_PARAMETER(FVector4f, SurfelGridCenter)
        SHADER_PARAMETER_ARRAY(FIntVector4, SurfelGridOrigin, [IRCACHE_CASCADE_COUNT])
        SHADER_PARAMETER_ARRAY(FIntVector4, SurfelGridVoxelScrolled, [IRCACHE_CASCADE_COUNT])
    END_SHADER_PARAMETER_STRUCT()

class FIrradianceTraceRGS : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FIrradianceTraceRGS)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FIrradianceTraceRGS, FGlobalShader)

	class FEnableTwoSidedGeometryDim : SHADER_PERMUTATION_BOOL("ENABLE_TWO_SIDED_GEOMETRY");
	class FEnableTransmissionDim : SHADER_PERMUTATION_INT("ENABLE_TRANSMISSION", 2);
	class FUseSurfelDim : SHADER_PERMUTATION_BOOL("USE_SURFEL");
	using FPermutationDomain = TShaderPermutationDomain<FEnableTwoSidedGeometryDim, FEnableTransmissionDim,FUseSurfelDim>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		//OutEnvironment.CompilerFlags.Add(CFLAG_ForceDXC);
		// We need the skylight to do its own form of MIS because RTGI doesn't do its own
		OutEnvironment.SetDefine(TEXT("PATHTRACING_SKY_MIS"), 1);
		//OutEnvironment.SetDefine(TEXT("LIGHT_SAMPLING_TYPE"), 1);
		OutEnvironment.SetDefine(TEXT("SURFEL_TRACE"), 1);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FSurfelTraceCommonParamerter, TraceCommonParameters)
		//surfel gi
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWByteAddressBuffer, SurfelMetaBuf)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWByteAddressBuffer, SurfelGridMetaBuf)

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint32>, SurfelLifeBuf)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint32>, SurfelPoolBuf)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FSurfelVertexPacked>, SurfelRePositionBuf)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint32>, SurfelRePositionCountBuf)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FSurfelVertexPacked>, SurfelVertexBuf)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, SurfelIrradianceBuf)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint> , SurfelEntryCellBuf)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, SurfelAuxiBuf)
        SHADER_PARAMETER_RDG_BUFFER_SRV(RWStructuredBuffer<uint32>, SurfelEntryIndirectionBuf)

	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FIrradianceTraceRGS, "/Engine/Private/SurfelCache/SurfelTrace.usf", "SurfelTraceRGS", SF_RayGen);

 class FIrradianceValidationRGS : public FGlobalShader
 {
     DECLARE_GLOBAL_SHADER(FIrradianceValidationRGS)
 	SHADER_USE_ROOT_PARAMETER_STRUCT(FIrradianceValidationRGS, FGlobalShader)

 	class FEnableTwoSidedGeometryDim : SHADER_PERMUTATION_BOOL("ENABLE_TWO_SIDED_GEOMETRY");
 	class FEnableTransmissionDim : SHADER_PERMUTATION_INT("ENABLE_TRANSMISSION", 2);
 	class FUseSurfelDim : SHADER_PERMUTATION_BOOL("USE_SURFEL");
 	using FPermutationDomain = TShaderPermutationDomain<FEnableTwoSidedGeometryDim, FEnableTransmissionDim,FUseSurfelDim>;

 	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
 	{
 		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
 	}

 	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
 	{
 		//OutEnvironment.CompilerFlags.Add(CFLAG_ForceDXC);
 		// We need the skylight to do its own form of MIS because RTGI doesn't do its own
 		OutEnvironment.SetDefine(TEXT("PATHTRACING_SKY_MIS"), 1);
 		//OutEnvironment.SetDefine(TEXT("LIGHT_SAMPLING_TYPE"), 1);
 		OutEnvironment.SetDefine(TEXT("SURFEL_VALIDATION"), 1);
 	}

 	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
 		SHADER_PARAMETER_STRUCT_INCLUDE(FSurfelTraceCommonParamerter, TraceCommonParameters)
 		//surfel gi
 		SHADER_PARAMETER_RDG_BUFFER_UAV(RWByteAddressBuffer, SurfelMetaBuf)
 		SHADER_PARAMETER_RDG_BUFFER_UAV(RWByteAddressBuffer, SurfelGridMetaBuf)

 		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint32>, SurfelLifeBuf)
 		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint32>, SurfelPoolBuf)
 		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FSurfelVertexPacked>, SurfelRePositionBuf)
 		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint32>, SurfelRePositionCountBuf)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FSurfelVertexPacked>, SurfelVertexBuf)
 		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, SurfelIrradianceBuf)
 		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint> , SurfelEntryCellBuf)
 		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, SurfelAuxiBuf)
         SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint32>, SurfelEntryIndirectionBuf)
 	END_SHADER_PARAMETER_STRUCT()
 };
 IMPLEMENT_GLOBAL_SHADER(FIrradianceValidationRGS, "/Engine/Private/SurfelCache/SurfelTrace.usf", "SurfelValidationRGS", SF_RayGen);

 class FIrradianceTraceAccessibilityRGS : public FGlobalShader
 {
     DECLARE_GLOBAL_SHADER(FIrradianceTraceAccessibilityRGS)
 	SHADER_USE_ROOT_PARAMETER_STRUCT(FIrradianceTraceAccessibilityRGS, FGlobalShader)

 	class FEnableTwoSidedGeometryDim : SHADER_PERMUTATION_BOOL("ENABLE_TWO_SIDED_GEOMETRY");
 	class FEnableTransmissionDim : SHADER_PERMUTATION_INT("ENABLE_TRANSMISSION", 2);
 	class FUseSurfelDim : SHADER_PERMUTATION_BOOL("USE_SURFEL");
 	using FPermutationDomain = TShaderPermutationDomain<FEnableTwoSidedGeometryDim, FEnableTransmissionDim,FUseSurfelDim>;

 	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
 	{
 		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
 	}

 	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
 	{
 		//OutEnvironment.CompilerFlags.Add(CFLAG_ForceDXC);
 		// We need the skylight to do its own form of MIS because RTGI doesn't do its own
 		OutEnvironment.SetDefine(TEXT("PATHTRACING_SKY_MIS"), 1);
 		//OutEnvironment.SetDefine(TEXT("LIGHT_SAMPLING_TYPE"), 1);
 		OutEnvironment.SetDefine(TEXT("SURFEL_ACESSIBILITY"), 1);
 	}

 	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
 		SHADER_PARAMETER_STRUCT_INCLUDE(FSurfelTraceCommonParamerter, TraceCommonParameters)
 		//surfel gi
 		SHADER_PARAMETER_RDG_BUFFER_UAV(RWByteAddressBuffer, SurfelMetaBuf)
 		SHADER_PARAMETER_RDG_BUFFER_UAV(RWByteAddressBuffer, SurfelGridMetaBuf)

 		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint32>, SurfelLifeBuf)
 		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint32>, SurfelPoolBuf)
 		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FSurfelVertexPacked>, SurfelRePositionBuf)
 		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint32>, SurfelRePositionCountBuf)
 		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FSurfelVertexPacked>, SurfelVertexBuf)
 		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, SurfelIrradianceBuf)
 		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint> , SurfelEntryCellBuf)
 		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, SurfelAuxiBuf)
         SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint32>, SurfelEntryIndirectionBuf)
 	END_SHADER_PARAMETER_STRUCT()
 };
 IMPLEMENT_GLOBAL_SHADER(FIrradianceTraceAccessibilityRGS, "/Engine/Private/SurfelCache/SurfelTrace.usf", "AccessiblityRGS", SF_RayGen);


extern void InclusivePrefixScan(FRDGBuilder& GraphBuilder, FRDGBufferRef& InputBuf);

void FDeferredShadingSceneRenderer::PrepareFusionSurfelIrradiance(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	int EnableTransmission = CVarRayTracingGlobalIlluminationEnableTransmission.GetValueOnRenderThread();
	//const bool bEnableAdaptive = CVarGlobalIlluminationAdaptiveSamplingEnable.GetValueOnRenderThread();
	// Declare all RayGen shaders that require material closest hit shaders to be bound
	for (int EnableTwoSidedGeometry = 0; EnableTwoSidedGeometry < 2; ++EnableTwoSidedGeometry)
	{
		//for (uint32 i = 0; i < (uint32)(ELightSamplingType::MAX); i++)
		for(int EnableSurfel = 0; EnableSurfel < 2; ++EnableSurfel)
		{
            {
                FIrradianceTraceRGS::FPermutationDomain PermutationVector;
                PermutationVector.Set<FIrradianceTraceRGS::FEnableTwoSidedGeometryDim>(EnableTwoSidedGeometry == 1);
                PermutationVector.Set<FIrradianceTraceRGS::FEnableTransmissionDim>(EnableTransmission);
                PermutationVector.Set<FIrradianceTraceRGS::FUseSurfelDim>(EnableSurfel == 1);
                TShaderMapRef<FIrradianceTraceRGS> RayGenerationShader(View.ShaderMap, PermutationVector);
                OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
            }
            
            {
                FIrradianceValidationRGS::FPermutationDomain PermutationVector;
                PermutationVector.Set<FIrradianceValidationRGS::FEnableTwoSidedGeometryDim>(EnableTwoSidedGeometry == 1);
                PermutationVector.Set<FIrradianceValidationRGS::FEnableTransmissionDim>(EnableTransmission);
                PermutationVector.Set<FIrradianceValidationRGS::FUseSurfelDim>(EnableSurfel == 1);
                TShaderMapRef<FIrradianceValidationRGS> RayGenerationShader(View.ShaderMap, PermutationVector);
                OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
            }
            
            {
                FIrradianceTraceAccessibilityRGS::FPermutationDomain PermutationVector;
                PermutationVector.Set<FIrradianceTraceAccessibilityRGS::FEnableTwoSidedGeometryDim>(EnableTwoSidedGeometry == 1);
                PermutationVector.Set<FIrradianceTraceAccessibilityRGS::FEnableTransmissionDim>(EnableTransmission);
                PermutationVector.Set<FIrradianceTraceAccessibilityRGS::FUseSurfelDim>(EnableSurfel == 1);
                TShaderMapRef<FIrradianceTraceAccessibilityRGS> RayGenerationShader(View.ShaderMap, PermutationVector);
                OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
            }
		}
	}
}

void FDeferredShadingSceneRenderer::RenderFusionIrradianceCache(FRDGBuilder& GraphBuilder,
    FSceneTextureParameters& SceneTextures,
    FViewInfo& View,
    const IScreenSpaceDenoiser::FAmbientOcclusionRayTracingConfig& RayTracingConfig,
    int32 UpscaleFactor,
    IScreenSpaceDenoiser::FDiffuseIndirectInputs* OutDenoiserInputs,
    FSurfelBufResources& SurfelRes)
{
    //Update Scroll
    
	auto EyePos  = FVector3f(View.ViewMatrices.GetViewOrigin());
	Scene->SurfelGridCenter = EyePos;

    for(int32 cascade = 0; cascade < IRCACHE_CASCADE_COUNT; cascade++)
    {
        float cell_diameter = IRCACHE_GRID_CELL_DIAMETER * (1 << cascade);
        FIntVector3 cascade_center = FIntVector3(FMath::FloorToInt( EyePos.X / cell_diameter), FMath::FloorToInt(EyePos.X / cell_diameter), FMath::FloorToInt(EyePos.X / cell_diameter));
		FIntVector3 cascade_origin = cascade_center - FIntVector3(IRCACHE_CASCADE_SIZE / 2);

		Scene->SurfelPrevScroll[cascade] = Scene->SurfelCurScroll[cascade];
		Scene->SurfelCurScroll[cascade] = cascade_origin;
    }
    FVector4f SurfelGridCenter = FVector4f(EyePos,0.0);
    FIntVector4 SurfelGridOrigin[IRCACHE_CASCADE_COUNT];
    FIntVector4 SurfelGridVoxelScrolled[IRCACHE_CASCADE_COUNT];
	for (int32 cascade = 0; cascade < IRCACHE_CASCADE_COUNT; cascade++)
	{
		auto cur_scroll = Scene->SurfelCurScroll[cascade];
		auto prev_scroll = Scene->SurfelPrevScroll[cascade];
		auto scroll_amount = cur_scroll - prev_scroll;


		// SurfelGridData.SurfelCascades[cascade].Origin = FIntVector4(cur_scroll, 0);
		// SurfelGridData.SurfelCascades[cascade].VoxelScrolled = FIntVector4(scroll_amount, 0);
        SurfelGridOrigin[cascade] = FIntVector4(cur_scroll, 0);
		SurfelGridVoxelScrolled[cascade] = FIntVector4(scroll_amount, 0);
	}
    auto Size = View.ViewRect.Size();

	FRDGTextureDesc DebugDesc = FRDGTextureDesc::Create2D(
		Size,
		PF_A32B32G32R32F,
		FClearValueBinding::None,
		TexCreate_ShaderResource | TexCreate_UAV);

	auto DebugTex = GraphBuilder.CreateTexture(DebugDesc, TEXT("SurfelDebugTex"));
	FRDGBufferRef SurfelMetaBuf = nullptr;
	FRDGBufferRef SurfelGridMetaBuf = nullptr, SurfelGridMetaBuf2 = nullptr;
	FRDGBufferRef SurfelPoolBuf = nullptr;
	FRDGBufferRef SurfelLifeBuf = nullptr;
	FRDGBufferRef SurfelEntryCellBuf = nullptr;
	FRDGBufferRef SurfelVertexBuf = nullptr;
	FRDGBufferRef SurfelIrradianceBuf = nullptr;
	FRDGBufferRef SurfelRePositionBuf = nullptr;
	FRDGBufferRef SurfelRePositionCountBuf = nullptr;
    FRDGBufferRef SurfelIndirectionBuf = nullptr;
	FRDGBufferRef SurfelAuxiBuf = nullptr;
    if (View.ViewState->SurfelMetaBuf)
	{
		SurfelMetaBuf = GraphBuilder.RegisterExternalBuffer(View.ViewState->SurfelMetaBuf, TEXT("SurfelMetaBuf"));
		SurfelGridMetaBuf = GraphBuilder.RegisterExternalBuffer(View.ViewState->SurfelGridMetaBuf, TEXT("SurfelGridMetaBuf"));
		SurfelPoolBuf = GraphBuilder.RegisterExternalBuffer(View.ViewState->SurfelPoolBuf, TEXT("SurfelPoolBuf"));
		SurfelLifeBuf = GraphBuilder.RegisterExternalBuffer(View.ViewState->SurfelLifeBuf, TEXT("SurfelLifeBuf"));
		SurfelEntryCellBuf = GraphBuilder.RegisterExternalBuffer(View.ViewState->SurfelEntryCellBuf, TEXT("SurfelEntryCellBuf"));
		SurfelVertexBuf = GraphBuilder.RegisterExternalBuffer(View.ViewState->SurfelVertexBuf, TEXT("SurfelVertexBuf"));
		SurfelIrradianceBuf = GraphBuilder.RegisterExternalBuffer(View.ViewState->SurfelIrradianceBuf, TEXT("SurfelIrradianceBuf"));
		SurfelRePositionBuf = GraphBuilder.RegisterExternalBuffer(View.ViewState->SurfelRePositionBuf, TEXT("SurfelRePositionBuf"));
		SurfelRePositionCountBuf = GraphBuilder.RegisterExternalBuffer(View.ViewState->SurfelRePositionCountBuf, TEXT("SurfelRePositionCountBuf"));
		SurfelAuxiBuf = GraphBuilder.RegisterExternalBuffer(View.ViewState->SurfelAuxiBuf, TEXT("SurfelAuxiBuf"));
        SurfelIndirectionBuf = GraphBuilder.RegisterExternalBuffer(View.ViewState->SurfelIndirectionBuf, TEXT("SurfelIndirectionBuf"));
		SurfelGridMetaBuf2 = GraphBuilder.RegisterExternalBuffer(View.ViewState->SurfelGridMetaBuf2, TEXT("SurfelGridMetaBuf2"));
    }
	else
	{
		SurfelMetaBuf = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateByteAddressDesc(sizeof(uint32) * 8), TEXT("SurfelMetaBuf"), ERDGBufferFlags::MultiFrame);
		SurfelGridMetaBuf = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateByteAddressDesc(sizeof(uint32) * 2 * MAX_GRID_CELLS), TEXT("SurfelGridMetaBuf1"), ERDGBufferFlags::MultiFrame);
        SurfelGridMetaBuf2 = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateByteAddressDesc(sizeof(uint32) * 2 * MAX_GRID_CELLS), TEXT("SurfelGridMetaBuf2"), ERDGBufferFlags::MultiFrame);
		SurfelPoolBuf = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateByteAddressDesc(sizeof(uint32) * MAX_ENTRIES), TEXT("SurfelPoolBuf"), ERDGBufferFlags::MultiFrame);
		SurfelLifeBuf = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateByteAddressDesc(sizeof(uint32) * MAX_ENTRIES), TEXT("SurfelLifeBuf"), ERDGBufferFlags::MultiFrame);
		SurfelEntryCellBuf = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateByteAddressDesc(sizeof(uint32) * MAX_ENTRIES), TEXT("SurfelEntryCellBuf"), ERDGBufferFlags::MultiFrame);
		SurfelVertexBuf = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FSurfelVertexPacked), MAX_ENTRIES), TEXT("SurfelVertexBuf"), ERDGBufferFlags::MultiFrame);

		SurfelIrradianceBuf = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f), 3 * MAX_ENTRIES), TEXT("SurfelIrradianceBuf"), ERDGBufferFlags::MultiFrame);
		SurfelRePositionBuf = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FSurfelVertexPacked), MAX_ENTRIES), TEXT("SurfelRePositionBuf"), ERDGBufferFlags::MultiFrame);
		SurfelRePositionCountBuf = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32) , MAX_ENTRIES), TEXT("SurfelRePositionCountBuf"), ERDGBufferFlags::MultiFrame);
		SurfelAuxiBuf = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f)  , 4 * 16 * MAX_ENTRIES), TEXT("SurfelAuxiBuf"), ERDGBufferFlags::MultiFrame);

        SurfelIndirectionBuf = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32)  , INDIRECTION_BUF_ELEM_COUNT), TEXT("SurfelIndirectionBuf"), ERDGBufferFlags::MultiFrame);

		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(SurfelMetaBuf), 0);
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(SurfelGridMetaBuf), 0);
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(SurfelPoolBuf), 0);
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(SurfelLifeBuf), 0);
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(SurfelEntryCellBuf), 0);
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(SurfelRePositionBuf), 0);
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(SurfelRePositionCountBuf), 0);
        AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(SurfelIndirectionBuf), 0);
        AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(SurfelGridMetaBuf2), 0);

		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(SurfelAuxiBuf), 0);
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(SurfelIrradianceBuf), 0);
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(SurfelVertexBuf), 0);
    }
    if( Scene->SurfelParity == 1)
    {
        std::swap(SurfelGridMetaBuf, SurfelGridMetaBuf2);
    }
    if ( !Scene->SurfelInitialized ) 
    {

        TShaderMapRef<FClearEntriesPoolCS> ComputeShader(GetGlobalShaderMap(ERHIFeatureLevel::SM5));
        FClearEntriesPoolCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FClearEntriesPoolCS::FParameters>();
        
        PassParameters->SurfelPoolBuf = GraphBuilder.CreateUAV(SurfelPoolBuf);
        PassParameters->SurfelLifeBuf = GraphBuilder.CreateUAV(SurfelLifeBuf);

        ClearUnusedGraphResources(ComputeShader, PassParameters);
        FComputeShaderUtils::AddPass(
            GraphBuilder,
            RDG_EVENT_NAME("ClearSurfel"),
            ComputeShader,
            PassParameters,
            FComputeShaderUtils::GetGroupCount(MAX_ENTRIES, FClearEntriesPoolCS::GetThreadBlockSize()));
        Scene->SurfelInitialized = true;
    }
    else
    {
        TShaderMapRef<FScollCascadeCS> ComputeShader(GetGlobalShaderMap(ERHIFeatureLevel::SM5));
        FScollCascadeCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FScollCascadeCS::FParameters>();
        PassParameters->SurfelGridMetaBuf = GraphBuilder.CreateSRV(SurfelGridMetaBuf);
        PassParameters->SurfelGridMetaBuf2 = GraphBuilder.CreateUAV(SurfelGridMetaBuf2);
        PassParameters->SurfelPoolBuf = GraphBuilder.CreateUAV(SurfelPoolBuf);
        PassParameters->SurfelLifeBuf = GraphBuilder.CreateUAV(SurfelLifeBuf);
        PassParameters->SurfelEntryCellBuf = GraphBuilder.CreateUAV(SurfelEntryCellBuf);
        PassParameters->SurfelIrradianceBuf = GraphBuilder.CreateUAV(SurfelIrradianceBuf);
        PassParameters->SurfelMetaBuf = GraphBuilder.CreateUAV(SurfelMetaBuf);
        // PassParameters->SurfelGridData = SurfelGridData;
        PassParameters->SurfelGridCenter = SurfelGridCenter;
        for(int32 i = 0 ;i < IRCACHE_CASCADE_COUNT;i++)
        {
            PassParameters->SurfelGridOrigin[i] = SurfelGridOrigin[i];
            PassParameters->SurfelGridVoxelScrolled[i] = SurfelGridVoxelScrolled[i];
        }
        ClearUnusedGraphResources(ComputeShader, PassParameters);
        FComputeShaderUtils::AddPass(
            GraphBuilder,
            RDG_EVENT_NAME("ScollCascade"),
            ComputeShader,
            PassParameters,
           FIntVector(IRCACHE_CASCADE_SIZE, IRCACHE_CASCADE_SIZE, IRCACHE_CASCADE_SIZE * IRCACHE_CASCADE_COUNT) );

        std::swap(SurfelGridMetaBuf, SurfelGridMetaBuf2);
        Scene->SurfelParity = (Scene->SurfelParity + 1) % 2;
    }

    auto DispatchIndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(2), TEXT("SurfelIndirectArgs"));
    {
        TShaderMapRef<FPrepareAgeArgsCS> ComputeShader(GetGlobalShaderMap(ERHIFeatureLevel::SM5));
        FPrepareAgeArgsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FPrepareAgeArgsCS::FParameters>();
        PassParameters->RWDispatchArgs = GraphBuilder.CreateUAV(DispatchIndirectArgs, EPixelFormat::PF_R8_UINT);
        PassParameters->SurfelMetaBuf = GraphBuilder.CreateSRV(SurfelMetaBuf);

        ClearUnusedGraphResources(ComputeShader, PassParameters);
        FComputeShaderUtils::AddPass(
            GraphBuilder,
            RDG_EVENT_NAME("DispatchSurfelArgsCS"),
            ComputeShader,
            PassParameters,
            FIntVector(1, 1, 1));
    }

    auto EntryOccupancyBuf = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), MAX_ENTRIES), TEXT("EntryOccupancyBuf"));
    {
        TShaderMapRef<FAgeEntriesCS> ComputeShader(GetGlobalShaderMap(ERHIFeatureLevel::SM5));
        FAgeEntriesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FAgeEntriesCS::FParameters>();
        PassParameters->IndirectDispatchArgs = DispatchIndirectArgs;

        PassParameters->SurfelMetaBuf = GraphBuilder.CreateUAV(SurfelMetaBuf, EPixelFormat::PF_R8_UINT);
        PassParameters->SurfelGridMetaBuf = GraphBuilder.CreateUAV(SurfelGridMetaBuf, EPixelFormat::PF_R8_UINT);

        PassParameters->SurfelPoolBuf = GraphBuilder.CreateUAV(SurfelPoolBuf);
        PassParameters->SurfelLifeBuf = GraphBuilder.CreateUAV(SurfelLifeBuf);
        PassParameters->SurfelEntryCellBuf = GraphBuilder.CreateUAV(SurfelEntryCellBuf);
        PassParameters->SurfelVertexBuf = GraphBuilder.CreateUAV(SurfelVertexBuf);
        PassParameters->SurfelIrradianceBuf = GraphBuilder.CreateUAV(SurfelIrradianceBuf);
        PassParameters->SurfelRePositionBuf = GraphBuilder.CreateUAV(SurfelRePositionBuf);
        PassParameters->SurfelRePositionCountBuf = GraphBuilder.CreateUAV(SurfelRePositionCountBuf);
        PassParameters->SurfelEntryOccupancyBuf = GraphBuilder.CreateUAV(EntryOccupancyBuf);
        PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;

        ClearUnusedGraphResources(ComputeShader, PassParameters);
        FComputeShaderUtils::AddPass(
            GraphBuilder,
            RDG_EVENT_NAME("AgeSurfelCS"),
            ComputeShader,
            PassParameters,
            DispatchIndirectArgs,
            0);
    }
    InclusivePrefixScan(GraphBuilder, EntryOccupancyBuf);
    {
        TShaderMapRef<FCompactEntriesCS> ComputeShader(GetGlobalShaderMap(ERHIFeatureLevel::SM5));
        FCompactEntriesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FCompactEntriesCS::FParameters>();
        PassParameters->IndirectDispatchArgs = DispatchIndirectArgs;

        PassParameters->SurfelMetaBuf = GraphBuilder.CreateUAV(SurfelMetaBuf, EPixelFormat::PF_R8_UINT);
        PassParameters->SurfelLifeBuf = GraphBuilder.CreateUAV(SurfelLifeBuf);
        PassParameters->SurfelEntryIndirectionBuf = GraphBuilder.CreateUAV(SurfelIndirectionBuf);
        PassParameters->SurfelEntryOccupancyBuf = GraphBuilder.CreateSRV(EntryOccupancyBuf);
       
        PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;

        ClearUnusedGraphResources(ComputeShader, PassParameters);
        FComputeShaderUtils::AddPass(
            GraphBuilder,
            RDG_EVENT_NAME("CompactEntriesCS"),
            ComputeShader,
            PassParameters,
            DispatchIndirectArgs,
            0);
    }

    //Trace 
    {
        auto IndirectArgsBuf = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(4), TEXT("SurfelTraceIndirectArgs"));
        {
            TShaderMapRef<FPrepareTraceArgsCS> ComputeShader(GetGlobalShaderMap(ERHIFeatureLevel::SM5));
            FPrepareTraceArgsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FPrepareTraceArgsCS::FParameters>();
            PassParameters->RWDispatchArgs = GraphBuilder.CreateUAV(IndirectArgsBuf, EPixelFormat::PF_R8_UINT);
            PassParameters->SurfelMetaBuf = GraphBuilder.CreateSRV(SurfelMetaBuf);

            ClearUnusedGraphResources(ComputeShader, PassParameters);
            FComputeShaderUtils::AddPass(
                GraphBuilder,
                RDG_EVENT_NAME("DispatchSurfelTraceArgsCS"),
                ComputeShader,
                PassParameters,
                FIntVector(1, 1, 1));
        }
        {
            TShaderMapRef<FResetEntriesCS> ComputeShader(GetGlobalShaderMap(ERHIFeatureLevel::SM5));
            FResetEntriesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FResetEntriesCS::FParameters>();
            PassParameters->IndirectDispatchArgs = IndirectArgsBuf;

            PassParameters->SurfelMetaBuf = GraphBuilder.CreateSRV(SurfelMetaBuf, EPixelFormat::PF_R8_UINT);
            PassParameters->SurfelLifeBuf = GraphBuilder.CreateSRV(SurfelLifeBuf);
            PassParameters->SurfelIrradianceBuf = GraphBuilder.CreateSRV(SurfelIrradianceBuf);
            PassParameters->SurfelAuxiBuf = GraphBuilder.CreateUAV(SurfelAuxiBuf);
            PassParameters->SurfelEntryIndirectionBuf = GraphBuilder.CreateSRV(SurfelIndirectionBuf);
           // PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;

            ClearUnusedGraphResources(ComputeShader, PassParameters);
            FComputeShaderUtils::AddPass(
                GraphBuilder,
                RDG_EVENT_NAME("ResetEntries"),
                ComputeShader,
                PassParameters,
                IndirectArgsBuf,
                12 * 2);
        }

        {
            int32 RayTracingGISamplesPerPixel = 1;
            if (RayTracingGISamplesPerPixel <= 0) return ;
            float MaxShadowDistance = 1.0e27;
            if (GRayTracingGlobalIlluminationMaxShadowDistance > 0.0)
            {
                MaxShadowDistance = GRayTracingGlobalIlluminationMaxShadowDistance;
            }
            else if (Scene->SkyLight)
            {
                // Adjust ray TMax so shadow rays do not hit the sky sphere 
                MaxShadowDistance = FMath::Max(0.0, 0.99 * Scene->SkyLight->SkyDistanceThreshold);
            }
            FSurfelTraceCommonParamerter    TraceCommonParameter;
            TraceCommonParameter.SamplesPerPixel = RayTracingGISamplesPerPixel;
            int32 CVarRayTracingGlobalIlluminationMaxBouncesValue = CVarRayTracingGlobalIlluminationMaxBounces.GetValueOnRenderThread();
            TraceCommonParameter.MaxBounces = CVarRayTracingGlobalIlluminationMaxBouncesValue > -1 ? CVarRayTracingGlobalIlluminationMaxBouncesValue : View.FinalPostProcessSettings.RayTracingGIMaxBounces;
            TraceCommonParameter.MaxNormalBias = GetRaytracingMaxNormalBias();
            float MaxRayDistanceForGI = GRayTracingGlobalIlluminationMaxRayDistance;
            if (MaxRayDistanceForGI == -1.0)
            {
                MaxRayDistanceForGI = View.FinalPostProcessSettings.AmbientOcclusionRadius;
            }
            TraceCommonParameter.MaxRayDistanceForGI = MaxRayDistanceForGI;
            TraceCommonParameter.MaxRayDistanceForAO = View.FinalPostProcessSettings.AmbientOcclusionRadius;
            TraceCommonParameter.MaxShadowDistance = MaxShadowDistance;
            TraceCommonParameter.UpscaleFactor = UpscaleFactor;
            TraceCommonParameter.EvalSkyLight = GRayTracingGlobalIlluminationEvalSkyLight != 0;
            TraceCommonParameter.UseRussianRoulette = GRayTracingGlobalIlluminationUseRussianRoulette != 0;
            TraceCommonParameter.UseFireflySuppression = CVarRayTracingGlobalIlluminationFireflySuppression.GetValueOnRenderThread() != 0;
            TraceCommonParameter.DiffuseThreshold = GRayTracingGlobalIlluminationDiffuseThreshold;
            TraceCommonParameter.NextEventEstimationSamples = GRayTracingGlobalIlluminationNextEventEstimationSamples;
            TraceCommonParameter.TLAS = View.GetRayTracingSceneViewChecked();
            TraceCommonParameter.ViewUniformBuffer = View.ViewUniformBuffer;
            SetupLightParameters(Scene, View, GraphBuilder, &TraceCommonParameter.SceneLights, &TraceCommonParameter.SceneLightCount, &TraceCommonParameter.SkylightParameters);
            TraceCommonParameter.SceneTextures = SceneTextures;
            TraceCommonParameter.AccumulateEmissive = FMath::Clamp(CVarFusionSurfelAccumulateEmissive.GetValueOnRenderThread(), 0, 1);
            // TODO: should be converted to RDG
            TraceCommonParameter.RenderTileOffsetX = 0;
            TraceCommonParameter.RenderTileOffsetY = 0;
            // TraceCommonParameter.SurfelGridData =  SurfelGridData;
            TraceCommonParameter.SurfelGridCenter = SurfelGridCenter;
            for(int32 i = 0 ;i < IRCACHE_CASCADE_COUNT;i++)
            {
                TraceCommonParameter.SurfelGridOrigin[i] = SurfelGridOrigin[i];
                TraceCommonParameter.SurfelGridVoxelScrolled[i] = SurfelGridVoxelScrolled[i];
            }
            //AcessBility
            FIntPoint RayTracingResolution = FIntPoint(MAX_ENTRIES, 1);
            {
                FIrradianceTraceAccessibilityRGS::FParameters* PassParameters = GraphBuilder.AllocParameters<FIrradianceTraceAccessibilityRGS::FParameters>();
                PassParameters->TraceCommonParameters = TraceCommonParameter;
                PassParameters->SurfelMetaBuf = GraphBuilder.CreateUAV(SurfelMetaBuf, EPixelFormat::PF_R8_UINT);
                PassParameters->SurfelGridMetaBuf = GraphBuilder.CreateUAV(SurfelGridMetaBuf, EPixelFormat::PF_R8_UINT);

                PassParameters->SurfelPoolBuf = GraphBuilder.CreateUAV(SurfelPoolBuf);
                PassParameters->SurfelLifeBuf = GraphBuilder.CreateUAV(SurfelLifeBuf);
                PassParameters->SurfelEntryCellBuf = GraphBuilder.CreateUAV(SurfelEntryCellBuf);
                PassParameters->SurfelVertexBuf = GraphBuilder.CreateSRV(SurfelVertexBuf);
                PassParameters->SurfelIrradianceBuf = GraphBuilder.CreateUAV(SurfelIrradianceBuf);
                PassParameters->SurfelRePositionBuf = GraphBuilder.CreateUAV(SurfelRePositionBuf);
                PassParameters->SurfelRePositionCountBuf = GraphBuilder.CreateUAV(SurfelRePositionCountBuf);
                PassParameters->SurfelAuxiBuf =  GraphBuilder.CreateUAV(SurfelAuxiBuf);
				PassParameters->SurfelEntryIndirectionBuf = GraphBuilder.CreateSRV(SurfelIndirectionBuf);
                
                FIrradianceTraceAccessibilityRGS::FPermutationDomain PermutationVector;
                PermutationVector.Set<FIrradianceTraceAccessibilityRGS::FEnableTwoSidedGeometryDim>(CVarRayTracingGlobalIlluminationEnableTwoSidedGeometry.GetValueOnRenderThread() != 0);
                PermutationVector.Set<FIrradianceTraceAccessibilityRGS::FEnableTransmissionDim>(CVarRayTracingGlobalIlluminationEnableTransmission.GetValueOnRenderThread());
                PermutationVector.Set<FIrradianceTraceAccessibilityRGS::FUseSurfelDim>(CVarSurfelGIUseSurfel.GetValueOnRenderThread() != 0);
                TShaderMapRef<FIrradianceTraceAccessibilityRGS> RayGenerationShader(GetGlobalShaderMap(FeatureLevel), PermutationVector);
                ClearUnusedGraphResources(RayGenerationShader, PassParameters);

                GraphBuilder.AddPass(
                    RDG_EVENT_NAME("SurfelAcessbility %dx%d", RayTracingResolution.X, RayTracingResolution.Y),
                    PassParameters,
                    ERDGPassFlags::Compute,
                    [PassParameters, this, &View, RayGenerationShader, RayTracingResolution, IndirectArgsBuf](FRHIRayTracingCommandList& RHICmdList)
                    {
						IndirectArgsBuf->MarkResourceAsUsed();
                        FRHIRayTracingScene* RayTracingSceneRHI = View.GetRayTracingSceneChecked();

                        FRayTracingShaderBindingsWriter GlobalResources;
                        SetShaderParameters(GlobalResources, RayGenerationShader, *PassParameters);
                        RHICmdList.RayTraceDispatchIndirect(View.RayTracingMaterialPipeline, RayGenerationShader.GetRayTracingShader(), RayTracingSceneRHI, GlobalResources, IndirectArgsBuf->GetIndirectRHICallBuffer(), 12 * 1);
                    });
            }
            //Validation
            {
                FIrradianceValidationRGS::FParameters* PassParameters = GraphBuilder.AllocParameters<FIrradianceValidationRGS::FParameters>();
                PassParameters->TraceCommonParameters = TraceCommonParameter;

                PassParameters->SurfelMetaBuf = GraphBuilder.CreateUAV(SurfelMetaBuf, EPixelFormat::PF_R8_UINT);
                PassParameters->SurfelGridMetaBuf = GraphBuilder.CreateUAV(SurfelGridMetaBuf, EPixelFormat::PF_R8_UINT);

                PassParameters->SurfelPoolBuf = GraphBuilder.CreateUAV(SurfelPoolBuf);
                PassParameters->SurfelLifeBuf = GraphBuilder.CreateUAV(SurfelLifeBuf);
                PassParameters->SurfelEntryCellBuf = GraphBuilder.CreateUAV(SurfelEntryCellBuf);
                PassParameters->SurfelVertexBuf = GraphBuilder.CreateSRV(SurfelVertexBuf);
                PassParameters->SurfelIrradianceBuf = GraphBuilder.CreateUAV(SurfelIrradianceBuf);
                PassParameters->SurfelRePositionBuf = GraphBuilder.CreateUAV(SurfelRePositionBuf);
                PassParameters->SurfelRePositionCountBuf = GraphBuilder.CreateUAV(SurfelRePositionCountBuf);
                PassParameters->SurfelAuxiBuf =  GraphBuilder.CreateUAV(SurfelAuxiBuf);
				PassParameters->SurfelEntryIndirectionBuf = GraphBuilder.CreateSRV(SurfelIndirectionBuf);
                
                FIrradianceValidationRGS::FPermutationDomain PermutationVector;
                PermutationVector.Set<FIrradianceValidationRGS::FEnableTwoSidedGeometryDim>(CVarRayTracingGlobalIlluminationEnableTwoSidedGeometry.GetValueOnRenderThread() != 0);
                PermutationVector.Set<FIrradianceValidationRGS::FEnableTransmissionDim>(CVarRayTracingGlobalIlluminationEnableTransmission.GetValueOnRenderThread());
                PermutationVector.Set<FIrradianceValidationRGS::FUseSurfelDim>(CVarSurfelGIUseSurfel.GetValueOnRenderThread() != 0);
                TShaderMapRef<FIrradianceValidationRGS> RayGenerationShader(GetGlobalShaderMap(FeatureLevel), PermutationVector);
                ClearUnusedGraphResources(RayGenerationShader, PassParameters);

                GraphBuilder.AddPass(
                    RDG_EVENT_NAME("SurfelAcessbility %dx%d", RayTracingResolution.X, RayTracingResolution.Y),
                    PassParameters,
                    ERDGPassFlags::Compute,
                    [PassParameters, this, &View, RayGenerationShader, RayTracingResolution](FRHIRayTracingCommandList& RHICmdList)
                    {
                        FRHIRayTracingScene* RayTracingSceneRHI = View.GetRayTracingSceneChecked();

                        FRayTracingShaderBindingsWriter GlobalResources;
                        SetShaderParameters(GlobalResources, RayGenerationShader, *PassParameters);
                        RHICmdList.RayTraceDispatch(View.RayTracingMaterialPipeline, RayGenerationShader.GetRayTracingShader(), RayTracingSceneRHI, GlobalResources, RayTracingResolution.X, RayTracingResolution.Y);
                    });
            }
            //Trace
            {
    
                FIrradianceTraceRGS::FParameters* PassParameters = GraphBuilder.AllocParameters<FIrradianceTraceRGS::FParameters>();
                //surfel
                PassParameters->TraceCommonParameters = TraceCommonParameter;
                
                PassParameters->SurfelMetaBuf = GraphBuilder.CreateUAV(SurfelMetaBuf, EPixelFormat::PF_R8_UINT);
                PassParameters->SurfelGridMetaBuf = GraphBuilder.CreateUAV(SurfelGridMetaBuf, EPixelFormat::PF_R8_UINT);

                PassParameters->SurfelPoolBuf = GraphBuilder.CreateUAV(SurfelPoolBuf);
                PassParameters->SurfelLifeBuf = GraphBuilder.CreateUAV(SurfelLifeBuf);
                PassParameters->SurfelEntryCellBuf = GraphBuilder.CreateUAV(SurfelEntryCellBuf);
                PassParameters->SurfelVertexBuf = GraphBuilder.CreateSRV(SurfelVertexBuf);
                PassParameters->SurfelIrradianceBuf = GraphBuilder.CreateUAV(SurfelIrradianceBuf);
                PassParameters->SurfelRePositionBuf = GraphBuilder.CreateUAV(SurfelRePositionBuf);
                PassParameters->SurfelRePositionCountBuf = GraphBuilder.CreateUAV(SurfelRePositionCountBuf);
                PassParameters->SurfelAuxiBuf =  GraphBuilder.CreateUAV(SurfelAuxiBuf);
				PassParameters->SurfelEntryIndirectionBuf = GraphBuilder.CreateSRV(SurfelIndirectionBuf);

                FIrradianceTraceRGS::FPermutationDomain PermutationVector;
                PermutationVector.Set<FIrradianceTraceRGS::FEnableTwoSidedGeometryDim>(CVarRayTracingGlobalIlluminationEnableTwoSidedGeometry.GetValueOnRenderThread() != 0);
                PermutationVector.Set<FIrradianceTraceRGS::FEnableTransmissionDim>(CVarRayTracingGlobalIlluminationEnableTransmission.GetValueOnRenderThread());
                PermutationVector.Set<FIrradianceTraceRGS::FUseSurfelDim>(CVarSurfelGIUseSurfel.GetValueOnRenderThread() != 0);
                TShaderMapRef<FIrradianceTraceRGS> RayGenerationShader(GetGlobalShaderMap(FeatureLevel), PermutationVector);
                ClearUnusedGraphResources(RayGenerationShader, PassParameters);
      
                GraphBuilder.AddPass(
                    RDG_EVENT_NAME("SurfelTrace %dx%d", RayTracingResolution.X, RayTracingResolution.Y),
                    PassParameters,
                    ERDGPassFlags::Compute,
                    [PassParameters, this, &View, RayGenerationShader, RayTracingResolution](FRHIRayTracingCommandList& RHICmdList)
                    {
                        FRHIRayTracingScene* RayTracingSceneRHI = View.GetRayTracingSceneChecked();

                        FRayTracingShaderBindingsWriter GlobalResources;
                        SetShaderParameters(GlobalResources, RayGenerationShader, *PassParameters);
                        RHICmdList.RayTraceDispatch(View.RayTracingMaterialPipeline, RayGenerationShader.GetRayTracingShader(), RayTracingSceneRHI, GlobalResources, RayTracingResolution.X, RayTracingResolution.Y);
                    });

            }
        }
		{
			TShaderMapRef<FSumIrradianceCS> ComputeShader(GetGlobalShaderMap(ERHIFeatureLevel::SM5));
			FSumIrradianceCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSumIrradianceCS::FParameters>();
			PassParameters->IndirectDispatchArgs = IndirectArgsBuf;

			PassParameters->SurfelMetaBuf = GraphBuilder.CreateUAV(SurfelMetaBuf, EPixelFormat::PF_R8_UINT);
			PassParameters->SurfelLifeBuf = GraphBuilder.CreateSRV(SurfelLifeBuf);
			PassParameters->SurfelEntryIndirectionBuf = GraphBuilder.CreateSRV(SurfelIndirectionBuf);
			PassParameters->SurfelIrradianceBuf = GraphBuilder.CreateUAV(SurfelIrradianceBuf);
			PassParameters->SurfelAuxiBuf = GraphBuilder.CreateUAV(SurfelAuxiBuf);
			PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;

			ClearUnusedGraphResources(ComputeShader, PassParameters);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("SumIrradiance"),
				ComputeShader,
				PassParameters,
				IndirectArgsBuf,
				12 * 2);
		}
    }


	//vis
	{
		FRDGTextureRef GBufferATexture = SceneTextures.GBufferATexture;
		FRDGTextureRef GBufferBTexture = SceneTextures.GBufferBTexture;
		FRDGTextureRef GBufferCTexture = SceneTextures.GBufferCTexture;
		FRDGTextureRef SceneDepthTexture = SceneTextures.SceneDepthTexture;
		FRDGTextureRef SceneVelocityTexture = SceneTextures.GBufferVelocityTexture;

		FIntPoint TexSize = SceneTextures.SceneDepthTexture->Desc.Extent;
		FVector4f BufferTexSize = FVector4f(TexSize.X, TexSize.Y, 1.0 / TexSize.X, 1.0 / TexSize.Y);

		TShaderMapRef<FIrradianceVisualizeCS> ComputeShader(GetGlobalShaderMap(ERHIFeatureLevel::SM5));
		FIrradianceVisualizeCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FIrradianceVisualizeCS::FParameters>();

		PassParameters->SurfelGridMetaBuf = GraphBuilder.CreateSRV(SurfelGridMetaBuf, EPixelFormat::PF_R8_UINT);
		PassParameters->SurfelGridMetaBuf2 = GraphBuilder.CreateUAV(SurfelGridMetaBuf2, EPixelFormat::PF_R8_UINT);
		PassParameters->SurfelEntryCellBuf = GraphBuilder.CreateUAV(SurfelEntryCellBuf);
		PassParameters->SurfelIrradianceBuf = GraphBuilder.CreateUAV(SurfelIrradianceBuf);

		PassParameters->SurfelLifeBuf = GraphBuilder.CreateUAV(SurfelLifeBuf);
		PassParameters->SurfelPoolBuf = GraphBuilder.CreateUAV(SurfelPoolBuf);
		PassParameters->SurfelMetaBuf = GraphBuilder.CreateUAV(SurfelMetaBuf, EPixelFormat::PF_R8_UINT);
		PassParameters->SurfelRePositionBuf = GraphBuilder.CreateUAV(SurfelRePositionBuf);
		PassParameters->SurfelRePositionCountBuf = GraphBuilder.CreateUAV(SurfelRePositionCountBuf);
		PassParameters->SurfelVertexBuf = GraphBuilder.CreateSRV(SurfelVertexBuf);

		PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
		PassParameters->NormalTexture = GBufferATexture;
		PassParameters->DepthTexture = SceneDepthTexture;
		PassParameters->RWDebugOutTex = GraphBuilder.CreateUAV(DebugTex);
		PassParameters->PointClampSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		PassParameters->LinearClampSampler = TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		PassParameters->TexBufferSize = BufferTexSize;

		ClearUnusedGraphResources(ComputeShader, PassParameters);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("VisualizeSurfelCS"),
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(TexSize, FIrradianceVisualizeCS::GetThreadBlockSize()));
	}

    //temp res
	SurfelRes.SurfelMetaBuf = SurfelMetaBuf;
	SurfelRes.SurfelGridMetaBuf = SurfelGridMetaBuf;
	SurfelRes.SurfelPoolBuf = SurfelPoolBuf;
	SurfelRes.SurfelLifeBuf = SurfelLifeBuf;
	SurfelRes.SurfelEntryCellBuf = SurfelEntryCellBuf;
	SurfelRes.SurfelVertexBuf = SurfelVertexBuf;
	SurfelRes.SurfelIrradianceBuf = SurfelIrradianceBuf;
	SurfelRes.SurfelRePositionBuf = SurfelRePositionBuf;
	SurfelRes.SurfelRePositionCountBuf = SurfelRePositionCountBuf;
	SurfelRes.SurfelAuxiBuf = SurfelAuxiBuf;
    SurfelRes.SurfelGridMetaBuf2 = SurfelGridMetaBuf2;
    SurfelRes.SurfelEntryIndirectionBuf = SurfelIndirectionBuf;

	GraphBuilder.QueueBufferExtraction(SurfelRes.SurfelIrradianceBuf, &View.ViewState->SurfelIrradianceBuf);
	GraphBuilder.QueueBufferExtraction(SurfelRes.SurfelVertexBuf, &View.ViewState->SurfelVertexBuf);
	GraphBuilder.QueueBufferExtraction(SurfelRes.SurfelMetaBuf, &View.ViewState->SurfelMetaBuf);
	GraphBuilder.QueueBufferExtraction(SurfelRes.SurfelGridMetaBuf, &View.ViewState->SurfelGridMetaBuf);
	GraphBuilder.QueueBufferExtraction(SurfelRes.SurfelLifeBuf, &View.ViewState->SurfelLifeBuf);
	GraphBuilder.QueueBufferExtraction(SurfelRes.SurfelPoolBuf, &View.ViewState->SurfelPoolBuf);
	GraphBuilder.QueueBufferExtraction(SurfelRes.SurfelEntryCellBuf, &View.ViewState->SurfelEntryCellBuf);
	GraphBuilder.QueueBufferExtraction(SurfelRes.SurfelRePositionBuf, &View.ViewState->SurfelRePositionBuf);
	GraphBuilder.QueueBufferExtraction(SurfelRes.SurfelRePositionCountBuf, &View.ViewState->SurfelRePositionCountBuf);
	GraphBuilder.QueueBufferExtraction(SurfelRes.SurfelAuxiBuf, &View.ViewState->SurfelAuxiBuf);
	GraphBuilder.QueueBufferExtraction(SurfelRes.SurfelGridMetaBuf2, &View.ViewState->SurfelGridMetaBuf2);
	GraphBuilder.QueueBufferExtraction(SurfelRes.SurfelEntryIndirectionBuf, &View.ViewState->SurfelIndirectionBuf);
	
}

#endif //RHI_RAYTRACING