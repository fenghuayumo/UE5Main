#include "RayTracing/RayTracingLighting.h"
#include "RayTracing/RayTracingDeferredMaterials.h"
#include "RayTracing/RayTracingReflections.h"
#include "RendererPrivate.h"
#include "GlobalShader.h"
#include "DeferredShadingRenderer.h"
#include "SceneTextureParameters.h"
#include "ReflectionEnvironment.h"

#include "Fusion.h"

#if RHI_RAYTRACING

 static TAutoConsoleVariable<int32> CVarFusionReflectionsGenerateRaysWithRGS(
 	TEXT("r.Fusion.RestirRTR.ExperimentalDeferred.GenerateRaysWithRGS"),
 	1,
 	TEXT("Whether to generate reflection rays directly in RGS or in a separate compute shader (default: 1)"),
 	ECVF_RenderThreadSafe
 );

 static TAutoConsoleVariable<int32> CVarFusionReflectionsGlossy(
 	TEXT("r.Fusion.RestirRTR.ExperimentalDeferred.Glossy"),
 	1,
 	TEXT("Whether to use glossy reflections with GGX sampling or to force mirror-like reflections for performance (default: 1)"),
 	ECVF_RenderThreadSafe
 );

 static TAutoConsoleVariable<float> CVarFusionReflectionsAnyHitMaxRoughness(
 	TEXT("r.Fusion.RestirRTR.ExperimentalDeferred.AnyHitMaxRoughness"),
 	0.1,
 	TEXT("Allows skipping AnyHit shader execution for rough reflection rays (default: 0.1)"),
 	ECVF_RenderThreadSafe
 );


 static TAutoConsoleVariable<float> CVarFusionReflectionsSmoothBias(
 	TEXT("r.Fusion.RestirRTR.ExperimentalDeferred.SmoothBias"),
 	0.0,
 	TEXT("Whether to bias reflections towards smooth / mirror-like directions. Improves performance, but is not physically based. (default: 0)\n")
 	TEXT("The bias is implemented as a non-linear function, affecting low roughness values more than high roughness ones.\n")
 	TEXT("Roughness values higher than this CVar value remain entirely unaffected.\n"),
 	ECVF_RenderThreadSafe
 );

 static TAutoConsoleVariable<float> CVarFusionReflectionsMipBias(
 	TEXT("r.Fusion.RestirRTR.ExperimentalDeferred.MipBias"),
 	0.0,
 	TEXT("Global texture mip bias applied during ray tracing material evaluation. (default: 0)\n")
 	TEXT("Improves ray tracing reflection performance at the cost of lower resolution textures in reflections. Values are clamped to range [0..15].\n"),
 	ECVF_RenderThreadSafe
 );

 static TAutoConsoleVariable<int32> CVarFusionReflectionsSpatialResolve(
 	TEXT("r.Fusion.RestirRTR.ExperimentalDeferred.SpatialResolve"),
 	1,
 	TEXT("Whether to use a basic spatial resolve (denoising) filter on reflection output. Not compatible with regular screen space denoiser. (default: 1)"),
 	ECVF_RenderThreadSafe
 );

 static TAutoConsoleVariable<float> CVarFusionReflectionsSpatialResolveMaxRadius(
 	TEXT("r.Fusion.RestirRTR.ExperimentalDeferred.SpatialResolve.MaxRadius"),
 	8.0f,
 	TEXT("Maximum radius in pixels of the native reflection image. Actual radius depends on output pixel roughness, rougher reflections using larger radius. (default: 8)"),
 	ECVF_RenderThreadSafe
 );

 static TAutoConsoleVariable<int32> CVarFusionReflectionsSpatialResolveNumSamples(
 	TEXT("r.Fusion.RestirRTR.ExperimentalDeferred.SpatialResolve.NumSamples"),
 	8,
 	TEXT("Maximum number of screen space samples to take during spatial resolve step. More samples produces smoother output at higher GPU cost. Specialized shader is used for 4, 8, 12 and 16 samples. (default: 8)"),
 	ECVF_RenderThreadSafe
 );

 static TAutoConsoleVariable<float> CVarFusionReflectionsTemporalWeight(
 	TEXT("r.Fusion.RestirRTR.ExperimentalDeferred.SpatialResolve.TemporalWeight"),
 	0.95f, // Up to 95% of the reflection can come from history buffer, at least 5% always from current frame
 	TEXT("Defines whether to perform temporal accumulation during reflection spatial resolve and how much weight to give to history. Valid values in range [0..1]. (default: 0.90)"),
 	ECVF_RenderThreadSafe
 );

 static TAutoConsoleVariable<int32> CVarFusionReflectionsTemporalQuality(
 	TEXT("r.Fusion.RestirRTR.ExperimentalDeferred.SpatialResolve.TemporalQuality"),
 	2,
 	TEXT("0: Disable temporal accumulation\n")
 	TEXT("1: Tile-based temporal accumulation (low quality)\n")
 	TEXT("2: Tile-based temporal accumulation with randomized tile offsets per frame (medium quality)\n")
 	TEXT("(default: 2)"),
 	ECVF_RenderThreadSafe
 );

 static TAutoConsoleVariable<float> CVarFusionReflectionsHorizontalResolutionScale(
 	TEXT("r.Fusion.RestirRTR.ExperimentalDeferred.HorizontalResolutionScale"),
 	1.0,
 	TEXT("Reflection resolution scaling for the X axis between 0.25 and 4.0. Can only be used when spatial resolve is enabled. (default: 1)"),
 	ECVF_RenderThreadSafe
 );
