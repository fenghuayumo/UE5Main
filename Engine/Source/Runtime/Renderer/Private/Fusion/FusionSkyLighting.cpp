
#include "DeferredShadingRenderer.h"
#include "SceneTextureParameters.h"

#if RHI_RAYTRACING

#include "RayTracing/RayTracingMaterialHitShaders.h"
#include "ClearQuad.h"
#include "DistanceFieldAmbientOcclusion.h"
#include "SceneRendering.h"
#include "ScenePrivate.h"
#include "SceneRenderTargets.h"
#include "RenderGraphBuilder.h"
#include "RenderTargetPool.h"
#include "VisualizeTexture.h"
#include "RayGenShaderUtils.h"
#include "SceneTextureParameters.h"
#include "ScreenSpaceDenoise.h"
#include "RayTracingDefinitions.h"
#include "PathTracing.h"

#include "RayTracing/RaytracingOptions.h"
#include "PostProcess/PostProcessing.h"
#include "PostProcess/SceneFilterRendering.h"
#include "HairStrands/HairStrandsRendering.h"
#include "Fusion/FusionDenoiser.h"

static TAutoConsoleVariable<int32> CVarRestirSkyLightInitialCandidates(
	TEXT("r.Fusion.SkyLight.InitialSamples"), 4,
	TEXT("How many lights to test sample during the initial candidate search"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarRestirSkyLightNumReservoirs(
	TEXT("r.Fusion.SkyLight.NumReservoirs"), 1,
	TEXT("Number of independent light reservoirs per pixel\n")
	TEXT("  1-N - Explicit number of reservoirs\n"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarRestirSkyLightTemporal(
	TEXT("r.Fusion.SkyLight.Temporal"), 1,
	TEXT("Whether to apply Temporal resmapling"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarRestirSkyLightTemporalNormalRejectionThreshold(
    TEXT("r.Fusion.SkyLight.Temporal.NormalRejectionThreshold"), 0.5f,
    TEXT("Rejection threshold for rejecting samples based on normal differences (default 0.5)"),
    ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarRestirSkyLightTemporalDepthRejectionThreshold(
    TEXT("r.Fusion.SkyLight.Temporal.DepthRejectionThreshold"), 0.1f,
    TEXT("Rejection threshold for rejecting samples based on depth differences (default 0.1)"),
    ECVF_RenderThreadSafe);


static TAutoConsoleVariable<int32> CVarRestirSkyLightTemporalApplySpatialHash(
	TEXT("r.Fusion.SkyLight.Temporal.ApplySpatialHash"), 0,
	TEXT("Apply a spatial hash during temporal reprojection reprojection, can improve behavior of flat surfaces, but enhance noise on thin surfaces"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarRestirSkyLightSpatial(
	TEXT("r.Fusion.SkyLight.Spatial"), 1,
	TEXT("Whether to apply spatial resmapling"),
	ECVF_RenderThreadSafe);


static TAutoConsoleVariable<float> CVarRestirSkyLightSpatialSamplingRadius(
	TEXT("r.Fusion.SkyLight.Spatial.SamplingRadius"), 32.0f,
	TEXT("Spatial radius for sampling in pixels (Default 32.0)"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarRestirSkyLightSpatialSamples(
	TEXT("r.Fusion.SkyLight.Spatial.Samples"), 8,
	TEXT("Spatial samples per pixel"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarRestirSkyLightSpatialSamplesBoost(
	TEXT("r.Fusion.SkyLight.Spatial.SamplesBoost"), 16,
	TEXT("Spatial samples per pixel when invalid history is detected"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarRestirSkyLightSpatialNormalRejectionThreshold(
	TEXT("r.Fusion.SkyLight.Spatial.NormalRejectionThreshold"), 0.5f,
	TEXT("Rejection threshold for rejecting samples based on normal differences (default 0.5)"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarRestirSkyLightSpatialDepthRejectionThreshold(
	TEXT("r.Fusion.SkyLight.Spatial.DepthRejectionThreshold"), 0.1f,
	TEXT("Rejection threshold for rejecting samples based on depth differences (default 0.1)"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarRestirSkyLightSpatialApplyApproxVisibility(
	TEXT("r.Fusion.SkyLight.Spatial.ApplyApproxVisibility"), 0,
	TEXT("Apply an approximate visibility test on sample selected during spatial sampling"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarRestirSkyLightSpatialDiscountNaiveSamples(
	TEXT("r.Fusion.SkyLight.Spatial.DiscountNaiveSamples"), 1,
	TEXT("During spatial sampling, reduce the weights of 'naive' samples that lack history"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarRestirSkyLightTemporalMaxHistory(
	TEXT("r.Fusion.SkyLight.Temporal.MaxHistory"), 30,
	TEXT("Maximum temporal history for samples (default 30)"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarRestirSkyLightTestInitialVisibility(
	TEXT("r.Fusion.SkyLight.TestInitialVisibility"),
	1,
	TEXT("Test initial samples for visibility (default = 1)\n")
	TEXT("  0 - Do not test visibility during inital sampling\n")
	TEXT("  1 - Test visibility on final merged reservoir  (default)\n")
	TEXT("  2 - Test visibility on reservoirs prior to merging\n"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarRestirSkyLightTemporalApplyApproxVisibility(
	TEXT("r.Fusion.SkyLight.Temporal.ApplyApproxVisibility"), 0,
	TEXT("Apply an approximate visibility test on sample selected during reprojection"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarRestirSkyLightFeedbackVisibility(
	TEXT("r.Fusion.SkyLight.FeedbackVisibility"),
	1,
	TEXT("Whether to feedback the final visibility result to the history (default = 1)"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarRestirSkyLightApplyBoilingFilter(
	TEXT("r.Fusion.SkyLight.ApplyBoilingFilter"), 1,
	TEXT("Whether to apply boiling filter when temporally resampling"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarRestirSkyLightBoilingFilterStrength(
	TEXT("r.Fusion.SkyLight.BoilingFilterStrength"), 0.05f,
	TEXT("Strength of Boiling filter"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarRestirSkyLightEnableHairVoxel(
	TEXT("r.Fusion.SkyLight.EnableHairVoxel"),
	1,
	TEXT("Whether to test hair voxels for visibility when evaluating (default = 1)\n"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarRestirSkyLightEnableSkyDenoiser(
	TEXT("r.Fusion.SkyLight.Denoiser"),
	0,
	TEXT("Whether to use restir sky denoiser (default = 1)\n"),
	ECVF_RenderThreadSafe);
	
extern TAutoConsoleVariable<float> CVarRayTracingSkyLightScreenPercentage;

extern float GRayTracingSkyLightMaxRayDistance;
extern float GRayTracingSkyLightMaxShadowThickness;

extern TAutoConsoleVariable<int32> CVarRayTracingSkyLightEnableTwoSidedGeometry;
extern TAutoConsoleVariable<int32> CVarRayTracingSkyLightEnableMaterials;

BEGIN_SHADER_PARAMETER_STRUCT(FRestirSkyLightCommonParameters, )
	SHADER_PARAMETER(float, MaxNormalBias)
	SHADER_PARAMETER(int32, MaxTemporalHistory)
	SHADER_PARAMETER_SRV(RaytracingAccelerationStructure, TLAS)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint4>, RWLightReservoirUAV)
	SHADER_PARAMETER(FIntVector, ReservoirBufferDim)
    SHADER_PARAMETER(float, SkyLightMaxRayDistance)
    SHADER_PARAMETER(int, bSkyLightTransmission)
    SHADER_PARAMETER(float, SkyLightMaxShadowThickness)
    
     SHADER_PARAMETER_STRUCT_INCLUDE(FPathTracingSkylight, SkyLightParameters)
	//SHADER_PARAMETER(int32, RISSkylightBufferTiles)
	//SHADER_PARAMETER(int32, RISSkylightBufferTileSize)
	//SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint2>, RISSkylightBuffer)
	//SHADER_PARAMETER(float, InvSkylightSize)
	//SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, SkylightTexture)
	//SHADER_PARAMETER_SAMPLER(SamplerState, SkylightTextureSampler)

    SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWDebugDiffuseUAV)
    SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, RWDebugRayDistanceUAV)
END_SHADER_PARAMETER_STRUCT()

class FSkyLightInitialSamplesRGS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSkyLightInitialSamplesRGS)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FSkyLightInitialSamplesRGS, FGlobalShader)
    class FEnableTwoSidedGeometryDim : SHADER_PERMUTATION_BOOL("ENABLE_TWO_SIDED_GEOMETRY");
	class FEnableMaterialsDim : SHADER_PERMUTATION_BOOL("ENABLE_MATERIALS");
	class FHairLighting : SHADER_PERMUTATION_INT("USE_HAIR_LIGHTING", 2);

	using FPermutationDomain = TShaderPermutationDomain<FEnableTwoSidedGeometryDim, FEnableMaterialsDim, FHairLighting>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("USE_ALTERNATE_RNG"), 0);
	    OutEnvironment.SetDefine(TEXT("USE_LDS_FOR_SPATIAL_RESAMPLE"), 1);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(int32, OutputSlice)
		SHADER_PARAMETER(int32, HistoryReservoir)
		SHADER_PARAMETER(int32, InitialCandidates)
		SHADER_PARAMETER(int32, InitialSampleVisibility)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)

		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)

		SHADER_PARAMETER_TEXTURE(Texture2D, SSProfilesTexture)
        SHADER_PARAMETER_STRUCT_INCLUDE(FRestirSkyLightCommonParameters, RestirSkyLightCommonParameters)
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FSkyLightInitialSamplesRGS, "/Engine/Private/RestirDI/RestirSkyLighting.usf", "GenerateInitialSamplesRGS", SF_RayGen);

class FSkyLightTemporalResamplingRGS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSkyLightTemporalResamplingRGS)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FSkyLightTemporalResamplingRGS, FGlobalShader)
    
	class FEnableTwoSidedGeometryDim : SHADER_PERMUTATION_BOOL("ENABLE_TWO_SIDED_GEOMETRY");
	class FEnableMaterialsDim : SHADER_PERMUTATION_BOOL("ENABLE_MATERIALS");
	class FHairLighting : SHADER_PERMUTATION_INT("USE_HAIR_LIGHTING", 2);

	using FPermutationDomain = TShaderPermutationDomain<FEnableTwoSidedGeometryDim, FEnableMaterialsDim, FHairLighting>;
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("USE_ALTERNATE_RNG"), 0);
	    OutEnvironment.SetDefine(TEXT("USE_LDS_FOR_SPATIAL_RESAMPLE"), 1);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )

		SHADER_PARAMETER(int32, InputSlice)
		SHADER_PARAMETER(int32, OutputSlice)
		SHADER_PARAMETER(int32, HistoryReservoir)
		SHADER_PARAMETER(float, TemporalDepthRejectionThreshold)
		SHADER_PARAMETER(float, TemporalNormalRejectionThreshold)
		SHADER_PARAMETER(int32, ApplyApproximateVisibilityTest)
		SHADER_PARAMETER(int32, InitialCandidates)
		SHADER_PARAMETER(int32, InitialSampleVisibility)
		SHADER_PARAMETER(int32, SpatiallyHashTemporalReprojection)

		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)

		SHADER_PARAMETER(FIntVector, ReservoirHistoryBufferDim)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint4>, LightReservoirHistory)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, DepthHistory)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, NormalHistory)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)

		SHADER_PARAMETER_TEXTURE(Texture2D, SSProfilesTexture)
        SHADER_PARAMETER_STRUCT_INCLUDE(FRestirSkyLightCommonParameters, RestirSkyLightCommonParameters)
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FSkyLightTemporalResamplingRGS, "/Engine/Private/RestirDI/RestirSkyLighting.usf", "ApplyTemporalResamplingRGS", SF_RayGen);


class FSkyLightBoilingFilterCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSkyLightBoilingFilterCS)
	SHADER_USE_PARAMETER_STRUCT(FSkyLightBoilingFilterCS, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.CompilerFlags.Add(CFLAG_ForceDXC);
        OutEnvironment.SetDefine(TEXT("USE_ALTERNATE_RNG"), 0);
	    OutEnvironment.SetDefine(TEXT("USE_LDS_FOR_SPATIAL_RESAMPLE"), 1);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )

		SHADER_PARAMETER(int32, InputSlice)
		SHADER_PARAMETER(int32, OutputSlice)
		SHADER_PARAMETER(float, BoilingFilterStrength)

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint4>, RWLightReservoirUAV)
		SHADER_PARAMETER(FIntVector, ReservoirBufferDim)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)

	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FSkyLightBoilingFilterCS, "/Engine/Private/RestirDI/BoilingFilter.usf", "BoilingFilterCS", SF_Compute);

class FSkyLightSpatialResamplingRGS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSkyLightSpatialResamplingRGS)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FSkyLightSpatialResamplingRGS, FGlobalShader)
    class FEnableTwoSidedGeometryDim : SHADER_PERMUTATION_BOOL("ENABLE_TWO_SIDED_GEOMETRY");
	class FEnableMaterialsDim : SHADER_PERMUTATION_BOOL("ENABLE_MATERIALS");
	class FHairLighting : SHADER_PERMUTATION_INT("USE_HAIR_LIGHTING", 2);

	using FPermutationDomain = TShaderPermutationDomain<FEnableTwoSidedGeometryDim, FEnableMaterialsDim, FHairLighting>;
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("USE_ALTERNATE_RNG"), 0);
	    OutEnvironment.SetDefine(TEXT("USE_LDS_FOR_SPATIAL_RESAMPLE"), 1);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )

		SHADER_PARAMETER(int32, InputSlice)
		SHADER_PARAMETER(int32, OutputSlice)
		SHADER_PARAMETER(int32, HistoryReservoir)
		SHADER_PARAMETER(float, SpatialSamplingRadius)
		SHADER_PARAMETER(int32, SpatialSamples)
		SHADER_PARAMETER(int32, SpatialSamplesBoost)
		SHADER_PARAMETER(float, SpatialDepthRejectionThreshold)
		SHADER_PARAMETER(float, SpatialNormalRejectionThreshold)
		SHADER_PARAMETER(int32, ApplyApproximateVisibilityTest)
		SHADER_PARAMETER(uint32, NeighborOffsetMask)
		SHADER_PARAMETER(int32, DiscountNaiveSamples)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)

		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)

		SHADER_PARAMETER_TEXTURE(Texture2D, SSProfilesTexture)

		SHADER_PARAMETER_SRV(Buffer<float2>, NeighborOffsets)
        SHADER_PARAMETER_STRUCT_INCLUDE(FRestirSkyLightCommonParameters, RestirSkyLightCommonParameters)
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FSkyLightSpatialResamplingRGS, "/Engine/Private/RestirDI/RestirSkyLighting.usf", "ApplySpatialResamplingRGS", SF_RayGen);


class FSkyLightEvaluateRGS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSkyLightEvaluateRGS)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FSkyLightEvaluateRGS, FGlobalShader)

    class FEnableTwoSidedGeometryDim : SHADER_PERMUTATION_BOOL("ENABLE_TWO_SIDED_GEOMETRY");
	class FEnableMaterialsDim : SHADER_PERMUTATION_BOOL("ENABLE_MATERIALS");
	class FHairLighting : SHADER_PERMUTATION_INT("USE_HAIR_LIGHTING", 2);

	using FPermutationDomain = TShaderPermutationDomain<FEnableTwoSidedGeometryDim, FEnableMaterialsDim, FHairLighting>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("USE_ALTERNATE_RNG"), 0);
	    OutEnvironment.SetDefine(TEXT("USE_LDS_FOR_SPATIAL_RESAMPLE"), 1);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )

		SHADER_PARAMETER(int32, InputSlice)
		SHADER_PARAMETER(int32, NumReservoirs)
		SHADER_PARAMETER(int32, DemodulateMaterials)
		SHADER_PARAMETER(int32, DebugOutput)
		SHADER_PARAMETER(int32, FeedbackVisibility)
		SHADER_PARAMETER(uint32, bUseHairVoxel)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)

		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWDiffuseUAV)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, RWRayDistanceUAV)
		SHADER_PARAMETER(FIntVector, ReservoirHistoryBufferDim)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint4>, RWLightReservoirHistoryUAV)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HairCategorizationTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HairLightChannelMaskTexture)

		SHADER_PARAMETER_TEXTURE(Texture2D, SSProfilesTexture)

		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualVoxelParameters, VirtualVoxel)
        SHADER_PARAMETER_STRUCT_INCLUDE(FRestirSkyLightCommonParameters, RestirSkyLightCommonParameters)
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FSkyLightEvaluateRGS, "/Engine/Private/RestirDI/RestirSkyLighting.usf", "EvaluateSampledLightingRGS", SF_RayGen);


class FPreprocessSkylightForRISCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FPreprocessSkylightForRISCS)
	SHADER_USE_PARAMETER_STRUCT(FPreprocessSkylightForRISCS, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.CompilerFlags.Add(CFLAG_ForceDXC);
        OutEnvironment.SetDefine(TEXT("USE_ALTERNATE_RNG"), 0);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )

		SHADER_PARAMETER_TEXTURE(TextureCube, SkyLightCubemap0)
		SHADER_PARAMETER_TEXTURE(TextureCube, SkyLightCubemap1)
		SHADER_PARAMETER_SAMPLER(SamplerState, SkyLightCubemapSampler0)
		SHADER_PARAMETER_SAMPLER(SamplerState, SkyLightCubemapSampler1)
		SHADER_PARAMETER(float, SkylightBlendFactor)
		SHADER_PARAMETER(FVector3f, SkyColor)

		SHADER_PARAMETER(float, SkylightInvResolution)

		// Preprocessing just writes the top mip level
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, LightPdfUAV0)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, PreprocessedSkylight)

	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FPreprocessSkylightForRISCS, "/Engine/Private/RestirDI/PresampleLights.usf", "PreprocessSkylightCS", SF_Compute);

class FComputeLightingRisBufferCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FComputeLightingRisBufferCS)
	SHADER_USE_PARAMETER_STRUCT(FComputeLightingRisBufferCS, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.CompilerFlags.Add(CFLAG_ForceDXC);
		OutEnvironment.SetDefine(TEXT("USE_ALTERNATE_RNG"), 0);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )

		SHADER_PARAMETER(int32, PdfTexDimensions)
		SHADER_PARAMETER(int32, MaxMipLevel)
		SHADER_PARAMETER(int32, RisTileSize)
		SHADER_PARAMETER(float, WeightedSampling)

		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, LightPdfTexture)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint2>, RisBuffer)

	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FComputeLightingRisBufferCS, "/Engine/Private/RestirDI/PresampleLights.usf", "PreSampleLightsCS", SF_Compute);

class FComputeLightingPdfCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FComputeLightingPdfCS)
	SHADER_USE_PARAMETER_STRUCT(FComputeLightingPdfCS, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.CompilerFlags.Add(CFLAG_ForceDXC);
		OutEnvironment.SetDefine(TEXT("USE_ALTERNATE_RNG"), 0);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )

		SHADER_PARAMETER(int32, PdfTexDimensions)

		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, LightPdfTexture)

		// one per mip level, as UAVs only allow per mip binding
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, LightPdfUAV0)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, LightPdfUAV1)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, LightPdfUAV2)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, LightPdfUAV3)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, LightPdfUAV4)

	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FComputeLightingPdfCS, "/Engine/Private/RestirDI/PresampleLights.usf", "ComputeLightPdfCS", SF_Compute);

/**
* This buffer provides a table with a low discrepency sequence
*/
class FDiscSampleBuffer : public FRenderResource
{
public:

	/** The buffer used for storage. */
	FBufferRHIRef DiscSampleBufferRHI;
	/** Shader resource view in to the vertex buffer. */
	FShaderResourceViewRHIRef DiscSampleBufferSRV;

	const uint32 NumSamples = 8192;

	/**
	 * Initialize RHI resources.
	 */
	virtual void InitRHI() override
	{
		if (RHISupportsRayTracingShaders(GShaderPlatformForFeatureLevel[GetFeatureLevel()]))
		{
			// Create a sequence of low-discrepancy samples within a unit radius around the origin
			// for "randomly" sampling neighbors during spatial resampling
			TResourceArray<uint8> Buffer;

			Buffer.AddZeroed(NumSamples * 2);

			const int32 R = 250;
			const float phi2 = 1.0f / 1.3247179572447f;
			uint32 num = 0;
			float U = 0.5f;
			float V = 0.5f;
			while (num < NumSamples * 2) {
				U += phi2;
				V += phi2 * phi2;
				if (U >= 1.0f) U -= 1.0f;
				if (V >= 1.0f) V -= 1.0f;

				float rSq = (U - 0.5f) * (U - 0.5f) + (V - 0.5f) * (V - 0.5f);
				if (rSq > 0.25f)
					continue;

				Buffer[num++] = uint8((U - 0.5f) * R + 127.5f);
				Buffer[num++] = uint8((V - 0.5f) * R + 127.5f);
				
			}

			FRHIResourceCreateInfo CreateInfo(TEXT("RTXDIDiscSamples"), &Buffer);
			DiscSampleBufferRHI = RHICreateVertexBuffer(
				/*Size=*/ sizeof(uint8) * 2 * NumSamples,
				/*Usage=*/ BUF_Volatile | BUF_ShaderResource,
				CreateInfo);
			DiscSampleBufferSRV = RHICreateShaderResourceView(
				DiscSampleBufferRHI, /*Stride=*/ sizeof(uint8) * 2, PF_R8G8
			);
		}
	}

