// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
LightGridInjection.cpp
=============================================================================*/

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "HAL/IConsoleManager.h"
#include "RHI.h"
#include "UniformBuffer.h"
#include "ShaderParameters.h"
#include "RendererInterface.h"
#include "EngineDefines.h"
#include "PrimitiveSceneProxy.h"
#include "Shader.h"
#include "SceneUtils.h"
#include "PostProcess/SceneRenderTargets.h"
#include "LightSceneInfo.h"
#include "GlobalShader.h"
#include "SceneRendering.h"
#include "DeferredShadingRenderer.h"
#include "BasePassRendering.h"
#include "RendererModule.h"
#include "ScenePrivate.h"
#include "ClearQuad.h"
#include "VolumetricFog.h"
#include "VolumetricCloudRendering.h"
#include "Components/LightComponent.h"
#include "Engine/MapBuildDataRegistry.h"

int32 GLightGridPixelSize = 64;
FAutoConsoleVariableRef CVarLightGridPixelSize(
	TEXT("r.Forward.LightGridPixelSize"),
	GLightGridPixelSize,
	TEXT("Size of a cell in the light grid, in pixels."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLightGridSizeZ = 32;
FAutoConsoleVariableRef CVarLightGridSizeZ(
	TEXT("r.Forward.LightGridSizeZ"),
	GLightGridSizeZ,
	TEXT("Number of Z slices in the light grid."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GMaxCulledLightsPerCell = 32;
FAutoConsoleVariableRef CVarMaxCulledLightsPerCell(
	TEXT("r.Forward.MaxCulledLightsPerCell"),
	GMaxCulledLightsPerCell,
	TEXT("Controls how much memory is allocated for each cell for light culling.  When r.Forward.LightLinkedListCulling is enabled, this is used to compute a global max instead of a per-cell limit on culled lights."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLightLinkedListCulling = 1;
FAutoConsoleVariableRef CVarLightLinkedListCulling(
	TEXT("r.Forward.LightLinkedListCulling"),
	GLightLinkedListCulling,
	TEXT("Uses a reverse linked list to store culled lights, removing the fixed limit on how many lights can affect a cell - it becomes a global limit instead."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLightCullingQuality = 1;
FAutoConsoleVariableRef CVarLightCullingQuality(
	TEXT("r.LightCulling.Quality"),
	GLightCullingQuality,
	TEXT("Whether to run compute light culling pass.\n")
	TEXT(" 0: off \n")
	TEXT(" 1: on (default)\n"),
	ECVF_RenderThreadSafe
);

float GLightCullingMaxDistanceOverrideKilometers = -1.0f;
FAutoConsoleVariableRef CVarLightCullingMaxDistanceOverride(
	TEXT("r.LightCulling.MaxDistanceOverrideKilometers"),
	GLightCullingMaxDistanceOverrideKilometers,
	TEXT("Used to override the maximum far distance at which we can store data in the light grid.\n If this is increase, you might want to update r.Forward.LightGridSizeZ to a reasonable value according to your use case light count and distribution.")
	TEXT(" <=0: off \n")
	TEXT(" >0: the far distance in kilometers.\n"),
	ECVF_RenderThreadSafe
);

extern TAutoConsoleVariable<int32> CVarVirtualShadowOnePassProjection;

void SetupDummyForwardLightUniformParameters(FRDGBuilder& GraphBuilder, FForwardLightData& ForwardLightData)
{
	const FRDGSystemTextures& SystemTextures = FRDGSystemTextures::Get(GraphBuilder);
	
	ForwardLightData.DirectionalLightShadowmapAtlas = SystemTextures.Black;
	ForwardLightData.DirectionalLightStaticShadowmap = GBlackTexture->TextureRHI;

	FRDGBufferRef ForwardLocalLightBuffer = GSystemTextures.GetDefaultBuffer(GraphBuilder, sizeof(FVector4f));
	ForwardLightData.ForwardLocalLightBuffer = GraphBuilder.CreateSRV(ForwardLocalLightBuffer, PF_A32B32G32R32F);

	FRDGBufferRef NumCulledLightsGrid = GSystemTextures.GetDefaultBuffer(GraphBuilder, sizeof(uint32));
	ForwardLightData.NumCulledLightsGrid = GraphBuilder.CreateSRV(NumCulledLightsGrid, PF_R32_UINT);

	if (RHISupportsBufferLoadTypeConversion(GMaxRHIShaderPlatform))
	{
		FRDGBufferRef CulledLightDataGrid = GSystemTextures.GetDefaultBuffer(GraphBuilder, sizeof(uint16));
		ForwardLightData.CulledLightDataGrid = GraphBuilder.CreateSRV(CulledLightDataGrid, PF_R16_UINT);
	}
	else
	{
		FRDGBufferRef CulledLightDataGrid = GSystemTextures.GetDefaultBuffer(GraphBuilder, sizeof(uint32));
		ForwardLightData.CulledLightDataGrid = GraphBuilder.CreateSRV(CulledLightDataGrid, PF_R32_UINT);
	}
}

TRDGUniformBufferRef<FForwardLightData> CreateDummyForwardLightUniformBuffer(FRDGBuilder& GraphBuilder)
{
	FForwardLightData* ForwardLightData = GraphBuilder.AllocParameters<FForwardLightData>();
	SetupDummyForwardLightUniformParameters(GraphBuilder, *ForwardLightData);
	return GraphBuilder.CreateUniformBuffer(ForwardLightData);
}

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FForwardLightData, "ForwardLightData");

FForwardLightData::FForwardLightData()
{
	FMemory::Memzero(*this);
	ShadowmapSampler = TStaticSamplerState<SF_Point,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI();
	DirectionalLightStaticShadowmap = GBlackTexture->TextureRHI;
	StaticShadowmapSampler = TStaticSamplerState<SF_Bilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI();
	DummyRectLightSourceTexture = GWhiteTexture->TextureRHI;
}

int32 NumCulledLightsGridStride = 2;
int32 NumCulledGridPrimitiveTypes = 2;
int32 LightLinkStride = 2;

// 65k indexable light limit
typedef uint16 FLightIndexType;
// UINT_MAX indexable light limit
typedef uint32 FLightIndexType32;

uint32 LightGridInjectionGroupSize = 4;


class FLightGridInjectionCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLightGridInjectionCS);
	SHADER_USE_PARAMETER_STRUCT(FLightGridInjectionCS, FGlobalShader)
public:
	class FUseLinkedListDim : SHADER_PERMUTATION_BOOL("USE_LINKED_CULL_LIST");
	using FPermutationDomain = TShaderPermutationDomain<FUseLinkedListDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FReflectionCaptureShaderData, ReflectionCapture)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWNumCulledLightsGrid)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWCulledLightDataGrid)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWNextCulledLightLink)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWStartOffsetGrid)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWCulledLightLinks)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, LightViewSpacePositionAndRadius)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, LightViewSpaceDirAndPreprocAngle)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, ForwardLocalLightBuffer)

		SHADER_PARAMETER(FIntVector, CulledGridSize)
		SHADER_PARAMETER(uint32, NumReflectionCaptures)
		SHADER_PARAMETER(FVector3f, LightGridZParams)
		SHADER_PARAMETER(uint32, NumLocalLights)
		SHADER_PARAMETER(uint32, NumGridCells)
		SHADER_PARAMETER(uint32, MaxCulledLightsPerCell)
		SHADER_PARAMETER(uint32, LightGridPixelSizeShift)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), LightGridInjectionGroupSize);
		FForwardLightingParameters::ModifyCompilationEnvironment(Parameters.Platform, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("LIGHT_LINK_STRIDE"), LightLinkStride);
		OutEnvironment.SetDefine(TEXT("ENABLE_LIGHT_CULLING_VIEW_SPACE_BUILD_DATA"), ENABLE_LIGHT_CULLING_VIEW_SPACE_BUILD_DATA);
	}
};