static TAutoConsoleVariable<int32> CVarRestirRTTemporal(
	TEXT("r.Fusion.RestirRTR.TemporalResampling"),
	0,
	TEXT("Whether to temporal resampling  (default: 1)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarRestirRTSpatial(
	TEXT("r.Fusion.RestirRTR.SpatialResampling"),
	0,
	TEXT("Whether to temporal resampling  (default: 1)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarRestirRTRTemporalMaxHistory(
	TEXT("r.Fusion.RestirRTR.Temporal.MaxHistory"),
	10,
	TEXT("set max history frames to use(default: 1)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarRestirRTRTemporalAppoxVisibility(
	TEXT("r.Fusion.RestirRTR.Temporal.AppoxVisibility"),
	0,
	TEXT("Whether to use visibility ray in temporal resampling  (default: 1)"),
	ECVF_RenderThreadSafe
);


static TAutoConsoleVariable<float> CVarRestirRTRSpatialSamplingRadius(
	TEXT("r.Fusion.RestirRTR.Spatial.SamplingRadius"), 4.0f,
	TEXT("Spatial radius for sampling in pixels (Default 4.0)"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarRestirRTRSpatialSamples(
	TEXT("r.Fusion.RestirRTR.Spatial.Samples"), 1,
	TEXT("Spatial samples per pixel"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarRestirRTRSpatialSamplesBoost(
	TEXT("r.Fusion.RestirRTR.Spatial.SamplesBoost"), 1,
	TEXT("Spatial samples per pixel when invalid history is detected"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarRestirRTRSpatialNormalRejectionThreshold(
	TEXT("r.Fusion.RestirRTR.Spatial.NormalRejectionThreshold"), 0.5f,
	TEXT("Rejection threshold for rejecting samples based on normal differences (default 0.5)"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarRestirRTRSpatialDepthRejectionThreshold(
	TEXT("r.Fusion.RestirRTR.Spatial.DepthRejectionThreshold"), 0.1f,
	TEXT("Rejection threshold for rejecting samples based on depth differences (default 0.1)"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarRestirRTRSpatialApplyApproxVisibility(
	TEXT("r.Fusion.RestirRTR.Spatial.ApplyApproxVisibility"), 0,
	TEXT("Apply an approximate visibility test on sample selected during spatial sampling"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarRestirRTREvalApplyApproxVisibility(
	TEXT("r.Fusion.RestirRTR.Eval.ApplyApproxVisibility"), 1,
	TEXT("Apply an approximate visibility test on sample selected during evaluate phase"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarRestirRTRFeedBackVisility(
	TEXT("r.Fusion.RestirRTR.FeedBackVisility"), 1,
	TEXT("Apply an approximate visibility test on sample selected during evaluate phase"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarRestirRTRResolve(
	TEXT("r.Fusion.RestirRTR.Resolve"), 1,
	TEXT("Whether Use Fusion Restir Reflection Resolve"),
	ECVF_RenderThreadSafe);

extern TAutoConsoleVariable<int32> CVarRayTracingReflectionsUseSurfel;;
extern TAutoConsoleVariable<int32> CVarRayTracingReflectionsTemporalQuality;
extern TAutoConsoleVariable<float> CVarRayTracingReflectionsSpatialResolveMaxRadius;
extern TAutoConsoleVariable<int32> CVarRayTracingReflectionsSpatialResolveNumSamples;

extern TAutoConsoleVariable<float> CVarRayTracingReflectionsTemporalWeight;
namespace 
{
	struct FSortedReflectionRay
	{
		float  Origin[3];
		uint32 PixelCoordinates; // X in low 16 bits, Y in high 16 bits
		uint32 Direction[2]; // FP16
		float  Pdf;
		float  Roughness; // Only technically need 8 bits, the rest could be repurposed
	};

	struct FRayIntersectionBookmark
	{
		uint32 Data[2];
	};
} // anon namespace

struct  PackedReservoir
{
	// Internal compressed Reflection sample data
	FIntVector4		CreationGeometry;
	FIntVector4		HitGeometry;
	FIntVector4		LightInfo;
    FVector4f        PdfInfo;
};

class FFusionReflectionRGS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FFusionReflectionRGS)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FFusionReflectionRGS, FGlobalShader)

	class FDeferredMaterialMode : SHADER_PERMUTATION_ENUM_CLASS("DIM_DEFERRED_MATERIAL_MODE", EDeferredMaterialMode);
	class FAMDHitToken : SHADER_PERMUTATION_BOOL("DIM_AMD_HIT_TOKEN");
	class FUseSurfelDim : SHADER_PERMUTATION_BOOL("USE_SURFEL");
	using FPermutationDomain = TShaderPermutationDomain<FDeferredMaterialMode, FAMDHitToken, FUseSurfelDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, RayTracingResolution)
		SHADER_PARAMETER(FIntPoint, TileAlignedResolution)
		SHADER_PARAMETER(float, ReflectionMaxNormalBias)
		SHADER_PARAMETER(float, ReflectionMaxRoughness)
		SHADER_PARAMETER(float, ReflectionSmoothBias)
		SHADER_PARAMETER(float, AnyHitMaxRoughness)
		SHADER_PARAMETER(float, TextureMipBias)
		SHADER_PARAMETER(FVector2f, UpscaleFactor)
		SHADER_PARAMETER(int, ShouldDoDirectLighting)
		SHADER_PARAMETER(int, ShouldDoEmissiveAndIndirectLighting)
		SHADER_PARAMETER(int, ShouldDoReflectionCaptures)
		SHADER_PARAMETER(int, DenoisingOutputFormat)
		SHADER_PARAMETER_SRV(RaytracingAccelerationStructure, TLAS)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FSortedReflectionRay>, RayBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FRayIntersectionBookmark>, BookmarkBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FDeferredMaterialPayload>, MaterialBuffer)

		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, ColorOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, ReflectionDenoiserData)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
        
        SHADER_PARAMETER_SRV(StructuredBuffer<FRTLightingData>, LightDataBuffer)
		SHADER_PARAMETER_STRUCT_REF(FReflectionUniformParameters, ReflectionStruct)
		SHADER_PARAMETER_STRUCT_REF(FRaytracingLightDataPacked, LightDataPacked)
		SHADER_PARAMETER_STRUCT_REF(FReflectionCaptureShaderData, ReflectionCapture)
        SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FForwardLightData, Forward)
        
        // SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FPathTracingLight>, SceneLights)
		// SHADER_PARAMETER(uint32, SceneLightCount)
		// SHADER_PARAMETER_STRUCT_INCLUDE(FPathTracingLightGrid, LightGridParameters)
		// SHADER_PARAMETER_STRUCT_INCLUDE(FPathTracingSkylight, SkylightParameters)

		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		SHADER_PARAMETER_TEXTURE(Texture2D, SSProfilesTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, TransmissionProfilesLinearSampler)
		
		//surfel gi
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWByteAddressBuffer, SurfelMetaBuf)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWByteAddressBuffer, SurfelGridMetaBuf)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint32> , SurfelEntryCellBuf)
		
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint32>, SurfelLifeBuf)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint32>, SurfelPoolBuf)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FSurfelVertexPacked>, SurfelRePositionBuf)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint32>, SurfelRePositionCountBuf)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FSurfelVertexPacked>, SurfelVertexBuf)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, SurfelIrradianceBuf)

        //
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<PackedReservoir>, RWRTReservoirUAV)
        SHADER_PARAMETER(FIntVector, ReservoirBufferDim)

	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		if (!ShouldCompileRayTracingShadersForProject(Parameters.Platform))
		{
			return false;
		}

		FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FDeferredMaterialMode>() == EDeferredMaterialMode::None)
		{
			return false;
		}

		if (PermutationVector.Get<FDeferredMaterialMode>() != EDeferredMaterialMode::Shade
			&& PermutationVector.Get<FUseSurfelDim>())
		{
			// DIM_GENERATE_RAYS only makes sense for "Shade" mode
			return false;
		}

		if (PermutationVector.Get<FAMDHitToken>() && !(IsD3DPlatform(Parameters.Platform) && IsPCPlatform(Parameters.Platform)))
		{
			return false;
		}

		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("UE_RAY_TRACING_DISPATCH_1D"), 1); // Always using 1D dispatches
		OutEnvironment.SetDefine(TEXT("ENABLE_TWO_SIDED_GEOMETRY"), 1); // Always using double-sided ray tracing for shadow rays
        OutEnvironment.SetDefine(TEXT("GENERATE_REFLECTION_SAMPLES"), 1);
		OutEnvironment.SetDefine(TEXT("DIM_GENERATE_RAYS"), 1);
	}
};
IMPLEMENT_GLOBAL_SHADER(FFusionReflectionRGS, "/Engine/Private/RestirRTR/RestirReflection.usf", "RayTracingDeferredReflectionsRGS", SF_RayGen);

class FReflectionTemporalSamplingRGS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FReflectionTemporalSamplingRGS)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FReflectionTemporalSamplingRGS, FGlobalShader)

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
		OutEnvironment.SetDefine(TEXT("TEMPORAL_SPATIAL_RESAMPLING"), 1);
	}

	static uint32 GetThreadBlockSize()
	{
		return 8;
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		        SHADER_PARAMETER(FIntPoint, RayTracingBufferSize)
        //restir
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<PackedReservoir>, RWRTReservoirUAV)
        SHADER_PARAMETER(FIntVector, ReservoirBufferDim)
        SHADER_PARAMETER(FIntVector, ReservoirHistoryBufferDim)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<PackedReservoir>, RTReservoirHistory)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, DepthHistory)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, NormalHistory)

        SHADER_PARAMETER(int32, MaxTemporalHistory)
        SHADER_PARAMETER(FVector2f, UpscaleFactor)

        SHADER_PARAMETER(float, ReflectionMaxRoughness)
		SHADER_PARAMETER(float, ReflectionSmoothBias)
		SHADER_PARAMETER(float, ReflectionMaxNormalBias)
		
	    SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_SRV(RaytracingAccelerationStructure, TLAS)
		SHADER_PARAMETER(int, ApproxVisibility)
		SHADER_PARAMETER(int, InputSlice)
		SHADER_PARAMETER(int, OutputSlice)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FReflectionTemporalSamplingRGS, "/Engine/Private/RestirRTR/RestirResampling.usf", "TemporalResamplingRGS", SF_RayGen);


