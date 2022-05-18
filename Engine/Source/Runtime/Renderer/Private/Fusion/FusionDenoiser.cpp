#include "FusionDenoiser.h"
#include "SceneTextureParameters.h"
#include "ScenePrivate.h"

#include "StaticBoundShaderState.h"
#include "SceneUtils.h"
#include "PostProcess/SceneRenderTargets.h"
#include "SceneRenderTargetParameters.h"
#include "ScenePrivate.h"
#include "ClearQuad.h"
#include "PipelineStateCache.h"
#include "SceneTextureParameters.h"
#include "BlueNoise.h"
#include "PostProcess/PostProcessing.h"

#define GIDENOISE_VAR(type, name, value, comment) \
	static type GRayTracingGIDenoise##name = value;\
	static FAutoConsoleVariableRef CVarRayTracingGIDenoise##name(\
		TEXT("r.Fusion.GIDenoise."#name),\
		GRayTracingGIDenoise##name,\
		TEXT(comment));

#define GET_GIDENOISE_CMD_VAR(Name) (GRayTracingGIDenoise##Name)
#define GET_GIDENOISE_VAR(Name) ( GRayTracingGIDenoise##Name )

GIDENOISE_VAR(int32, EnableTemporal, 1, "Denoise")
GIDENOISE_VAR(float, TemporalBlendWeight, 0.02, "Temporal Blend Weight")
GIDENOISE_VAR(float, TemporalMomentBlendWeight, 0.1, "Temporal Moment BlendWeight")
GIDENOISE_VAR(float, TemporalColorTolerance, 50, "Temporal Color Tolerance")
GIDENOISE_VAR(float, TemporalNormalTolerance, 0.5, "Temporal Normal Tolerance")
GIDENOISE_VAR(float, TemporalDepthTolerance, 1, "Temporal Depth Tolerance")
GIDENOISE_VAR(float, ColorClamp, 5, "Color Clamp")
GIDENOISE_VAR(int32, HistoryLength, 32, "History Length")

GIDENOISE_VAR(int32, EnableSpatial, 1, "Enable Spatial")
GIDENOISE_VAR(float, SpatialBlendWeight, 0.9, "Temporal filter strength")
GIDENOISE_VAR(float, SpatialBaseRadius, 15, "Temporal Color Tolerance")

GIDENOISE_VAR(int32, EnableATrous, 1, "Enable ATrous")
GIDENOISE_VAR(int32, SpatialFilterType, 1, "Spatial Filter Type")
GIDENOISE_VAR(int32, ATrousIteration, 6, "Spatial Filter Iteration")
GIDENOISE_VAR(int32, ATrousCameraSwitchIteration, 1, "Additional Iteration when camera is switched")
GIDENOISE_VAR(int32, ATrousCopyIteration, 1, "Spatial Filter Copy Iteration")
GIDENOISE_VAR(int32, ATrousSampleDepthAsNormal, 0, "Calculate normal from depth texture")
GIDENOISE_VAR(float, ATrousFilterWidth, 2.0, "Spatial Filter Width")
GIDENOISE_VAR(float, ATrousVarianceGain, 1, "Spatial Filter Variance Gain")
GIDENOISE_VAR(float, ATrousNormalTolerance,1.0, "Spatial Filter Normal Tolerance")
GIDENOISE_VAR(float, ATrousDepthTolerance, 1, "Spatial Filter Depth Tolerance")
GIDENOISE_VAR(float, ATrousAOTolerance, 1, "Spatial Filter AO Tolerance")
GIDENOISE_VAR(float, DiffuseBoost, 1, "Multiplier for diffuse GI")
GIDENOISE_VAR(float, SHSharpness, 2, "Normal sharpness for SH mode")

GIDENOISE_VAR(int32, DebugType, 0, "Debug Type(0=disabled; 1=variance; 2=1st moment; 3=2nd moment; 4=history; 5=motion vector; 6=hit distance)")

DECLARE_GPU_STAT_NAMED(FusionDiffuseDenoiser, TEXT("FusionGI Denoiser"));

class FDenoiseTemporalFilterCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FDenoiseTemporalFilterCS);
	SHADER_USE_PARAMETER_STRUCT(FDenoiseTemporalFilterCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FMatrix44f, ReprojectionMatrix)
		SHADER_PARAMETER(FMatrix44f, InverseProjectionMatrixThis)
		SHADER_PARAMETER(FMatrix44f, InverseProjectionMatrixLast)
		SHADER_PARAMETER(FIntPoint, GBufferDim)
		SHADER_PARAMETER(FIntPoint, DenoiseDim)
		SHADER_PARAMETER(FIntPoint, UpscaleFactorBits)
		SHADER_PARAMETER(float, BlendWeight)
		SHADER_PARAMETER(float, MomentBlendWeight)
		SHADER_PARAMETER(float, ColorKernel)
		SHADER_PARAMETER(float, NormalKernel)
		SHADER_PARAMETER(float, DepthKernel)
		SHADER_PARAMETER(float, ColorClamp)
		SHADER_PARAMETER(int, Enable)
		SHADER_PARAMETER(int, UseSH)
		SHADER_PARAMETER(int, HistoryLength)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, DepthTextureThis)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, DepthTextureLast)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, NormalTextureThis)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, NormalTextureLast)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, ColorInput)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, DistanceInput)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<uint4>, ColorLast)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, MomentLast)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, ColorThis)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, MomentThis)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearSampler)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};