IMPLEMENT_GLOBAL_SHADER(FLightGridInjectionCS, "/Engine/Private/LightGridInjection.usf", "LightGridInjectionCS", SF_Compute);


class FLightGridCompactCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLightGridCompactCS)
	SHADER_USE_PARAMETER_STRUCT(FLightGridCompactCS, FGlobalShader)
public:
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWNumCulledLightsGrid)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWCulledLightDataGrid)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWNextCulledLightData)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, StartOffsetGrid)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, CulledLightLinks)

		SHADER_PARAMETER(FIntVector, CulledGridSize)
		SHADER_PARAMETER(uint32, NumReflectionCaptures)
		SHADER_PARAMETER(uint32, NumLocalLights)
		SHADER_PARAMETER(uint32, NumGridCells)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), LightGridInjectionGroupSize);
		FForwardLightingParameters::ModifyCompilationEnvironment(Parameters.Platform, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("LIGHT_LINK_STRIDE"), LightLinkStride);
		OutEnvironment.SetDefine(TEXT("MAX_CAPTURES"), GMaxNumReflectionCaptures);
		OutEnvironment.SetDefine(TEXT("ENABLE_LIGHT_CULLING_VIEW_SPACE_BUILD_DATA"), ENABLE_LIGHT_CULLING_VIEW_SPACE_BUILD_DATA);
	}
};

IMPLEMENT_GLOBAL_SHADER(FLightGridCompactCS, "/Engine/Private/LightGridInjection.usf", "LightGridCompactCS", SF_Compute);

/**
 */
FORCEINLINE float GetTanRadAngleOrZero(float coneAngle)
{
	if (coneAngle < PI / 2.001f)
	{
		return FMath::Tan(coneAngle);
	}

	return 0.0f;
}


FVector GetLightGridZParams(float NearPlane, float FarPlane)
{
	// S = distribution scale
	// B, O are solved for given the z distances of the first+last slice, and the # of slices.
	//
	// slice = log2(z*B + O) * S

	// Don't spend lots of resolution right in front of the near plane
	double NearOffset = .095 * 100;
	// Space out the slices so they aren't all clustered at the near plane
	double S = 4.05;

	double N = NearPlane + NearOffset;
	double F = FarPlane;

	double O = (F - N * exp2((GLightGridSizeZ - 1) / S)) / (F - N);
	double B = (1 - O) / N;

	return FVector(B, O, S);
}