class FReflectionSpatialSamplingRGS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FReflectionSpatialSamplingRGS)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FReflectionSpatialSamplingRGS, FGlobalShader)

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
		OutEnvironment.SetDefine(TEXT("TEMPORAL_SPATIAL_RESAMPLING"), 1);
	}

	static uint32 GetThreadBlockSize()
	{
		return 8;
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		        SHADER_PARAMETER(FIntPoint, RayTracingBufferSize)
        //restir
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<PackedReservoir>, RWRTReservoirUAV)
        SHADER_PARAMETER(FIntVector, ReservoirBufferDim)
        SHADER_PARAMETER(FIntVector, ReservoirHistoryBufferDim)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<PackedReservoir>, RTReservoirHistory)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, DepthHistory)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, NormalHistory)

        SHADER_PARAMETER(int32, MaxTemporalHistory)
        SHADER_PARAMETER(FVector2f, UpscaleFactor)

        SHADER_PARAMETER(float, ReflectionMaxRoughness)
		SHADER_PARAMETER(float, ReflectionSmoothBias)
		SHADER_PARAMETER(float, ReflectionMaxNormalBias)
		
	    SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_SRV(RaytracingAccelerationStructure, TLAS)
		SHADER_PARAMETER(int, ApproxVisibility)
		SHADER_PARAMETER(int, InputSlice)
		SHADER_PARAMETER(int, OutputSlice)
		SHADER_PARAMETER(float, SpatialDepthRejectionThreshold)
		SHADER_PARAMETER(float, SpatialNormalRejectionThreshold)
		SHADER_PARAMETER(float, SpatialSamplingRadius)
		SHADER_PARAMETER(int, SpatialSamples)
		SHADER_PARAMETER(int, SpatialSamplesBoost)

	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FReflectionSpatialSamplingRGS, "/Engine/Private/RestirRTR/RestirResampling.usf", "SpatialResamplingRGS", SF_RayGen);

class FEvaluateRestirReflectionRGS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FEvaluateRestirReflectionRGS)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FEvaluateRestirReflectionRGS, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("USE_ALTERNATE_RNG"), 0);
		OutEnvironment.SetDefine(TEXT("USE_LDS_FOR_SPATIAL_RESAMPLE"), 1);
		// We need the skylight to do its own form of MIS because RTGI doesn't do its own
		OutEnvironment.SetDefine(TEXT("PATHTRACING_SKY_MIS"), 1);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )

	   SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<PackedReservoir>, RWRTReservoirUAV)
        SHADER_PARAMETER(FIntVector, ReservoirBufferDim)
        SHADER_PARAMETER(FIntVector, ReservoirHistoryBufferDim)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<PackedReservoir>, RWRTReservoirHistoryUAV)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, ColorOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, DebugTex)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, RawReflectionColor)
     	SHADER_PARAMETER(float, ReflectionMaxNormalBias)

		SHADER_PARAMETER(FVector2f, UpscaleFactor)
		SHADER_PARAMETER(float, ReflectionMaxRoughness)
		SHADER_PARAMETER(float, ReflectionSmoothBias)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		SHADER_PARAMETER_SRV(RaytracingAccelerationStructure, TLAS)
		SHADER_PARAMETER(int, InputSlice)
		SHADER_PARAMETER(int, OutputSlice)
		SHADER_PARAMETER(int, ApproxVisibility)
		SHADER_PARAMETER(int, FeedbackVisibility)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FEvaluateRestirReflectionRGS, "/Engine/Private/RestirRTR/RestirEvaluate.usf", "RestirEvaluateRGS", SF_RayGen);

class FFusionReflectionResovleCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FFusionReflectionResovleCS)
	SHADER_USE_PARAMETER_STRUCT(FFusionReflectionResovleCS, FGlobalShader)

	class FNumSamples : SHADER_PERMUTATION_SPARSE_INT("DIM_NUM_SAMPLES", 0, 4, 8, 12, 16);

	using FPermutationDomain = TShaderPermutationDomain<FNumSamples>;

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
        OutEnvironment.SetDefine(TEXT("REFLECTION_RESOLVE_CS"), 1);
	}

	static FIntPoint GetGroupSize()
	{
		return FIntPoint(8, 8);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )

        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<PackedReservoir>, RWRTReservoirUAV)
        SHADER_PARAMETER(FIntVector, ReservoirBufferDim)
        SHADER_PARAMETER(FIntVector, ReservoirHistoryBufferDim)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<PackedReservoir>, RWRTReservoirHistoryUAV)
        // SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, ColorOutput)
        // SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SSProfilesTexture)

        SHADER_PARAMETER(FIntPoint, RayTracingBufferSize)
		SHADER_PARAMETER(FVector2f, UpscaleFactor)
		SHADER_PARAMETER(float, SpatialResolveMaxRadius)
		SHADER_PARAMETER(int, SpatialResolveNumSamples)
		SHADER_PARAMETER(float, ReflectionMaxRoughness)
		SHADER_PARAMETER(float, ReflectionSmoothBias)
		SHADER_PARAMETER(float, ReflectionHistoryWeight)
		SHADER_PARAMETER(FVector4f, HistoryScreenPositionScaleBias)
		SHADER_PARAMETER(uint32, ThreadIdOffset)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DepthBufferHistory)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ReflectionHistory)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, RawReflectionColor)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ReflectionDenoiserData)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, ColorOutput)
		SHADER_PARAMETER(int, InputSlice)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, DebugTex)
		
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FFusionReflectionResovleCS, "/Engine/Private/RestirRTR/RestirReflectionResolve.usf", "ReflectionResolveCS", SF_Compute);

void FDeferredShadingSceneRenderer::PrepareFusionReflections(const FViewInfo& View, const FScene& Scene, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	FFusionReflectionRGS::FPermutationDomain PermutationVector;

	const bool bHitTokenEnabled = CanUseRayTracingAMDHitToken();

	PermutationVector.Set<FFusionReflectionRGS::FAMDHitToken>(bHitTokenEnabled);

	{
		PermutationVector.Set<FFusionReflectionRGS::FDeferredMaterialMode>(EDeferredMaterialMode::Gather);
		PermutationVector.Set<FFusionReflectionRGS::FUseSurfelDim>(false);
		auto RayGenShader = View.ShaderMap->GetShader<FFusionReflectionRGS>(PermutationVector);
		OutRayGenShaders.Add(RayGenShader.GetRayTracingShader());
	}
	for(int EnableSurfel = 0; EnableSurfel < 2; EnableSurfel++)
	{
		PermutationVector.Set<FFusionReflectionRGS::FDeferredMaterialMode>(EDeferredMaterialMode::Shade);
		PermutationVector.Set<FFusionReflectionRGS::FUseSurfelDim>(EnableSurfel == 1);
		
		auto RayGenShader = View.ShaderMap->GetShader<FFusionReflectionRGS>(PermutationVector);
		OutRayGenShaders.Add(RayGenShader.GetRayTracingShader());
	}
	{
		FEvaluateRestirReflectionRGS::FPermutationDomain PermutationVector2;
		auto RayGenShader = View.ShaderMap->GetShader<FEvaluateRestirReflectionRGS>(PermutationVector2);
		OutRayGenShaders.Add(RayGenShader.GetRayTracingShader());
	}
	{
		FReflectionTemporalSamplingRGS::FPermutationDomain PermutationVector2;
		auto RayGenShader = View.ShaderMap->GetShader<FReflectionTemporalSamplingRGS>(PermutationVector2);
		OutRayGenShaders.Add(RayGenShader.GetRayTracingShader());
	}
	{
		FReflectionSpatialSamplingRGS::FPermutationDomain PermutationVector2;
		auto RayGenShader = View.ShaderMap->GetShader<FReflectionSpatialSamplingRGS>(PermutationVector2);
		OutRayGenShaders.Add(RayGenShader.GetRayTracingShader());
	}
}