	/**
	 * Release RHI resources.
	 */
	virtual void ReleaseRHI() override
	{
		DiscSampleBufferSRV.SafeRelease();
		DiscSampleBufferRHI.SafeRelease();
	}
};

/** The global resource for the disc sample buffer. */
TGlobalResource<FDiscSampleBuffer> GDiscSampleBuffer;


	
struct FSkylightRIS
{
	FRDGBufferRef RISBuffer;
	FRDGTextureRef EnvTexture;
	FLinearColor SkyColor;
	float InvSize;
};

static FSkylightRIS BuildSkylightRISStructures(FRDGBuilder& GraphBuilder, int32 TileSize, int32 TileCount, const FViewInfo& View)
{
	FRDGBufferRef RisBuffer;
	FRDGTextureRef EnvTexture;
	const int32 RisBufferElements = TileCount * TileSize;

	FReflectionUniformParameters Parameters;
	SetupReflectionUniformParameters(View, Parameters);

	const FScene* Scene = (const FScene*)View.Family->Scene;
	FLinearColor SkyColor = FLinearColor::Black;
	float InvSize = 0.0f;

	if (RisBufferElements > 0 && Scene->SkyLight)
	{
		SkyColor = Scene->SkyLight->GetEffectiveLightColor();

		// follow the practice of the path tracer and double the dimension to roughly match the sample rate of the cubemap
		uint32 TexSize = FMath::RoundUpToPowerOfTwo(2 * Scene->SkyLight->CaptureCubeMapResolution);
		InvSize = 1.0f / TexSize;

		const uint32 MaxMip = FMath::FloorLog2(TexSize);
		const uint32 NumMips = MaxMip + 1;

		// Create env map
		FRDGTextureDesc TexDesc = FRDGTextureDesc::Create2D(
			FIntPoint(TexSize, TexSize),
			PF_FloatRGBA, // RGBA fp16
			FClearValueBinding::Black,
			TexCreate_ShaderResource | TexCreate_UAV,
			1);

		// Create env map pdf
		FRDGTextureDesc PdfDesc = FRDGTextureDesc::Create2D(
			FIntPoint(TexSize, TexSize),
			PF_R32_FLOAT,
			FClearValueBinding::None,
			TexCreate_ShaderResource | TexCreate_UAV,
			NumMips);

		FRDGTextureRef CdfTexture = GraphBuilder.CreateTexture(PdfDesc, TEXT("RTXDIEnvMapCDF"));
		EnvTexture = GraphBuilder.CreateTexture(TexDesc, TEXT("RTXDIEnvMap"));

		// first populate the envmap and level 0 of the CDF
		{
			FPreprocessSkylightForRISCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FPreprocessSkylightForRISCS::FParameters>();

			PassParameters->SkyColor = FVector3f(SkyColor.R, SkyColor.G, SkyColor.B);
			PassParameters->SkyLightCubemap0 = Parameters.SkyLightCubemap;
			PassParameters->SkyLightCubemap1 = Parameters.SkyLightBlendDestinationCubemap;
			PassParameters->SkyLightCubemapSampler0 = Parameters.SkyLightCubemapSampler;
			PassParameters->SkyLightCubemapSampler1 = Parameters.SkyLightBlendDestinationCubemapSampler;
			PassParameters->SkylightBlendFactor = Parameters.SkyLightParameters.W;
			PassParameters->SkylightInvResolution = InvSize;

			PassParameters->LightPdfUAV0 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(CdfTexture, 0));
			PassParameters->PreprocessedSkylight = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(EnvTexture));

			auto SkyLightProcessingShader = View.ShaderMap->GetShader< FPreprocessSkylightForRISCS>();

			uint32 NumGrids = FMath::DivideAndRoundUp(TexSize, 16u);
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("RTXDIProcessSkyLight"), SkyLightProcessingShader, PassParameters, FIntVector(NumGrids, NumGrids, 1));
		}

		// each pass generates 5 mip levels, starting at 1, since 0 is computed when collapsing the EnvMap
		for (uint32 BaseMip = 1; BaseMip < NumMips; BaseMip += 5)
		{
			uint32 BaseMipSize = TexSize >> BaseMip;
			// compute the local light CDF as a mip-mapped texture
			FComputeLightingPdfCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FComputeLightingPdfCS::FParameters>();

			PassParameters->PdfTexDimensions = BaseMipSize;
			{
				PassParameters->LightPdfTexture = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMipLevel(CdfTexture, BaseMip - 1));
			}

			PassParameters->LightPdfUAV0 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(CdfTexture, FMath::Min(BaseMip + 0u, MaxMip)));
			PassParameters->LightPdfUAV1 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(CdfTexture, FMath::Min(BaseMip + 1u, MaxMip)));
			PassParameters->LightPdfUAV2 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(CdfTexture, FMath::Min(BaseMip + 2u, MaxMip)));
			PassParameters->LightPdfUAV3 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(CdfTexture, FMath::Min(BaseMip + 3u, MaxMip)));
			PassParameters->LightPdfUAV4 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(CdfTexture, FMath::Min(BaseMip + 4u, MaxMip)));

			auto LightCdfShader = View.ShaderMap->GetShader<FComputeLightingPdfCS>();

			uint32 NumGrids = FMath::DivideAndRoundUp(BaseMipSize, 16u);

			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("RTXDIComputeSkyLightPdf"), LightCdfShader, PassParameters, FIntVector(NumGrids, NumGrids, 1));
		}

		FRDGBufferDesc RisBufferDesc = FRDGBufferDesc::CreateBufferDesc(sizeof(FVector2f), RisBufferElements);

		RisBuffer = GraphBuilder.CreateBuffer(RisBufferDesc, TEXT("SkylightRisBuffer"));

		{
			FComputeLightingRisBufferCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FComputeLightingRisBufferCS::FParameters>();

			PassParameters->LightPdfTexture = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(CdfTexture));
			PassParameters->MaxMipLevel = MaxMip;
			PassParameters->PdfTexDimensions = TexSize;
			PassParameters->RisTileSize = TileSize;
			PassParameters->WeightedSampling = 0.5; // always using even balance betweeen weighted and unweighted
			PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
			PassParameters->RisBuffer = GraphBuilder.CreateUAV(RisBuffer, PF_R32G32_UINT);

			auto LightPresampleShader = View.ShaderMap->GetShader<FComputeLightingRisBufferCS>();

			// dispatch handles 256 elements of a tile per block
			int32 RoundedTiles = FMath::DivideAndRoundUp(TileSize, 256);

			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("RTXDIPresampleSkyLight"), LightPresampleShader, PassParameters, FIntVector(RoundedTiles, TileCount, 1));
		}
	}
	else
	{
		// RIS is not in use, create tiny stand-in buffer
		// ToDo: refactor to have a constant one that persists rather than requiring a UAV clear
		FRDGBufferDesc RisBufferDesc = FRDGBufferDesc::CreateBufferDesc(sizeof(FVector2f), 1);

		RisBuffer = GraphBuilder.CreateBuffer(RisBufferDesc, TEXT("SkylightRisBuffer"));

		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(RisBuffer, PF_R32G32_UINT), 0);

		EnvTexture = GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy); //GraphBuilder.CreateTexture(TexDesc, TEXT("RTXDIEnvMap"));

	}

	return { RisBuffer,EnvTexture, SkyColor, InvSize };
}