IMPLEMENT_GLOBAL_SHADER(FDenoiseTemporalFilterCS, "/Engine/Private/FusionDenoiser/RayTracingGIDenoiseTemporalFilter.usf", "TemporalFilter_CS", SF_Compute);

class FDenoiseSpatialFilterCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FDenoiseSpatialFilterCS);
	SHADER_USE_PARAMETER_STRUCT(FDenoiseSpatialFilterCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FMatrix44f, InverseWVPMatrix)
		SHADER_PARAMETER(FMatrix44f, WVPMatrix)
		SHADER_PARAMETER(FIntPoint, GBufferDim)
		SHADER_PARAMETER(FIntPoint, DenoiseDim)
		SHADER_PARAMETER(FIntPoint, UpscaleFactorBits)
		SHADER_PARAMETER(float, BlendWeight)
		SHADER_PARAMETER(float, MomentBlendWeight)
		SHADER_PARAMETER(float, BaseRadius)
		SHADER_PARAMETER(float, NormalKernel)
		SHADER_PARAMETER(float, DepthKernel)
		SHADER_PARAMETER(float, ColorKernel)
		SHADER_PARAMETER(float, AOKernel)
		SHADER_PARAMETER(float, RandomRotation)
		SHADER_PARAMETER(int, Enable)
		SHADER_PARAMETER(int, UseSH)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, DepthTextureThis)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, NormalTextureThis)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, InputMoment)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<uint4>, InputColor)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint4>, OutputColor)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearSampler)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};
IMPLEMENT_GLOBAL_SHADER(FDenoiseSpatialFilterCS, "/Engine/Private/FusionDenoiser/RayTracingGIDenoiseSpatialFilter.usf", "SpatialFilter_CS", SF_Compute);

class FDenoiseSpatialATrousFilterCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FDenoiseSpatialATrousFilterCS);
	SHADER_USE_PARAMETER_STRUCT(FDenoiseSpatialATrousFilterCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FMatrix44f, InverseWVPMatrix)
		SHADER_PARAMETER(FMatrix44f, WVPMatrix)
		SHADER_PARAMETER(FMatrix44f, InverseProjectionMatrix)
		SHADER_PARAMETER(FIntPoint, GBufferDim)
		SHADER_PARAMETER(FIntPoint, DenoiseDim)
		SHADER_PARAMETER(FIntPoint, UpscaleFactorBits)
		SHADER_PARAMETER(float, NormalKernel)
		SHADER_PARAMETER(float, VarianceGain)
		SHADER_PARAMETER(float, DepthKernel)
		SHADER_PARAMETER(float, AOKernel)
		SHADER_PARAMETER(float, RandomRotation)
		SHADER_PARAMETER(int, Enable)
		SHADER_PARAMETER(int, UseSH)
		SHADER_PARAMETER(int, Step)
		SHADER_PARAMETER(int, FilterType)
		SHADER_PARAMETER(float, FilterWidth)
		SHADER_PARAMETER(int, SampleDepthAsNormal)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, DepthTextureThis)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, NormalTextureThis)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<uint4>, InputColor)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, InputMoment)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint4>, OutputColor)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearSampler)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};
IMPLEMENT_GLOBAL_SHADER(FDenoiseSpatialATrousFilterCS, "/Engine/Private/FusionDenoiser/RayTracingGIDenoiseSpatialATrousFilter.usf", "AtrousFilter_CS", SF_Compute);

class FCompositeDenoisePS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FCompositeDenoisePS);
	SHADER_USE_PARAMETER_STRUCT(FCompositeDenoisePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FMatrix44f, InverseProjectionMatrix)
		SHADER_PARAMETER(FVector4f, ViewportInfo)
		SHADER_PARAMETER(FIntPoint, UpscaleFactorBits)
		SHADER_PARAMETER(FIntPoint, GBufferDim)
		SHADER_PARAMETER(FIntPoint, DenoiseDim)
		SHADER_PARAMETER(float, DenoiseBufferScale)
		SHADER_PARAMETER(float, DiffuseBoost)
		SHADER_PARAMETER(float, SHSharpness)
		SHADER_PARAMETER(int, DebugMode)
		SHADER_PARAMETER(int, UseSH)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, DepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, NormalTexture)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, AlbedoTexture)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<uint4>, DenoiseTexture)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, MomentTexture)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};
IMPLEMENT_GLOBAL_SHADER(FCompositeDenoisePS, "/Engine/Private/FusionDenoiser/RayTracingGIDenoiseComposite.usf", "Composite_PS", SF_Pixel);