void FDeferredShadingSceneRenderer::PrepareFusionReflectionsDeferredMaterial(const FViewInfo& View, const FScene& Scene, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	FFusionReflectionRGS::FPermutationDomain PermutationVector;

	//const bool bGenerateRaysWithRGS = CVarFusionReflectionsGenerateRaysWithRGS.GetValueOnRenderThread() == 1;
	const bool bHitTokenEnabled = CanUseRayTracingAMDHitToken();

	PermutationVector.Set<FFusionReflectionRGS::FAMDHitToken>(bHitTokenEnabled);
	PermutationVector.Set<FFusionReflectionRGS::FDeferredMaterialMode>(EDeferredMaterialMode::Gather);
	auto RayGenShader = View.ShaderMap->GetShader<FFusionReflectionRGS>(PermutationVector);
	OutRayGenShaders.Add(RayGenShader.GetRayTracingShader());

}

static void AddReflectionResolvePass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FFusionReflectionRGS::FParameters& CommonParameters,
	FRDGTextureRef DepthBufferHistory,
	FRDGTextureRef ReflectionHistory, float ReflectionHistoryWeight, const FVector4f& HistoryScreenPositionScaleBias,
	FRDGTextureRef RawReflectionColor,
	FRDGTextureRef ReflectionDenoiserData,
	FIntPoint RayTracingBufferSize,
	FIntPoint ResolvedOutputSize,
	FRDGTextureRef ColorOutput)
{
	auto* PassParameters = GraphBuilder.AllocParameters<FFusionReflectionResovleCS::FParameters>();
	PassParameters->RayTracingBufferSize           = RayTracingBufferSize;
	PassParameters->UpscaleFactor                  = CommonParameters.UpscaleFactor;
	PassParameters->SpatialResolveMaxRadius        = FMath::Clamp<float>(CVarRayTracingReflectionsSpatialResolveMaxRadius.GetValueOnRenderThread(), 0.0f, 32.0f);
	PassParameters->SpatialResolveNumSamples       = FMath::Clamp<int32>(CVarRayTracingReflectionsSpatialResolveNumSamples.GetValueOnRenderThread(), 1, 32);
	PassParameters->ReflectionMaxRoughness         = CommonParameters.ReflectionMaxRoughness;
	PassParameters->ReflectionSmoothBias           = CommonParameters.ReflectionSmoothBias;
	PassParameters->ReflectionHistoryWeight        = ReflectionHistoryWeight;
	PassParameters->HistoryScreenPositionScaleBias = HistoryScreenPositionScaleBias;
	PassParameters->ViewUniformBuffer              = CommonParameters.ViewUniformBuffer;
	PassParameters->SceneTextures                  = CommonParameters.SceneTextures;
	PassParameters->DepthBufferHistory             = DepthBufferHistory;
	PassParameters->ReflectionHistory              = ReflectionHistory;
	PassParameters->RawReflectionColor             = RawReflectionColor;
	PassParameters->ReflectionDenoiserData         = ReflectionDenoiserData;
	PassParameters->ColorOutput                    = GraphBuilder.CreateUAV(ColorOutput);

	// 
	const uint32 FrameIndex = View.ViewState ? View.ViewState->GetFrameIndex() : 0;
	static const uint32 Offsets[8] = { 7, 2, 0, 5, 3, 1, 4, 6 }; // Just a randomized list of offsets (added to DispatchThreadId in the shader)
	PassParameters->ThreadIdOffset = ReflectionHistoryWeight > 0 && CVarRayTracingReflectionsTemporalQuality.GetValueOnRenderThread() == 2 
		? Offsets[FrameIndex % UE_ARRAY_COUNT(Offsets)] : 0;

	FFusionReflectionResovleCS::FPermutationDomain PermutationVector;
	if ((PassParameters->SpatialResolveNumSamples % 4 == 0) && PassParameters->SpatialResolveNumSamples <= 16)
	{
		// Static unrolled loop
		PermutationVector.Set<FFusionReflectionResovleCS::FNumSamples>(PassParameters->SpatialResolveNumSamples);
	}
	else
	{
		// Dynamic loop
		PermutationVector.Set<FFusionReflectionResovleCS::FNumSamples>(0);
	}

	auto ComputeShader = View.ShaderMap->GetShader<FFusionReflectionResovleCS>(PermutationVector);
	ClearUnusedGraphResources(ComputeShader, PassParameters);

	FIntVector GroupCount;
	GroupCount.X = FMath::DivideAndRoundUp(ResolvedOutputSize.X, FFusionReflectionResovleCS::GetGroupSize().X);
	GroupCount.Y = FMath::DivideAndRoundUp(ResolvedOutputSize.Y, FFusionReflectionResovleCS::GetGroupSize().Y);
	GroupCount.Z = 1;
	FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("RayTracingReflectionResolve"), ComputeShader, PassParameters, GroupCount);
}