void FDeferredShadingSceneRenderer::PrepareFusionSkyLight(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{

	for (uint32 TwoSidedGeometryIndex = 0; TwoSidedGeometryIndex < 2; ++TwoSidedGeometryIndex)
	{
		for (uint32 EnableMaterialsIndex = 0; EnableMaterialsIndex < 2; ++EnableMaterialsIndex)
		{
            for (int32 HairLighting = 0; HairLighting < 2; ++HairLighting)
            {
                {
                    FSkyLightInitialSamplesRGS::FPermutationDomain PermutationVector;
                    PermutationVector.Set<FSkyLightInitialSamplesRGS::FEnableTwoSidedGeometryDim>(TwoSidedGeometryIndex != 0);
                    PermutationVector.Set<FSkyLightInitialSamplesRGS::FEnableMaterialsDim>(EnableMaterialsIndex != 0);
                    PermutationVector.Set<FSkyLightInitialSamplesRGS::FHairLighting>(HairLighting);
                    TShaderMapRef<FSkyLightInitialSamplesRGS> RayGenerationShader(View.ShaderMap, PermutationVector);
                    OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
                }
                
                {
                    FSkyLightTemporalResamplingRGS::FPermutationDomain PermutationVector;
                    PermutationVector.Set<FSkyLightTemporalResamplingRGS::FEnableTwoSidedGeometryDim>(TwoSidedGeometryIndex != 0);
                    PermutationVector.Set<FSkyLightTemporalResamplingRGS::FEnableMaterialsDim>(EnableMaterialsIndex != 0);
                    PermutationVector.Set<FSkyLightTemporalResamplingRGS::FHairLighting>(HairLighting);
                    TShaderMapRef<FSkyLightTemporalResamplingRGS> RayGenerationShader(View.ShaderMap, PermutationVector);
                    OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
                }

                {
                    FSkyLightSpatialResamplingRGS::FPermutationDomain PermutationVector;
                    PermutationVector.Set<FSkyLightSpatialResamplingRGS::FEnableTwoSidedGeometryDim>(TwoSidedGeometryIndex != 0);
                    PermutationVector.Set<FSkyLightSpatialResamplingRGS::FEnableMaterialsDim>(EnableMaterialsIndex != 0);
                    PermutationVector.Set<FSkyLightSpatialResamplingRGS::FHairLighting>(HairLighting);
                    TShaderMapRef<FSkyLightSpatialResamplingRGS> RayGenerationShader(View.ShaderMap, PermutationVector);
                    OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
                }

                {
                    FSkyLightEvaluateRGS::FPermutationDomain PermutationVector;
                    PermutationVector.Set<FSkyLightEvaluateRGS::FEnableTwoSidedGeometryDim>(TwoSidedGeometryIndex != 0);
                    PermutationVector.Set<FSkyLightEvaluateRGS::FEnableMaterialsDim>(EnableMaterialsIndex != 0);
                    PermutationVector.Set<FSkyLightEvaluateRGS::FHairLighting>(HairLighting);
                    TShaderMapRef<FSkyLightEvaluateRGS> RayGenerationShader(View.ShaderMap, PermutationVector);
                    OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
                }
            }
		}
	}
}

DECLARE_GPU_STAT_NAMED(FusionSkyLighting, TEXT("FusionSkyLighting"));

void FDeferredShadingSceneRenderer::RenderFusionSkyLight(
	FRDGBuilder& GraphBuilder,
	FRDGTextureRef SceneColorTexture,
	FRDGTextureRef& OutSkyLightTexture,
	FRDGTextureRef& OutHitDistanceTexture)
    {
		FSceneTextureParameters SceneTextures = GetSceneTextureParameters(GraphBuilder, Views[0]);
		auto& View = Views[0];

		float	ResolutionFraction = FMath::Clamp(CVarRayTracingSkyLightScreenPercentage.GetValueOnRenderThread() / 100.0f, 0.25f, 1.0f);
		int32 UpscaleFactor = int32(1.0 / ResolutionFraction);
		int InitialCandidates = CVarRestirSkyLightInitialCandidates.GetValueOnRenderThread();

		{
			RDG_EVENT_SCOPE(GraphBuilder, "RestirSkyLighting");
			RDG_GPU_STAT_SCOPE(GraphBuilder, FusionSkyLighting);

			const FViewInfo& ReferenceView = Views[0];
			const bool bEnableSkylight = true, bUseMISCompensation = true;
			FPathTracingSkylight SkylightParameters;
			if( !PrepareSkyTexture(GraphBuilder, Scene, Views[0], bEnableSkylight, bUseMISCompensation, &SkylightParameters) )
			{
				OutSkyLightTexture = GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);
				OutHitDistanceTexture = GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);
				return ;
			}
			
			// const int32 SkyTiles = (Scene->SkyLight != nullptr ) ? 16 : 0;
			// FSkylightRIS SkylightRIS = BuildSkylightRISStructures(GraphBuilder, 256, SkyTiles, ReferenceView);

			FRDGTextureDesc Desc = SceneColorTexture->Desc;
			Desc.Format = PF_FloatRGBA;
			Desc.Flags &= ~(TexCreate_FastVRAM);
			Desc.Extent /= UpscaleFactor;
			OutSkyLightTexture = GraphBuilder.CreateTexture(Desc, TEXT("RayTracingSkylight"));
			auto DebugDiffuse = GraphBuilder.CreateTexture(Desc, TEXT("DebugSkylight"));

			Desc.Format = PF_G16R16;
			OutHitDistanceTexture = GraphBuilder.CreateTexture(Desc, TEXT("RayTracingSkyLightHitDistance"));
			auto DebugRayDist = GraphBuilder.CreateTexture(Desc, TEXT("DebugSkylightDist"));
			FIntPoint LightingResolution = ReferenceView.ViewRect.Size();

			const int32 RequestedReservoirs = CVarRestirSkyLightNumReservoirs.GetValueOnAnyThread();
			const int32 NumReservoirs = FMath::Max(RequestedReservoirs, 1);
			FIntPoint PaddedSize = Desc.Extent;

			FIntVector ReservoirBufferDim = FIntVector(PaddedSize.X, PaddedSize.Y, NumReservoirs + 1);
			FRDGBufferDesc ReservoirDesc = FRDGBufferDesc::CreateStructuredDesc(16, ReservoirBufferDim.X * ReservoirBufferDim.Y * ReservoirBufferDim.Z);

			FRDGBufferRef LightReservoirs = GraphBuilder.CreateBuffer(ReservoirDesc, TEXT("SkyLightReservoirs"));
			
			FIntVector ReservoirHistoryBufferDim = FIntVector(PaddedSize.X, PaddedSize.Y, NumReservoirs);
			FRDGBufferDesc ReservoirHistoryDesc = FRDGBufferDesc::CreateStructuredDesc(16, ReservoirHistoryBufferDim.X * ReservoirHistoryBufferDim.Y * ReservoirHistoryBufferDim.Z);
			FRDGBufferRef LightReservoirsHistory = GraphBuilder.CreateBuffer(ReservoirHistoryDesc, TEXT("SkyLightReservoirsHistory"));

			FRestirSkyLightCommonParameters CommonParameters;
			CommonParameters.MaxNormalBias = GetRaytracingMaxNormalBias();
			CommonParameters.TLAS = View.GetRayTracingSceneViewChecked();
			CommonParameters.RWLightReservoirUAV = GraphBuilder.CreateUAV(LightReservoirs);
			CommonParameters.ReservoirBufferDim = ReservoirBufferDim;
			CommonParameters.MaxTemporalHistory = FMath::Max(1, CVarRestirSkyLightTemporalMaxHistory.GetValueOnRenderThread());
			CommonParameters.SkyLightParameters = SkylightParameters;
			CommonParameters.SkyLightMaxRayDistance = GRayTracingSkyLightMaxRayDistance;
			CommonParameters.bSkyLightTransmission = Scene->SkyLight->bTransmission;
			CommonParameters.SkyLightMaxShadowThickness = GRayTracingSkyLightMaxShadowThickness;
			CommonParameters.RWDebugDiffuseUAV = GraphBuilder.CreateUAV(DebugDiffuse);
			CommonParameters.RWDebugRayDistanceUAV = GraphBuilder.CreateUAV(OutHitDistanceTexture);
		
			// CommonParameters.RISSkylightBuffer = GraphBuilder.CreateSRV(SkylightRIS.RISBuffer, PF_R32G32_UINT);
			// CommonParameters.RISSkylightBufferTiles = SkyTiles;
			// CommonParameters.RISSkylightBufferTileSize = 256;
			// CommonParameters.InvSkylightSize = SkylightRIS.InvSize;
			// CommonParameters.SkylightTexture = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(SkylightRIS.EnvTexture));
			// CommonParameters.SkylightTextureSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();

			const bool bCameraCut = !ReferenceView.PrevViewInfo.RestirSkyLightHistory.LightReservoirs.IsValid() || ReferenceView.bCameraCut;
			const int32 PrevHistoryCount = ReferenceView.PrevViewInfo.RestirSkyLightHistory.ReservoirDimensions.Z;

			int32 InitialSlice = 0;
			const bool bUseHairLighting = false;
			for (int32 Reservoir = 0; Reservoir < NumReservoirs; Reservoir++)
			{
				{
					FSkyLightInitialSamplesRGS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSkyLightInitialSamplesRGS::FParameters>();

					PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
					PassParameters->SceneTextures = SceneTextures; //SceneTextures;
					PassParameters->SSProfilesTexture = View.RayTracingSubSurfaceProfileTexture;
					PassParameters->OutputSlice = Reservoir;
					PassParameters->HistoryReservoir = Reservoir;
					PassParameters->InitialCandidates = FMath::Max(1, InitialCandidates);
					PassParameters->InitialSampleVisibility = CVarRestirSkyLightTestInitialVisibility.GetValueOnRenderThread();

					PassParameters->RestirSkyLightCommonParameters = CommonParameters;
					FSkyLightInitialSamplesRGS::FPermutationDomain PermutationVector;
					PermutationVector.Set<FSkyLightInitialSamplesRGS::FEnableTwoSidedGeometryDim>(CVarRayTracingSkyLightEnableTwoSidedGeometry.GetValueOnRenderThread() != 0);
					PermutationVector.Set<FSkyLightInitialSamplesRGS::FEnableMaterialsDim>(CVarRayTracingSkyLightEnableMaterials.GetValueOnRenderThread() != 0);
					PermutationVector.Set<FSkyLightInitialSamplesRGS::FHairLighting>(bUseHairLighting ? 1 : 0);

					TShaderMapRef<FSkyLightInitialSamplesRGS> RayGenShader(GetGlobalShaderMap(ERHIFeatureLevel::SM5), PermutationVector);
					ClearUnusedGraphResources(RayGenShader, PassParameters);

					GraphBuilder.AddPass(
						RDG_EVENT_NAME("CreateInitialSamples"),
						PassParameters,
						ERDGPassFlags::Compute,
						[PassParameters, this, &View, RayGenShader, LightingResolution](FRHIRayTracingCommandList& RHICmdList)
					{
						FRayTracingShaderBindingsWriter GlobalResources;
						SetShaderParameters(GlobalResources, RayGenShader, *PassParameters);

						FRHIRayTracingScene* RayTracingSceneRHI = View.GetRayTracingSceneChecked();
						RHICmdList.RayTraceDispatch(View.RayTracingMaterialPipeline, RayGenShader.GetRayTracingShader(), RayTracingSceneRHI, GlobalResources, LightingResolution.X, LightingResolution.Y);
					});
				}

				// Temporal candidate merge pass, optionally merged with initial candidate pass
				if (CVarRestirSkyLightTemporal.GetValueOnRenderThread() != 0 && !bCameraCut && Reservoir < PrevHistoryCount)
				{
					{
						FSkyLightTemporalResamplingRGS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSkyLightTemporalResamplingRGS::FParameters>();

						PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
						PassParameters->SceneTextures = SceneTextures; //SceneTextures;
						PassParameters->SSProfilesTexture = View.RayTracingSubSurfaceProfileTexture;

						PassParameters->ReservoirHistoryBufferDim = ReservoirHistoryBufferDim;
						PassParameters->InputSlice = Reservoir;
						PassParameters->OutputSlice = Reservoir;
						PassParameters->HistoryReservoir = Reservoir;
						PassParameters->TemporalDepthRejectionThreshold = FMath::Clamp(CVarRestirSkyLightTemporalDepthRejectionThreshold.GetValueOnRenderThread(), 0.0f, 1.0f);
						PassParameters->TemporalNormalRejectionThreshold = FMath::Clamp(CVarRestirSkyLightTemporalNormalRejectionThreshold.GetValueOnRenderThread(), -1.0f, 1.0f);
						PassParameters->ApplyApproximateVisibilityTest = CVarRestirSkyLightTemporalApplyApproxVisibility.GetValueOnAnyThread();
						PassParameters->InitialCandidates = FMath::Max(1, InitialCandidates);
						PassParameters->InitialSampleVisibility = CVarRestirSkyLightTestInitialVisibility.GetValueOnRenderThread();

						PassParameters->SpatiallyHashTemporalReprojection = FMath::Clamp(CVarRestirSkyLightTemporalApplySpatialHash.GetValueOnRenderThread(), 0, 1);

						PassParameters->LightReservoirHistory = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(ReferenceView.PrevViewInfo.RestirSkyLightHistory.LightReservoirs));
						PassParameters->NormalHistory = RegisterExternalTextureWithFallback(GraphBuilder, ReferenceView.PrevViewInfo.GBufferA, GSystemTextures.BlackDummy);
						PassParameters->DepthHistory = RegisterExternalTextureWithFallback(GraphBuilder, ReferenceView.PrevViewInfo.DepthBuffer, GSystemTextures.BlackDummy);

						PassParameters->RestirSkyLightCommonParameters = CommonParameters;

						FSkyLightTemporalResamplingRGS::FPermutationDomain PermutationVector;
						PermutationVector.Set<FSkyLightTemporalResamplingRGS::FEnableTwoSidedGeometryDim>(CVarRayTracingSkyLightEnableTwoSidedGeometry.GetValueOnRenderThread() != 0);
						PermutationVector.Set<FSkyLightTemporalResamplingRGS::FEnableMaterialsDim>(CVarRayTracingSkyLightEnableMaterials.GetValueOnRenderThread() != 0);
						PermutationVector.Set<FSkyLightTemporalResamplingRGS::FHairLighting>(bUseHairLighting ? 1 : 0);
						TShaderMapRef<FSkyLightTemporalResamplingRGS> RayGenShader(GetGlobalShaderMap(ERHIFeatureLevel::SM5), PermutationVector);

						ClearUnusedGraphResources(RayGenShader, PassParameters);

						GraphBuilder.AddPass(
							RDG_EVENT_NAME("%sTemporalResample", TEXT("FusedInitialCandidateAnd") ),
							PassParameters,
							ERDGPassFlags::Compute,
							[PassParameters, this, &View, RayGenShader, LightingResolution](FRHIRayTracingCommandList& RHICmdList)
						{
							FRayTracingShaderBindingsWriter GlobalResources;
							SetShaderParameters(GlobalResources, RayGenShader, *PassParameters);

							FRHIRayTracingScene* RayTracingSceneRHI = View.GetRayTracingSceneChecked();
							RHICmdList.RayTraceDispatch(View.RayTracingMaterialPipeline, RayGenShader.GetRayTracingShader(), RayTracingSceneRHI, GlobalResources, LightingResolution.X, LightingResolution.Y);

						});
					}
					// Boiling filter pass to prevent runaway samples
					if (CVarRestirSkyLightApplyBoilingFilter.GetValueOnRenderThread() != 0)
					{
						FSkyLightBoilingFilterCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSkyLightBoilingFilterCS::FParameters>();

						PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;

						PassParameters->RWLightReservoirUAV = GraphBuilder.CreateUAV(LightReservoirs);
						PassParameters->ReservoirBufferDim = ReservoirBufferDim;
						PassParameters->InputSlice = Reservoir;
						PassParameters->OutputSlice = Reservoir;
						PassParameters->BoilingFilterStrength = FMath::Clamp(CVarRestirSkyLightBoilingFilterStrength.GetValueOnRenderThread(), 0.00001f, 1.0f);

						auto ComputeShader = View.ShaderMap->GetShader<FSkyLightBoilingFilterCS>();

						ClearUnusedGraphResources(ComputeShader, PassParameters);
						FIntPoint GridSize = FMath::DivideAndRoundUp<FIntPoint>(View.ViewRect.Size(), 16);

						FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("BoilingFilter"), ComputeShader, PassParameters, FIntVector(GridSize.X, GridSize.Y, 1));
					}
				}
			}
			// Spatial resampling passes, one per reservoir
			for (int32 Reservoir = NumReservoirs; Reservoir > 0; Reservoir--)
			{
				if (CVarRestirSkyLightSpatial.GetValueOnRenderThread() != 0)
				{
					FSkyLightSpatialResamplingRGS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSkyLightSpatialResamplingRGS::FParameters>();

					PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
					PassParameters->SceneTextures = SceneTextures; //SceneTextures;
					PassParameters->SSProfilesTexture = View.RayTracingSubSurfaceProfileTexture;

					PassParameters->InputSlice = Reservoir - 1;
					PassParameters->OutputSlice = Reservoir;
					PassParameters->HistoryReservoir = Reservoir - 1;
					PassParameters->SpatialSamples = FMath::Max(CVarRestirSkyLightSpatialSamples.GetValueOnRenderThread(), 1);
					PassParameters->SpatialSamplesBoost = FMath::Max(CVarRestirSkyLightSpatialSamplesBoost.GetValueOnRenderThread(), 1);
					PassParameters->SpatialSamplingRadius = FMath::Max(1.0f, CVarRestirSkyLightSpatialSamplingRadius.GetValueOnRenderThread());
					PassParameters->SpatialDepthRejectionThreshold = FMath::Clamp(CVarRestirSkyLightSpatialDepthRejectionThreshold.GetValueOnRenderThread(), 0.0f, 1.0f);
					PassParameters->SpatialNormalRejectionThreshold = FMath::Clamp(CVarRestirSkyLightSpatialNormalRejectionThreshold.GetValueOnRenderThread(), -1.0f, 1.0f);
					PassParameters->ApplyApproximateVisibilityTest = CVarRestirSkyLightSpatialApplyApproxVisibility.GetValueOnRenderThread();
					PassParameters->DiscountNaiveSamples = CVarRestirSkyLightSpatialDiscountNaiveSamples.GetValueOnRenderThread();

					PassParameters->NeighborOffsetMask = GDiscSampleBuffer.NumSamples - 1;
					PassParameters->NeighborOffsets = GDiscSampleBuffer.DiscSampleBufferSRV;
					PassParameters->RestirSkyLightCommonParameters = CommonParameters;

					FSkyLightSpatialResamplingRGS::FPermutationDomain PermutationVector;
					PermutationVector.Set<FSkyLightSpatialResamplingRGS::FEnableTwoSidedGeometryDim>(CVarRayTracingSkyLightEnableTwoSidedGeometry.GetValueOnRenderThread() != 0);
						PermutationVector.Set<FSkyLightSpatialResamplingRGS::FEnableMaterialsDim>(CVarRayTracingSkyLightEnableMaterials.GetValueOnRenderThread() != 0);
						PermutationVector.Set<FSkyLightSpatialResamplingRGS::FHairLighting>(bUseHairLighting ? 1 : 0);
					TShaderMapRef<FSkyLightSpatialResamplingRGS> RayGenShader(GetGlobalShaderMap(ERHIFeatureLevel::SM5), PermutationVector);
					ClearUnusedGraphResources(RayGenShader, PassParameters);

					GraphBuilder.AddPass(
						RDG_EVENT_NAME("SpatialResample"),
						PassParameters,
						ERDGPassFlags::Compute,
						[PassParameters, this, &View, RayGenShader, LightingResolution](FRHIRayTracingCommandList& RHICmdList)
					{
						FRayTracingShaderBindingsWriter GlobalResources;
						SetShaderParameters(GlobalResources, RayGenShader, *PassParameters);

						FRHIRayTracingScene* RayTracingSceneRHI = View.GetRayTracingSceneChecked();
						RHICmdList.RayTraceDispatch(View.RayTracingMaterialPipeline, RayGenShader.GetRayTracingShader(), RayTracingSceneRHI, GlobalResources, LightingResolution.X, LightingResolution.Y);

					});
					InitialSlice = Reservoir;
				}
			}

			//Shading evaluation pass
			{

				FSkyLightEvaluateRGS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSkyLightEvaluateRGS::FParameters>();

				PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
				PassParameters->SceneTextures = SceneTextures; //SceneTextures;
				PassParameters->SSProfilesTexture = View.RayTracingSubSurfaceProfileTexture;
				PassParameters->RWDiffuseUAV = GraphBuilder.CreateUAV(OutSkyLightTexture);
				PassParameters->RWRayDistanceUAV = GraphBuilder.CreateUAV(OutHitDistanceTexture);
				PassParameters->ReservoirHistoryBufferDim = ReservoirHistoryBufferDim;
				PassParameters->RWLightReservoirHistoryUAV = GraphBuilder.CreateUAV(LightReservoirsHistory);
				PassParameters->InputSlice = InitialSlice;
				PassParameters->NumReservoirs = NumReservoirs;
				PassParameters->FeedbackVisibility = CVarRestirSkyLightFeedbackVisibility.GetValueOnRenderThread();

				// if (bUseHairLighting)
				// {
				//     const bool bUseHairVoxel = CVarRestirSkyLightEnableHairVoxel.GetValueOnRenderThread() > 0;
				//     PassParameters->bUseHairVoxel = (bUseHairDeepShadow && bUseHairVoxel) ? 1 : 0;
					
				//     //EHartNV ToDo: change to strand data
				//     // old PassParameters->HairCategorizationTexture = HairResources.CategorizationTexture;
				//     // new? PassParameters->HairStrands = HairStrands::BindHairStrandsViewUniformParameters(View);
				//     PassParameters->HairLightChannelMaskTexture = View.HairStrandsViewData.VisibilityData.LightChannelMaskTexture;
				//     PassParameters->VirtualVoxel = HairStrands::BindHairStrandsVoxelUniformParameters(View);
				// }

				PassParameters->RestirSkyLightCommonParameters = CommonParameters;
				FSkyLightEvaluateRGS::FPermutationDomain PermutationVector;
				PermutationVector.Set<FSkyLightEvaluateRGS::FEnableTwoSidedGeometryDim>(CVarRayTracingSkyLightEnableTwoSidedGeometry.GetValueOnRenderThread() != 0);
				PermutationVector.Set<FSkyLightEvaluateRGS::FEnableMaterialsDim>(CVarRayTracingSkyLightEnableMaterials.GetValueOnRenderThread() != 0);
				PermutationVector.Set<FSkyLightEvaluateRGS::FHairLighting>(bUseHairLighting ? 1 : 0);
				TShaderMapRef<FSkyLightEvaluateRGS> RayGenShader(GetGlobalShaderMap(ERHIFeatureLevel::SM5), PermutationVector);

				ClearUnusedGraphResources(RayGenShader, PassParameters);

				GraphBuilder.AddPass(
					RDG_EVENT_NAME("ShadeSamples"),
					PassParameters,
					ERDGPassFlags::Compute,
					[PassParameters, this, &View, RayGenShader, LightingResolution](FRHIRayTracingCommandList& RHICmdList)
				{
					FRayTracingShaderBindingsWriter GlobalResources;
					SetShaderParameters(GlobalResources, RayGenShader, *PassParameters);

					FRHIRayTracingScene* RayTracingSceneRHI = View.GetRayTracingSceneChecked();
					RHICmdList.RayTraceDispatch(View.RayTracingMaterialPipeline, RayGenShader.GetRayTracingShader(), RayTracingSceneRHI, GlobalResources, LightingResolution.X, LightingResolution.Y);
				});
			}

			if( !ReferenceView.bStatePrevViewInfoIsReadOnly)
			{
				//Extract history feedback here
				GraphBuilder.QueueBufferExtraction(LightReservoirsHistory, &ReferenceView.ViewState->PrevFrameViewInfo.RestirSkyLightHistory.LightReservoirs);

				//Extract scene textures as each effect potentially using them most do so to ensure it happens
				GraphBuilder.QueueTextureExtraction(SceneTextures.GBufferATexture, &ReferenceView.ViewState->PrevFrameViewInfo.GBufferA);
				GraphBuilder.QueueTextureExtraction(SceneTextures.SceneDepthTexture, &ReferenceView.ViewState->PrevFrameViewInfo.DepthBuffer);

				ReferenceView.ViewState->PrevFrameViewInfo.RestirSkyLightHistory.ReservoirDimensions = ReservoirHistoryBufferDim;
			}
		}
        //Denoise
        {
            const IScreenSpaceDenoiser* DefaultDenoiser = IScreenSpaceDenoiser::GetDefaultDenoiser();
            const IScreenSpaceDenoiser* DenoiserToUse = CVarRestirSkyLightEnableSkyDenoiser.GetValueOnRenderThread() ? FFusionDenoiser::GetDenoiser() : DefaultDenoiser;// GRayTracingGlobalIlluminationDenoiser == 1 ? DefaultDenoiser : GScreenSpaceDenoiser;

            IScreenSpaceDenoiser::FDiffuseIndirectInputs DenoiserInputs;
            DenoiserInputs.Color = OutSkyLightTexture;
            DenoiserInputs.RayHitDistance = OutHitDistanceTexture;
            {
                IScreenSpaceDenoiser::FAmbientOcclusionRayTracingConfig RayTracingConfig;
                RayTracingConfig.ResolutionFraction = ResolutionFraction;
                RayTracingConfig.RayCountPerPixel = InitialCandidates;

                RDG_EVENT_SCOPE(GraphBuilder, "%s%s(SkyLight) %dx%d",
                    DenoiserToUse != DefaultDenoiser ? TEXT("ThirdParty ") : TEXT(""),
                    DenoiserToUse->GetDebugName(),
                    View.ViewRect.Width(), View.ViewRect.Height());

                IScreenSpaceDenoiser::FDiffuseIndirectOutputs DenoiserOutputs = DenoiserToUse->DenoiseSkyLight(
                    GraphBuilder,
                    View,
                    &View.PrevViewInfo,
                    SceneTextures,
                    DenoiserInputs,
                    RayTracingConfig);

                OutSkyLightTexture = DenoiserOutputs.Color;
            }
        }
    }
#endif