FFusionDenoiser::FFusionDenoiser(const IScreenSpaceDenoiser* InWrappedDenoiser)
	: WrappedDenoiser(InWrappedDenoiser)
{
	check(WrappedDenoiser);
}

IScreenSpaceDenoiser::EShadowRequirements FFusionDenoiser::GetShadowRequirements(const FViewInfo& View, const FLightSceneInfo& LightSceneInfo, const FShadowRayTracingConfig& RayTracingConfig) const
{
	return WrappedDenoiser->GetShadowRequirements(View, LightSceneInfo, RayTracingConfig);
}

void FFusionDenoiser::DenoiseShadowVisibilityMasks(FRDGBuilder& GraphBuilder, const FViewInfo& View, FPreviousViewInfo* PreviousViewInfos, const FSceneTextureParameters& SceneTextures, const TStaticArray<FShadowVisibilityParameters, IScreenSpaceDenoiser::kMaxBatchSize>& InputParameters, const int32 InputParameterCount, TStaticArray<FShadowVisibilityOutputs, IScreenSpaceDenoiser::kMaxBatchSize>& Outputs) const
{
	WrappedDenoiser->DenoiseShadowVisibilityMasks(GraphBuilder, View, PreviousViewInfos, SceneTextures, InputParameters, InputParameterCount, Outputs);
}

IScreenSpaceDenoiser::FPolychromaticPenumbraOutputs FFusionDenoiser::DenoisePolychromaticPenumbraHarmonics(FRDGBuilder& GraphBuilder, const FViewInfo& View, FPreviousViewInfo* PreviousViewInfos, const FSceneTextureParameters& SceneTextures, const FPolychromaticPenumbraHarmonics& Inputs) const
{
	return WrappedDenoiser->DenoisePolychromaticPenumbraHarmonics(GraphBuilder, View, PreviousViewInfos, SceneTextures, Inputs);
}

IScreenSpaceDenoiser::FReflectionsOutputs FFusionDenoiser::DenoiseReflections(FRDGBuilder& GraphBuilder, const FViewInfo& View, FPreviousViewInfo* PreviousViewInfos, const FSceneTextureParameters& SceneTextures, const FReflectionsInputs& Inputs, const FReflectionsRayTracingConfig Config) const
{ 
	return WrappedDenoiser->DenoiseReflections(GraphBuilder, View, PreviousViewInfos, SceneTextures, Inputs, Config);
}

IScreenSpaceDenoiser::FReflectionsOutputs FFusionDenoiser::DenoiseWaterReflections(FRDGBuilder& GraphBuilder, const FViewInfo& View, FPreviousViewInfo* PreviousViewInfos, const FSceneTextureParameters& SceneTextures, const FReflectionsInputs& Inputs, const FReflectionsRayTracingConfig Config) const
{
	return WrappedDenoiser->DenoiseWaterReflections(GraphBuilder, View, PreviousViewInfos, SceneTextures, Inputs, Config);
}

IScreenSpaceDenoiser::FAmbientOcclusionOutputs FFusionDenoiser::DenoiseAmbientOcclusion(FRDGBuilder& GraphBuilder, const FViewInfo& View, FPreviousViewInfo* PreviousViewInfos, const FSceneTextureParameters& SceneTextures, const FAmbientOcclusionInputs& Inputs, const FAmbientOcclusionRayTracingConfig Config) const
{
	return WrappedDenoiser->DenoiseAmbientOcclusion(GraphBuilder, View, PreviousViewInfos, SceneTextures, Inputs, Config);
}