void FDeferredShadingSceneRenderer::RenderFusionReflections(
	FRDGBuilder& GraphBuilder,
	const FSceneTextureParameters& SceneTextures,
	const FViewInfo& View,
	int DenoiserMode,
	const FRayTracingReflectionOptions& Options,
	IScreenSpaceDenoiser::FReflectionsInputs* OutDenoiserInputs,
	FSurfelBufResources* SurfelRes,
	FRadianceVolumeProbeConfigs* ProbeConfig)
{
	const float ResolutionFraction = Options.ResolutionFraction;

	FVector2f UpscaleFactor = FVector2f(1.0f);
	int32  UpscaleFactorInt = int32(1.0f / ResolutionFraction);
	FIntPoint RayTracingResolution = View.ViewRect.Size();
	FIntPoint RayTracingBufferSize = SceneTextures.SceneDepthTexture->Desc.Extent;
    const bool bSpatialResolve = true;
	if (bSpatialResolve )
	{
		FVector2f ResolutionFloat = FMath::Max(FVector2f(4.0f), FVector2f(RayTracingResolution) *  ResolutionFraction);
		FVector2f BufferSizeFloat = FMath::Max(FVector2f(4.0f), FVector2f(RayTracingBufferSize) *  ResolutionFraction);

		RayTracingResolution.X = (int32)FMath::CeilToFloat(ResolutionFloat.X);
		RayTracingResolution.Y = (int32)FMath::CeilToFloat(ResolutionFloat.Y);

		RayTracingBufferSize.X = (int32)FMath::CeilToFloat(BufferSizeFloat.X);
		RayTracingBufferSize.Y = (int32)FMath::CeilToFloat(BufferSizeFloat.Y);

		UpscaleFactor = FVector2f(View.ViewRect.Size()) / FVector2f(RayTracingResolution);
	}
	else
	{
		RayTracingResolution = FIntPoint::DivideAndRoundUp(RayTracingResolution, UpscaleFactorInt);
		RayTracingBufferSize = RayTracingBufferSize / UpscaleFactorInt;
		UpscaleFactor = FVector2f((float)UpscaleFactorInt);
	}

	FRDGTextureDesc OutputDesc = FRDGTextureDesc::Create2D(
		RayTracingBufferSize,
		PF_FloatRGBA, FClearValueBinding(FLinearColor(0, 0, 0, 0)),
		TexCreate_ShaderResource | TexCreate_UAV);

	OutDenoiserInputs->Color = GraphBuilder.CreateTexture(OutputDesc,
		bSpatialResolve
		? TEXT("RayTracingReflectionsRaw") :
		TEXT("RayTracingReflections"));

	FRDGTextureRef ReflectionDenoiserData;
	if (bSpatialResolve)
	{
		OutputDesc.Format      = PF_FloatRGBA;
		ReflectionDenoiserData = GraphBuilder.CreateTexture(OutputDesc, TEXT("RayTracingReflectionsSpatialResolveData"));
	}
	// else
	// {
	// 	OutputDesc.Format                  = PF_R16F;
	// 	ReflectionDenoiserData             = GraphBuilder.CreateTexture(OutputDesc, TEXT("RayTracingReflectionsHitDistance"));
	// 	OutDenoiserInputs->RayHitDistance  = ReflectionDenoiserData;
	// }

	const uint32 SortTileSize             = 64; // Ray sort tile is 32x32, material sort tile is 64x64, so we use 64 here (tile size is not configurable).
	const FIntPoint TileAlignedResolution = FIntPoint::DivideAndRoundUp(RayTracingResolution, SortTileSize) * SortTileSize;

	FFusionReflectionRGS::FParameters CommonParameters;
	CommonParameters.UpscaleFactor           = UpscaleFactor;
	CommonParameters.RayTracingResolution    = RayTracingResolution;
	CommonParameters.TileAlignedResolution   = TileAlignedResolution;
	CommonParameters.ReflectionMaxRoughness  = Options.MaxRoughness;
	CommonParameters.ReflectionSmoothBias    = CVarFusionReflectionsGlossy.GetValueOnRenderThread() ? CVarFusionReflectionsSmoothBias.GetValueOnRenderThread() : -1;
	CommonParameters.AnyHitMaxRoughness      = CVarFusionReflectionsAnyHitMaxRoughness.GetValueOnRenderThread();
	CommonParameters.TextureMipBias          = FMath::Clamp(CVarFusionReflectionsMipBias.GetValueOnRenderThread(), 0.0f, 15.0f);

	CommonParameters.ShouldDoDirectLighting              = Options.bDirectLighting;
	CommonParameters.ShouldDoEmissiveAndIndirectLighting = Options.bEmissiveAndIndirectLighting;
	CommonParameters.ShouldDoReflectionCaptures          = Options.bReflectionCaptures;

	CommonParameters.DenoisingOutputFormat               = bSpatialResolve ? 1 : 0;

	CommonParameters.TLAS                    = View.GetRayTracingSceneViewChecked();
	CommonParameters.SceneTextures           = SceneTextures;
	CommonParameters.ViewUniformBuffer       = View.ViewUniformBuffer;
	CommonParameters.SSProfilesTexture       = View.RayTracingSubSurfaceProfileTexture;
	CommonParameters.LightDataPacked         = View.RayTracingLightData.UniformBuffer;
	CommonParameters.LightDataBuffer         = View.RayTracingLightData.LightBufferSRV;
	CommonParameters.ReflectionStruct        = CreateReflectionUniformBuffer(View, EUniformBufferUsage::UniformBuffer_SingleFrame);
	CommonParameters.ReflectionCapture       = View.ReflectionCaptureUniformBuffer;
	CommonParameters.Forward                 = View.ForwardLightingResources.ForwardLightUniformBuffer;
	CommonParameters.ReflectionMaxNormalBias = GetRaytracingMaxNormalBias();
	//SetupLightParameters(Scene, View, GraphBuilder, &CommonParameters.SceneLights, &CommonParameters.SceneLightCount, &CommonParameters.SkylightParameters);

	if (!CommonParameters.SceneTextures.GBufferVelocityTexture)
	{
		CommonParameters.SceneTextures.GBufferVelocityTexture = GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);
	}

	const bool bHitTokenEnabled = CanUseRayTracingAMDHitToken();

	// Generate sorted reflection rays

	const uint32 TileAlignedNumRays          = TileAlignedResolution.X * TileAlignedResolution.Y;
	const FRDGBufferDesc SortedRayBufferDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(FSortedReflectionRay), TileAlignedNumRays);
	FRDGBufferRef SortedRayBuffer            = GraphBuilder.CreateBuffer(SortedRayBufferDesc, TEXT("ReflectionRayBuffer"));

	const FRDGBufferDesc DeferredMaterialBufferDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(FDeferredMaterialPayload), TileAlignedNumRays);
	FRDGBufferRef DeferredMaterialBuffer            = GraphBuilder.CreateBuffer(DeferredMaterialBufferDesc, TEXT("RayTracingReflectionsMaterialBuffer"));

	const FRDGBufferDesc BookmarkBufferDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(FRayIntersectionBookmark), TileAlignedNumRays);
	FRDGBufferRef BookmarkBuffer            = GraphBuilder.CreateBuffer(BookmarkBufferDesc, TEXT("RayTracingReflectionsBookmarkBuffer"));

	// Trace reflection material gather rays

	{
		FFusionReflectionRGS::FParameters& PassParameters = *GraphBuilder.AllocParameters<FFusionReflectionRGS::FParameters>();
		PassParameters                        = CommonParameters;
		PassParameters.MaterialBuffer         = GraphBuilder.CreateUAV(DeferredMaterialBuffer);
		PassParameters.RayBuffer              = GraphBuilder.CreateUAV(SortedRayBuffer);
		PassParameters.BookmarkBuffer         = GraphBuilder.CreateUAV(BookmarkBuffer);
		PassParameters.ColorOutput            = GraphBuilder.CreateUAV(OutDenoiserInputs->Color);
		PassParameters.ReflectionDenoiserData = GraphBuilder.CreateUAV(ReflectionDenoiserData);

		FFusionReflectionRGS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FFusionReflectionRGS::FAMDHitToken>(bHitTokenEnabled);
		PermutationVector.Set<FFusionReflectionRGS::FDeferredMaterialMode>(EDeferredMaterialMode::Gather);
		PermutationVector.Set<FFusionReflectionRGS::FUseSurfelDim>(false);
		auto RayGenShader = View.ShaderMap->GetShader<FFusionReflectionRGS>(PermutationVector);
		ClearUnusedGraphResources(RayGenShader, &PassParameters);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("RayTracingDeferredReflectionsGather %dx%d", RayTracingResolution.X, RayTracingResolution.Y),
			&PassParameters,
			ERDGPassFlags::Compute,
		[&PassParameters, this, &View, TileAlignedNumRays, RayGenShader](FRHIRayTracingCommandList& RHICmdList)
		{
			FRayTracingPipelineState* Pipeline = View.RayTracingMaterialGatherPipeline;

			FRayTracingShaderBindingsWriter GlobalResources;
			SetShaderParameters(GlobalResources, RayGenShader, PassParameters);
			FRHIRayTracingScene* RayTracingSceneRHI = View.GetRayTracingSceneChecked();
			RHICmdList.RayTraceDispatch(Pipeline, RayGenShader.GetRayTracingShader(), RayTracingSceneRHI, GlobalResources, TileAlignedNumRays, 1);
		});
	}

	// Sort hit points by material within 64x64 (4096 element) tiles

	SortDeferredMaterials(GraphBuilder, View, 5, TileAlignedNumRays, DeferredMaterialBuffer);

	// Shade reflection points

	FIntVector ReservoirBufferDim = FIntVector(RayTracingBufferSize.X, RayTracingBufferSize.Y, 2);
	FRDGBufferDesc ReservoirDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(PackedReservoir), ReservoirBufferDim.X * ReservoirBufferDim.Y * ReservoirBufferDim.Z);

	FRDGBufferRef RTReservoirs = GraphBuilder.CreateBuffer(ReservoirDesc, TEXT("RTReservoirs"));
	FIntVector ReservoirHistoryBufferDim = FIntVector(RayTracingBufferSize.X, RayTracingBufferSize.Y, 1);
	FRDGBufferDesc ReservoirHistoryDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(PackedReservoir), ReservoirHistoryBufferDim.X * ReservoirHistoryBufferDim.Y * ReservoirHistoryBufferDim.Z);
	FRDGBufferRef RTReservoirsHistory = GraphBuilder.CreateBuffer(ReservoirHistoryDesc, TEXT("RTReservoirsHistory"));
    {
		FFusionReflectionRGS::FParameters& PassParameters = *GraphBuilder.AllocParameters<FFusionReflectionRGS::FParameters>();
		PassParameters                        = CommonParameters;
		PassParameters.MaterialBuffer         = GraphBuilder.CreateUAV(DeferredMaterialBuffer);
		PassParameters.RayBuffer              = GraphBuilder.CreateUAV(SortedRayBuffer);
		PassParameters.BookmarkBuffer         = GraphBuilder.CreateUAV(BookmarkBuffer);
		PassParameters.ColorOutput            = GraphBuilder.CreateUAV(OutDenoiserInputs->Color);
		PassParameters.ReflectionDenoiserData = GraphBuilder.CreateUAV(ReflectionDenoiserData);
        PassParameters.RWRTReservoirUAV       = GraphBuilder.CreateUAV(RTReservoirs);
        PassParameters.ReservoirBufferDim       = ReservoirBufferDim;
		//SetupLightParameters(Scene, View, GraphBuilder, &PassParameters.SceneLights, &PassParameters.SceneLightCount, &PassParameters.SkylightParameters);
        bool bUseSurfel = SurfelRes && SurfelRes->SurfelIrradianceBuf && CVarRayTracingReflectionsUseSurfel.GetValueOnRenderThread() != 0;
		if (bUseSurfel)
		{
			FRDGBufferRef SurfelMetaBuf = SurfelRes->SurfelMetaBuf;
			FRDGBufferRef SurfelGridMetaBuf = SurfelRes->SurfelGridMetaBuf;
			FRDGBufferRef SurfelEntryCellBuf = SurfelRes->SurfelEntryCellBuf;
			FRDGBufferRef SurfelPoolBuf = SurfelRes->SurfelPoolBuf;
			FRDGBufferRef SurfelLifeBuf = SurfelRes->SurfelLifeBuf;
			FRDGBufferRef SurfelVertexBuf = SurfelRes->SurfelVertexBuf;
			FRDGBufferRef SurfelIrradianceBuf = SurfelRes->SurfelIrradianceBuf;
			FRDGBufferRef SurfelRePositionBuf = SurfelRes->SurfelRePositionBuf;
			FRDGBufferRef SurfelRePositionCountBuf = SurfelRes->SurfelRePositionCountBuf;

			PassParameters.SurfelMetaBuf = GraphBuilder.CreateUAV(SurfelMetaBuf, EPixelFormat::PF_R8_UINT);
			PassParameters.SurfelGridMetaBuf = GraphBuilder.CreateUAV(SurfelGridMetaBuf, EPixelFormat::PF_R8_UINT);
			PassParameters.SurfelEntryCellBuf = GraphBuilder.CreateUAV(SurfelEntryCellBuf);

			PassParameters.SurfelPoolBuf = GraphBuilder.CreateUAV(SurfelPoolBuf);
			PassParameters.SurfelLifeBuf = GraphBuilder.CreateUAV(SurfelLifeBuf);
			PassParameters.SurfelVertexBuf = GraphBuilder.CreateUAV(SurfelMetaBuf);
			PassParameters.SurfelIrradianceBuf = GraphBuilder.CreateUAV(SurfelIrradianceBuf);
			PassParameters.SurfelRePositionBuf = GraphBuilder.CreateUAV(SurfelRePositionBuf);
			PassParameters.SurfelRePositionCountBuf = GraphBuilder.CreateUAV(SurfelRePositionCountBuf);
		}

		FFusionReflectionRGS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FFusionReflectionRGS::FAMDHitToken>(bHitTokenEnabled);
		PermutationVector.Set<FFusionReflectionRGS::FDeferredMaterialMode>(EDeferredMaterialMode::Shade);
		PermutationVector.Set<FFusionReflectionRGS::FUseSurfelDim>(bUseSurfel);
		auto RayGenShader = View.ShaderMap->GetShader<FFusionReflectionRGS>(PermutationVector);
		ClearUnusedGraphResources(RayGenShader, &PassParameters);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("RayTracingDeferredReflectionsShade %dx%d", RayTracingResolution.X, RayTracingResolution.Y),
			&PassParameters,
			ERDGPassFlags::Compute,
		[&PassParameters, &View, TileAlignedNumRays, RayGenShader](FRHIRayTracingCommandList& RHICmdList)
		{
			FRayTracingShaderBindingsWriter GlobalResources;
			SetShaderParameters(GlobalResources, RayGenShader, PassParameters);
			FRHIRayTracingScene* RayTracingSceneRHI = View.GetRayTracingSceneChecked();
			RHICmdList.RayTraceDispatch(View.RayTracingMaterialPipeline, RayGenShader.GetRayTracingShader(), RayTracingSceneRHI, GlobalResources, TileAlignedNumRays, 1);
		});
	}

    //tempoarl resampling
    const bool bCameraCut = !View.PrevViewInfo.RestirReflectionHistory.Reservoirs.IsValid() || View.bCameraCut;
    if (CVarRestirRTTemporal.GetValueOnRenderThread() != 0 && !bCameraCut )
    {
        TShaderMapRef<FReflectionTemporalSamplingRGS> RayGenShader(GetGlobalShaderMap(ERHIFeatureLevel::SM5));
        FReflectionTemporalSamplingRGS::FParameters* PassParameters = GraphBuilder.AllocParameters<FReflectionTemporalSamplingRGS::FParameters>();
       
        PassParameters->NormalHistory = RegisterExternalTextureWithFallback(GraphBuilder, View.PrevViewInfo.GBufferA, GSystemTextures.BlackDummy);
		PassParameters->DepthHistory = RegisterExternalTextureWithFallback(GraphBuilder, View.PrevViewInfo.DepthBuffer, GSystemTextures.BlackDummy);
        PassParameters->SceneTextures     = CommonParameters.SceneTextures;
        PassParameters->MaxTemporalHistory = CVarRestirRTRTemporalMaxHistory.GetValueOnRenderThread();
        PassParameters->UpscaleFactor = UpscaleFactor;
        PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
        PassParameters->ReservoirHistoryBufferDim = ReservoirHistoryBufferDim;
        PassParameters->RTReservoirHistory = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(View.PrevViewInfo.RestirReflectionHistory.Reservoirs));
        PassParameters->RWRTReservoirUAV       = GraphBuilder.CreateUAV(RTReservoirs);
        PassParameters->ReservoirBufferDim       = ReservoirBufferDim;
		PassParameters->ReflectionMaxRoughness         = CommonParameters.ReflectionMaxRoughness;
        PassParameters->ReflectionSmoothBias           = CommonParameters.ReflectionSmoothBias;
		PassParameters->InputSlice = 0;
		PassParameters->OutputSlice = 0;
		PassParameters->ReflectionMaxNormalBias		= GetRaytracingMaxNormalBias();
		PassParameters->RayTracingBufferSize	= RayTracingBufferSize;

		PassParameters->TLAS = View.GetRayTracingSceneViewChecked();
		PassParameters->ApproxVisibility = CVarRestirRTRTemporalAppoxVisibility.GetValueOnRenderThread();
        ClearUnusedGraphResources(RayGenShader, PassParameters);
        // FComputeShaderUtils::AddPass(
        //     GraphBuilder,
        //     RDG_EVENT_NAME("ReflectionTemporalSpatialSamplingCS"),
        //     ComputeShader,
        //     PassParameters,
        //     FComputeShaderUtils::GetGroupCount(RayTracingResolution, FReflectionTemporalSpatialSamplingCS::GetThreadBlockSize()));
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("ReflectionTemporalSamplingRGS %dx%d", RayTracingResolution.X, RayTracingResolution.Y),
			PassParameters,
			ERDGPassFlags::Compute,
			[PassParameters,&View, RayGenShader, RayTracingResolution](FRHIRayTracingCommandList& RHICmdList)
			{
				FRayTracingShaderBindingsWriter GlobalResources;
				SetShaderParameters(GlobalResources, RayGenShader, *PassParameters);

				FRHIRayTracingScene* RayTracingSceneRHI = View.GetRayTracingSceneChecked();
				RHICmdList.RayTraceDispatch(View.RayTracingMaterialPipeline, RayGenShader.GetRayTracingShader(), RayTracingSceneRHI, GlobalResources, RayTracingResolution.X, RayTracingResolution.Y);
			});
    }
	//Spatial
	int InitialSlice = 0;
	if (CVarRestirRTSpatial.GetValueOnRenderThread() != 0)
	{
		TShaderMapRef<FReflectionSpatialSamplingRGS> RayGenShader(GetGlobalShaderMap(ERHIFeatureLevel::SM5));
        FReflectionSpatialSamplingRGS::FParameters* PassParameters = GraphBuilder.AllocParameters<FReflectionSpatialSamplingRGS::FParameters>();
		PassParameters->RayTracingBufferSize           = RayTracingBufferSize;
        PassParameters->NormalHistory = RegisterExternalTextureWithFallback(GraphBuilder, View.PrevViewInfo.GBufferA, GSystemTextures.BlackDummy);
		PassParameters->DepthHistory = RegisterExternalTextureWithFallback(GraphBuilder, View.PrevViewInfo.DepthBuffer, GSystemTextures.BlackDummy);
        PassParameters->SceneTextures     = CommonParameters.SceneTextures;
        PassParameters->MaxTemporalHistory = CVarRestirRTRTemporalMaxHistory.GetValueOnRenderThread();
        PassParameters->UpscaleFactor = UpscaleFactor;
        PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
        PassParameters->RWRTReservoirUAV       = GraphBuilder.CreateUAV(RTReservoirs);
        PassParameters->ReservoirBufferDim       = ReservoirBufferDim;
		PassParameters->ReflectionMaxRoughness         = CommonParameters.ReflectionMaxRoughness;
        PassParameters->ReflectionSmoothBias           = CommonParameters.ReflectionSmoothBias;

		PassParameters->ReflectionMaxNormalBias		= GetRaytracingMaxNormalBias();
		PassParameters->TLAS = View.GetRayTracingSceneViewChecked();
		PassParameters->ApproxVisibility = CVarRestirRTRSpatialApplyApproxVisibility.GetValueOnRenderThread();
		PassParameters->InputSlice = 0;
		PassParameters->OutputSlice = 1;
		PassParameters->SpatialDepthRejectionThreshold = CVarRestirRTRSpatialDepthRejectionThreshold.GetValueOnRenderThread();
		PassParameters->SpatialNormalRejectionThreshold = CVarRestirRTRSpatialNormalRejectionThreshold.GetValueOnRenderThread();
		PassParameters->SpatialSamplingRadius = CVarRestirRTRSpatialSamplingRadius.GetValueOnRenderThread();
		PassParameters->SpatialSamples = CVarRestirRTRSpatialSamples.GetValueOnRenderThread();
		PassParameters->SpatialSamplesBoost = CVarRestirRTRSpatialSamplesBoost.GetValueOnRenderThread();

        ClearUnusedGraphResources(RayGenShader, PassParameters);
		GraphBuilder.AddPass(
		RDG_EVENT_NAME("ReflectionSpatialSamplingRGS %dx%d", RayTracingResolution.X, RayTracingResolution.Y),
		PassParameters,
		ERDGPassFlags::Compute,
		[PassParameters,&View, RayGenShader, RayTracingResolution](FRHIRayTracingCommandList& RHICmdList)
		{
			FRayTracingShaderBindingsWriter GlobalResources;
			SetShaderParameters(GlobalResources, RayGenShader, *PassParameters);

			FRHIRayTracingScene* RayTracingSceneRHI = View.GetRayTracingSceneChecked();
			RHICmdList.RayTraceDispatch(View.RayTracingMaterialPipeline, RayGenShader.GetRayTracingShader(), RayTracingSceneRHI, GlobalResources, RayTracingResolution.X, RayTracingResolution.Y);
		});
		InitialSlice = 1;
	}
    //Evaluate 
    {
		FIntPoint OutputSize = CVarRestirRTRResolve.GetValueOnRenderThread() == 1? SceneTextures.SceneDepthTexture->Desc.Extent : (SceneTextures.SceneDepthTexture->Desc.Extent / UpscaleFactorInt);
        FRDGTextureDesc ResolvedOutputDesc = FRDGTextureDesc::Create2D(
			OutputSize, // full res buffer
			PF_FloatRGBA, FClearValueBinding(FLinearColor(0, 0, 0, 0)), 
			TexCreate_ShaderResource | TexCreate_UAV);
		
		FRDGTextureRef RawReflectionColor = OutDenoiserInputs->Color;
		OutDenoiserInputs->Color = GraphBuilder.CreateTexture(ResolvedOutputDesc, TEXT("RayTracingReflections"));
		auto DebugTex = GraphBuilder.CreateTexture(ResolvedOutputDesc, TEXT("DebugReflectionTex"));
		
		const FScreenSpaceDenoiserHistory& ReflectionsHistory = View.PrevViewInfo.ReflectionsHistory;

		const bool bValidHistory = ReflectionsHistory.IsValid() && !View.bCameraCut;

		FRDGTextureRef DepthBufferHistoryTexture = GraphBuilder.RegisterExternalTexture(
			bValidHistory && View.PrevViewInfo.DepthBuffer.IsValid()
			? View.PrevViewInfo.DepthBuffer
			: GSystemTextures.BlackDummy);

		FRDGTextureRef ReflectionHistoryTexture = GraphBuilder.RegisterExternalTexture(
			bValidHistory 
			? ReflectionsHistory.RT[0] 
			: GSystemTextures.BlackDummy);

		const float HistoryWeight = bValidHistory
			? FMath::Clamp(CVarRayTracingReflectionsTemporalWeight.GetValueOnRenderThread(), 0.0f, 0.99f)
			: 0.0;

		FIntPoint ViewportOffset = View.ViewRect.Min;
		FIntPoint ViewportExtent = View.ViewRect.Size();
		FIntPoint BufferSize     = SceneTextures.SceneDepthTexture->Desc.Extent;

		if (bValidHistory)
		{
			ViewportOffset = ReflectionsHistory.Scissor.Min;
			ViewportExtent = ReflectionsHistory.Scissor.Size();
			BufferSize     = ReflectionsHistory.RT[0]->GetDesc().Extent;
		}

		FVector2f InvBufferSize(1.0f / float(BufferSize.X), 1.0f / float(BufferSize.Y));

		FVector4f HistoryScreenPositionScaleBias = FVector4f(
			ViewportExtent.X * 0.5f * InvBufferSize.X,
			-ViewportExtent.Y * 0.5f * InvBufferSize.Y,
			(ViewportExtent.X * 0.5f + ViewportOffset.X) * InvBufferSize.X,
			(ViewportExtent.Y * 0.5f + ViewportOffset.Y) * InvBufferSize.Y);

		if (CVarRestirRTRResolve.GetValueOnRenderThread())
		{
			 auto* PassParameters = GraphBuilder.AllocParameters<FFusionReflectionResovleCS::FParameters>();
			 PassParameters->RayTracingBufferSize           = RayTracingBufferSize;
			 PassParameters->UpscaleFactor                  = CommonParameters.UpscaleFactor;
			 PassParameters->SpatialResolveMaxRadius        = FMath::Clamp<float>(CVarRayTracingReflectionsSpatialResolveMaxRadius.GetValueOnRenderThread(), 0.0f, 32.0f);
			 PassParameters->SpatialResolveNumSamples       = FMath::Clamp<int32>(CVarRayTracingReflectionsSpatialResolveNumSamples.GetValueOnRenderThread(), 1, 32);
			 PassParameters->ReflectionMaxRoughness         = CommonParameters.ReflectionMaxRoughness;
			 PassParameters->ReflectionSmoothBias           = CommonParameters.ReflectionSmoothBias;
			 PassParameters->ReflectionHistoryWeight        = HistoryWeight;
			 PassParameters->HistoryScreenPositionScaleBias = HistoryScreenPositionScaleBias;
			 PassParameters->ViewUniformBuffer              = CommonParameters.ViewUniformBuffer;
			 PassParameters->SceneTextures                  = CommonParameters.SceneTextures;
			 PassParameters->DepthBufferHistory             = DepthBufferHistoryTexture;
			 PassParameters->ReflectionHistory              = ReflectionHistoryTexture;
			 PassParameters->RawReflectionColor             = RawReflectionColor;
			 PassParameters->ReflectionDenoiserData         = ReflectionDenoiserData;
			 PassParameters->ColorOutput                    = GraphBuilder.CreateUAV(OutDenoiserInputs->Color);
			 PassParameters->ReservoirHistoryBufferDim = ReservoirHistoryBufferDim;
			 PassParameters->RWRTReservoirHistoryUAV = GraphBuilder.CreateUAV(RTReservoirsHistory);
			 PassParameters->ReservoirBufferDim = ReservoirBufferDim;
			 PassParameters->RWRTReservoirUAV       = GraphBuilder.CreateUAV(RTReservoirs);
			 PassParameters->InputSlice = InitialSlice;
			 PassParameters->DebugTex = GraphBuilder.CreateUAV(DebugTex);
			 // 
			 const uint32 FrameIndex = View.ViewState ? View.ViewState->GetFrameIndex() : 0;
			 static const uint32 Offsets[8] = { 7, 2, 0, 5, 3, 1, 4, 6 }; // Just a randomized list of offsets (added to DispatchThreadId in the shader)
			 PassParameters->ThreadIdOffset = HistoryWeight > 0 && CVarRayTracingReflectionsTemporalQuality.GetValueOnRenderThread() == 2
			     ? Offsets[FrameIndex % UE_ARRAY_COUNT(Offsets)] : 0;

			 FFusionReflectionResovleCS::FPermutationDomain PermutationVector;
			 if ((PassParameters->SpatialResolveNumSamples % 4 == 0) && PassParameters->SpatialResolveNumSamples <= 16)
			 {
			     // Static unrolled loop
			     PermutationVector.Set<FFusionReflectionResovleCS::FNumSamples>(PassParameters->SpatialResolveNumSamples);
			 }
			 else
			 {
			     // Dynamic loop
			     PermutationVector.Set<FFusionReflectionResovleCS::FNumSamples>(0);
			 }

			 auto ComputeShader = View.ShaderMap->GetShader<FFusionReflectionResovleCS>(PermutationVector);
			 ClearUnusedGraphResources(ComputeShader, PassParameters);

			 FIntVector GroupCount;
			 GroupCount.X = FMath::DivideAndRoundUp(View.ViewRect.Size().X, FFusionReflectionResovleCS::GetGroupSize().X);
			 GroupCount.Y = FMath::DivideAndRoundUp(View.ViewRect.Size().Y, FFusionReflectionResovleCS::GetGroupSize().Y);
			 GroupCount.Z = 1;
			 FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("RayTracingReflectionResolve"), ComputeShader, PassParameters, GroupCount);
		}
		else
		{
			FEvaluateRestirReflectionRGS::FParameters* PassParameters = GraphBuilder.AllocParameters<FEvaluateRestirReflectionRGS::FParameters>();
			PassParameters->RWRTReservoirUAV = GraphBuilder.CreateUAV(RTReservoirs);
			PassParameters->ReservoirBufferDim = ReservoirBufferDim;
			PassParameters->ReservoirHistoryBufferDim = ReservoirHistoryBufferDim;
			PassParameters->RWRTReservoirHistoryUAV = GraphBuilder.CreateUAV(RTReservoirsHistory);
			PassParameters->UpscaleFactor = CommonParameters.UpscaleFactor;
			PassParameters->ReflectionMaxRoughness = CommonParameters.ReflectionMaxRoughness;
			PassParameters->ViewUniformBuffer = CommonParameters.ViewUniformBuffer;
			PassParameters->SceneTextures = CommonParameters.SceneTextures;
			PassParameters->ReflectionMaxNormalBias = GetRaytracingMaxNormalBias();
			PassParameters->ColorOutput = GraphBuilder.CreateUAV(OutDenoiserInputs->Color);
			PassParameters->DebugTex = GraphBuilder.CreateUAV(DebugTex);
			PassParameters->TLAS = CommonParameters.TLAS;
			PassParameters->InputSlice = InitialSlice;
			PassParameters->FeedbackVisibility = CVarRestirRTRFeedBackVisility.GetValueOnRenderThread();
			PassParameters->ApproxVisibility = CVarRestirRTREvalApplyApproxVisibility.GetValueOnRenderThread();
			PassParameters->RawReflectionColor = RawReflectionColor;
			PassParameters->ReflectionSmoothBias = CommonParameters.ReflectionSmoothBias;
			FEvaluateRestirReflectionRGS::FPermutationDomain PermutationVector;
			//auto RayGenShader = View.ShaderMap->GetShader<FEvaluateRestirReflectionRGS>(PermutationVector);
			TShaderMapRef<FEvaluateRestirReflectionRGS> RayGenShader(GetGlobalShaderMap(ERHIFeatureLevel::SM5), PermutationVector);
			ClearUnusedGraphResources(RayGenShader, PassParameters);

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("FusionReflectionEValuate %dx%d", RayTracingResolution.X, RayTracingResolution.Y),
				PassParameters,
				ERDGPassFlags::Compute,
				[PassParameters, &View, RayGenShader, RayTracingResolution](FRHIRayTracingCommandList& RHICmdList)
				{
					FRayTracingShaderBindingsWriter GlobalResources;
					SetShaderParameters(GlobalResources, RayGenShader, *PassParameters);

					FRHIRayTracingScene* RayTracingSceneRHI = View.GetRayTracingSceneChecked();
					RHICmdList.RayTraceDispatch(View.RayTracingMaterialPipeline, RayGenShader.GetRayTracingShader(), RayTracingSceneRHI, GlobalResources, RayTracingResolution.X, RayTracingResolution.Y);
				});
		}

		if ( View.ViewState)
		{
			GraphBuilder.QueueTextureExtraction(SceneTextures.SceneDepthTexture, &View.ViewState->PrevFrameViewInfo.DepthBuffer);
			GraphBuilder.QueueTextureExtraction(OutDenoiserInputs->Color, &View.ViewState->PrevFrameViewInfo.ReflectionsHistory.RT[0]);
			View.ViewState->PrevFrameViewInfo.ReflectionsHistory.Scissor = View.ViewRect;
		}

    }

    if (!View.bStatePrevViewInfoIsReadOnly)
	{
		//Extract history feedback here
		GraphBuilder.QueueBufferExtraction(RTReservoirsHistory, &View.ViewState->PrevFrameViewInfo.RestirReflectionHistory.Reservoirs);

		View.ViewState->PrevFrameViewInfo.RestirReflectionHistory.ReservoirDimensions = ReservoirHistoryBufferDim;
	}
}
#else // RHI_RAYTRACING
void FDeferredShadingSceneRenderer::RenderFusionReflections(
	FRDGBuilder& GraphBuilder,
	const FSceneTextureParameters& SceneTextures,
	const FViewInfo& View,
	int DenoiserMode,
	const FRayTracingReflectionOptions& Options,
	IScreenSpaceDenoiser::FReflectionsInputs* OutDenoiserInputs)
{
	checkNoEntry();
}
#endif // RHI_RAYTRACING