void FSceneRenderer::ComputeLightGrid(FRDGBuilder& GraphBuilder, bool bCullLightsToGrid, FSortedLightSetSceneInfo &SortedLightSet)
{
	RDG_CSV_STAT_EXCLUSIVE_SCOPE(GraphBuilder, ComputeLightGrid);
	QUICK_SCOPE_CYCLE_COUNTER(STAT_ComputeLightGrid);
	RDG_EVENT_SCOPE(GraphBuilder, "ComputeLightGrid");

	static const auto AllowStaticLightingVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.AllowStaticLighting"));
	const bool bAllowStaticLighting = (!AllowStaticLightingVar || AllowStaticLightingVar->GetValueOnRenderThread() != 0);
	const bool bAllowFormatConversion = RHISupportsBufferLoadTypeConversion(GMaxRHIShaderPlatform);

	const FRDGSystemTextures& SystemTextures = FRDGSystemTextures::Get(GraphBuilder);

	TArray<FForwardLightData*, TInlineAllocator<4>> ForwardLightDataPerView;
#if WITH_EDITOR
	bool bMultipleDirLightsConflictForForwardShading = false;
#endif

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		FViewInfo& View = Views[ViewIndex];

		View.ForwardLightingResources.SelectedForwardDirectionalLightProxy = nullptr;

		FForwardLightData* ForwardLightData = GraphBuilder.AllocParameters<FForwardLightData>();
		ForwardLightData->DirectionalLightShadowmapAtlas = SystemTextures.Black;
		ForwardLightData->DirectionalLightStaticShadowmap = GBlackTexture->TextureRHI;

		TArray<FForwardLocalLightData, SceneRenderingAllocator> ForwardLocalLightData;
		TArray<int32, SceneRenderingAllocator>  LocalLightVisibleLightInfosIndex;
#if ENABLE_LIGHT_CULLING_VIEW_SPACE_BUILD_DATA
		TArray<FVector4f, SceneRenderingAllocator> ViewSpacePosAndRadiusData;
		TArray<FVector4f, SceneRenderingAllocator> ViewSpaceDirAndPreprocAngleData;
#endif // ENABLE_LIGHT_CULLING_VIEW_SPACE_BUILD_DATA

		float FurthestLight = 1000;

		int32 ConflictingLightCountForForwardShading = 0;

		// Track the end markers for different types
		int32 SimpleLightsEnd = 0;
		int32 ClusteredSupportedEnd = 0;

		if (bCullLightsToGrid)
		{
			// Simple lights are copied without view dependent checks, so same in and out
			SimpleLightsEnd = SortedLightSet.SimpleLightsEnd;
			// 1. insert simple lights
			if (SimpleLightsEnd > 0)
			{
				ForwardLocalLightData.Reserve(SimpleLightsEnd);
				LocalLightVisibleLightInfosIndex.Reserve(SimpleLightsEnd);
#if ENABLE_LIGHT_CULLING_VIEW_SPACE_BUILD_DATA
				ViewSpacePosAndRadiusData.Reserve(SimpleLightsEnd);
				ViewSpaceDirAndPreprocAngleData.Reserve(SimpleLightsEnd);
#endif // ENABLE_LIGHT_CULLING_VIEW_SPACE_BUILD_DATA

				const FSimpleLightArray &SimpleLights = SortedLightSet.SimpleLights;

				// Pack both values into a single float to keep float4 alignment
				const FFloat16 SimpleLightSourceLength16f = FFloat16(0);
				FLightingChannels SimpleLightLightingChannels;

				// Put simple lights in all lighting channels
				SimpleLightLightingChannels.bChannel0 = SimpleLightLightingChannels.bChannel1 = SimpleLightLightingChannels.bChannel2 = true;
				const uint32 SimpleLightLightingChannelMask = GetLightingChannelMaskForStruct(SimpleLightLightingChannels);

				// Now using the sorted lights, and keep track of ranges as we go.
				for (int32 SortedIndex = 0; SortedIndex < SimpleLightsEnd; ++SortedIndex)
				{
					check(SortedLightSet.SortedLights[SortedIndex].LightSceneInfo == nullptr);
					check(!SortedLightSet.SortedLights[SortedIndex].SortKey.Fields.bIsNotSimpleLight);

					int32 SimpleLightIndex = SortedLightSet.SortedLights[SortedIndex].SimpleLightIndex;

					ForwardLocalLightData.AddUninitialized(1);
					FForwardLocalLightData& LightData = ForwardLocalLightData.Last();
					// Simple lights have no 'VisibleLight' info
					LocalLightVisibleLightInfosIndex.Add(INDEX_NONE);

					const FSimpleLightEntry& SimpleLight = SimpleLights.InstanceData[SimpleLightIndex];
					const FSimpleLightPerViewEntry& SimpleLightPerViewData = SimpleLights.GetViewDependentData(SimpleLightIndex, ViewIndex, Views.Num());

					const FVector3f LightTranslatedWorldPosition(View.ViewMatrices.GetPreViewTranslation() + SimpleLightPerViewData.Position);
					LightData.LightPositionAndInvRadius = FVector4f(LightTranslatedWorldPosition, 1.0f / FMath::Max(SimpleLight.Radius, KINDA_SMALL_NUMBER));
					LightData.LightColorAndFalloffExponent = FVector4f((FVector3f)SimpleLight.Color, SimpleLight.Exponent);

					// No shadowmap channels for simple lights
					uint32 ShadowMapChannelMask = 0;
					ShadowMapChannelMask |= SimpleLightLightingChannelMask << 8;

					LightData.LightDirectionAndShadowMapChannelMask = FVector4f(FVector3f(1, 0, 0), *((float*)&ShadowMapChannelMask));

					// Pack both values into a single float to keep float4 alignment
					const FFloat16 VolumetricScatteringIntensity16f = FFloat16(SimpleLight.VolumetricScatteringIntensity);
					const uint32 PackedWInt = ((uint32)SimpleLightSourceLength16f.Encoded) | ((uint32)VolumetricScatteringIntensity16f.Encoded << 16);

					LightData.SpotAnglesAndSourceRadiusPacked = FVector4f(-2, 1, 0, *(float*)&PackedWInt);
					LightData.LightTangentAndSoftSourceRadius = FVector4f(1.0f, 0.0f, 0.0f, 0.0f);
					LightData.RectBarnDoorAndVirtualShadowMapId = FVector4f(0, -2, 0, 0);

#if ENABLE_LIGHT_CULLING_VIEW_SPACE_BUILD_DATA
					FVector4f ViewSpacePosAndRadius(FVector4f(View.ViewMatrices.GetViewMatrix().TransformPosition(SimpleLightPerViewData.Position)), SimpleLight.Radius);
					ViewSpacePosAndRadiusData.Add(ViewSpacePosAndRadius);
					ViewSpaceDirAndPreprocAngleData.AddZeroed();
#endif // ENABLE_LIGHT_CULLING_VIEW_SPACE_BUILD_DATA
				}
			}

			float SelectedForwardDirectionalLightIntensitySq = 0.0f;
			int32 SelectedForwardDirectionalLightPriority = -1;
			const TArray<FSortedLightSceneInfo, SceneRenderingAllocator>& SortedLights = SortedLightSet.SortedLights;
			ClusteredSupportedEnd = SimpleLightsEnd;
			// Next add all the other lights, track the end index for clustered supporting lights
			for (int SortedIndex = SimpleLightsEnd; SortedIndex < SortedLights.Num(); ++SortedIndex)
			{
				const FSortedLightSceneInfo& SortedLightInfo = SortedLights[SortedIndex];
				const FLightSceneInfo* const LightSceneInfo = SortedLightInfo.LightSceneInfo;
				const FLightSceneProxy* LightProxy = LightSceneInfo->Proxy;

				if (LightSceneInfo->ShouldRenderLight(View))
				{
					FLightRenderParameters LightParameters;
					LightProxy->GetLightShaderParameters(LightParameters);

					if (LightProxy->IsInverseSquared())
					{
						LightParameters.FalloffExponent = 0;
					}

					// When rendering reflection captures, the direct lighting of the light is actually the indirect specular from the main view
					if (View.bIsReflectionCapture)
					{
						LightParameters.Color *= LightProxy->GetIndirectLightingScale();
					}

					int32 ShadowMapChannel = LightProxy->GetShadowMapChannel();
					int32 DynamicShadowMapChannel = LightSceneInfo->GetDynamicShadowMapChannel();

					if (!bAllowStaticLighting)
					{
						ShadowMapChannel = INDEX_NONE;
					}

					// Static shadowing uses ShadowMapChannel, dynamic shadows are packed into light attenuation using DynamicShadowMapChannel
					uint32 LightTypeAndShadowMapChannelMaskPacked =
						(ShadowMapChannel == 0 ? 1 : 0) |
						(ShadowMapChannel == 1 ? 2 : 0) |
						(ShadowMapChannel == 2 ? 4 : 0) |
						(ShadowMapChannel == 3 ? 8 : 0) |
						(DynamicShadowMapChannel == 0 ? 16 : 0) |
						(DynamicShadowMapChannel == 1 ? 32 : 0) |
						(DynamicShadowMapChannel == 2 ? 64 : 0) |
						(DynamicShadowMapChannel == 3 ? 128 : 0);

					LightTypeAndShadowMapChannelMaskPacked |= LightProxy->GetLightingChannelMask() << 8;
					// pack light type in this uint32 as well
					LightTypeAndShadowMapChannelMaskPacked |= SortedLightInfo.SortKey.Fields.LightType << 16;

					const bool bDynamicShadows = ActiveViewFamily->EngineShowFlags.DynamicShadows && ActiveViewFamily->VisibleLightInfos.IsValidIndex(LightSceneInfo->Id);
					const int32 VirtualShadowMapId = bDynamicShadows ? ActiveViewFamily->VisibleLightInfos[LightSceneInfo->Id].GetVirtualShadowMapId( &View ) : INDEX_NONE;

					if ((SortedLightInfo.SortKey.Fields.LightType == LightType_Point && ActiveViewFamily->EngineShowFlags.PointLights) ||
						(SortedLightInfo.SortKey.Fields.LightType == LightType_Spot && ActiveViewFamily->EngineShowFlags.SpotLights) ||
						(SortedLightInfo.SortKey.Fields.LightType == LightType_Rect && ActiveViewFamily->EngineShowFlags.RectLights))
					{
						ForwardLocalLightData.AddUninitialized(1);
						FForwardLocalLightData& LightData = ForwardLocalLightData.Last();
						LocalLightVisibleLightInfosIndex.Add(LightSceneInfo->Id);

						// Track the last one to support clustered deferred
						if (!SortedLightInfo.SortKey.Fields.bClusteredDeferredNotSupported)
						{
							ClusteredSupportedEnd = FMath::Max(ClusteredSupportedEnd, ForwardLocalLightData.Num());
						}
						const float LightFade = GetLightFadeFactor(View, LightProxy);
						LightParameters.Color *= LightFade;

						const FVector3f LightTranslatedWorldPosition(View.ViewMatrices.GetPreViewTranslation() + LightParameters.WorldPosition);
						LightData.LightPositionAndInvRadius = FVector4f(LightTranslatedWorldPosition, LightParameters.InvRadius);
						LightData.LightColorAndFalloffExponent = FVector4f(LightParameters.Color, LightParameters.FalloffExponent);
						LightData.LightDirectionAndShadowMapChannelMask = FVector4f(LightParameters.Direction, *((float*)&LightTypeAndShadowMapChannelMaskPacked));

						LightData.SpotAnglesAndSourceRadiusPacked = FVector4f(LightParameters.SpotAngles.X, LightParameters.SpotAngles.Y, LightParameters.SourceRadius, 0);

						LightData.LightTangentAndSoftSourceRadius = FVector4f(LightParameters.Tangent, LightParameters.SoftSourceRadius);

						// NOTE: This cast of VirtualShadowMapId to float is not ideal, but bitcast has issues here with INDEX_NONE -> NaN
						// and 32-bit floats have enough mantissa to cover all reasonable numbers here for now.
						LightData.RectBarnDoorAndVirtualShadowMapId = FVector4f(LightParameters.RectLightBarnCosAngle, LightParameters.RectLightBarnLength, float(VirtualShadowMapId), 0);
						checkSlow(int(LightData.RectBarnDoorAndVirtualShadowMapId.Z) == VirtualShadowMapId);

						float VolumetricScatteringIntensity = LightProxy->GetVolumetricScatteringIntensity();

						if (LightNeedsSeparateInjectionIntoVolumetricFogForOpaqueShadow(View, LightSceneInfo, ActiveViewFamily->VisibleLightInfos[LightSceneInfo->Id])
							|| (LightNeedsSeparateInjectionIntoVolumetricFogForLightFunction(LightSceneInfo) && CheckForLightFunction(LightSceneInfo)))
						{
							// Disable this lights forward shading volumetric scattering contribution
							VolumetricScatteringIntensity = 0;
						}

						// Pack both values into a single float to keep float4 alignment
						const FFloat16 SourceLength16f = FFloat16(LightParameters.SourceLength);
						const FFloat16 VolumetricScatteringIntensity16f = FFloat16(VolumetricScatteringIntensity);
						const uint32 PackedWInt = ((uint32)SourceLength16f.Encoded) | ((uint32)VolumetricScatteringIntensity16f.Encoded << 16);
						LightData.SpotAnglesAndSourceRadiusPacked.W = *(float*)&PackedWInt;

						const FSphere BoundingSphere = LightProxy->GetBoundingSphere();
						const float Distance = View.ViewMatrices.GetViewMatrix().TransformPosition(BoundingSphere.Center).Z + BoundingSphere.W;
						FurthestLight = FMath::Max(FurthestLight, Distance);

#if ENABLE_LIGHT_CULLING_VIEW_SPACE_BUILD_DATA
						// Note: inverting radius twice seems stupid (but done in shader anyway otherwise)
						const FVector3f LightViewPosition = FVector4f(View.ViewMatrices.GetViewMatrix().TransformPosition(LightParameters.WorldPosition)); // LWC_TODO: precision loss
						FVector4f ViewSpacePosAndRadius(LightViewPosition, 1.0f / LightParameters.InvRadius);
						ViewSpacePosAndRadiusData.Add(ViewSpacePosAndRadius);

						float PreProcAngle = SortedLightInfo.SortKey.Fields.LightType == LightType_Spot ? GetTanRadAngleOrZero(LightSceneInfo->Proxy->GetOuterConeAngle()) : 0.0f;

						FVector4f ViewSpaceDirAndPreprocAngle(FVector4f(View.ViewMatrices.GetViewMatrix().TransformVector((FVector)LightParameters.Direction)), PreProcAngle); // LWC_TODO: precision loss
						ViewSpaceDirAndPreprocAngleData.Add(ViewSpaceDirAndPreprocAngle);
#endif // ENABLE_LIGHT_CULLING_VIEW_SPACE_BUILD_DATA
					}
					else if (SortedLightInfo.SortKey.Fields.LightType == LightType_Directional && ActiveViewFamily->EngineShowFlags.DirectionalLights)
					{
						// The selected forward directional light is also used for volumetric lighting using ForwardLightData UB.
						// Also some people noticed that depending on the order a two directional lights are made visible in a level, the selected light for volumetric fog lighting will be different.
						// So to be clear and avoid such issue, we select the most intense directional light for forward shading and volumetric lighting.
						const float LightIntensitySq = FVector3f(LightParameters.Color).SizeSquared();
						const int32 LightForwardShadingPriority = LightProxy->GetDirectionalLightForwardShadingPriority();
#if WITH_EDITOR
						if (LightForwardShadingPriority > SelectedForwardDirectionalLightPriority)
						{
							// Reset the count if the new light has a higher priority than the previous one.
							ConflictingLightCountForForwardShading = 1;
						}
						else if (LightForwardShadingPriority == SelectedForwardDirectionalLightPriority)
						{
							// Accumulate new light if also has the highest priority value.
							ConflictingLightCountForForwardShading++;
						}
#endif
						if (LightForwardShadingPriority > SelectedForwardDirectionalLightPriority
							|| (LightForwardShadingPriority == SelectedForwardDirectionalLightPriority && LightIntensitySq > SelectedForwardDirectionalLightIntensitySq))
						{

							SelectedForwardDirectionalLightPriority = LightForwardShadingPriority;
							SelectedForwardDirectionalLightIntensitySq = LightIntensitySq;
							View.ForwardLightingResources.SelectedForwardDirectionalLightProxy = LightProxy;

							ForwardLightData->HasDirectionalLight = 1;
							ForwardLightData->DirectionalLightColor = FVector3f(LightParameters.Color);
							ForwardLightData->DirectionalLightVolumetricScatteringIntensity = LightProxy->GetVolumetricScatteringIntensity();
							ForwardLightData->DirectionalLightDirection = LightParameters.Direction;
							ForwardLightData->DirectionalLightShadowMapChannelMask = LightTypeAndShadowMapChannelMaskPacked;
							ForwardLightData->DirectionalLightVSM = INDEX_NONE;

							const FVector2D FadeParams = LightProxy->GetDirectionalLightDistanceFadeParameters(View.GetFeatureLevel(), LightSceneInfo->IsPrecomputedLightingValid(), View.MaxShadowCascades);

							ForwardLightData->DirectionalLightDistanceFadeMAD = FVector2f(FadeParams.Y, -FadeParams.X * FadeParams.Y);	// LWC_TODO: Precision loss

							const FMatrix TranslatedWorldToWorld = FTranslationMatrix(-View.ViewMatrices.GetPreViewTranslation());

							if (bDynamicShadows)
							{
								const TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& DirectionalLightShadowInfos = ActiveViewFamily->VisibleLightInfos[LightSceneInfo->Id].AllProjectedShadows;

								ForwardLightData->DirectionalLightVSM = VirtualShadowMapId;

								ForwardLightData->NumDirectionalLightCascades = 0;
								// Unused cascades should compare > all scene depths
								ForwardLightData->CascadeEndDepths = FVector4f(MAX_FLT, MAX_FLT, MAX_FLT, MAX_FLT);

								for (const FProjectedShadowInfo* ShadowInfo : DirectionalLightShadowInfos)
								{
									if (ShadowInfo->DependentView)
									{
										// when rendering stereo views, allow using the shadows rendered for the primary view as 'close enough'
										if (ShadowInfo->DependentView != &View && ShadowInfo->DependentView != View.GetPrimaryView())
										{
											continue;
										}
									}

									const int32 CascadeIndex = ShadowInfo->CascadeSettings.ShadowSplitIndex;

									if (ShadowInfo->IsWholeSceneDirectionalShadow() && !ShadowInfo->HasVirtualShadowMap() && ShadowInfo->bAllocated && CascadeIndex < GMaxForwardShadowCascades)
									{
										const FMatrix WorldToShadow = ShadowInfo->GetWorldToShadowMatrix(ForwardLightData->DirectionalLightShadowmapMinMax[CascadeIndex]);
										const FMatrix44f TranslatedWorldToShadow = FMatrix44f(TranslatedWorldToWorld * WorldToShadow);

										ForwardLightData->NumDirectionalLightCascades++;
										ForwardLightData->DirectionalLightTranslatedWorldToShadowMatrix[CascadeIndex] = TranslatedWorldToShadow;
										ForwardLightData->CascadeEndDepths[CascadeIndex] = ShadowInfo->CascadeSettings.SplitFar;

										if (CascadeIndex == 0)
										{
											ForwardLightData->DirectionalLightShadowmapAtlas = GraphBuilder.RegisterExternalTexture(ShadowInfo->RenderTargets.DepthTarget);
											ForwardLightData->DirectionalLightDepthBias = ShadowInfo->GetShaderDepthBias();
											FVector2D AtlasSize = ForwardLightData->DirectionalLightShadowmapAtlas->Desc.Extent;
											ForwardLightData->DirectionalLightShadowmapAtlasBufferSize = FVector4f(AtlasSize.X, AtlasSize.Y, 1.0f / AtlasSize.X, 1.0f / AtlasSize.Y);
										}
									}
								}
							}

							const FStaticShadowDepthMap* StaticShadowDepthMap = LightSceneInfo->Proxy->GetStaticShadowDepthMap();
							const uint32 bStaticallyShadowedValue = LightSceneInfo->IsPrecomputedLightingValid() && StaticShadowDepthMap && StaticShadowDepthMap->Data && StaticShadowDepthMap->TextureRHI ? 1 : 0;
							ForwardLightData->DirectionalLightUseStaticShadowing = bStaticallyShadowedValue;
							if (bStaticallyShadowedValue)
							{
								const FMatrix44f TranslatedWorldToShadow = FMatrix44f(TranslatedWorldToWorld * StaticShadowDepthMap->Data->WorldToLight);
								ForwardLightData->DirectionalLightStaticShadowBufferSize = FVector4f(StaticShadowDepthMap->Data->ShadowMapSizeX, StaticShadowDepthMap->Data->ShadowMapSizeY, 1.0f / StaticShadowDepthMap->Data->ShadowMapSizeX, 1.0f / StaticShadowDepthMap->Data->ShadowMapSizeY);
								ForwardLightData->DirectionalLightTranslatedWorldToStaticShadow = TranslatedWorldToShadow;
								ForwardLightData->DirectionalLightStaticShadowmap = StaticShadowDepthMap->TextureRHI;
							}
							else
							{
								ForwardLightData->DirectionalLightStaticShadowBufferSize = FVector4f(0, 0, 0, 0);
								ForwardLightData->DirectionalLightTranslatedWorldToStaticShadow = FMatrix44f::Identity;
								ForwardLightData->DirectionalLightStaticShadowmap = GWhiteTexture->TextureRHI;
							}
						}
					}
				}
			}
		}

#if WITH_EDITOR
		// For any views, if there are more than two light that compete for the forward shaded light, we report it.
		bMultipleDirLightsConflictForForwardShading |= ConflictingLightCountForForwardShading >= 2;
#endif

		// Store off the number of lights before we add a fake entry
		const int32 NumLocalLightsFinal = ForwardLocalLightData.Num();

		FRDGBufferRef ForwardLocalLightBuffer = CreateUploadBuffer(GraphBuilder, TEXT("ForwardLocalLightBuffer"), TConstArrayView<FForwardLocalLightData>(ForwardLocalLightData));
		View.ForwardLightingResources.LocalLightVisibleLightInfosIndex = LocalLightVisibleLightInfosIndex;

		const FIntPoint LightGridSizeXY = FIntPoint::DivideAndRoundUp(View.ViewRect.Size(), GLightGridPixelSize);
		ForwardLightData->ForwardLocalLightBuffer = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ForwardLocalLightBuffer, PF_A32B32G32R32F));
		ForwardLightData->NumLocalLights = NumLocalLightsFinal;
		ForwardLightData->NumReflectionCaptures = View.NumBoxReflectionCaptures + View.NumSphereReflectionCaptures;
		ForwardLightData->NumGridCells = LightGridSizeXY.X * LightGridSizeXY.Y * GLightGridSizeZ;
		ForwardLightData->CulledGridSize = FIntVector(LightGridSizeXY.X, LightGridSizeXY.Y, GLightGridSizeZ);
		ForwardLightData->MaxCulledLightsPerCell = GMaxCulledLightsPerCell;
		ForwardLightData->LightGridPixelSizeShift = FMath::FloorLog2(GLightGridPixelSize);
		ForwardLightData->SimpleLightsEndIndex = SimpleLightsEnd;
		ForwardLightData->ClusteredDeferredSupportedEndIndex = ClusteredSupportedEnd;
		ForwardLightData->DirectLightingShowFlag = ActiveViewFamily->EngineShowFlags.DirectLighting ? 1 : 0;

		// Clamp far plane to something reasonable
		const float KilometersToCentimeters = 100000.0f;
		const float LightCullingMaxDistance = GLightCullingMaxDistanceOverrideKilometers <= 0.0f ? (float)HALF_WORLD_MAX / 5.0f : GLightCullingMaxDistanceOverrideKilometers * KilometersToCentimeters;
		float FarPlane = FMath::Min(FMath::Max(FurthestLight, View.FurthestReflectionCaptureDistance), LightCullingMaxDistance);
		FVector ZParams = GetLightGridZParams(View.NearClippingDistance, FarPlane + 10.f);
		ForwardLightData->LightGridZParams = (FVector3f)ZParams;

		const uint64 NumIndexableLights = !bAllowFormatConversion ? (1llu << (sizeof(FLightIndexType32) * 8llu)) : (1llu << (sizeof(FLightIndexType) * 8llu));

		if ((uint64)ForwardLocalLightData.Num() > NumIndexableLights)
		{
			static bool bWarned = false;

			if (!bWarned)
			{
				UE_LOG(LogRenderer, Warning, TEXT("Exceeded indexable light count, glitches will be visible (%u / %llu)"), ForwardLocalLightData.Num(), NumIndexableLights);
				bWarned = true;
			}
		}