FSSDSignalTextures FFusionDenoiser::DenoiseDiffuseIndirect(FRDGBuilder& GraphBuilder, const FViewInfo& View, FPreviousViewInfo* PreviousViewInfos, const FSceneTextureParameters& SceneTextures, const FDiffuseIndirectInputs& Inputs, const FAmbientOcclusionRayTracingConfig Config) const
#if RHI_RAYTRACING
{
    // The new denoiser is a modified version of SVGF denoiser
    // It has following steps:
    // 1. Temporal reprojection and accumulation
    // 2. Determine variance value
    // 3. Spatial filtering
    // 4. Composite
    // For SH denoising, we perform filtering on SH coefs and finally reconstruct incoming radiance in composite pass.
    RDG_GPU_STAT_SCOPE(GraphBuilder, FusionDiffuseDenoiser);
	RDG_EVENT_SCOPE(GraphBuilder, "FusionDiffuseDenoiser");

    FRHICommandListImmediate& RHICmdList = GraphBuilder.RHICmdList;

    FRDGTextureRef SceneDepthTexture = SceneTextures.SceneDepthTexture;
    FRDGTextureRef SceneNormalTexture = SceneTextures.GBufferATexture;
    FRDGTextureRef SceneAlbedoTexture = SceneTextures.GBufferCTexture;
    FRDGTextureRef DepthTexLast = RegisterExternalTextureWithFallback(GraphBuilder, View.PrevViewInfo.DepthBuffer, GSystemTextures.BlackDummy);
    FRDGTextureRef NormalTexLast = RegisterExternalTextureWithFallback(GraphBuilder, View.PrevViewInfo.GBufferA, GSystemTextures.BlackDummy);

    FIntPoint GBufferRes = View.ViewRect.Size();
    FIntPoint UpscaleFactor(1.0f / Config.ResolutionFraction, 1.0f / Config.ResolutionFraction);
    FIntPoint UpscaleFactorBits(FMath::FloorLog2(UpscaleFactor.X), FMath::FloorLog2(UpscaleFactor.Y));
    FIntPoint DenoiseBufferRes = FIntPoint::DivideAndRoundUp(GBufferRes, FIntPoint(1 << UpscaleFactorBits.X, 1 << UpscaleFactorBits.Y));
    bool UseSH = Config.UseSphericalHarmonicsGI;
    float BufferScale = float(DenoiseBufferRes.X) / GBufferRes.X;
    FRDGTextureRef DenoiseIntensity[2];
    const TCHAR* DenoiseTextureNames[] = { TEXT("DenoiseIntensity0"), TEXT("DenoiseIntensity1")};

    if (!View.State ||
        ((FSceneViewState*)View.State)->DenoiseTexture[0].GetReference() == nullptr ||
        ((FSceneViewState*)View.State)->DenoiseTexture[0]->GetDesc().Extent != DenoiseBufferRes)
    {
        FRDGTextureDesc RTDesc = FRDGTextureDesc::Create2D(
            DenoiseBufferRes,
            PF_R32G32B32A32_UINT,
            FClearValueBinding::Black,
            TexCreate_ShaderResource | TexCreate_UAV | TexCreate_RenderTargetable);
        DenoiseIntensity[0] = GraphBuilder.CreateTexture(RTDesc, DenoiseTextureNames[0], ERDGTextureFlags::MultiFrame);
		AddClearRenderTargetPass(GraphBuilder, DenoiseIntensity[0], FVector4f(0, 0, 0, 0));

        RTDesc.Format = PF_FloatRGBA;
        DenoiseIntensity[1] = GraphBuilder.CreateTexture(RTDesc, DenoiseTextureNames[1], ERDGTextureFlags::MultiFrame);
		AddClearRenderTargetPass(GraphBuilder, DenoiseIntensity[1], FVector4f(0, 0, 0, 0));
    }
    else
    {
        DenoiseIntensity[0] = GraphBuilder.RegisterExternalTexture(((FSceneViewState*)View.State)->DenoiseTexture[0], DenoiseTextureNames[0]);
        DenoiseIntensity[1] = GraphBuilder.RegisterExternalTexture(((FSceneViewState*)View.State)->DenoiseTexture[1], DenoiseTextureNames[1]);
    }
    int FrameCounter = ((FSceneViewState*)View.State)->FrameIndex;
    FRDGTextureRef ColorLast = DenoiseIntensity[0];
    FRDGTextureRef MomentLast = DenoiseIntensity[1];

    FRDGTextureDesc RTDesc = FRDGTextureDesc::Create2D(
        DenoiseBufferRes,
        PF_R32G32B32A32_UINT,
        FClearValueBinding::Black,
        TexCreate_ShaderResource | TexCreate_UAV | TexCreate_RenderTargetable);

    FRDGTextureRef ColorThis = GraphBuilder.CreateTexture(RTDesc, TEXT("ColorThis"));
    RTDesc.Format = PF_FloatRGBA;
    FRDGTextureRef MomentThis = GraphBuilder.CreateTexture(RTDesc, TEXT("MomentThis"));


    FRDGTextureDesc OutputDesc = FRDGTextureDesc::Create2D(
        SceneTextures.SceneDepthTexture->Desc.Extent,
        PF_FloatRGBA,
        FClearValueBinding::Black,
        TexCreate_ShaderResource | TexCreate_UAV | TexCreate_RenderTargetable);

    FSSDSignalTextures SignalOutput;
    SignalOutput.Textures[0] = GraphBuilder.CreateTexture(OutputDesc, TEXT("DenoisedTexture"));
    SignalOutput.Textures[1] = Inputs.AmbientOcclusionMask;

    {
        // Temporal filtering
        TShaderMapRef<FDenoiseTemporalFilterCS> ComputeShader(View.ShaderMap);
        FDenoiseTemporalFilterCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FDenoiseTemporalFilterCS::FParameters>();
        FMatrix44f ReprojectMatrix = FMatrix44f(View.ViewMatrices.GetInvViewProjectionMatrix() * View.PrevViewInfo.ViewMatrices.GetViewProjectionMatrix());
        PassParameters->ReprojectionMatrix = ReprojectMatrix;
        PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
        PassParameters->InverseProjectionMatrixLast = FMatrix44f(View.PrevViewInfo.ViewMatrices.GetInvProjectionMatrix());
        PassParameters->InverseProjectionMatrixThis = FMatrix44f(View.ViewMatrices.GetInvProjectionMatrix());
        PassParameters->GBufferDim = GBufferRes;
        PassParameters->DenoiseDim = DenoiseBufferRes;
        PassParameters->UpscaleFactorBits = UpscaleFactorBits;
        PassParameters->LinearSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		PassParameters->UseSH = UseSH;

        PassParameters->Enable = GET_GIDENOISE_VAR(EnableTemporal);
        PassParameters->BlendWeight = GET_GIDENOISE_VAR(TemporalBlendWeight);
        PassParameters->MomentBlendWeight = GET_GIDENOISE_VAR(TemporalMomentBlendWeight);
        PassParameters->ColorKernel = GET_GIDENOISE_CMD_VAR(TemporalColorTolerance);
        PassParameters->NormalKernel = GET_GIDENOISE_VAR(TemporalNormalTolerance);
        PassParameters->DepthKernel = GET_GIDENOISE_VAR(TemporalDepthTolerance);
		PassParameters->HistoryLength = GET_GIDENOISE_VAR(HistoryLength);
		PassParameters->ColorClamp = GET_GIDENOISE_VAR(ColorClamp);

        PassParameters->SceneTextures = SceneTextures;
        PassParameters->SceneTextures.GBufferVelocityTexture = SceneTextures.GBufferVelocityTexture ? SceneTextures.GBufferVelocityTexture : GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);

        PassParameters->DepthTextureThis = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(SceneDepthTexture));
        PassParameters->DepthTextureLast = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(DepthTexLast));
        PassParameters->NormalTextureThis = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(SceneNormalTexture));
        PassParameters->NormalTextureLast = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(NormalTexLast));
        PassParameters->MomentLast = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(MomentLast));
        PassParameters->ColorLast = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(ColorLast));
        PassParameters->ColorInput = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(Inputs.Color));
        PassParameters->DistanceInput = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(Inputs.RayHitDistance));
        PassParameters->MomentThis = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(MomentThis));
        PassParameters->ColorThis = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ColorThis));
        ClearUnusedGraphResources(ComputeShader, PassParameters);
        FComputeShaderUtils::AddPass(
            GraphBuilder,
            RDG_EVENT_NAME("GI Denoise Temporal Filter"),
            ComputeShader,
            PassParameters,
            FIntVector((DenoiseBufferRes.X + 15) / 16, (DenoiseBufferRes.Y + 15) / 16, 1));

        FRHICopyTextureInfo CopyInfo;
        CopyInfo.Size = MomentThis->Desc.GetSize();
        AddCopyTexturePass(GraphBuilder, MomentThis, MomentLast, CopyInfo);
    }

    RTDesc.Format = PF_R32G32B32A32_UINT;
    FRDGTextureRef ColorAndVariance = GraphBuilder.CreateTexture(RTDesc, TEXT("ColorAndVariance"));
    float ATrousDepthTolerance = FMath::Max(1e-5f, GET_GIDENOISE_VAR(ATrousDepthTolerance) * 0.2f);

    {
        // Calculate variance
        TShaderMapRef<FDenoiseSpatialFilterCS> ComputeShader(View.ShaderMap);

        FDenoiseSpatialFilterCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FDenoiseSpatialFilterCS::FParameters>();

        FMatrix44f ReprojectMatrix = FMatrix44f(View.ViewMatrices.GetInvViewProjectionMatrix() * View.PrevViewInfo.ViewMatrices.GetViewProjectionMatrix());
        PassParameters->InverseWVPMatrix = FMatrix44f(View.ViewMatrices.GetInvViewProjectionMatrix());
        PassParameters->WVPMatrix = FMatrix44f(View.ViewMatrices.GetViewProjectionMatrix());
        PassParameters->GBufferDim = GBufferRes;
        PassParameters->DenoiseDim = DenoiseBufferRes;
        PassParameters->LinearSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
        PassParameters->Enable = GET_GIDENOISE_CMD_VAR(EnableSpatial);
        PassParameters->UseSH = UseSH;
        PassParameters->BlendWeight = GET_GIDENOISE_CMD_VAR(SpatialBlendWeight);
        PassParameters->BaseRadius = GET_GIDENOISE_CMD_VAR(SpatialBaseRadius);
        PassParameters->NormalKernel = GET_GIDENOISE_VAR(ATrousNormalTolerance);
        PassParameters->DepthKernel = ATrousDepthTolerance;
        PassParameters->ColorKernel = 1;
        PassParameters->AOKernel = GET_GIDENOISE_VAR(ATrousAOTolerance);
        PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
        PassParameters->UpscaleFactorBits = UpscaleFactorBits;
        PassParameters->DepthTextureThis = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(SceneDepthTexture));
        PassParameters->NormalTextureThis = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(SceneNormalTexture));
        PassParameters->InputColor = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(ColorThis));
        PassParameters->InputMoment = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(MomentLast));
        PassParameters->OutputColor = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ColorAndVariance));
        PassParameters->RandomRotation = float(rand()) / RAND_MAX * 2 * 3.141593;

        ClearUnusedGraphResources(ComputeShader, PassParameters);

        FComputeShaderUtils::AddPass(
            GraphBuilder,
            RDG_EVENT_NAME("GI Denoise Variance Filter"),
            ComputeShader,
            PassParameters,
            FIntVector((DenoiseBufferRes.X + 15) / 16, (DenoiseBufferRes.Y + 15) / 16, 1));
    }

    int DebugType = 0;
    if (GET_GIDENOISE_CMD_VAR(DebugType) > 0)
    {
        DebugType = GET_GIDENOISE_CMD_VAR(DebugType);
    }
    else
    {
        DebugType = 0;
    }


    FRDGTextureRef InputColor = ColorAndVariance;
    RTDesc.Format = PF_R32G32B32A32_UINT;
    FRDGTextureRef OutputColor = GraphBuilder.CreateTexture(RTDesc, TEXT("OutputColor"));
    {
        // Spatial filtering
        // If FilterType is 0, a A-trous filters is used.
        // Otherwise, a separable gaussian filter is used, which performs horizontal filtering
        // followed by vertical filtering.
        int FilterType = GET_GIDENOISE_CMD_VAR(SpatialFilterType);
        if(FilterType == -1)
            FilterType = 0;

        float FilterWidth = GET_GIDENOISE_VAR(ATrousFilterWidth) * 2 * (FilterType == 0 ? 1 : 8);
        int SampleDepthAsNormal = GET_GIDENOISE_VAR(ATrousSampleDepthAsNormal);
        int EnableATrous = GET_GIDENOISE_VAR(EnableATrous);
        int nAdditionalIteration = ((FSceneViewState*)View.State)->CameraSwitchFrameCount > 0 ? GET_GIDENOISE_CMD_VAR(ATrousCameraSwitchIteration) : 0;
        int nIteration = EnableATrous ? GET_GIDENOISE_VAR(ATrousIteration) + nAdditionalIteration : 1;
        if (nIteration <= 0) nIteration = 1;
        int copyIteration = FMath::Min(nIteration - 1, GET_GIDENOISE_VAR(ATrousCopyIteration));
        TShaderMapRef<FDenoiseSpatialATrousFilterCS> ComputeShader(View.ShaderMap);
        FMatrix44f ReprojectMatrix = FMatrix44f(View.ViewMatrices.GetInvViewProjectionMatrix() * View.PrevViewInfo.ViewMatrices.GetViewProjectionMatrix());
        for (int i = 0; i < nIteration; i++)
        {
            {
                FDenoiseSpatialATrousFilterCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FDenoiseSpatialATrousFilterCS::FParameters>();
                PassParameters->InverseWVPMatrix = FMatrix44f(View.ViewMatrices.GetInvViewProjectionMatrix());
                PassParameters->WVPMatrix = FMatrix44f(View.ViewMatrices.GetViewProjectionMatrix());
                PassParameters->InverseProjectionMatrix = FMatrix44f(View.ViewMatrices.GetInvProjectionMatrix());
                PassParameters->GBufferDim = GBufferRes;
                PassParameters->DenoiseDim = DenoiseBufferRes;
                PassParameters->LinearSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
                PassParameters->Enable = GET_GIDENOISE_VAR(EnableATrous);
                PassParameters->UseSH = UseSH;
                PassParameters->VarianceGain = GET_GIDENOISE_VAR(ATrousVarianceGain);
                PassParameters->NormalKernel = GET_GIDENOISE_VAR(ATrousNormalTolerance);
                PassParameters->DepthKernel = ATrousDepthTolerance;
                PassParameters->AOKernel = GET_GIDENOISE_VAR(ATrousAOTolerance);
                PassParameters->DepthTextureThis = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(SceneDepthTexture));
                PassParameters->NormalTextureThis = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(SceneNormalTexture));
                PassParameters->Step = i;
                PassParameters->FilterType = FilterType == 0 ? 0 : 1;
                PassParameters->FilterWidth = FilterWidth;
                PassParameters->SampleDepthAsNormal = SampleDepthAsNormal;
                PassParameters->InputMoment = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(MomentLast));
                PassParameters->InputColor = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(InputColor));
                PassParameters->OutputColor = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(OutputColor));
                PassParameters->RandomRotation = float(rand()) / RAND_MAX * 2 * 3.141593;
                PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
                PassParameters->UpscaleFactorBits = UpscaleFactorBits;

                ClearUnusedGraphResources(ComputeShader, PassParameters);

                FComputeShaderUtils::AddPass(
                    GraphBuilder,
                    RDG_EVENT_NAME("GI Denoise Spatial Filter"),
                    ComputeShader,
                    PassParameters,
                    FIntVector((DenoiseBufferRes.X + 15) / 16, (DenoiseBufferRes.Y + 15) / 16, 1));
            }

            if (FilterType != 0)
            {
                FRDGTextureRef TempColor = OutputColor;
                OutputColor = InputColor;
                InputColor = TempColor;

                FDenoiseSpatialATrousFilterCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FDenoiseSpatialATrousFilterCS::FParameters>();
                PassParameters->InverseWVPMatrix = FMatrix44f(View.ViewMatrices.GetInvViewProjectionMatrix());
                PassParameters->WVPMatrix = FMatrix44f(View.ViewMatrices.GetViewProjectionMatrix());
                PassParameters->InverseProjectionMatrix = FMatrix44f(View.ViewMatrices.GetInvProjectionMatrix());
                PassParameters->GBufferDim = GBufferRes;
                PassParameters->DenoiseDim = DenoiseBufferRes;
                PassParameters->LinearSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
                PassParameters->Enable = GET_GIDENOISE_VAR(EnableATrous);
                PassParameters->UseSH = UseSH;
                PassParameters->VarianceGain = GET_GIDENOISE_VAR(ATrousVarianceGain);
                PassParameters->NormalKernel = GET_GIDENOISE_VAR(ATrousNormalTolerance);
                PassParameters->DepthKernel = ATrousDepthTolerance;
                PassParameters->AOKernel = GET_GIDENOISE_VAR(ATrousAOTolerance);
                PassParameters->DepthTextureThis = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(SceneDepthTexture));
                PassParameters->NormalTextureThis = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(SceneNormalTexture));
                PassParameters->Step = i;
                PassParameters->FilterType = 2;
                PassParameters->FilterWidth = FilterWidth;
                PassParameters->SampleDepthAsNormal = SampleDepthAsNormal;
                PassParameters->InputMoment = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(MomentLast));
                PassParameters->InputColor = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(InputColor));
                PassParameters->OutputColor = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(OutputColor));
                PassParameters->RandomRotation = float(rand()) / RAND_MAX * 2 * 3.141593;
                PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
                PassParameters->UpscaleFactorBits = UpscaleFactorBits;

                ClearUnusedGraphResources(ComputeShader, PassParameters);

                FComputeShaderUtils::AddPass(
                    GraphBuilder,
                    RDG_EVENT_NAME("GI Denoise Spatial Filter 2"),
                    ComputeShader,
                    PassParameters,
                    FIntVector((DenoiseBufferRes.X + 15) / 16, (DenoiseBufferRes.Y + 15) / 16, 1));
            }

            if (copyIteration == i)
            {
                FRHICopyTextureInfo CopyInfo;
                CopyInfo.Size = MomentThis->Desc.GetSize();
                AddCopyTexturePass(GraphBuilder, OutputColor, ColorLast, CopyInfo);
            }

            FRDGTextureRef TempColor = OutputColor;
            OutputColor = InputColor;
            InputColor = TempColor;
        }
    }

    {
        // Composite pass
        FIntPoint ViewRectSize = GBufferRes;
        FCompositeDenoisePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FCompositeDenoisePS::FParameters>();
        PassParameters->InverseProjectionMatrix = FMatrix44f(View.ViewMatrices.GetInvProjectionMatrix());
        PassParameters->DenoiseBufferScale = BufferScale;
        PassParameters->DiffuseBoost = GET_GIDENOISE_CMD_VAR(DiffuseBoost);
        PassParameters->SHSharpness = GET_GIDENOISE_CMD_VAR(SHSharpness);
        PassParameters->DebugMode = DebugType;
        PassParameters->UseSH = UseSH;
        PassParameters->GBufferDim = GBufferRes;
        PassParameters->DenoiseDim = DenoiseBufferRes;
        PassParameters->UpscaleFactorBits = UpscaleFactorBits;
        PassParameters->DepthTexture = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(SceneDepthTexture));
        PassParameters->NormalTexture = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(SceneNormalTexture));
        PassParameters->DenoiseTexture = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(OutputColor));
        PassParameters->MomentTexture = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(MomentLast));
        PassParameters->SceneTextures = SceneTextures;
		PassParameters->SceneTextures.GBufferVelocityTexture = SceneTextures.GBufferVelocityTexture ? SceneTextures.GBufferVelocityTexture : GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);
        PassParameters->AlbedoTexture = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(SceneAlbedoTexture));
        PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
        PassParameters->ViewportInfo = FVector4f(GBufferRes.X, GBufferRes.Y, 1.0f / GBufferRes.X, 1.0f / GBufferRes.Y);
        PassParameters->RenderTargets[0] = FRenderTargetBinding(SignalOutput.Textures[0], ERenderTargetLoadAction::EClear);


        GraphBuilder.AddPass(
            RDG_EVENT_NAME("GI Denoise Composite"),
            PassParameters,
            ERDGPassFlags::Raster,
            [PassParameters, &View, ViewRectSize](FRHICommandList& RHICmdList)
            {
                TShaderMapRef<FPostProcessVS> VertexShader(View.ShaderMap);
                TShaderMapRef<FCompositeDenoisePS> PixelShader(View.ShaderMap);

                FGraphicsPipelineStateInitializer GraphicsPSOInit;
                RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

                GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One>::GetRHI();
                GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
                GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
                GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
                GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
                GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
                GraphicsPSOInit.PrimitiveType = PT_TriangleList;
                SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

                RHICmdList.SetViewport(0.0f, 0.0f, 0.0f, ViewRectSize.X, ViewRectSize.Y, 1.0f);
                SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *PassParameters);

                ClearUnusedGraphResources(PixelShader, PassParameters);

                DrawRectangle(
                    RHICmdList,
                    0, 0,
                    ViewRectSize.X, ViewRectSize.Y,
                    0, 0,
                    ViewRectSize.X, ViewRectSize.Y,
                    ViewRectSize,
                    ViewRectSize,
                    VertexShader);
            });
    }

    if (View.State)
    {
        GraphBuilder.QueueTextureExtraction(DenoiseIntensity[0], &((FSceneViewState*)View.State)->DenoiseTexture[0]);
        GraphBuilder.QueueTextureExtraction(DenoiseIntensity[1], &((FSceneViewState*)View.State)->DenoiseTexture[1]);
    }

	return SignalOutput;
}
#else
{
    FSSDSignalTextures SignalOutput;
    return SignalOutput;
}
#endif

IScreenSpaceDenoiser::FDiffuseIndirectOutputs FFusionDenoiser::DenoiseSkyLight(FRDGBuilder& GraphBuilder, const FViewInfo& View, FPreviousViewInfo* PreviousViewInfos, const FSceneTextureParameters& SceneTextures, const FDiffuseIndirectInputs& Inputs, const FAmbientOcclusionRayTracingConfig Config) const
{
    FSSDSignalTextures SignalTexture = DenoiseDiffuseIndirect(GraphBuilder, View, PreviousViewInfos, SceneTextures, Inputs, Config);
	IScreenSpaceDenoiser::FDiffuseIndirectOutputs DiffuseIndirectOutputs;
    DiffuseIndirectOutputs.Color = SignalTexture.Textures[0];
	return DiffuseIndirectOutputs;
    // return WrappedDenoiser->DenoiseSkyLight(GraphBuilder, View, PreviousViewInfos, SceneTextures, Inputs, Config);
}

IScreenSpaceDenoiser::FDiffuseIndirectOutputs FFusionDenoiser::DenoiseReflectedSkyLight(FRDGBuilder& GraphBuilder, const FViewInfo& View, FPreviousViewInfo* PreviousViewInfos, const FSceneTextureParameters& SceneTextures, const FDiffuseIndirectInputs& Inputs, const FAmbientOcclusionRayTracingConfig Config) const
{
	return WrappedDenoiser->DenoiseReflectedSkyLight(GraphBuilder, View, PreviousViewInfos, SceneTextures, Inputs, Config);
}