#if ENABLE_LIGHT_CULLING_VIEW_SPACE_BUILD_DATA
		const SIZE_T LightIndexTypeSize = !bAllowFormatConversion ? sizeof(FLightIndexType32) : sizeof(FLightIndexType);
		// Fuse these loops as I see no reason why not and we build some temporary data that is needed in the build pass and is 
		// not needed to be stored permanently.
#else // !ENABLE_LIGHT_CULLING_VIEW_SPACE_BUILD_DATA

		ForwardLightDataPerView.Emplace(ForwardLightData);
	}

	const SIZE_T LightIndexTypeSize = !bAllowFormatConversion ? sizeof(FLightIndexType32) : sizeof(FLightIndexType);

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		const FViewInfo& View = Views[ViewIndex];
		FForwardLightData* ForwardLightData = ForwardLightDataPerView[ViewIndex];

		const FIntPoint LightGridSizeXY = FIntPoint::DivideAndRoundUp(View.ViewRect.Size(), GLightGridPixelSize);

#endif // ENABLE_LIGHT_CULLING_VIEW_SPACE_BUILD_DATA

		// Allocate buffers using the scene render targets size so we won't reallocate every frame with dynamic resolution
		const FIntPoint MaxLightGridSizeXY = FIntPoint::DivideAndRoundUp(View.GetSceneTexturesConfig().Extent, GLightGridPixelSize);

		const int32 MaxNumCells = MaxLightGridSizeXY.X * MaxLightGridSizeXY.Y * GLightGridSizeZ * NumCulledGridPrimitiveTypes;

		// Used to pass to the GetDynamicLighting but not actually used, since USE_SOURCE_TEXTURE is 0
		ForwardLightData->DummyRectLightSourceTexture = GWhiteTexture->TextureRHI;

		const FIntVector NumGroups = FIntVector::DivideAndRoundUp(FIntVector(LightGridSizeXY.X, LightGridSizeXY.Y, GLightGridSizeZ), LightGridInjectionGroupSize);

		{
			RDG_EVENT_SCOPE(GraphBuilder, "CullLights %ux%ux%u NumLights %u NumCaptures %u",
				ForwardLightData->CulledGridSize.X,
				ForwardLightData->CulledGridSize.Y,
				ForwardLightData->CulledGridSize.Z,
				ForwardLightData->NumLocalLights,
				ForwardLightData->NumReflectionCaptures);

			const uint32 CulledLightLinksElements = MaxNumCells * GMaxCulledLightsPerCell * LightLinkStride;
			const EPixelFormat CulledLightDataGridFormat = LightIndexTypeSize == sizeof(uint16) ? PF_R16_UINT : PF_R32_UINT;

			FRDGBufferRef CulledLightLinksBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), CulledLightLinksElements), TEXT("CulledLightLinks"));
			FRDGBufferRef StartOffsetGridBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), MaxNumCells), TEXT("StartOffsetGrid"));
			FRDGBufferRef NextCulledLightLinkBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 1), TEXT("NextCulledLightLink"));
			FRDGBufferRef NextCulledLightDataBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 1), TEXT("NextCulledLightData"));
			FRDGBufferUAVRef NextCulledLightDataUAV = GraphBuilder.CreateUAV(NextCulledLightDataBuffer, PF_R32_UINT);
			FRDGBufferRef CulledLightDataGrid       = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(LightIndexTypeSize, MaxNumCells * GMaxCulledLightsPerCell), TEXT("CulledLightDataGrid"));
			FRDGBufferUAVRef CulledLightDataGridUAV = GraphBuilder.CreateUAV(CulledLightDataGrid, CulledLightDataGridFormat);
			FRDGBufferRef NumCulledLightsGrid       = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), MaxNumCells * NumCulledLightsGridStride), TEXT("NumCulledLightsGrid"));
			FRDGBufferUAVRef NumCulledLightsGridUAV = GraphBuilder.CreateUAV(NumCulledLightsGrid, PF_R32_UINT);

			FLightGridInjectionCS::FParameters *PassParameters = GraphBuilder.AllocParameters<FLightGridInjectionCS::FParameters>();

			PassParameters->View                    = View.ViewUniformBuffer;
			PassParameters->ReflectionCapture       = View.ReflectionCaptureUniformBuffer;
			PassParameters->RWNumCulledLightsGrid   = NumCulledLightsGridUAV;
			PassParameters->RWCulledLightDataGrid   = CulledLightDataGridUAV;
			PassParameters->RWNextCulledLightLink   = GraphBuilder.CreateUAV(NextCulledLightLinkBuffer, PF_R32_UINT);
			PassParameters->RWStartOffsetGrid       = GraphBuilder.CreateUAV(StartOffsetGridBuffer, PF_R32_UINT);
			PassParameters->RWCulledLightLinks      = GraphBuilder.CreateUAV(CulledLightLinksBuffer, PF_R32_UINT);
			PassParameters->ForwardLocalLightBuffer = ForwardLightData->ForwardLocalLightBuffer;
			PassParameters->CulledGridSize          = ForwardLightData->CulledGridSize;
			PassParameters->LightGridZParams        = ForwardLightData->LightGridZParams;
			PassParameters->NumReflectionCaptures   = ForwardLightData->NumReflectionCaptures;
			PassParameters->NumLocalLights          = ForwardLightData->NumLocalLights;
			PassParameters->MaxCulledLightsPerCell  = ForwardLightData->MaxCulledLightsPerCell;
			PassParameters->NumGridCells            = ForwardLightData->NumGridCells;
			PassParameters->LightGridPixelSizeShift = ForwardLightData->LightGridPixelSizeShift;