FSSDSignalTextures FFusionDenoiser::DenoiseDiffuseIndirectHarmonic(FRDGBuilder& GraphBuilder,
		const FViewInfo& View,
		FPreviousViewInfo* PreviousViewInfos,
		const FSceneTextureParameters& SceneTextures,
		const FDiffuseIndirectHarmonic& Inputs,
		const HybridIndirectLighting::FCommonParameters& CommonDiffuseParameters) const
{
	return WrappedDenoiser->DenoiseDiffuseIndirectHarmonic(GraphBuilder, View, PreviousViewInfos, SceneTextures, Inputs, CommonDiffuseParameters);
}

bool FFusionDenoiser::SupportsScreenSpaceDiffuseIndirectDenoiser(EShaderPlatform Platform) const
{
	return WrappedDenoiser->SupportsScreenSpaceDiffuseIndirectDenoiser(Platform);
}

FSSDSignalTextures FFusionDenoiser::DenoiseScreenSpaceDiffuseIndirect(FRDGBuilder& GraphBuilder, const FViewInfo& View, FPreviousViewInfo* PreviousViewInfos, const FSceneTextureParameters& SceneTextures, const FDiffuseIndirectInputs& Inputs, const FAmbientOcclusionRayTracingConfig Config) const
{
	return WrappedDenoiser->DenoiseScreenSpaceDiffuseIndirect(GraphBuilder, View, PreviousViewInfos, SceneTextures, Inputs, Config);
}

const IScreenSpaceDenoiser* FFusionDenoiser::GetWrappedDenoiser() const
{
	return WrappedDenoiser;
}

IScreenSpaceDenoiser* FFusionDenoiser::GetDenoiser()
{
    const IScreenSpaceDenoiser* DenoiserToWrap = GScreenSpaceDenoiser ? GScreenSpaceDenoiser : IScreenSpaceDenoiser::GetDefaultDenoiser();

   static FFusionDenoiser denoiser(DenoiserToWrap);
   return &denoiser;
}