#if ENABLE_LIGHT_CULLING_VIEW_SPACE_BUILD_DATA
			check(ViewSpacePosAndRadiusData.Num() == ForwardLocalLightData.Num());
			check(ViewSpaceDirAndPreprocAngleData.Num() == ForwardLocalLightData.Num());

			FRDGBufferRef LightViewSpacePositionAndRadius  = CreateUploadBuffer(GraphBuilder, TEXT("ViewSpacePosAndRadiusData"), TConstArrayView<FVector4f>(ViewSpacePosAndRadiusData));
			FRDGBufferRef LightViewSpaceDirAndPreprocAngle = CreateUploadBuffer(GraphBuilder, TEXT("ViewSpacePosAndRadiusData"), TConstArrayView<FVector4f>(ViewSpaceDirAndPreprocAngleData));

			PassParameters->LightViewSpacePositionAndRadius  = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(LightViewSpacePositionAndRadius, PF_A32B32G32R32F));
			PassParameters->LightViewSpaceDirAndPreprocAngle = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(LightViewSpaceDirAndPreprocAngle, PF_A32B32G32R32F));
#endif // ENABLE_LIGHT_CULLING_VIEW_SPACE_BUILD_DATA

			FLightGridInjectionCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FLightGridInjectionCS::FUseLinkedListDim>(GLightLinkedListCulling != 0);
			TShaderMapRef<FLightGridInjectionCS> ComputeShader(View.ShaderMap, PermutationVector);

			if (GLightLinkedListCulling != 0)
			{
				AddClearUAVPass(GraphBuilder, PassParameters->RWStartOffsetGrid, 0xFFFFFFFF);
				AddClearUAVPass(GraphBuilder, PassParameters->RWNextCulledLightLink, 0);
				AddClearUAVPass(GraphBuilder, NextCulledLightDataUAV, 0);
				AddClearUAVPass(GraphBuilder, NumCulledLightsGridUAV, 0);
				FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("LightGridInject:LinkedList"), ComputeShader, PassParameters, NumGroups);

				{
					TShaderMapRef<FLightGridCompactCS> ComputeShaderCompact(View.ShaderMap);
					FLightGridCompactCS::FParameters *PassParametersCompact = GraphBuilder.AllocParameters<FLightGridCompactCS::FParameters>();
					PassParametersCompact->View = View.ViewUniformBuffer;

					PassParametersCompact->CulledLightLinks = GraphBuilder.CreateSRV(CulledLightLinksBuffer, PF_R32_UINT);
					PassParametersCompact->RWNumCulledLightsGrid = NumCulledLightsGridUAV;
					PassParametersCompact->RWCulledLightDataGrid = CulledLightDataGridUAV;
					PassParametersCompact->RWNextCulledLightData = NextCulledLightDataUAV;
					PassParametersCompact->StartOffsetGrid = GraphBuilder.CreateSRV(StartOffsetGridBuffer, PF_R32_UINT);

					PassParametersCompact->CulledGridSize = ForwardLightData->CulledGridSize;
					PassParametersCompact->NumReflectionCaptures = ForwardLightData->NumReflectionCaptures;
					PassParametersCompact->NumLocalLights = ForwardLightData->NumLocalLights;
					PassParametersCompact->NumGridCells = ForwardLightData->NumGridCells;

					FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("CompactLinks"), ComputeShaderCompact, PassParametersCompact, NumGroups);
				}
			}
			else
			{
				AddClearUAVPass(GraphBuilder, NumCulledLightsGridUAV, 0);
				FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("LightGridInject:NotLinkedList"), ComputeShader, PassParameters, NumGroups);
			}

			ForwardLightData->CulledLightDataGrid = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(CulledLightDataGrid, CulledLightDataGridFormat));
			ForwardLightData->NumCulledLightsGrid = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(NumCulledLightsGrid, PF_R32_UINT));
			View.ForwardLightingResources.SetUniformBuffer(GraphBuilder.CreateUniformBuffer(ForwardLightData));
		}
	}

#if WITH_EDITOR
	if (bMultipleDirLightsConflictForForwardShading)
	{
		OnGetOnScreenMessages.AddLambda([](FScreenMessageWriter& ScreenMessageWriter)->void
		{
			static const FText Message = NSLOCTEXT("Renderer", "MultipleDirLightsConflictForForwardShading", "Multiple directional lights are competing to be the single one used for forward shading, translucent, water or volumetric fog. Please adjust their ForwardShadingPriority.\nAs a fallback, the main directional light will be selected based on overall brightness.");
			ScreenMessageWriter.DrawLine(Message, 10, FColor::Orange);
		});
	}
#endif
}

void FDeferredShadingSceneRenderer::GatherLightsAndComputeLightGrid(FRDGBuilder& GraphBuilder, bool bNeedLightGrid, FSortedLightSetSceneInfo &SortedLightSet)
{
	bool bShadowedLightsInClustered = ShouldUseClusteredDeferredShading()
		&& CVarVirtualShadowOnePassProjection.GetValueOnRenderThread()
		&& ActiveViewFamily->VirtualShadowMapArray.IsEnabled();

	GatherAndSortLights(SortedLightSet, bShadowedLightsInClustered);
	
	if (!bNeedLightGrid)
	{
		TRDGUniformBufferRef<FForwardLightData> ForwardLightUniformBuffer = CreateDummyForwardLightUniformBuffer(GraphBuilder);
		for (auto& View : Views)
		{
			View.ForwardLightingResources.SetUniformBuffer(ForwardLightUniformBuffer);
		}
		return;
	}

	bool bAnyViewUsesForwardLighting = false;
	bool bAnyViewUsesLumen = false;
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		const FViewInfo& View = Views[ViewIndex];
		bAnyViewUsesForwardLighting |= View.bTranslucentSurfaceLighting || ShouldRenderVolumetricFog() || View.bHasSingleLayerWaterMaterial || VolumetricCloudWantsToSampleLocalLights(Scene, ActiveViewFamily->EngineShowFlags);
		bAnyViewUsesLumen |= GetViewPipelineState(View).DiffuseIndirectMethod == EDiffuseIndirectMethod::Lumen || GetViewPipelineState(View).ReflectionsMethod == EReflectionsMethod::Lumen;
	}
	
	const bool bCullLightsToGrid = GLightCullingQuality 
		&& (IsForwardShadingEnabled(ShaderPlatform) || bAnyViewUsesForwardLighting || IsRayTracingEnabled() || ShouldUseClusteredDeferredShading() ||
			bAnyViewUsesLumen || ActiveViewFamily->EngineShowFlags.VisualizeMeshDistanceFields || ActiveViewFamily->VirtualShadowMapArray.IsEnabled());

	// Store this flag if lights are injected in the grids, check with 'AreLightsInLightGrid()'
	bAreLightsInLightGrid = bCullLightsToGrid;
	
	ComputeLightGrid(GraphBuilder, bCullLightsToGrid, SortedLightSet);
}

void FDeferredShadingSceneRenderer::RenderForwardShadowProjections(
	FRDGBuilder& GraphBuilder,
	const FMinimalSceneTextures& SceneTextures,
	FRDGTextureRef& OutForwardScreenSpaceShadowMask,
	FRDGTextureRef& OutForwardScreenSpaceShadowMaskSubPixel)
{
	CheckShadowDepthRenderCompleted();

	const bool bIsHairEnable = HairStrands::HasViewHairStrandsData(Views);
	bool bScreenShadowMaskNeeded = false;

	FRDGTextureRef SceneDepthTexture = SceneTextures.Depth.Target;

	for (auto LightIt = Scene->Lights.CreateConstIterator(); LightIt; ++LightIt)
	{
		const FLightSceneInfoCompact& LightSceneInfoCompact = *LightIt;
		const FLightSceneInfo* const LightSceneInfo = LightSceneInfoCompact.LightSceneInfo;
		const FVisibleLightInfo& VisibleLightInfo = ActiveViewFamily->VisibleLightInfos[LightSceneInfo->Id];

		bScreenShadowMaskNeeded |= VisibleLightInfo.ShadowsToProject.Num() > 0 || VisibleLightInfo.CapsuleShadowsToProject.Num() > 0 || LightSceneInfo->Proxy->GetLightFunctionMaterial() != nullptr;
	}

	if (bScreenShadowMaskNeeded)
	{
		CSV_SCOPED_TIMING_STAT_EXCLUSIVE(RenderForwardShadingShadowProjections);

		FRDGTextureMSAA ForwardScreenSpaceShadowMask;
		FRDGTextureMSAA ForwardScreenSpaceShadowMaskSubPixel;

		{
			FRDGTextureDesc Desc(FRDGTextureDesc::Create2D(SceneTextures.Config.Extent, PF_B8G8R8A8, FClearValueBinding::White, TexCreate_RenderTargetable | TexCreate_ShaderResource));
			Desc.NumSamples = SceneDepthTexture->Desc.NumSamples;
			ForwardScreenSpaceShadowMask = CreateTextureMSAA(GraphBuilder, Desc, TEXT("ShadowMaskTexture"), GFastVRamConfig.ScreenSpaceShadowMask);
			if (bIsHairEnable)
			{
				ForwardScreenSpaceShadowMaskSubPixel = CreateTextureMSAA(GraphBuilder, Desc, TEXT("ShadowMaskSubPixelTexture"), GFastVRamConfig.ScreenSpaceShadowMask);
			}
		}

		RDG_EVENT_SCOPE(GraphBuilder, "ShadowProjectionOnOpaque");
		RDG_GPU_STAT_SCOPE(GraphBuilder, ShadowProjection);

		// All shadows render with min blending
		AddClearRenderTargetPass(GraphBuilder, ForwardScreenSpaceShadowMask.Target);
		if (bIsHairEnable)
		{
			AddClearRenderTargetPass(GraphBuilder, ForwardScreenSpaceShadowMaskSubPixel.Target);
		}

		const bool bProjectingForForwardShading = true;

		for (auto LightIt = Scene->Lights.CreateConstIterator(); LightIt; ++LightIt)
		{
			const FLightSceneInfoCompact& LightSceneInfoCompact = *LightIt;
			const FLightSceneInfo* const LightSceneInfo = LightSceneInfoCompact.LightSceneInfo;
			FVisibleLightInfo& VisibleLightInfo = ActiveViewFamily->VisibleLightInfos[LightSceneInfo->Id];

			const bool bIssueLightDrawEvent = VisibleLightInfo.ShadowsToProject.Num() > 0 || VisibleLightInfo.CapsuleShadowsToProject.Num() > 0;

			FString LightNameWithLevel;
			GetLightNameForDrawEvent(LightSceneInfo->Proxy, LightNameWithLevel);
			RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, bIssueLightDrawEvent, "%s", *LightNameWithLevel);

			if (VisibleLightInfo.ShadowsToProject.Num() > 0)
			{
				RenderShadowProjections(
					GraphBuilder,
					SceneTextures,
					ForwardScreenSpaceShadowMask.Target,
					ForwardScreenSpaceShadowMaskSubPixel.Target,
					LightSceneInfo,
					bProjectingForForwardShading);

				if (bIsHairEnable)
				{
					RenderHairStrandsShadowMask(GraphBuilder, Views, LightSceneInfo, bProjectingForForwardShading, ForwardScreenSpaceShadowMask.Target);
				}
			}

			RenderCapsuleDirectShadows(GraphBuilder, SceneTextures.UniformBuffer, *LightSceneInfo, ForwardScreenSpaceShadowMask.Target, VisibleLightInfo.CapsuleShadowsToProject, bProjectingForForwardShading);

			if (LightSceneInfo->GetDynamicShadowMapChannel() >= 0 && LightSceneInfo->GetDynamicShadowMapChannel() < 4)
			{
				RenderLightFunction(
					GraphBuilder,
					SceneTextures,
					LightSceneInfo,
					ForwardScreenSpaceShadowMask.Target,
					true, true, false);
			}
		}

		AddCopyToResolveTargetPass(GraphBuilder, ForwardScreenSpaceShadowMask.Target, ForwardScreenSpaceShadowMask.Resolve, FResolveParams());
		OutForwardScreenSpaceShadowMask = ForwardScreenSpaceShadowMask.Resolve;
		if (bIsHairEnable)
		{
			AddCopyToResolveTargetPass(GraphBuilder, ForwardScreenSpaceShadowMaskSubPixel.Target, ForwardScreenSpaceShadowMaskSubPixel.Resolve, FResolveParams());
			OutForwardScreenSpaceShadowMaskSubPixel = ForwardScreenSpaceShadowMaskSubPixel.Resolve;
		}
	}
}
