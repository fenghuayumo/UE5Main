///*
//* Copyright (c) 2021 NVIDIA CORPORATION.  All rights reserved.
//*
//* NVIDIA Corporation and its licensors retain all intellectual property and proprietary
//* rights in and to this software, related documentation and any modifications thereto.
//* Any use, reproduction, disclosure or distribution of this software and related
//* documentation without an express license agreement from NVIDIA Corporation is strictly
//* prohibited.
//*
//* TO THE MAXIMUM EXTENT PERMITTED BY APPLICABLE LAW, THIS SOFTWARE IS PROVIDED *AS IS*
//* AND NVIDIA AND ITS SUPPLIERS DISCLAIM ALL WARRANTIES, EITHER EXPRESS OR IMPLIED,
//* INCLUDING, BUT NOT LIMITED TO, IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
//* PARTICULAR PURPOSE.  IN NO EVENT SHALL NVIDIA OR ITS SUPPLIERS BE LIABLE FOR ANY
//* SPECIAL, INCIDENTAL, INDIRECT, OR CONSEQUENTIAL DAMAGES WHATSOEVER (INCLUDING, WITHOUT
//* LIMITATION, DAMAGES FOR LOSS OF BUSINESS PROFITS, BUSINESS INTERRUPTION, LOSS OF
//* BUSINESS INFORMATION, OR ANY OTHER PECUNIARY LOSS) ARISING OUT OF THE USE OF OR
//* INABILITY TO USE THIS SOFTWARE, EVEN IF NVIDIA HAS BEEN ADVISED OF THE POSSIBILITY OF
//* SUCH DAMAGES.
//*/
//
///*=============================================================================
//	SampledLightRendering.cpp: Light rendering implementation.
//=============================================================================*/
//
//#include "LightRendering.h"
//#include "RendererModule.h"
//#include "DeferredShadingRenderer.h"
//#include "ScenePrivate.h"
//#include "PostProcess/SceneFilterRendering.h"
//#include "PipelineStateCache.h"
//#include "ClearQuad.h"
//#include "Engine/SubsurfaceProfile.h"
//#include "ShowFlags.h"
//#include "VisualizeTexture.h"
//#include "RayTracing/RaytracingOptions.h"
//#include "SceneTextureParameters.h"
//#include "HairStrands/HairStrandsRendering.h"
//#include "ScreenPass.h"
//#include "SkyAtmosphereRendering.h"
//
//#include "Modules/ModuleManager.h"
//#include "Misc/MessageDialog.h"
//
//#if RHI_RAYTRACING
//#include "RayTracing/RayTracingLighting.h"
//
//static TAutoConsoleVariable<int32> CVarSampledLightingDenoiser(
//	TEXT("r.RayTracing.SampledLighting.Denoiser"),
//	2,
//	TEXT("Choose the denoising algorithm.\n")
//	TEXT(" 0: Disabled ;\n")
//	TEXT(" 1: Forces the default denoiser of the renderer;\n")
//	TEXT(" 2: GScreenSpaceDenoiser whitch may be overriden by a third party plugin. This needs the NRD denoiser plugin to work correctly (default)\n"),
//	ECVF_RenderThreadSafe);
//
//
//static TAutoConsoleVariable<int32> CVarSampledLightingCompositeDiffuse(
//	TEXT("r.RayTracing.SampledLighting.CompositeDiffuse"), 1,
//	TEXT("Whether to composite the diffuse signal"),
//	ECVF_RenderThreadSafe);
//
//static TAutoConsoleVariable<int32> CVarSampledLightingCompositeSpecular(
//	TEXT("r.RayTracing.SampledLighting.CompositeSpecular"), 1,
//	TEXT("Whether to composite the specular signal"),
//	ECVF_RenderThreadSafe);
//
//static TAutoConsoleVariable<int32> CVarSampledLightingCompositeMode(
//	TEXT("r.RayTracing.SampledLighting.CompositeMode"), 0,
//	TEXT("How to composite the signal (add = 0, replace = 1)"),
//	ECVF_RenderThreadSafe);
//
//static TAutoConsoleVariable<int32> CVarSampledLightingMode(
//	TEXT("r.RayTracing.SampledLighting.Mode"), 1,
//	TEXT("Which mode to process sampled lighting with\n")
//	TEXT("  0 - monolithic single pass \n")
//	TEXT("  1 - multipass ReSTIRs style (Default)"),
//	ECVF_RenderThreadSafe);
//
//static TAutoConsoleVariable<int32> CVarSampledLightingSpatial(
//	TEXT("r.RayTracing.SampledLighting.Spatial"), 1,
//	TEXT("Whether to apply spatial resmapling"),
//	ECVF_RenderThreadSafe);
//
//static TAutoConsoleVariable<int32> CVarSampledLightingInitialCandidates(
//	TEXT("r.RayTracing.SampledLighting.InitialSamples"), 8,
//	TEXT("How many lights to test sample during the initial candidate search"),
//	ECVF_RenderThreadSafe);
//
//static TAutoConsoleVariable<int32> CVarSampledLightingInitialCandidatesBoost(
//	TEXT("r.RayTracing.SampledLighting.InitialSamplesBoost"), 32,
//	TEXT("How many lights to test sample during the initial candidate search when history is invalidated"),
//	ECVF_RenderThreadSafe);
//
//static TAutoConsoleVariable<int32> CVarSampledLightingTemporal(
//	TEXT("r.RayTracing.SampledLighting.Temporal"), 1,
//	TEXT("Whether to use temporal resampling for the reserviors"),
//	ECVF_RenderThreadSafe);
//
//static TAutoConsoleVariable<int32> CVarSampledLightingApplyBoilingFilter(
//	TEXT("r.RayTracing.SampledLighting.ApplyBoilingFilter"), 1,
//	TEXT("Whether to apply boiling filter when temporally resampling"),
//	ECVF_RenderThreadSafe);
//
//static TAutoConsoleVariable<float> CVarSampledLightingBoilingFilterStrength(
//	TEXT("r.RayTracing.SampledLighting.BoilingFilterStrength"), 0.05f,
//	TEXT("Strength of Boiling filter"),
//	ECVF_RenderThreadSafe);
//
//static TAutoConsoleVariable<float> CVarSampledLightingSpatialSamplingRadius(
//	TEXT("r.RayTracing.SampledLighting.Spatial.SamplingRadius"), 32.0f,
//	TEXT("Spatial radius for sampling in pixels (Default 32.0)"),
//	ECVF_RenderThreadSafe);
//
//static TAutoConsoleVariable<int32> CVarSampledLightingSpatialSamples(
//	TEXT("r.RayTracing.SampledLighting.Spatial.Samples"), 8,
//	TEXT("Spatial samples per pixel"),
//	ECVF_RenderThreadSafe);
//
//static TAutoConsoleVariable<int32> CVarSampledLightingSpatialSamplesBoost(
//	TEXT("r.RayTracing.SampledLighting.Spatial.SamplesBoost"), 16,
//	TEXT("Spatial samples per pixel when invalid history is detected"),
//	ECVF_RenderThreadSafe);
//
//static TAutoConsoleVariable<float> CVarSampledLightingSpatialNormalRejectionThreshold(
//	TEXT("r.RayTracing.SampledLighting.Spatial.NormalRejectionThreshold"), 0.5f,
//	TEXT("Rejection threshold for rejecting samples based on normal differences (default 0.5)"),
//	ECVF_RenderThreadSafe);
//
//static TAutoConsoleVariable<float> CVarSampledLightingSpatialDepthRejectionThreshold(
//	TEXT("r.RayTracing.SampledLighting.Spatial.DepthRejectionThreshold"), 0.1f,
//	TEXT("Rejection threshold for rejecting samples based on depth differences (default 0.1)"),
//	ECVF_RenderThreadSafe);
//
//static TAutoConsoleVariable<int32> CVarSampledLightingSpatialApplyApproxVisibility(
//	TEXT("r.RayTracing.SampledLighting.Spatial.ApplyApproxVisibility"), 0,
//	TEXT("Apply an approximate visibility test on sample selected during spatial sampling"),
//	ECVF_RenderThreadSafe);
//
//static TAutoConsoleVariable<int32> CVarSampledLightingSpatialDiscountNaiveSamples(
//	TEXT("r.RayTracing.SampledLighting.Spatial.DiscountNaiveSamples"), 1,
//	TEXT("During spatial sampling, reduce the weights of 'naive' samples that lack history"),
//	ECVF_RenderThreadSafe);
//
//static TAutoConsoleVariable<int32> CVarSampledLightingTemporalMaxHistory(
//	TEXT("r.RayTracing.SampledLighting.Temporal.MaxHistory"), 10,
//	TEXT("Maximum temporal history for samples (default 10)"),
//	ECVF_RenderThreadSafe);
//
//static TAutoConsoleVariable<float> CVarSampledLightingTemporalNormalRejectionThreshold(
//	TEXT("r.RayTracing.SampledLighting.Temporal.NormalRejectionThreshold"), 0.5f,
//	TEXT("Rejection threshold for rejecting samples based on normal differences (default 0.5)"),
//	ECVF_RenderThreadSafe);
//
//static TAutoConsoleVariable<float> CVarSampledLightingTemporalDepthRejectionThreshold(
//	TEXT("r.RayTracing.SampledLighting.Temporal.DepthRejectionThreshold"), 0.1f,
//	TEXT("Rejection threshold for rejecting samples based on depth differences (default 0.1)"),
//	ECVF_RenderThreadSafe);
//
//static TAutoConsoleVariable<int32> CVarSampledLightingTemporalApplyApproxVisibility(
//	TEXT("r.RayTracing.SampledLighting.Temporal.ApplyApproxVisibility"), 0,
//	TEXT("Apply an approximate visibility test on sample selected during reprojection"),
//	ECVF_RenderThreadSafe);
//
//static TAutoConsoleVariable<int32> CVarSampledLightingTemporalApplySpatialHash(
//	TEXT("r.RayTracing.SampledLighting.Temporal.ApplySpatialHash"), 1,
//	TEXT("Apply a spatial hash during temporal reprojection reprojection, can improve behavior of flat surfaces, but enhance noise on thin surfaces"),
//	ECVF_RenderThreadSafe);
//
//static TAutoConsoleVariable<int32> CVarSampledLightingDemodulateMaterials(
//	TEXT("r.RayTracing.SampledLighting.DemodulateMaterials"), 1,
//	TEXT("Whether to demodulate the material contributiuon from the signal for denoising"),
//	ECVF_RenderThreadSafe);
//
//static TAutoConsoleVariable<int32> CVarSampledLightingFaceCull(
//	TEXT("r.RayTracing.SampledLighting.FaceCull"), 0,
//	TEXT("Face culling to use for visibility tests\n")
//	TEXT("  0 - none (Default)\n")
//	TEXT("  1 - front faces (equivalent to backface culling in shadow maps)\n")
//	TEXT("  2 - back faces"),
//	ECVF_RenderThreadSafe);
//
//static TAutoConsoleVariable<int32> CVarSampledLightingApproximateVisibilityMode(
//	TEXT("r.RayTracing.SampledLighting.ApproximateVisibilityMode"), 0,
//	TEXT("Visibility mode for approximate visibility tests (default 0/accurate)\n")
//	TEXT("  0 - Accurate, any hit shaders process alpha coverage\n")
//	TEXT("  1 - Force opaque, anyhit shaders ignored, alpha coverage considered 100%\n")
//	TEXT("  2 - Force transparent, anyhit shaders ignored, alpha coverage considered 0%"),
//	ECVF_RenderThreadSafe);
//
//static TAutoConsoleVariable<int32> CVarSampledLightingNumReservoirs(
//	TEXT("r.RayTracing.SampledLighting.NumReservoirs"), -1,
//	TEXT("Number of independent light reservoirs per pixel\n")
//	TEXT("  1-N - Explicit number of reservoirs\n")
//	TEXT("  -1 - Auto-select based on subsampling (default)"),
//	ECVF_RenderThreadSafe);
//
//static TAutoConsoleVariable<int32> CVarSampledLightingMinReservoirs(
//	TEXT("r.RayTracing.SampledLighting.MinReservoirs"), 1,
//	TEXT("Minimum number of light reservoirs when auto-seleting(default 1)"),
//	ECVF_RenderThreadSafe);
//
//static TAutoConsoleVariable<int32> CVarSampledLightingMaxReservoirs(
//	TEXT("r.RayTracing.SampledLighting.MaxReservoirs"), 2,
//	TEXT("Maximum number of light reservoirs when auto-seleting (default 2)"),
//	ECVF_RenderThreadSafe);
//
//static TAutoConsoleVariable<int32> CVarSampledLightingFusedSampling(
//	TEXT("r.RayTracing.SampledLighting.FusedSampling"), 1,
//	TEXT("Whether to fuse initial candidate and temporal sampling (default 0)"),
//	ECVF_RenderThreadSafe);
//
//static TAutoConsoleVariable<int32> CVarSampledLighting(
//	TEXT("r.RayTracing.SampledDirectLighting"), 0,
//	TEXT("Whether to use sampling for evaluating direct lighting"),
//	ECVF_RenderThreadSafe);
//
//static TAutoConsoleVariable<int32> CVarRayTracingSampledLightingDirectionalLight(
//	TEXT("r.RayTracing.SampledLighting.Lights.Directional"),
//	1,
//	TEXT("Enables ray traced sampled lighting for directional lights (default = 1)"),
//	ECVF_RenderThreadSafe);
//
//static TAutoConsoleVariable<int32> CVarRayTracingSampledLightingPointLight(
//	TEXT("r.RayTracing.SampledLighting.Lights.Point"),
//	1,
//	TEXT("Enables ray traced sampled lighting for point lights (default = 1)"),
//	ECVF_RenderThreadSafe);
//
//static TAutoConsoleVariable<int32> CVarRayTracingSampledLightingSpotLight(
//	TEXT("r.RayTracing.SampledLighting.Lights.Spot"),
//	1,
//	TEXT("Enables ray traced sampled lighting for spot lights (default = 1)"),
//	ECVF_RenderThreadSafe);
//
//static TAutoConsoleVariable<int32> CVarRayTracingSampledLightingRectLight(
//	TEXT("r.RayTracing.SampledLighting.Lights.Rect"),
//	1,
//	TEXT("Enables ray traced sampled lighting for rect light (default = 1)"),
//	ECVF_RenderThreadSafe);
//
//static TAutoConsoleVariable<int32> CVarRayTracingSampledLightingSkylight(
//	TEXT("r.RayTracing.SampledLighting.Lights.Sky"),
//	0,
//	TEXT("Enables ray traced sampled lighting for skylight (default = 0)"),
//	ECVF_RenderThreadSafe);
//
//static TAutoConsoleVariable<int32> CVarRayTracingSampledLightingParticle(
//	TEXT("r.RayTracing.SampledLighting.Lights.Particle"),
//	1,
//	TEXT("Enables ray traced sampled lighting for particle lights (default = 0)\n")
//	TEXT(" 0 - off, particle lights use standard rendering systems\n")
//	TEXT(" 1 - on, particle systems opting in use sampled lighting with shadow casting\n")
//	TEXT(" 2 - forced, all particle lights will be used with sampled lighting"),
//	ECVF_RenderThreadSafe);
//
//static TAutoConsoleVariable<int32> CVarRayTracingSampledLightingFunctionLights(
//	TEXT("r.RayTracing.SampledLighting.Lights.FunctionLights"),
//	1,
//	TEXT("Enables ray traced sampled lighting forlights with light functions (default = 0)"),
//	ECVF_RenderThreadSafe);
//
//static TAutoConsoleVariable<int32> CVarRayTracingSampledLightingDebugMode(
//	TEXT("r.RayTracing.SampledLighting.DebugMode"),
//	0,
//	TEXT("Debug visualization mode (default = 0)"),
//	ECVF_RenderThreadSafe);
//
//static TAutoConsoleVariable<int32> CVarRayTracingSampledLightingFeedbackVisibility(
//	TEXT("r.RayTracing.SampledLighting.FeedbackVisibility"),
//	1,
//	TEXT("Whether to feedback the final visibility result to the history (default = 1)"),
//	ECVF_RenderThreadSafe);
//
//static TAutoConsoleVariable<int32> CVarRayTracingSampledLightingTestInitialVisibility(
//	TEXT("r.RayTracing.SampledLighting.TestInitialVisibility"),
//	1,
//	TEXT("Test initial samples for visibility (default = 1)\n")
//	TEXT("  0 - Do not test visibility during inital sampling\n")
//	TEXT("  1 - Test visibility on final merged reservoir  (default)\n")
//	TEXT("  2 - Test visibility on reservoirs prior to merging\n"),
//	ECVF_RenderThreadSafe);
//
//static TAutoConsoleVariable<int32> CVarRayTracingSampledLightingEnableHairVoxel(
//	TEXT("r.RayTracing.SampledLighting.EnableHairVoxel"),
//	1,
//	TEXT("Whether to test hair voxels for visibility when evaluating (default = 1)\n"),
//	ECVF_RenderThreadSafe);
//
////
//// CVars tied to brute-force sampling
////
//static TAutoConsoleVariable<int32> CVarRayTracingSampledLightingDirectNumSamples(
//	TEXT("r.RayTracing.SampledLighting.Direct.NumSamples"),
//	4,
//	TEXT("Number of samples used when evaluating the direct sampling pass (no spatial or temporal reuse) (default = 4)"),
//	ECVF_RenderThreadSafe);
//
//static TAutoConsoleVariable<int32> CVarRayTracingSampledLightingDirectNumCandidates(
//	TEXT("r.RayTracing.SampledLighting.Direct.NumCandidates"),
//	4,
//	TEXT("Number of candidates used per-sample when evaluating the direct sampling pass (no spatial or temporal reuse) (default = 4)"),
//	ECVF_RenderThreadSafe);
//
////
//// CVars controlling RIS Buffer setup for light presampling
////
//
//static TAutoConsoleVariable<int32> CVarRayTracingSampledLightingRISTiles(
//	TEXT("r.RayTracing.SampledLighting.RIS.Tiles"),
//	1024,
//	TEXT("Number of tiles of presampled lights in the RIS buffer (default = 1024)"),
//	ECVF_RenderThreadSafe);
//
//static TAutoConsoleVariable<int32> CVarRayTracingSampledLightingRISTileSize(
//	TEXT("r.RayTracing.SampledLighting.RIS.TileSize"),
//	256,
//	TEXT("Number of samples per tile in the RIS buffer (default = 256)"),
//	ECVF_RenderThreadSafe);
//
//static TAutoConsoleVariable<float> CVarRayTracingSampledLightingRISWeighted(
//	TEXT("r.RayTracing.SampledLighting.RIS.WeightedSampling"),
//	0.75f,
//	TEXT("How heavily to weight light power in the MIS selection of lights during presampling (default 0.75 or 75% of samples based on light power)"),
//	ECVF_RenderThreadSafe);
//
////
//// CVars controlling ReGIR presampling of local lights
////
//static TAutoConsoleVariable<float> CVarRayTracingSampledLightingReGIRCellSize(
//	TEXT("r.RayTracing.SampledLighting.ReGIR.CellSize"),
//	400.0f,
//	TEXT("Size of grid cells near camera for ReGIR sampling in UE units (default 400/4m)"),
//	ECVF_RenderThreadSafe);
//
//static TAutoConsoleVariable<int32> CVarRayTracingSampledLightingReGIRSampleIterations(
//	TEXT("r.RayTracing.SampledLighting.ReGIR.SampleIterations"),
//	8,
//	TEXT("Number of sample iterations per grid sample (default 8)"),
//	ECVF_RenderThreadSafe);
//
//static TAutoConsoleVariable<int32> CVarRayTracingSampledLightingReGIRSamplesPerCell(
//	TEXT("r.RayTracing.SampledLighting.ReGIR.SamplesPerCell"),
//	64,
//	TEXT("Sample Pool size for each grid cell (default 64)"),
//	ECVF_RenderThreadSafe);
//
//static TAutoConsoleVariable<int32> CVarRayTracingSampledLightingReGIRCellsX(
//	TEXT("r.RayTracing.SampledLighting.ReGIR.CellsX"),
//	64,
//	TEXT("Cells in primary direction (default 64)"),
//	ECVF_RenderThreadSafe);
//
//static TAutoConsoleVariable<int32> CVarRayTracingSampledLightingReGIRCellsY(
//	TEXT("r.RayTracing.SampledLighting.ReGIR.CellsY"),
//	64,
//	TEXT("Cells in secondary direction (default 64)"),
//	ECVF_RenderThreadSafe);
//
//static TAutoConsoleVariable<int32> CVarRayTracingSampledLightingReGIRCellsZ(
//	TEXT("r.RayTracing.SampledLighting.ReGIR.CellsZ"),
//	8,
//	TEXT("Cells in tertiary direction (default 8)"),
//	ECVF_RenderThreadSafe);
//
////
//// CVars controlling shader permutations
////
//static TAutoConsoleVariable<int32> CVarRayTracingSampledLightingEvaluationMode(
//	TEXT("r.RayTracing.SampledLighting.Permute.EvaluationMode"),
//	1,
//	TEXT("Method for computing the light estimate used for driving sampling\n")
//	TEXT("  0 - Use standard integrated lighting via the GetDynamicLightingSplit function, similar to raster\n")
//	TEXT("  1 - Use sampled lighting like the path tracer (default)"),
//	ECVF_RenderThreadSafe);
//
//static TAutoConsoleVariable<int32> CVarRayTracingSampledLightingCompressedLights(
//	TEXT("r.RayTracing.SampledLighting.Permute.CompressedLightData"),
//	0,
//	TEXT("Whether to use compressed data for representing lights\n")
//	TEXT("  0 - Light data uses full fp32 or int32 precision (default)\n")
//	TEXT("  1 - Light data uses compressed representation, like 16 bit floats or fixed point"),
//	ECVF_RenderThreadSafe);
//
//static TAutoConsoleVariable<int32> CVarRayTracingSampledLightingPresampleLocalLights(
//	TEXT("r.RayTracing.SampledLighting.Permute.PresampleLocalLights"),
//	0,
//	TEXT("Whether to presample local lights\n")
//	TEXT("  0 - Do not presample, use uniform randoms for selecting local lights (default)\n")
//	TEXT("  1 - Presample lights using RIS buffer (sampling strictly on global power)")
//	TEXT("  2 - Presample lights using ReGIR (sampling on world-space grid)"),
//	ECVF_RenderThreadSafe);
//
////
//// CVars controlling strand-based hair
////
//static TAutoConsoleVariable<int32> CVarRayTracingSampledLightingHair(
//	TEXT("r.RayTracing.SampledLighting.Hair"),
//	1,
//	TEXT("Whether to evaluate sampled lighting on strand-based hair\n")
//	TEXT("  0 - Skip strand-based lighting pass\n")
//	TEXT("  1 - Run separate sampled lighting pass for strand-based hair (default)"),
//	ECVF_RenderThreadSafe);
//
//static TAutoConsoleVariable<int32> CVarRayTracingSampledLightingHairSamples(
//	TEXT("r.RayTracing.SampledLighting.Hair.Samples"),
//	4,
//	TEXT("How many light samples to evaluate on strand-based hair (default 4)"),
//	ECVF_RenderThreadSafe);
//
//static TAutoConsoleVariable<int32> CVarRayTracingSampledLightingHairCandidates(
//	TEXT("r.RayTracing.SampledLighting.Hair.Candidates"),
//	4,
//	TEXT("How many light candidates to test per sample for strand-based hair (default 4)"),
//	ECVF_RenderThreadSafe);
//
//struct FSampledLightingPresets
//{
//	int32 CorrectionMode;
//	int32 InitialSamples;
//	int32 SpatialSamples;
//	int32 DisocclusionSamples;
//	int32 MinReservoirs;
//	int32 MaxReservoirs;
//};
//
//static const FSampledLightingPresets SampledLightingPresets[] =
//{
//	{ 0, 2, 1, 8, 1, 2},
//	{ 0, 4, 4, 8, 1, 2},
//	{ 0, 16, 8, 16, 1, 2},
//	{ 1, 32, 8, 16, 1, 2},
//	{ 1, 32, 8, 16, 2, 3}
//};
//
//static FAutoConsoleCommand GSendRemoteTalkersToEndpointCommand(
//	TEXT("r.RayTracing.SampledLighting.Preset"),
//	TEXT("Command applies preset quality levels for sampled lighting\n")
//	TEXT("  Available levels: low, medium, high, epic, cinematic"),
//	FConsoleCommandWithArgsDelegate::CreateStatic(
//		[](const TArray< FString >& Args)
//{
//	int32 QualityLevel = -1;
//	if (Args.Num() == 1)
//	{
//		if (Args[0] == TEXT("low"))
//		{
//			QualityLevel = 0;
//		}
//		if (Args[0] == TEXT("medium"))
//		{
//			QualityLevel = 1;
//		}
//		if (Args[0] == TEXT("high"))
//		{
//			QualityLevel = 2;
//		}
//		if (Args[0] == TEXT("epic") || Args[0] == TEXT("ultra"))
//		{
//			QualityLevel = 3;
//		}
//		if (Args[0] == TEXT("cinematic"))
//		{
//			QualityLevel = 4;
//		}
//	}
//
//	if (QualityLevel == -1)
//	{
//		UE_LOG(LogConsoleResponse, Display, TEXT("Invalid arguments for setting sampled lighting presets (options: low, medium, high, epic, cinematic)"));
//	}
//	else
//	{
//		check(QualityLevel >= 0 && QualityLevel < (sizeof(SampledLightingPresets) / sizeof(SampledLightingPresets[0])));
//
//		const FSampledLightingPresets& Presets = SampledLightingPresets[QualityLevel];
//
//		// Correction mode / approximate visibility shared for temporal/spatial
//		CVarSampledLightingTemporalApplyApproxVisibility.AsVariable()->Set(Presets.CorrectionMode, ECVF_SetByConsole);
//		CVarSampledLightingSpatialApplyApproxVisibility.AsVariable()->Set(Presets.CorrectionMode, ECVF_SetByConsole);
//
//		// spatial sample count
//		CVarSampledLightingSpatialSamples.AsVariable()->Set(Presets.SpatialSamples, ECVF_SetByConsole);
//
//		// boosted spatial count
//		CVarSampledLightingSpatialSamplesBoost.AsVariable()->Set(Presets.DisocclusionSamples, ECVF_SetByConsole);
//
//		// initial sample count
//		CVarSampledLightingInitialCandidates.AsVariable()->Set(Presets.InitialSamples, ECVF_SetByConsole);
//
//		// min reservoirs
//		CVarSampledLightingMinReservoirs.AsVariable()->Set(Presets.MinReservoirs, ECVF_SetByConsole);
//
//		// max reservoirs
//		CVarSampledLightingMinReservoirs.AsVariable()->Set(Presets.MinReservoirs, ECVF_SetByConsole);
//		
//	}
//})
//);
//
//bool ShouldRenderRayTracingSampledLighting()
//{
//	return ShouldRenderRayTracingEffect(CVarSampledLighting.GetValueOnRenderThread() > 0, ERayTracingPipelineCompatibilityFlags::FullPipeline, nullptr);
//}
//
//bool SupportSampledLightingForType(ELightComponentType Type)
//{
//	bool Result = false;
//
//	switch (Type)
//	{
//		case LightType_Directional:
//			Result = CVarRayTracingSampledLightingDirectionalLight.GetValueOnAnyThread() != 0;
//			break;
//		case LightType_Point:
//			Result = CVarRayTracingSampledLightingPointLight.GetValueOnAnyThread() != 0;
//			break;
//		case LightType_Spot:
//			Result = CVarRayTracingSampledLightingSpotLight.GetValueOnAnyThread() != 0;
//			break;
//		case LightType_Rect:
//			Result = CVarRayTracingSampledLightingRectLight.GetValueOnAnyThread() != 0;
//			break;
//		default:
//			break;
//	}
//
//	return Result;
//}
//
//bool SupportSampledLightingForLightFunctions()
//{
//	return CVarRayTracingSampledLightingFunctionLights.GetValueOnRenderThread() != 0;
//}
//
//bool ShouldUseSampledSkyLighting()
//{
//	return ShouldRenderRayTracingSampledLighting() && CVarRayTracingSampledLightingSkylight.GetValueOnRenderThread() != 0;
//}
//
//int32 UseSampledLightingForParticles()
//{
//	int32 Result = 0;
//
//	if (ShouldRenderRayTracingSampledLighting())
//	{
//		Result = FMath::Clamp(CVarRayTracingSampledLightingParticle.GetValueOnRenderThread(), 0, 2);
//	}
//
//	return Result;
//}
//
//static bool SampledLightingLightFunctionApproved(const FLightSceneProxy* LightProxy)
//{
//	//EHartNV - ideally, we'd check if the light function material really was a light function, but that requires a scene
//	if (LightProxy->GetLightFunctionMaterial())
//	{
//		return SupportSampledLightingForLightFunctions();
//	}
//	return true;
//}
//
//bool ShouldRenderSampledShadowsForLight(const FLightSceneProxy& LightProxy)
//{
//	return ( ShouldRenderRayTracingSampledLighting() && LightProxy.SupportsSampledLighting())
//		&& SupportSampledLightingForType((ELightComponentType)LightProxy.GetLightType())
//		&& SampledLightingLightFunctionApproved(&LightProxy)
//		&& IsRayTracingEnabled();
//}
//
//bool ShouldRenderSampledShadowsForLight(const FLightSceneInfoCompact& LightInfo)
//{
//	// EHartNV - should sampled lighting support be cached on the LightSceneInfoCompact?
//	return (ShouldRenderRayTracingSampledLighting() && LightInfo.LightSceneInfo->Proxy->SupportsSampledLighting())
//		&& SupportSampledLightingForType((ELightComponentType)LightInfo.LightType)
//		&& SampledLightingLightFunctionApproved(LightInfo.LightSceneInfo->Proxy)
//		&& IsRayTracingEnabled();
//}
//
////defines to disable features
//#define SUPPORT_LIGHT_MATERIAL_FUNCTIONS	1
//#define SUPPORT_IES_PROFILES				1
//
//
//BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FSampledLightData, )
//	SHADER_PARAMETER(uint32, DirectionalLightCount)
//	SHADER_PARAMETER(uint32, DirectionalLightStart)
//	SHADER_PARAMETER(uint32, LocalLightCount)
//	SHADER_PARAMETER(uint32, LocalLightStart)
//	SHADER_PARAMETER(float, IESLightProfileInvCount)
//	SHADER_PARAMETER(uint32, LightHistoryOffset)
//	SHADER_PARAMETER(FVector3f, SkylightColor)
//	SHADER_PARAMETER_TEXTURE(Texture2D, LTCMatTexture)
//	SHADER_PARAMETER_SAMPLER(SamplerState, LTCMatSampler)
//	SHADER_PARAMETER_TEXTURE(Texture2D, LTCAmpTexture)
//	SHADER_PARAMETER_SAMPLER(SamplerState, LTCAmpSampler)
//	SHADER_PARAMETER_TEXTURE(Texture2D, RectLightTexture0)
//	SHADER_PARAMETER_TEXTURE(Texture2D, RectLightTexture1)
//	SHADER_PARAMETER_TEXTURE(Texture2D, RectLightTexture2)
//	SHADER_PARAMETER_TEXTURE(Texture2D, RectLightTexture3)
//	SHADER_PARAMETER_TEXTURE(Texture2D, RectLightTexture4)
//	SHADER_PARAMETER_TEXTURE(Texture2D, RectLightTexture5)
//	SHADER_PARAMETER_TEXTURE(Texture2D, RectLightTexture6)
//	SHADER_PARAMETER_TEXTURE(Texture2D, RectLightTexture7)
//	SHADER_PARAMETER_SAMPLER(SamplerState, IESLightProfileTextureSampler)
//	SHADER_PARAMETER_TEXTURE(Texture2DArray, IESLightProfileTexture)
//	SHADER_PARAMETER_TEXTURE(Texture2D, SSProfilesTexture)
//	SHADER_PARAMETER_SRV(StructuredBuffer<uint4>, LightDataBuffer)
//	SHADER_PARAMETER_SRV(StructuredBuffer<uint4>, PackedLightDataBuffer)
//	SHADER_PARAMETER_SRV(StructuredBuffer<int>, LightIndexRemapTable)
//	SHADER_PARAMETER_SRV(StructuredBuffer<int>, LightIndexBackwardRemapTable)
//END_GLOBAL_SHADER_PARAMETER_STRUCT()
//
//IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FSampledLightData, "SampledLightData");
//
//BEGIN_SHADER_PARAMETER_STRUCT(FSampledLightingCommonParameters, )
//	SHADER_PARAMETER(float, MaxNormalBias)
//	SHADER_PARAMETER(int32, VisibilityApproximateTestMode)
//	SHADER_PARAMETER(int32, VisibilityFaceCull)
//	SHADER_PARAMETER(int32, SupportTranslucency)
//	SHADER_PARAMETER(int32, InexactShadows)
//	SHADER_PARAMETER(float, MaxBiasForInexactGeometry)
//	SHADER_PARAMETER(float, ApproxVisibilityMinDistance)
//	SHADER_PARAMETER(int32, MaxTemporalHistory)
//	SHADER_PARAMETER_SRV(RaytracingAccelerationStructure, TLAS)
//	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint4>, RWLightReservoirUAV)
//	SHADER_PARAMETER(FIntVector, ReservoirBufferDim)
//	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint2>, RISBuffer)
//	SHADER_PARAMETER(int32, RISBufferTiles)
//	SHADER_PARAMETER(int32, RISBufferTileSize)
//	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint2>, ReGIRBuffer)
//	SHADER_PARAMETER(float, CellSize)
//	SHADER_PARAMETER(FIntVector, CellCount)
//	SHADER_PARAMETER(int32, ReGIRTileSize)
//	SHADER_PARAMETER(int32, RISSkylightBufferTiles)
//	SHADER_PARAMETER(int32, RISSkylightBufferTileSize)
//	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint2>, RISSkylightBuffer)
//	SHADER_PARAMETER(float, InvSkylightSize)
//	SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, SkylightTexture)
//	SHADER_PARAMETER_SAMPLER(SamplerState, SkylightSampler)
//END_SHADER_PARAMETER_STRUCT()
//
//BEGIN_SHADER_PARAMETER_STRUCT(FSampledLightingSceenTraceParameters, )
//	SHADER_PARAMETER(FVector4f, HZBUvFactorAndInvFactor)
//	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ClosestHZBTexture)
//	SHADER_PARAMETER(FVector2f, HZBBaseTexelSize)
//	SHADER_PARAMETER(float, MaxHierarchicalScreenTraceIterations)
//	SHADER_PARAMETER(float, RelativeDepthThickness)
//	SHADER_PARAMETER(float, MaxScreenTraceDistance)
//	SHADER_PARAMETER(FVector4f, HZBUVToScreenUVScaleBias)
//END_SHADER_PARAMETER_STRUCT()
//
//static void ApplySampledLightingGlobalSettings(FShaderCompilerEnvironment& OutEnvironment)
//{
//	OutEnvironment.SetDefine(TEXT("RTXDI_INTEGRATION_VERSION"), 4270);
//
//	OutEnvironment.SetDefine(TEXT("LIGHT_ESTIMATION_MODE"), 1);
//	OutEnvironment.SetDefine(TEXT("USE_ALTERNATE_RNG"), 0);
//	OutEnvironment.SetDefine(TEXT("USE_LDS_FOR_SPATIAL_RESAMPLE"), 1);
//}
//
//class FDirectLightRGS : public FGlobalShader
//{
//	DECLARE_GLOBAL_SHADER(FDirectLightRGS)
//	SHADER_USE_ROOT_PARAMETER_STRUCT(FDirectLightRGS, FGlobalShader)
//
//	class FHairShadingDim : SHADER_PERMUTATION_BOOL("HAIR_SHADING");
//	class FScreenTraceDim : SHADER_PERMUTATION_INT("SCREEN_TRACE", 2);
//
//	using FPermutationDomain = TShaderPermutationDomain<FHairShadingDim,FScreenTraceDim>;
//
//	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
//	{
//		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
//	}
//
//	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
//	{
//		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
//		ApplySampledLightingGlobalSettings(OutEnvironment);
//	}
//
//	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
//
//		SHADER_PARAMETER(int32, DemodulateMaterials)
//		SHADER_PARAMETER(uint32, BruteForceSamples)
//		SHADER_PARAMETER(uint32, BruteForceCandidates)
//		SHADER_PARAMETER_STRUCT_REF(FSampledLightData, SampledLightData)
//		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
//		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneLightingChannelParameters, SceneLightingChannels)
//
//		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWDiffuseUAV)
//		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWSpecularUAV)
//		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, RWRayDistanceUAV)
//		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
//
//		SHADER_PARAMETER_STRUCT_INCLUDE(FSampledLightingCommonParameters, SampledLightingCommonParameters)
//		SHADER_PARAMETER_STRUCT_INCLUDE(FSampledLightingSceenTraceParameters, ScreenTraceParameters)
//
//		SHADER_PARAMETER_TEXTURE(Texture2D, SSProfilesTexture)
//
//		// Hair only parameters
//		SHADER_PARAMETER(float, HairDualScatteringRoughnessOverride)
//		SHADER_PARAMETER(uint32, HairTransmittanceBufferMaxCount)
//		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, HairVisibilityNodeOffsetAndCount)
//		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FPackedHairSample>, HairVisibilityNodeData)
//		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>,HairVisibilityNodeCoords)
//		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FPackedHairTransmittanceMask>, HairTransmittanceBuffer)
//		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, HairVisibilityNodeCount)
//	
//
//	END_SHADER_PARAMETER_STRUCT()
//};
//
//IMPLEMENT_GLOBAL_SHADER(FDirectLightRGS, "/Engine/Private/RTXDI/RayTracingSampledDirectLighting.usf", "SampledDirectLightingRGS", SF_RayGen);
//
//class FGenerateInitialSamplesRGS : public FGlobalShader
//{
//	DECLARE_GLOBAL_SHADER(FGenerateInitialSamplesRGS)
//	SHADER_USE_ROOT_PARAMETER_STRUCT(FGenerateInitialSamplesRGS, FGlobalShader)
//
//	class FEvaluateLightingDim : SHADER_PERMUTATION_BOOL("EVALUATE_LIGHTING_SAMPLED");
//	class FCompressedLightDataDim : SHADER_PERMUTATION_BOOL("USE_COMPRESSED_LIGHT_DATA");
//	class FRisSampleLocalLightsDim : SHADER_PERMUTATION_INT("RIS_SAMPLE_LOCAL_LIGHTS", 3);
//
//	using FPermutationDomain = TShaderPermutationDomain<FEvaluateLightingDim, FCompressedLightDataDim, FRisSampleLocalLightsDim>;
//
//	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
//	{
//		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
//	}
//
//	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
//	{
//		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
//		ApplySampledLightingGlobalSettings(OutEnvironment);
//	}
//
//	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
//
//		SHADER_PARAMETER(int32, OutputSlice)
//		SHADER_PARAMETER(int32, HistoryReservoir)
//		SHADER_PARAMETER(int32, InitialCandidates)
//		SHADER_PARAMETER(int32, InitialSampleVisibility)
//		SHADER_PARAMETER_STRUCT_REF(FSampledLightData, SampledLightData)
//		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
//
//		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
//
//		SHADER_PARAMETER_TEXTURE(Texture2D, SSProfilesTexture)
//		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneLightingChannelParameters, SceneLightingChannels)
//
//		SHADER_PARAMETER_STRUCT_INCLUDE(FSampledLightingCommonParameters, SampledLightingCommonParameters)
//
//		END_SHADER_PARAMETER_STRUCT()
//};
//
//IMPLEMENT_GLOBAL_SHADER(FGenerateInitialSamplesRGS, "/Engine/Private/RTXDI/RayTracingSampledDirectLighting.usf", "GenerateInitialSamplesRGS", SF_RayGen);
//
//class FEvaluateSampledLightingRGS : public FGlobalShader
//{
//	DECLARE_GLOBAL_SHADER(FEvaluateSampledLightingRGS)
//	SHADER_USE_ROOT_PARAMETER_STRUCT(FEvaluateSampledLightingRGS, FGlobalShader)
//
//	class FEvaluateLightingDim : SHADER_PERMUTATION_BOOL("EVALUATE_LIGHTING_SAMPLED");
//	class FCompressedLightDataDim : SHADER_PERMUTATION_BOOL("USE_COMPRESSED_LIGHT_DATA");
//	class FHairLightingDim : SHADER_PERMUTATION_BOOL("USE_HAIR_LIGHTING");
//	class FScreenTraceDim : SHADER_PERMUTATION_INT("SCREEN_TRACE", 2);
//
//	using FPermutationDomain = TShaderPermutationDomain<FEvaluateLightingDim, FCompressedLightDataDim, FHairLightingDim, FScreenTraceDim>;
//
//	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
//	{
//		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
//	}
//
//	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
//	{
//		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
//		ApplySampledLightingGlobalSettings(OutEnvironment);
//	}
//
//	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
//
//		SHADER_PARAMETER(int32, InputSlice)
//		SHADER_PARAMETER(int32, NumReservoirs)
//		SHADER_PARAMETER(int32, DemodulateMaterials)
//		SHADER_PARAMETER(int32, DebugOutput)
//		SHADER_PARAMETER(int32, FeedbackVisibility)
//		SHADER_PARAMETER(uint32, bUseHairVoxel)
//		SHADER_PARAMETER_STRUCT_REF(FSampledLightData, SampledLightData)
//		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
//
//		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWDiffuseUAV)
//		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWSpecularUAV)
//		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, RWRayDistanceUAV)
//		SHADER_PARAMETER(FIntVector, ReservoirHistoryBufferDim)
//		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint4>, RWLightReservoirHistoryUAV)
//		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
//
//		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HairCategorizationTexture)
//		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HairLightChannelMaskTexture)
//
//		SHADER_PARAMETER_TEXTURE(Texture2D, SSProfilesTexture)
//		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneLightingChannelParameters, SceneLightingChannels)
//		SHADER_PARAMETER_STRUCT_INCLUDE(FSampledLightingCommonParameters, SampledLightingCommonParameters)
//		SHADER_PARAMETER_STRUCT_INCLUDE(FSampledLightingSceenTraceParameters, ScreenTraceParameters)
//
//		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualVoxelParameters, VirtualVoxel)
//
//		END_SHADER_PARAMETER_STRUCT()
//};
//
//IMPLEMENT_GLOBAL_SHADER(FEvaluateSampledLightingRGS, "/Engine/Private/RTXDI/RayTracingSampledDirectLighting.usf", "EvaluateSampledLightingRGS", SF_RayGen);
//
//class FApplySpatialResamplingRGS : public FGlobalShader
//{
//	DECLARE_GLOBAL_SHADER(FApplySpatialResamplingRGS)
//	SHADER_USE_ROOT_PARAMETER_STRUCT(FApplySpatialResamplingRGS, FGlobalShader)
//
//	class FEvaluateLightingDim : SHADER_PERMUTATION_BOOL("EVALUATE_LIGHTING_SAMPLED");
//	class FCompressedLightDataDim : SHADER_PERMUTATION_BOOL("USE_COMPRESSED_LIGHT_DATA");
//
//	using FPermutationDomain = TShaderPermutationDomain<FEvaluateLightingDim, FCompressedLightDataDim>;
//
//	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
//	{
//		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
//	}
//
//	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
//	{
//		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
//		ApplySampledLightingGlobalSettings(OutEnvironment);
//	}
//
//	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
//
//		SHADER_PARAMETER(int32, InputSlice)
//		SHADER_PARAMETER(int32, OutputSlice)
//		SHADER_PARAMETER(int32, HistoryReservoir)
//		SHADER_PARAMETER(float, SpatialSamplingRadius)
//		SHADER_PARAMETER(int32, SpatialSamples)
//		SHADER_PARAMETER(int32, SpatialSamplesBoost)
//		SHADER_PARAMETER(float, SpatialDepthRejectionThreshold)
//		SHADER_PARAMETER(float, SpatialNormalRejectionThreshold)
//		SHADER_PARAMETER(int32, ApplyApproximateVisibilityTest)
//		SHADER_PARAMETER(uint32, NeighborOffsetMask)
//		SHADER_PARAMETER(int32, DiscountNaiveSamples)
//	
//		SHADER_PARAMETER_STRUCT_REF(FSampledLightData, SampledLightData)
//		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
//
//		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
//
//		SHADER_PARAMETER_TEXTURE(Texture2D, SSProfilesTexture)
//		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneLightingChannelParameters, SceneLightingChannels)
//		SHADER_PARAMETER_STRUCT_INCLUDE(FSampledLightingCommonParameters, SampledLightingCommonParameters)
//
//		SHADER_PARAMETER_SRV(Buffer<float2>, NeighborOffsets)
//
//		END_SHADER_PARAMETER_STRUCT()
//};
//
//IMPLEMENT_GLOBAL_SHADER(FApplySpatialResamplingRGS, "/Engine/Private/RTXDI/RayTracingSampledDirectLighting.usf", "ApplySpatialResamplingRGS", SF_RayGen);
//
//
//class FApplyTemporalResamplingRGS : public FGlobalShader
//{
//	DECLARE_GLOBAL_SHADER(FApplyTemporalResamplingRGS)
//	SHADER_USE_ROOT_PARAMETER_STRUCT(FApplyTemporalResamplingRGS, FGlobalShader)
//
//	class FFuseInitialSamplingDim : SHADER_PERMUTATION_BOOL("FUSE_TEMPORAL_AND_INITIAL_SAMPLING");
//
//	class FEvaluateLightingDim : SHADER_PERMUTATION_BOOL("EVALUATE_LIGHTING_SAMPLED");
//	class FCompressedLightDataDim : SHADER_PERMUTATION_BOOL("USE_COMPRESSED_LIGHT_DATA");
//	class FRisSampleLocalLightsDim : SHADER_PERMUTATION_INT("RIS_SAMPLE_LOCAL_LIGHTS", 3);
//
//	using FPermutationDomain = TShaderPermutationDomain<FFuseInitialSamplingDim,FEvaluateLightingDim,FCompressedLightDataDim,FRisSampleLocalLightsDim>;
//
//		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
//	{
//		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
//	}
//
//	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
//	{
//		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
//		ApplySampledLightingGlobalSettings(OutEnvironment);
//	}
//
//	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
//
//		SHADER_PARAMETER(int32, InputSlice)
//		SHADER_PARAMETER(int32, OutputSlice)
//		SHADER_PARAMETER(int32, HistoryReservoir)
//		SHADER_PARAMETER(float, TemporalDepthRejectionThreshold)
//		SHADER_PARAMETER(float, TemporalNormalRejectionThreshold)
//		SHADER_PARAMETER(int32, ApplyApproximateVisibilityTest)
//		SHADER_PARAMETER(int32, InitialCandidates)
//		SHADER_PARAMETER(int32, InitialSampleVisibility)
//		SHADER_PARAMETER(int32, SpatiallyHashTemporalReprojection)
//
//		SHADER_PARAMETER_STRUCT_REF(FSampledLightData, SampledLightData)
//		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
//
//		SHADER_PARAMETER(FIntVector, ReservoirHistoryBufferDim)
//		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint4>, LightReservoirHistory)
//		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, DepthHistory)
//		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, NormalHistory)
//		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
//
//		SHADER_PARAMETER_TEXTURE(Texture2D, SSProfilesTexture)
//		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneLightingChannelParameters, SceneLightingChannels)
//		SHADER_PARAMETER_STRUCT_INCLUDE(FSampledLightingCommonParameters, SampledLightingCommonParameters)
//
//		END_SHADER_PARAMETER_STRUCT()
//};
//
//IMPLEMENT_GLOBAL_SHADER(FApplyTemporalResamplingRGS, "/Engine/Private/RTXDI/RayTracingSampledDirectLighting.usf", "ApplyTemporalResamplingRGS", SF_RayGen);
//
//class FApplyBoilingFilterCS : public FGlobalShader
//{
//	DECLARE_GLOBAL_SHADER(FApplyBoilingFilterCS)
//	SHADER_USE_PARAMETER_STRUCT(FApplyBoilingFilterCS, FGlobalShader)
//
//	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
//	{
//		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
//	}
//
//	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
//	{
//		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
//		OutEnvironment.CompilerFlags.Add(CFLAG_ForceDXC);
//		ApplySampledLightingGlobalSettings(OutEnvironment);
//	}
//
//	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
//
//		SHADER_PARAMETER(int32, InputSlice)
//		SHADER_PARAMETER(int32, OutputSlice)
//		SHADER_PARAMETER(float, BoilingFilterStrength)
//
//		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint4>, RWLightReservoirUAV)
//		SHADER_PARAMETER(FIntVector, ReservoirBufferDim)
//		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
//
//	END_SHADER_PARAMETER_STRUCT()
//};
//
//IMPLEMENT_GLOBAL_SHADER(FApplyBoilingFilterCS, "/Engine/Private/RTXDI/BoilingFilter.usf", "BoilingFilterCS", SF_Compute);
//
//class FComputeLightingPdfCS : public FGlobalShader
//{
//	DECLARE_GLOBAL_SHADER(FComputeLightingPdfCS)
//	SHADER_USE_PARAMETER_STRUCT(FComputeLightingPdfCS, FGlobalShader)
//
//	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
//	{
//		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
//	}
//
//	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
//	{
//		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
//		OutEnvironment.CompilerFlags.Add(CFLAG_ForceDXC);
//		ApplySampledLightingGlobalSettings(OutEnvironment);
//	}
//
//	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
//
//		SHADER_PARAMETER(int32, PdfTexDimensions)
//		SHADER_PARAMETER(int32, CreateBaseLevel)
//
//		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, LightPdfTexture)
//
//		SHADER_PARAMETER_STRUCT_REF(FSampledLightData, SampledLightData)
//
//		// one per mip level, as UAVs only allow per mip binding
//		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, LightPdfUAV0)
//		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, LightPdfUAV1)
//		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, LightPdfUAV2)
//		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, LightPdfUAV3)
//		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, LightPdfUAV4)
//
//	END_SHADER_PARAMETER_STRUCT()
//};
//
//IMPLEMENT_GLOBAL_SHADER(FComputeLightingPdfCS, "/Engine/Private/RTXDI/PresampleLights.usf", "ComputeLightPdfCS", SF_Compute);
//
//class FPreprocessSkylightForRISCS : public FGlobalShader
//{
//	DECLARE_GLOBAL_SHADER(FPreprocessSkylightForRISCS)
//	SHADER_USE_PARAMETER_STRUCT(FPreprocessSkylightForRISCS, FGlobalShader)
//
//	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
//	{
//		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
//	}
//
//	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
//	{
//		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
//		OutEnvironment.CompilerFlags.Add(CFLAG_ForceDXC);
//		ApplySampledLightingGlobalSettings(OutEnvironment);
//	}
//
//	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
//
//		SHADER_PARAMETER_TEXTURE(TextureCube, SkyLightCubemap0)
//		SHADER_PARAMETER_TEXTURE(TextureCube, SkyLightCubemap1)
//		SHADER_PARAMETER_SAMPLER(SamplerState, SkyLightCubemapSampler0)
//		SHADER_PARAMETER_SAMPLER(SamplerState, SkyLightCubemapSampler1)
//		SHADER_PARAMETER(float, SkylightBlendFactor)
//		SHADER_PARAMETER(FVector3f, SkyColor)
//
//		SHADER_PARAMETER(float, SkylightInvResolution)
//
//		// Preprocessing just writes the top mip level
//		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, LightPdfUAV0)
//		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, PreprocessedSkylight)
//
//		END_SHADER_PARAMETER_STRUCT()
//};
//
//IMPLEMENT_GLOBAL_SHADER(FPreprocessSkylightForRISCS, "/Engine/Private/RTXDI/PresampleLights.usf", "PreprocessSkylightCS", SF_Compute);
//
//class FComputeLightingRisBufferCS : public FGlobalShader
//{
//	DECLARE_GLOBAL_SHADER(FComputeLightingRisBufferCS)
//	SHADER_USE_PARAMETER_STRUCT(FComputeLightingRisBufferCS, FGlobalShader)
//
//		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
//	{
//		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
//	}
//
//	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
//	{
//		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
//		OutEnvironment.CompilerFlags.Add(CFLAG_ForceDXC);
//		ApplySampledLightingGlobalSettings(OutEnvironment);
//	}
//
//	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
//
//		SHADER_PARAMETER(int32, PdfTexDimensions)
//		SHADER_PARAMETER(int32, MaxMipLevel)
//		SHADER_PARAMETER(int32, RisTileSize)
//		SHADER_PARAMETER(int32, SampleEnvMap)
//		SHADER_PARAMETER(float, WeightedSampling)
//
//		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, LightPdfTexture)
//
//		SHADER_PARAMETER_STRUCT_REF(FSampledLightData, SampledLightData)
//
//		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
//
//		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint2>, RisBuffer)
//
//	END_SHADER_PARAMETER_STRUCT()
//};
//
//IMPLEMENT_GLOBAL_SHADER(FComputeLightingRisBufferCS, "/Engine/Private/RTXDI/PresampleLights.usf", "PreSampleLightsCS", SF_Compute);
//
//class FComputeReGIRSamplingBufferCS : public FGlobalShader
//{
//	DECLARE_GLOBAL_SHADER(FComputeReGIRSamplingBufferCS)
//	SHADER_USE_PARAMETER_STRUCT(FComputeReGIRSamplingBufferCS, FGlobalShader)
//
//		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
//	{
//		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
//	}
//
//	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
//	{
//		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
//		OutEnvironment.CompilerFlags.Add(CFLAG_ForceDXC);
//		ApplySampledLightingGlobalSettings(OutEnvironment);
//	}
//
//	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
//
//		SHADER_PARAMETER(int32, ReGIRTileSize)
//		SHADER_PARAMETER(float, CellSize)
//		SHADER_PARAMETER(FIntVector, CellCount)
//		SHADER_PARAMETER(int32, NumSampleIterations)
//
//
//		SHADER_PARAMETER_STRUCT_REF(FSampledLightData, SampledLightData)
//
//		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
//
//		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint2>, RisBuffer)
//
//	END_SHADER_PARAMETER_STRUCT()
//};
//
//IMPLEMENT_GLOBAL_SHADER(FComputeReGIRSamplingBufferCS, "/Engine/Private/RTXDI/PresampleLights.usf", "PreSampleLightsForGridCS", SF_Compute);
//
//class FCompositeSampledLightingPS : public FGlobalShader
//{
//	DECLARE_GLOBAL_SHADER(FCompositeSampledLightingPS);
//	SHADER_USE_PARAMETER_STRUCT(FCompositeSampledLightingPS, FGlobalShader);
//
//	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
//	{
//		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
//	}
//
//	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
//		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, Diffuse)
//		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, Specular)
//		SHADER_PARAMETER_SAMPLER(SamplerState, InputSampler)
//		SHADER_PARAMETER(int, ApplyDiffuse)
//		SHADER_PARAMETER(int, ApplySpecular)
//		SHADER_PARAMETER(int32, ModulateMaterials)
//		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTextures)
//		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
//		RENDER_TARGET_BINDING_SLOTS()
//	END_SHADER_PARAMETER_STRUCT()
//};
//
//IMPLEMENT_GLOBAL_SHADER(FCompositeSampledLightingPS, "/Engine/Private/RTXDI/CompositeSampledLightingPS.usf", "CompositeSampledLightingPS", SF_Pixel);
//
///**
// * This buffer provides a table with a low discrepency sequence
// */
//class FDiscSampleBuffer : public FRenderResource
//{
//public:
//
//	/** The buffer used for storage. */
//	FBufferRHIRef DiscSampleBufferRHI;
//	/** Shader resource view in to the vertex buffer. */
//	FShaderResourceViewRHIRef DiscSampleBufferSRV;
//
//	const uint32 NumSamples = 8192;
//
//	/**
//	 * Initialize RHI resources.
//	 */
//	virtual void InitRHI() override
//	{
//		if (RHISupportsRayTracingShaders(GShaderPlatformForFeatureLevel[GetFeatureLevel()]))
//		{
//			// Create a sequence of low-discrepancy samples within a unit radius around the origin
//			// for "randomly" sampling neighbors during spatial resampling
//			TResourceArray<uint8> Buffer;
//
//			Buffer.AddZeroed(NumSamples * 2);
//
//			const int32 R = 250;
//			const float phi2 = 1.0f / 1.3247179572447f;
//			uint32 num = 0;
//			float U = 0.5f;
//			float V = 0.5f;
//			while (num < NumSamples * 2) {
//				U += phi2;
//				V += phi2 * phi2;
//				if (U >= 1.0f) U -= 1.0f;
//				if (V >= 1.0f) V -= 1.0f;
//
//				float rSq = (U - 0.5f) * (U - 0.5f) + (V - 0.5f) * (V - 0.5f);
//				if (rSq > 0.25f)
//					continue;
//
//				Buffer[num++] = uint8((U - 0.5f) * R + 127.5f);
//				Buffer[num++] = uint8((V - 0.5f) * R + 127.5f);
//				
//			}
//
//			FRHIResourceCreateInfo CreateInfo(TEXT("RTXDIDiscSamples"), &Buffer);
//			DiscSampleBufferRHI = RHICreateVertexBuffer(
//				/*Size=*/ sizeof(uint8) * 2 * NumSamples,
//				/*Usage=*/ BUF_Volatile | BUF_ShaderResource,
//				CreateInfo);
//			DiscSampleBufferSRV = RHICreateShaderResourceView(
//				DiscSampleBufferRHI, /*Stride=*/ sizeof(uint8) * 2, PF_R8G8
//			);
//		}
//	}
//
//	/**
//	 * Release RHI resources.
//	 */
//	virtual void ReleaseRHI() override
//	{
//		DiscSampleBufferSRV.SafeRelease();
//		DiscSampleBufferRHI.SafeRelease();
//	}
//};
//
///** The global resource for the disc sample buffer. */
//TGlobalResource<FDiscSampleBuffer> GDiscSampleBuffer;
//
///*
// * Compressed light structure, packs per-light data into 80 bytes
// * Compression is relatively conservative
// */
//struct FPackedLightingData
//{
//	//uint Type;											// 2 bits stored as 8 (directional, spot, point, rect)
//	//uint RectLightTextureIndex;							// 7 bit stored as 8 (99 is the invalid index)
//	//float SoftSourceRadius;								// 16 bit half float
//	uint32 TypeRectLightTextureIndexAndSoftSourceRadius;	// 
//	int32 LightProfileIndex;								// 32 bits - uses sentinel of -1 for NONE
//	uint32 LightIDLightFunctionAndMask;						// Function and mask 8 bits each 
//	float InvRadius;										// 
//
//	float LightPosition[3];									// 96 bits
//	//float SourceRadius;									// 16 bit half float
//	//float SourceLength;									// 16 bit half float
//	uint32 SourceRadiusAndLength;							// 32 bits
//
//	//float3 Direction;										// 32 bits oct encoded
//	//float3 Tangent;										// 32 bits oct encoded
//	uint32 DirectionAndTangent[2];							// 64 bits (32 unused)
//	uint32 LightColor[2];									// 64 bits ( 3 x fp16 + bfloat16 magnitude)
//
//	//float FalloffExponent;								// 16 bit half float
//	//float SpecularScale;									// 16 bit half float
//	uint32 FalloffExponentAndSpecularScale;					// 32 bits
//	float DistanceFadeMAD[2];								// 64 bits
//	//float RectLightBarnCosAngle;							// 16 bit half float
//	//float RectLightBarnLength;							// 16 bit half float
//	//float SpotAngles[2];									// 32 bits (normally second arg is 1/(cos a - cos b) encoded as 
//	uint32 RectLightBarnOrSpotAngles;						// 32 bits
//	
//	
//	
//	
//
//	FVector2f UnitVectorToOctahedron(FVector3f N)
//	{
//		float Scale = FMath::Abs(N.X) + FMath::Abs(N.Y) + FMath::Abs(N.Z);
//		FVector2f Oct = FVector2f(N.X / Scale, N.Y / Scale);
//		if (N.Z <= 0.0f)
//		{
//			FVector2f Mirror = FVector2f(1.0f) - FVector2f(FMath::Abs(Oct.Y), FMath::Abs(Oct.X));
//
//			if (Oct.X < 0.0f)
//			{
//				Mirror.X *= -1.0f;
//			}
//
//			if (Oct.Y < 0.0f)
//			{
//				Mirror.Y *= -1.0f;
//			}
//			
//			Oct = Mirror;
//		}
//		return Oct;
//	}
//
//	uint32 UnitVectorToOctahedronPacked(FVector3f N)
//	{
//		FVector2f Oct = UnitVectorToOctahedron(N);
//
//		Oct = Oct * 0.5f + 0.5f;
//
//		const float MaxU16 = float(0xffff);
//		uint32 X = uint32(Oct.X * MaxU16);
//		uint32 Y = uint32(Oct.Y * MaxU16);
//
//		return X | (Y << 16);
//	}
//
//	// intentionally truncating
//	float ConvertToBFloat(float F)
//	{
//		union BFloat
//		{
//			float F;
//			uint32 U;
//		};
//
//		BFloat B;
//		B.F = F;
//		B.U &= 0xffff0000;
//
//		return B.F;
//	}
//
//	uint16 ExtractBFloat(float F)
//	{
//		union BFloat
//		{
//			float F;
//			uint32 U;
//		};
//
//		BFloat B;
//		B.F = F;
//
//		return B.U >> 16;
//	}
//
//	void EncodeLightColor(const float* InColor, uint32* OutColor)
//	{
//		float Magnitude = FMath::Max3(FMath::Abs(InColor[1]), FMath::Abs(InColor[0]), FMath::Abs(InColor[2]));
//
//		if (Magnitude == 0.0)
//		{
//			Magnitude = 1.0f;
//		}
//
//		Magnitude = ConvertToBFloat(Magnitude);
//
//		FFloat16 R = FFloat16(InColor[0] / Magnitude);
//		FFloat16 G = FFloat16(InColor[1] / Magnitude);
//		FFloat16 B = FFloat16(InColor[2] / Magnitude);
//
//
//		OutColor[0] = R.Encoded;
//		OutColor[0] |= G.Encoded << 16;
//		OutColor[1] = B.Encoded;
//		OutColor[1] |= ExtractBFloat(Magnitude) << 16;
//	}
//
//	FPackedLightingData(const FRayTracingSampledLightingData& LightData)
//	{
//		TypeRectLightTextureIndexAndSoftSourceRadius = FFloat16(LightData.SoftSourceRadius).Encoded;
//		TypeRectLightTextureIndexAndSoftSourceRadius |= (LightData.RectLightTextureIndex & 0xff) << 16;
//		TypeRectLightTextureIndexAndSoftSourceRadius |= (LightData.Type & 0xff) << 24;
//		LightProfileIndex = LightData.LightProfileIndex;
//		LightIDLightFunctionAndMask = LightData.FlagsLightFunctionAndMask;
//		LightIDLightFunctionAndMask |= (LightData.LightID & 0xffff) << 16;
//		InvRadius = LightData.InvRadius;
//
//		LightPosition[0] = LightData.LightPosition[0];
//		LightPosition[1] = LightData.LightPosition[1];
//		LightPosition[2] = LightData.LightPosition[2];
//		SourceRadiusAndLength = FFloat16(LightData.SourceLength).Encoded;
//		SourceRadiusAndLength |= FFloat16(LightData.SourceRadius).Encoded << 16;
//
//		DirectionAndTangent[0] = UnitVectorToOctahedronPacked(FVector3f(LightData.Tangent[0], LightData.Tangent[1], LightData.Tangent[2]));
//		DirectionAndTangent[1] = UnitVectorToOctahedronPacked(FVector3f(LightData.Direction[0], LightData.Direction[1], LightData.Direction[2]));
//		FalloffExponentAndSpecularScale = FFloat16(LightData.SpecularScale).Encoded;
//		FalloffExponentAndSpecularScale |= FFloat16(LightData.FalloffExponent).Encoded << 16;
//
//		EncodeLightColor(LightData.LightColor, LightColor);
//		if (LightData.Type == LightType_Spot)
//		{
//			// re-encode spot angles to bound range better
//			//  const float InvCosConeDifference = 1.0f / (CosInnerCone - CosOuterCone);
//			//  SpotAngles = FVector2f(CosOuterCone, InvCosConeDifference);
//			const float CosOuterCone = LightData.SpotAngles[0];
//			const float CosInnerCone = (1.0f / LightData.SpotAngles[1]) + CosOuterCone;
//			RectLightBarnOrSpotAngles = FFloat16(CosInnerCone).Encoded;
//			RectLightBarnOrSpotAngles |= FFloat16(CosOuterCone).Encoded << 16;
//		}
//		else
//		{
//			// Rect light parameters also handle the spread angle for directional 
//			RectLightBarnOrSpotAngles = FFloat16(LightData.RectLightBarnLength).Encoded;
//			RectLightBarnOrSpotAngles |= FFloat16(LightData.RectLightBarnCosAngle).Encoded << 16;
//		}
//
//		DistanceFadeMAD[0] = LightData.DistanceFadeMAD[0];
//		DistanceFadeMAD[1] = LightData.DistanceFadeMAD[1];
//	}
//}; // 64 bytes total
//
//static_assert(sizeof(FPackedLightingData) == 64, "FPackedLightingData compiled to incompatible size");
//
//static TAutoConsoleVariable<int32> CVarRayTracingSampledLightingEqualWeight(
//	TEXT("r.RayTracing.SampledLighting.EqualWeight"),
//	0,
//	TEXT("Whether to weight all lights equally for RIS (default = 0)"),
//	ECVF_RenderThreadSafe);
//
//
//
//// Simple wrapper struct to bundle light data
//struct FSampledLightingResourceData
//{
//	FSampledLightData LightData;
//	TResourceArray<FRayTracingSampledLightingData> LightDataArray;
//	TResourceArray<FPackedLightingData> PackedLightDataArray;
//	TResourceArray<int32> LightRemapTable;
//	TResourceArray<int32> LightBackwardRemapTable;
//};
//
//static void SetupSampledRaytracingLightData(
//	FRHICommandListImmediate& RHICmdList,
//	const FScene *Scene,
//	const TArray<FSortedLightSceneInfo, SceneRenderingAllocator>& Lights,
//	const FSimpleLightArray& SimpleLights,
//	const FViewInfo& View,
//	const FSampledLightingHistory& PrevLightHistory,
//	FSampledLightingResourceData& LightResourceData,
//	FSampledLightingHistory& LightHistory)
//{
//	TMap<UTextureLightProfile*, int32> IESLightProfilesMap;
//	TMap<FRHITexture*, uint32> RectTextureMap;
//
//	TArray<FSortedLightSceneInfo, SceneRenderingAllocator> SortedLights = Lights;
//
//	struct FSeparateLocal
//	{
//		FORCEINLINE bool operator()(const FSortedLightSceneInfo& A, const FSortedLightSceneInfo& B) const
//		{
//			int32 AIsLocal = A.LightSceneInfo->Proxy->GetLightType() != LightType_Directional;
//			int32 BIsLocal = B.LightSceneInfo->Proxy->GetLightType() != LightType_Directional;
//			return AIsLocal < BIsLocal;
//		}
//	};
//
//	SortedLights.StableSort(FSeparateLocal());
//
//	FSampledLightData& LightData = LightResourceData.LightData;
//	TResourceArray<FRayTracingSampledLightingData>& LightDataArray = LightResourceData.LightDataArray;
//	TResourceArray<FPackedLightingData>& PackedLightDataArray = LightResourceData.PackedLightDataArray;
//	TResourceArray<int32>& LightRemapTable = LightResourceData.LightRemapTable;
//	TResourceArray<int32>& LightBackwardRemapTable = LightResourceData.LightBackwardRemapTable;
//
//	//Get skylight color and add dummy light entry for skylight
//	FLinearColor SkyColor = Scene->SkyLight ? Scene->SkyLight->GetEffectiveLightColor() : FLinearColor::Black;
//	LightData.SkylightColor = FVector3f(SkyColor.R, SkyColor.G, SkyColor.B);
//	int32 NumSkylights = 0;
//	{
//		LightData.DirectionalLightStart = 1;
//		// add one dummy element to represent skylight
//		LightDataArray.AddZeroed(1);
//		NumSkylights = 1;
//	}
//
//	// initialize the light remapping tables to invalid (-1)
//	const int32 PrevLightCount = FMath::Max(PrevLightHistory.LightData.Num(),NumSkylights); // at least hold an entry for a skylight, since it is persistent
//	LightRemapTable.Empty();
//	LightRemapTable.AddZeroed(PrevLightCount);
//
//	for (int32 LightIndex = 0; LightIndex < PrevLightCount; LightIndex++)
//	{
//		LightRemapTable[LightIndex] = -1;
//	}
//
//	const int32 CurrentLightCount = SortedLights.Num() + SimpleLights.InstanceData.Num() + NumSkylights;
//	LightBackwardRemapTable.Empty();
//	LightBackwardRemapTable.AddZeroed(CurrentLightCount);
//
//	for (int32 LightIndex = NumSkylights; LightIndex < CurrentLightCount; LightIndex++)
//	{
//		LightBackwardRemapTable[LightIndex] = -1;
//	}
//
//	// zero is the special skylight entry
//	LightRemapTable[0] = 0;
//	LightBackwardRemapTable[0] = 0;
//
//	LightData.LTCMatTexture = GSystemTextures.LTCMat->GetRHI();
//	LightData.LTCMatSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
//	LightData.LTCAmpTexture = GSystemTextures.LTCAmp->GetRHI();
//	LightData.LTCAmpSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
//
//	FTextureRHIRef DymmyWhiteTexture = GWhiteTexture->TextureRHI;
//	LightData.RectLightTexture0 = DymmyWhiteTexture;
//	LightData.RectLightTexture1 = DymmyWhiteTexture;
//	LightData.RectLightTexture2 = DymmyWhiteTexture;
//	LightData.RectLightTexture3 = DymmyWhiteTexture;
//	LightData.RectLightTexture4 = DymmyWhiteTexture;
//	LightData.RectLightTexture5 = DymmyWhiteTexture;
//	LightData.RectLightTexture6 = DymmyWhiteTexture;
//	LightData.RectLightTexture7 = DymmyWhiteTexture;
//	static constexpr uint32 MaxRectLightTextureSlos = 8;
//	static constexpr uint32 InvalidTextureIndex = 99; 
//
//
//	for (auto Light : SortedLights)
//	{
//		if (View.Family->EngineShowFlags.TexturedLightProfiles)
//		{
//			UTextureLightProfile* IESLightProfileTexture = Light.LightSceneInfo->Proxy->GetIESTexture();
//			if (IESLightProfileTexture)
//			{
//				int32* IndexFound = IESLightProfilesMap.Find(IESLightProfileTexture);
//				if (!IndexFound)
//				{
//					IESLightProfilesMap.Add(IESLightProfileTexture, IESLightProfilesMap.Num());
//				}
//			}
//		}
//	}
//
//#if SUPPORT_IES_PROFILES
//	if (View.IESLightProfile2DResource != nullptr && IESLightProfilesMap.Num() > 0)
//	{
//		TArray<UTextureLightProfile*, SceneRenderingAllocator> IESProfilesArray;
//		IESProfilesArray.AddUninitialized(IESLightProfilesMap.Num());
//		for (auto It = IESLightProfilesMap.CreateIterator(); It; ++It)
//		{
//			IESProfilesArray[It->Value] = It->Key;
//		}
//
//		View.IESLightProfile2DResource->BuildIESLightProfilesTexture(RHICmdList,IESProfilesArray);
//	}
//#endif
//
//	{
//		// IES profiles
//		float IESInvProfileCount = 1.0f;
//
//#if SUPPORT_IES_PROFILES
//		if (View.IESLightProfile2DResource && View.IESLightProfile2DResource->GetIESLightProfilesCount())
//		{
//			LightData.IESLightProfileTexture = View.IESLightProfile2DResource->GetTexture();
//
//			uint32 ProfileCount = View.IESLightProfile2DResource->GetIESLightProfilesPerPage();
//			IESInvProfileCount = ProfileCount ? 1.f / static_cast<float>(ProfileCount) : 0.f;
//		}
//		else
//#endif
//		{
//			LightData.IESLightProfileTexture = GWhiteTexture->TextureRHI;
//		}
//
//		LightData.IESLightProfileInvCount = IESInvProfileCount;
//		LightData.IESLightProfileTextureSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
//	}
//
//	LightData.DirectionalLightCount = 0;
//	LightData.LocalLightCount = 0;
//
//	for (auto Light : SortedLights)
//	{
//		auto LightType = Light.LightSceneInfo->Proxy->GetLightType();
//
//		FLightRenderParameters LightParameters;
//		Light.LightSceneInfo->Proxy->GetLightShaderParameters(LightParameters);
//
//		if (Light.LightSceneInfo->Proxy->IsInverseSquared())
//		{
//			LightParameters.FalloffExponent = 0;
//		}
//
//		int32 IESLightProfileIndex = INDEX_NONE;
//#if SUPPORT_IES_PROFILES
//		if (View.Family->EngineShowFlags.TexturedLightProfiles)
//		{
//			UTextureLightProfile* IESLightProfileTexture = Light.LightSceneInfo->Proxy->GetIESTexture();
//			if (IESLightProfileTexture)
//			{
//				int32* IndexFound = IESLightProfilesMap.Find(IESLightProfileTexture);
//				if (!IndexFound)
//				{
//					check(0);
//				}
//				else
//				{
//					IESLightProfileIndex = *IndexFound;
//				}
//			}
//		}
//#endif
//
//		FRayTracingSampledLightingData LightDataElement;
//
//		LightDataElement.Type = LightType;
//		LightDataElement.RectLightTextureIndex = InvalidTextureIndex;
//
//		if (IESLightProfileIndex == INDEX_NONE)
//		{
//			LightDataElement.LightProfileIndex = 0xffffffff;
//		}
//#if SUPPORT_IES_PROFILES
//		else
//		{
//			FIESLightProfile2DResource::FIESLightProfileIndex Index = View.IESLightProfile2DResource->GetProfileIndex(IESLightProfileIndex);
//			LightDataElement.LightProfileIndex = Index.Page << 16 | Index.Start;
//		}
//#endif
//
//		const FVector3f LightColor(LightParameters.Color);
//		for (int32 Element = 0; Element < 3; Element++)
//		{
//			LightDataElement.Direction[Element] = LightParameters.Direction[Element];
//			LightDataElement.LightPosition[Element] = (float)LightParameters.WorldPosition[Element]; // LWC_TODO
//			LightDataElement.LightColor[Element] = LightColor[Element];
//			LightDataElement.Tangent[Element] = LightParameters.Tangent[Element];
//		}
//
//		// Note LWC: these get stored as float
//		const FVector2D FadeParams = Light.LightSceneInfo->Proxy->GetDirectionalLightDistanceFadeParameters(View.GetFeatureLevel(), Light.LightSceneInfo->IsPrecomputedLightingValid(), View.MaxShadowCascades);
//		const FVector2D DistanceFadeMAD = { FadeParams.Y, -FadeParams.X * FadeParams.Y };
//
//		for (int32 Element = 0; Element < 2; Element++)
//		{
//			LightDataElement.SpotAngles[Element] = LightParameters.SpotAngles[Element];
//			LightDataElement.DistanceFadeMAD[Element] = DistanceFadeMAD[Element];
//		}
//
//		LightDataElement.InvRadius = LightParameters.InvRadius;
//		LightDataElement.SpecularScale = LightParameters.SpecularScale;
//		LightDataElement.FalloffExponent = LightParameters.FalloffExponent;
//		LightDataElement.SourceRadius = LightParameters.SourceRadius;
//		LightDataElement.SourceLength = LightParameters.SourceLength;
//		LightDataElement.SoftSourceRadius = LightParameters.SoftSourceRadius;
//		LightDataElement.RectLightBarnCosAngle = LightParameters.RectLightBarnCosAngle;
//		LightDataElement.RectLightBarnLength = LightParameters.RectLightBarnLength;
//
//		LightDataElement.FlagsLightFunctionAndMask = 0;
//
//#if SUPPORT_LIGHT_MATERIAL_FUNCTIONS
//		const int32 *LightFunctionIndex = Scene->RayTracingLightFunctionMap.Find(Light.LightSceneInfo->Proxy->GetLightComponent());
//
//		if (View.Family->EngineShowFlags.LightFunctions && LightFunctionIndex && *LightFunctionIndex >= 0)
//		{
//			// set the light function index, 0 is reserved as none, so the index is offset by 1
//			LightDataElement.FlagsLightFunctionAndMask = *LightFunctionIndex + 1;
//		}
//#endif
//
//		// store light channel mask
//		uint8 LightMask = Light.LightSceneInfo->Proxy->GetLightingChannelMask();
//		LightDataElement.FlagsLightFunctionAndMask |= LightMask << 8;
//
//		// Stuff directional light's shadow angle factor into a RectLight parameter
//		if (LightType == LightType_Directional)
//		{
//			LightDataElement.RectLightBarnCosAngle = Light.LightSceneInfo->Proxy->GetShadowSourceAngleFactor();
//		}
//
//		{
//			const ULightComponent *Component = Light.LightSceneInfo->Proxy->GetLightComponent();
//			const int32 NewIndex = LightDataArray.Num();
//			const int32* IndexFound = PrevLightHistory.LightRemapTable.Find(Component);
//			if (IndexFound)
//			{
//				// record map of old light index to new index
//				LightRemapTable[*IndexFound] = NewIndex;
//
//				// record map of new light index to old index
//				LightBackwardRemapTable[NewIndex] = *IndexFound;
//			}
//
//			if (!View.bStatePrevViewInfoIsReadOnly)
//			{
//				LightHistory.LightRemapTable.Add(Component, NewIndex);
//			}
//
//			// Tracks the light pointer to ensure we have a stable identifier for light visualization purposes
//			LightDataElement.LightID = FCrc::TypeCrc32(Component);
//
//			FMemory::Memzero(LightDataElement.Pad);
//
//		}
//
//		LightDataArray.Add(LightDataElement);
//
//		const bool bRequireTexture = LightType == ELightComponentType::LightType_Rect && LightParameters.SourceTexture;
//		uint32 RectLightTextureIndex = InvalidTextureIndex;
//		if (bRequireTexture)
//		{
//			const uint32* IndexFound = RectTextureMap.Find(LightParameters.SourceTexture);
//			if (!IndexFound)
//			{
//				if (RectTextureMap.Num() < MaxRectLightTextureSlos)
//				{
//					RectLightTextureIndex = RectTextureMap.Num();
//					RectTextureMap.Add(LightParameters.SourceTexture, RectLightTextureIndex);
//				}
//			}
//			else
//			{
//				RectLightTextureIndex = *IndexFound;
//			}
//		}
//
//		const uint32 Count = LightData.DirectionalLightCount + LightData.LocalLightCount;
//
//		if (RectLightTextureIndex != InvalidTextureIndex)
//		{
//			LightDataArray[Count].RectLightTextureIndex = RectLightTextureIndex;
//			switch (RectLightTextureIndex)
//			{
//			case 0: LightData.RectLightTexture0 = LightParameters.SourceTexture; break;
//			case 1: LightData.RectLightTexture1 = LightParameters.SourceTexture; break;
//			case 2: LightData.RectLightTexture2 = LightParameters.SourceTexture; break;
//			case 3: LightData.RectLightTexture3 = LightParameters.SourceTexture; break;
//			case 4: LightData.RectLightTexture4 = LightParameters.SourceTexture; break;
//			case 5: LightData.RectLightTexture5 = LightParameters.SourceTexture; break;
//			case 6: LightData.RectLightTexture6 = LightParameters.SourceTexture; break;
//			case 7: LightData.RectLightTexture7 = LightParameters.SourceTexture; break;
//			}
//		}
//
//		if (LightType == LightType_Directional)
//		{
//			// directional lights must be before local lights
//			check(LightData.LocalLightCount == 0);
//
//			LightData.DirectionalLightCount++;
//		}
//		else
//		{
//			LightData.LocalLightCount++;
//		}
//	}
//
//	LightData.LocalLightStart = LightData.DirectionalLightStart + LightData.DirectionalLightCount;
//
//	//
//	// Append simple lights to the array
//	//
//	const int32 ComplexLightCount = LightDataArray.Num();
//	const int32 PrevComplexLightCount = PrevLightHistory.LightRemapTable.Num();
//	const int32 PrevSimpleLightCount = PrevLightCount - PrevComplexLightCount;
//
//	for (int32 LightIndex = 0; LightIndex < SimpleLights.InstanceData.Num(); LightIndex++)
//	{
//		auto& InstanceData = SimpleLights.InstanceData[LightIndex];
//		auto& ViewData = SimpleLights.GetViewDependentData(LightIndex, 0, 1); // note that last parameter NumViews is actually unused
//
//		FRayTracingSampledLightingData LightDataElement;
//
//		LightDataElement.Type = ELightComponentType::LightType_Point;
//
//		FVector3f DefaultDir = FVector3f(1, 0, 0);
//
//		for (int32 Element = 0; Element < 3; Element++)
//		{
//			LightDataElement.Direction[Element] = DefaultDir[Element];
//			LightDataElement.LightPosition[Element] = ViewData.Position[Element];
//			LightDataElement.LightColor[Element] = InstanceData.Color[Element];
//			LightDataElement.Tangent[Element] = DefaultDir[Element];
//		}
//
//		LightDataElement.InvRadius = 1.0f / FMath::Max(InstanceData.Radius, KINDA_SMALL_NUMBER);
//		LightDataElement.FalloffExponent = InstanceData.Exponent;
//
//		LightDataElement.SpecularScale = 1.0f;
//		LightDataElement.SourceRadius = 0.0f;
//		LightDataElement.SoftSourceRadius = 0.0f;
//		LightDataElement.SourceLength = 0.0f;
//		LightDataElement.RectLightTextureIndex = InvalidTextureIndex;
//
//		LightDataElement.RectLightBarnCosAngle = 0.0f;
//		LightDataElement.RectLightBarnLength = 0.0f;
//
//		LightDataElement.LightProfileIndex = 0xffffffff;
//		
//		const FVector2f SpotAngles = FVector2f(-2.0f, 1.0f);
//		const FVector2f DistanceFadeMAD = { 0.0f, 0.0f};
//
//		const uint8 LightMask = 0xff;
//		LightDataElement.FlagsLightFunctionAndMask = LightMask << 8;
//
//		for (int32 Element = 0; Element < 2; Element++)
//		{
//			LightDataElement.SpotAngles[Element] = SpotAngles[Element];
//			LightDataElement.DistanceFadeMAD[Element] = DistanceFadeMAD[Element];
//		}
//
//		LightDataElement.LightID = LightIndex;
//
//		FMemory::Memzero(LightDataElement.Pad);
//
//		{
//			const int32 NewIndex = LightDataArray.Num();
//			
//			// We make the assmuption that simple lights stay in order as they lack per-light tracking
//			if (LightIndex < PrevSimpleLightCount)
//			{
//				// record map of old light index to new index
//				LightRemapTable[PrevComplexLightCount + LightIndex] = NewIndex;
//
//				// record map of new light index to old index
//				LightBackwardRemapTable[NewIndex] = PrevComplexLightCount + LightIndex;
//			}
//		}
//
//		LightDataArray.Add(LightDataElement);
//
//		LightData.LocalLightCount++;
//	}
//
//	if (!View.bStatePrevViewInfoIsReadOnly)
//	{
//		//Save for next frame
//		LightHistory.LightData = LightDataArray;
//	}
//
//	LightData.LightHistoryOffset = LightDataArray.Num();
//
//	// Add the light data from last frame to the end, so it can be used in renormalizing temporal history
//	LightDataArray.Append(PrevLightHistory.LightData);
//
//	// create set of packed light data from full precision data
//	for (auto& LightDataElement : LightDataArray)
//	{
//		PackedLightDataArray.Add(LightDataElement);
//	}
//}
//
//TUniformBufferRef<FSampledLightData> CreateSampledLightDatadUniformBuffer(
//	FRHICommandListImmediate& RHICmdList,
//	const FScene *Scene,
//	const TArray<FSortedLightSceneInfo, SceneRenderingAllocator>& Lights,
//	const FSimpleLightArray& SimpleLights,
//	const FViewInfo& View,
//	const FSampledLightingHistory& PrevLightHistory,
//	EUniformBufferUsage Usage,
//	FSampledLightingHistory& OutLightHistory)
//{
//	FSampledLightingResourceData LightResourceData;
//
//	SetupSampledRaytracingLightData(RHICmdList, Scene, Lights, SimpleLights, View, PrevLightHistory, LightResourceData, OutLightHistory);
//
//	check( (LightResourceData.LightData.LocalLightCount + LightResourceData.LightData.DirectionalLightCount + LightResourceData.LightData.DirectionalLightStart) == (LightResourceData.LightDataArray.Num() - PrevLightHistory.LightData.Num()));
//	check(LightResourceData.LightDataArray.Num() == LightResourceData.PackedLightDataArray.Num());
//	check(LightResourceData.LightRemapTable.Num() >= PrevLightHistory.LightRemapTable.Num());
//
//	// need at least one element, as creating 0 element buffers is an error
//	if (LightResourceData.LightDataArray.Num() == 0)
//	{
//		LightResourceData.LightDataArray.AddZeroed(1);
//	}
//	if (LightResourceData.PackedLightDataArray.Num() == 0)
//	{
//		LightResourceData.PackedLightDataArray.AddZeroed(1);
//	}
//	if (LightResourceData.LightRemapTable.Num() == 0)
//	{
//		LightResourceData.LightRemapTable.Add(-1);
//	}
//	if (LightResourceData.LightBackwardRemapTable.Num() == 0)
//	{
//		LightResourceData.LightBackwardRemapTable.Add(-1);
//	}
//
//	LightResourceData.LightData.SSProfilesTexture = View.RayTracingSubSurfaceProfileTexture ? View.RayTracingSubSurfaceProfileTexture : GSystemTextures.BlackDummy->GetRHI();
//
//	{
//		// Full precision light data buffer
//		FRHIResourceCreateInfo CreateInfo(TEXT("RTXDILightDataBuffer"), &LightResourceData.LightDataArray);
//		FBufferRHIRef LightDataBuffer = RHICreateStructuredBuffer(sizeof(FVector4f), LightResourceData.LightDataArray.GetResourceDataSize(), BUF_Static | BUF_ShaderResource, CreateInfo);
//		LightResourceData.LightData.LightDataBuffer = RHICreateShaderResourceView(LightDataBuffer);
//	}
//
//	{
//		// Buffer to map last frame light indices to current frame indices
//		FRHIResourceCreateInfo CreateInfo(TEXT("RTXDILightRemapTable"), &LightResourceData.LightRemapTable);
//		FBufferRHIRef LightRemapBuffer = RHICreateStructuredBuffer(sizeof(int32), LightResourceData.LightRemapTable.GetResourceDataSize(), BUF_Static | BUF_ShaderResource, CreateInfo);
//		LightResourceData.LightData.LightIndexRemapTable = RHICreateShaderResourceView(LightRemapBuffer);
//	}
//
//	{
//		// Buffer to map current light indices to last frame index values
//		FRHIResourceCreateInfo CreateInfo(TEXT("RTXDILightBackwardMapTable"), &LightResourceData.LightBackwardRemapTable);
//		FBufferRHIRef BackwardRemapResource = RHICreateStructuredBuffer(sizeof(int32), LightResourceData.LightBackwardRemapTable.GetResourceDataSize(), BUF_Static | BUF_ShaderResource, CreateInfo);
//		LightResourceData.LightData.LightIndexBackwardRemapTable = RHICreateShaderResourceView(BackwardRemapResource);
//	}
//
//	{
//		// compressed light data buffer
//		FRHIResourceCreateInfo CreateInfo(TEXT("RTXDIPackedLightDataBuffer"), &LightResourceData.PackedLightDataArray);
//		FBufferRHIRef PackedLightData = RHICreateStructuredBuffer(sizeof(FVector4f), LightResourceData.PackedLightDataArray.GetResourceDataSize(), BUF_Static | BUF_ShaderResource, CreateInfo);
//		LightResourceData.LightData.PackedLightDataBuffer = RHICreateShaderResourceView(PackedLightData);
//	}
//
//	return CreateUniformBufferImmediate(LightResourceData.LightData, Usage);
//}
//
///*
// * Code for handling top-level shader permutations
// */
//struct SampledLightingPermutation
//{
//	bool EvaluationMode;
//	bool CompressedLights;
//};
//
//template<typename ShaderType>
//static TShaderRef<ShaderType> GetShaderPermutation(typename ShaderType::FPermutationDomain PermutationVector, SampledLightingPermutation Options, const FViewInfo& View)
//{
//	PermutationVector.template Set<typename ShaderType::FEvaluateLightingDim>(Options.EvaluationMode);
//
//	PermutationVector.template Set<typename ShaderType::FCompressedLightDataDim>(Options.CompressedLights);
//
//	return View.ShaderMap->GetShader<ShaderType>(PermutationVector);
//}
//
//template<typename ShaderType>
//static TShaderRef<ShaderType> GetShaderPermutation(SampledLightingPermutation Options, const FViewInfo& View)
//{
//	typename ShaderType::FPermutationDomain PermutationVector;
//	return GetShaderPermutation<ShaderType>(PermutationVector, Options, View);
//}
//
//template<typename ShaderType>
//static void AddShaderPermutation(typename ShaderType::FPermutationDomain PermutationVector, SampledLightingPermutation Options, const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
//{
//	auto RayGenShader = GetShaderPermutation<ShaderType>(PermutationVector, Options, View);
//
//	OutRayGenShaders.Add(RayGenShader.GetRayTracingShader());
//}
//
//template<typename ShaderType>
//static void AddShaderPermutation(SampledLightingPermutation Options, const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
//{
//	typename ShaderType::FPermutationDomain PermutationVector;
//	AddShaderPermutation< ShaderType>(PermutationVector, Options, View, OutRayGenShaders);
//}
//
//static SampledLightingPermutation GetPermutationOptions()
//{
//	SampledLightingPermutation Options;
//	Options.EvaluationMode = CVarRayTracingSampledLightingEvaluationMode.GetValueOnRenderThread() != 0;
//	Options.CompressedLights = CVarRayTracingSampledLightingCompressedLights.GetValueOnRenderThread() != 0;
//
//	return Options;
//}
//
//static FRDGBufferRef BuildRISStructures(FRDGBuilder& GraphBuilder, int32 TileSize, int32 TileCount, int32 LightCount, const FViewInfo& View, const TUniformBufferRef<FSampledLightData> &SampledLightDataUniformBuffer)
//{
//	FRDGBufferRef RisBuffer;
//	const int32 RisBufferElements = TileCount * TileSize;
//
//	if (RisBufferElements > 0)
//	{
//		// round the square root of the number of lights to the next power of 2 to create a square texture with
//		// at least one texel per light
//		const uint32 PdfTexSize = FMath::Max( FMath::RoundUpToPowerOfTwo(FMath::CeilToInt(FMath::Sqrt(float(LightCount)))), 2u);
//		const uint32 MaxMip = FMath::FloorLog2(PdfTexSize);
//		const uint32 NumMips = MaxMip + 1;
//
//		check(PdfTexSize * PdfTexSize >= (uint32)LightCount);
//
//		// Create light pdf
//		FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
//			FIntPoint(PdfTexSize, PdfTexSize),
//			PF_R32_FLOAT,
//			FClearValueBinding::None,
//			TexCreate_ShaderResource | TexCreate_UAV,
//			NumMips);
//
//		FRDGTextureRef CdfTexture = GraphBuilder.CreateTexture(Desc, TEXT("RTXDILightCDF"));
//
//		// each pass generates 5 mip levels
//		for (uint32 BaseMip = 0; BaseMip < NumMips; BaseMip += 5)
//		{
//			uint32 BaseMipSize = PdfTexSize >> BaseMip;
//			// compute the local light CDF as a mip-mapped texture
//			FComputeLightingPdfCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FComputeLightingPdfCS::FParameters>();
//
//			PassParameters->PdfTexDimensions = BaseMipSize;
//			PassParameters->CreateBaseLevel = BaseMip == 0;
//
//			if (BaseMip == 0)
//			{
//				PassParameters->LightPdfTexture = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMipLevel(GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy), 0));
//			}
//			else
//			{
//				PassParameters->LightPdfTexture = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMipLevel(CdfTexture, BaseMip - 1));
//			}
//
//			PassParameters->LightPdfUAV0 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(CdfTexture, FMath::Min(BaseMip + 0u, MaxMip)));
//			PassParameters->LightPdfUAV1 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(CdfTexture, FMath::Min(BaseMip + 1u, MaxMip)));
//			PassParameters->LightPdfUAV2 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(CdfTexture, FMath::Min(BaseMip + 2u, MaxMip)));
//			PassParameters->LightPdfUAV3 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(CdfTexture, FMath::Min(BaseMip + 3u, MaxMip)));
//			PassParameters->LightPdfUAV4 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(CdfTexture, FMath::Min(BaseMip + 4u, MaxMip)));
//
//			PassParameters->SampledLightData = SampledLightDataUniformBuffer;
//
//			auto LightCdfShader = View.ShaderMap->GetShader<FComputeLightingPdfCS>();
//
//			uint32 NumGrids = FMath::DivideAndRoundUp(BaseMipSize, 16u);
//
//			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("RTXDIComputeLightPdf"), LightCdfShader, PassParameters, FIntVector(NumGrids, NumGrids, 1));
//		}
//
//		FRDGBufferDesc RisBufferDesc = FRDGBufferDesc::CreateBufferDesc(sizeof(FVector2f), RisBufferElements);
//
//		RisBuffer = GraphBuilder.CreateBuffer(RisBufferDesc, TEXT("RisBuffer"));
//
//		{
//			FComputeLightingRisBufferCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FComputeLightingRisBufferCS::FParameters>();
//
//			PassParameters->LightPdfTexture = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(CdfTexture));
//			PassParameters->MaxMipLevel = MaxMip;
//			PassParameters->PdfTexDimensions = PdfTexSize;
//			PassParameters->RisTileSize = TileSize;
//			PassParameters->WeightedSampling = FMath::Clamp(CVarRayTracingSampledLightingRISWeighted.GetValueOnRenderThread(), 0.0f, 1.0f);
//			PassParameters->SampleEnvMap = 0;
//			PassParameters->SampledLightData = SampledLightDataUniformBuffer;
//			PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
//			PassParameters->RisBuffer = GraphBuilder.CreateUAV(RisBuffer, PF_R32G32_UINT);
//
//			auto LightPresampleShader = View.ShaderMap->GetShader<FComputeLightingRisBufferCS>();
//
//			// dispatch handles 256 elements of a tile per block
//			int32 RoundedTiles = FMath::DivideAndRoundUp(TileSize, 256);
//
//			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("RTXDIPresampleLights"), LightPresampleShader, PassParameters, FIntVector(RoundedTiles, TileCount, 1));
//		}
//	}
//	else
//	{
//		// RIS is not in use, create tiny stand-in buffer
//		// ToDo: refactor to have a constant one that persists rather than requiring a UAV clear
//		FRDGBufferDesc RisBufferDesc = FRDGBufferDesc::CreateBufferDesc(sizeof(FVector2f), 1);
//
//		RisBuffer = GraphBuilder.CreateBuffer(RisBufferDesc, TEXT("RisBuffer"));
//
//		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(RisBuffer, PF_R32G32_UINT), 0);
//	}
//
//	return RisBuffer;
//}
//
//static FRDGBufferRef BuildReGIRStructures(FRDGBuilder& GraphBuilder, int32 TileSize, FIntVector CellCount, const FViewInfo& View, const TUniformBufferRef<FSampledLightData>& SampledLightDataUniformBuffer)
//{
//	FRDGBufferRef ReGIRBuffer;
//	const int32 ReGIRBufferElements =  TileSize * CellCount.X * CellCount.Y * CellCount.Z;
//
//	if (ReGIRBufferElements > 0)
//	{
//
//		FRDGBufferDesc ReGIRBufferDesc = FRDGBufferDesc::CreateBufferDesc(sizeof(FVector2f), ReGIRBufferElements);
//
//		ReGIRBuffer = GraphBuilder.CreateBuffer(ReGIRBufferDesc, TEXT("ReGIRBuffer"));
//
//		{
//			FComputeReGIRSamplingBufferCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FComputeReGIRSamplingBufferCS::FParameters>();
//
//			PassParameters->ReGIRTileSize = TileSize;
//			
//			PassParameters->NumSampleIterations = FMath::Max( 1, CVarRayTracingSampledLightingReGIRSampleIterations.GetValueOnRenderThread());
//			PassParameters->CellSize = CVarRayTracingSampledLightingReGIRCellSize.GetValueOnRenderThread();
//			PassParameters->CellCount = CellCount;
//			PassParameters->SampledLightData = SampledLightDataUniformBuffer;
//			PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
//			PassParameters->RisBuffer = GraphBuilder.CreateUAV(ReGIRBuffer, PF_R32G32_UINT);
//
//			auto ReGIRShader = View.ShaderMap->GetShader<FComputeReGIRSamplingBufferCS>();
//
//			// dispatch handles 256 elements per block
//			int32 RoundedTiles = FMath::DivideAndRoundUp(ReGIRBufferElements, 256);
//
//			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("RTXDIReGIRSampleLights"), ReGIRShader, PassParameters, FIntVector(RoundedTiles, 1, 1));
//		}
//	}
//	else
//	{
//		// RIS is not in use, create tiny stand-in buffer
//		// ToDo: refactor to have a constant one that persists rather than requiring a UAV clear
//		FRDGBufferDesc ReGIRBufferDesc = FRDGBufferDesc::CreateBufferDesc(sizeof(FVector2f), 1);
//
//		ReGIRBuffer = GraphBuilder.CreateBuffer(ReGIRBufferDesc, TEXT("ReGIRBuffer"));
//
//		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(ReGIRBuffer, PF_R32G32_UINT), 0);
//	}
//
//	return ReGIRBuffer;
//}
//		
//struct FSkylightRIS
//{
//	FRDGBufferRef RISBuffer;
//	FRDGTextureRef EnvTexture;
//	FLinearColor SkyColor;
//	float InvSize;
//};
//
//static FSkylightRIS BuildSkylightRISStructures(FRDGBuilder& GraphBuilder, int32 TileSize, int32 TileCount, const FViewInfo& View, const TUniformBufferRef<FSampledLightData>& SampledLightDataUniformBuffer)
//{
//	FRDGBufferRef RisBuffer;
//	FRDGTextureRef EnvTexture;
//	const int32 RisBufferElements = TileCount * TileSize;
//
//	FReflectionUniformParameters Parameters;
//	SetupReflectionUniformParameters(View, Parameters);
//
//	const FScene* Scene = (const FScene*)View.Family->Scene;
//	FLinearColor SkyColor = FLinearColor::Black;
//	float InvSize = 0.0f;
//
//	if (RisBufferElements > 0 && Scene->SkyLight)
//	{
//		SkyColor = Scene->SkyLight->GetEffectiveLightColor();
//
//		// follow the practice of the path tracer and double the dimension to roughly match the sample rate of the cubemap
//		uint32 TexSize = FMath::RoundUpToPowerOfTwo(2 * Scene->SkyLight->CaptureCubeMapResolution);
//		InvSize = 1.0f / TexSize;
//
//		const uint32 MaxMip = FMath::FloorLog2(TexSize);
//		const uint32 NumMips = MaxMip + 1;
//
//		// Create env map
//		FRDGTextureDesc TexDesc = FRDGTextureDesc::Create2D(
//			FIntPoint(TexSize, TexSize),
//			PF_FloatRGBA, // RGBA fp16
//			FClearValueBinding::Black,
//			TexCreate_ShaderResource | TexCreate_UAV,
//			1);
//
//		// Create env map pdf
//		FRDGTextureDesc PdfDesc = FRDGTextureDesc::Create2D(
//			FIntPoint(TexSize, TexSize),
//			PF_R32_FLOAT,
//			FClearValueBinding::None,
//			TexCreate_ShaderResource | TexCreate_UAV,
//			NumMips);
//
//		FRDGTextureRef CdfTexture = GraphBuilder.CreateTexture(PdfDesc, TEXT("RTXDIEnvMapCDF"));
//		EnvTexture = GraphBuilder.CreateTexture(TexDesc, TEXT("RTXDIEnvMap"));
//
//		// first populate the envmap and level 0 of the CDF
//		{
//			FPreprocessSkylightForRISCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FPreprocessSkylightForRISCS::FParameters>();
//
//			PassParameters->SkyColor = FVector3f(SkyColor.R, SkyColor.G, SkyColor.B);
//			PassParameters->SkyLightCubemap0 = Parameters.SkyLightCubemap;
//			PassParameters->SkyLightCubemap1 = Parameters.SkyLightBlendDestinationCubemap;
//			PassParameters->SkyLightCubemapSampler0 = Parameters.SkyLightCubemapSampler;
//			PassParameters->SkyLightCubemapSampler1 = Parameters.SkyLightBlendDestinationCubemapSampler;
//			PassParameters->SkylightBlendFactor = Parameters.SkyLightParameters.W;
//			PassParameters->SkylightInvResolution = InvSize;
//
//			PassParameters->LightPdfUAV0 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(CdfTexture, 0));
//			PassParameters->PreprocessedSkylight = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(EnvTexture));
//
//			auto SkyLightProcessingShader = View.ShaderMap->GetShader< FPreprocessSkylightForRISCS>();
//
//			uint32 NumGrids = FMath::DivideAndRoundUp(TexSize, 16u);
//			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("RTXDIProcessSkyLight"), SkyLightProcessingShader, PassParameters, FIntVector(NumGrids, NumGrids, 1));
//		}
//
//		// each pass generates 5 mip levels, starting at 1, since 0 is computed when collapsing the EnvMap
//		for (uint32 BaseMip = 1; BaseMip < NumMips; BaseMip += 5)
//		{
//			uint32 BaseMipSize = TexSize >> BaseMip;
//			// compute the local light CDF as a mip-mapped texture
//			FComputeLightingPdfCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FComputeLightingPdfCS::FParameters>();
//
//			PassParameters->PdfTexDimensions = BaseMipSize;
//			PassParameters->CreateBaseLevel = BaseMip == 0;
//
//			if (BaseMip == 0)
//			{
//				PassParameters->LightPdfTexture = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMipLevel(GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy), 0));
//			}
//			else
//			{
//				PassParameters->LightPdfTexture = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMipLevel(CdfTexture, BaseMip - 1));
//			}
//
//			PassParameters->LightPdfUAV0 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(CdfTexture, FMath::Min(BaseMip + 0u, MaxMip)));
//			PassParameters->LightPdfUAV1 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(CdfTexture, FMath::Min(BaseMip + 1u, MaxMip)));
//			PassParameters->LightPdfUAV2 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(CdfTexture, FMath::Min(BaseMip + 2u, MaxMip)));
//			PassParameters->LightPdfUAV3 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(CdfTexture, FMath::Min(BaseMip + 3u, MaxMip)));
//			PassParameters->LightPdfUAV4 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(CdfTexture, FMath::Min(BaseMip + 4u, MaxMip)));
//
//			PassParameters->SampledLightData = SampledLightDataUniformBuffer;
//
//			auto LightCdfShader = View.ShaderMap->GetShader<FComputeLightingPdfCS>();
//
//			uint32 NumGrids = FMath::DivideAndRoundUp(BaseMipSize, 16u);
//
//			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("RTXDIComputeSkyLightPdf"), LightCdfShader, PassParameters, FIntVector(NumGrids, NumGrids, 1));
//		}
//
//		FRDGBufferDesc RisBufferDesc = FRDGBufferDesc::CreateBufferDesc(sizeof(FVector2f), RisBufferElements);
//
//		RisBuffer = GraphBuilder.CreateBuffer(RisBufferDesc, TEXT("SkylightRisBuffer"));
//
//		{
//			FComputeLightingRisBufferCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FComputeLightingRisBufferCS::FParameters>();
//
//			PassParameters->LightPdfTexture = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(CdfTexture));
//			PassParameters->MaxMipLevel = MaxMip;
//			PassParameters->PdfTexDimensions = TexSize;
//			PassParameters->RisTileSize = TileSize;
//			PassParameters->SampleEnvMap = 1;
//			PassParameters->WeightedSampling = 0.5; // always using even balance betweeen weighted and unweighted
//			PassParameters->SampledLightData = SampledLightDataUniformBuffer;
//			PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
//			PassParameters->RisBuffer = GraphBuilder.CreateUAV(RisBuffer, PF_R32G32_UINT);
//
//			auto LightPresampleShader = View.ShaderMap->GetShader<FComputeLightingRisBufferCS>();
//
//			// dispatch handles 256 elements of a tile per block
//			int32 RoundedTiles = FMath::DivideAndRoundUp(TileSize, 256);
//
//			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("RTXDIPresampleSkyLight"), LightPresampleShader, PassParameters, FIntVector(RoundedTiles, TileCount, 1));
//		}
//	}
//	else
//	{
//		// RIS is not in use, create tiny stand-in buffer
//		// ToDo: refactor to have a constant one that persists rather than requiring a UAV clear
//		FRDGBufferDesc RisBufferDesc = FRDGBufferDesc::CreateBufferDesc(sizeof(FVector2f), 1);
//
//		RisBuffer = GraphBuilder.CreateBuffer(RisBufferDesc, TEXT("SkylightRisBuffer"));
//
//		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(RisBuffer, PF_R32G32_UINT), 0);
//
//		EnvTexture = GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy); //GraphBuilder.CreateTexture(TexDesc, TEXT("RTXDIEnvMap"));
//
//	}
//
//	return { RisBuffer,EnvTexture, SkyColor, InvSize };
//}
//
//void FDeferredShadingSceneRenderer::PrepareRayTracingSampledDirectLighting(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
//{
//	// Declare all RayGen shaders that require material closest hit shaders to be bound
//
//	if (!ShouldRenderRayTracingSampledLighting())
//	{
//		return;
//	}
//
//	static auto CVarEnableScreenTrace = IConsoleManager::Get().FindConsoleVariable(TEXT("r.RayTracing.Shadows.ScreenTraceMode"));
//
//	SampledLightingPermutation Options = GetPermutationOptions();
//	const int32 PresampleLightsMode = FMath::Clamp( CVarRayTracingSampledLightingPresampleLocalLights.GetValueOnRenderThread(), 0, 2);
//	const int32 ScreenTraceMode = CVarEnableScreenTrace ? FMath::Clamp(CVarEnableScreenTrace->GetInt(), 0, 1) : 0;
//
//	for (int32 Permutation = 0; Permutation < 2; Permutation++)
//	{
//		FDirectLightRGS::FPermutationDomain PermutationVector;
//		PermutationVector.Set<FDirectLightRGS::FHairShadingDim>(Permutation != 0);
//		PermutationVector.Set<FDirectLightRGS::FScreenTraceDim>(ScreenTraceMode);
//
//		auto RayGenShader = View.ShaderMap->GetShader<FDirectLightRGS>(PermutationVector);
//		OutRayGenShaders.Add(RayGenShader.GetRayTracingShader());
//	}
//
//	{
//		FGenerateInitialSamplesRGS::FPermutationDomain PermutationVector;
//		PermutationVector.Set<FGenerateInitialSamplesRGS::FRisSampleLocalLightsDim>(PresampleLightsMode);
//		AddShaderPermutation<FGenerateInitialSamplesRGS>(PermutationVector, Options, View, OutRayGenShaders);
//	}
//	AddShaderPermutation< FApplySpatialResamplingRGS>(Options,View, OutRayGenShaders);
//
//	for (int32 Permutation = 0; Permutation < 2; Permutation++)
//	{
//		FApplyTemporalResamplingRGS::FPermutationDomain PermutationVector;
//
//		PermutationVector.Set<FApplyTemporalResamplingRGS::FFuseInitialSamplingDim>(Permutation != 0);
//		PermutationVector.Set<FApplyTemporalResamplingRGS::FRisSampleLocalLightsDim>(PresampleLightsMode);
//
//		AddShaderPermutation< FApplyTemporalResamplingRGS>(PermutationVector, Options, View, OutRayGenShaders);
//	}
//
//	for (int32 Permutation = 0; Permutation < 2; Permutation++)
//	{
//		FEvaluateSampledLightingRGS::FPermutationDomain PermutationVector;
//
//		PermutationVector.Set<FEvaluateSampledLightingRGS::FHairLightingDim>(Permutation != 0);
//		PermutationVector.Set<FEvaluateSampledLightingRGS::FScreenTraceDim>(ScreenTraceMode);
//
//		AddShaderPermutation< FEvaluateSampledLightingRGS>(PermutationVector, Options, View, OutRayGenShaders);
//	}
//}
//
//
//void FDeferredShadingSceneRenderer::RenderSampledDirectLighting(
//	FRDGBuilder& GraphBuilder,
//	TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
//	const FSceneTexturesConfig& SceneTexturesConfig,
//	const TArray<FSortedLightSceneInfo,SceneRenderingAllocator> &SampledLights,
//	const FSimpleLightArray &SimpleLights,
//	FRDGTextureRef SceneColorTexture,
//	FRDGTextureRef LightingChannelsTexture)
//{
//	RDG_EVENT_SCOPE(GraphBuilder, "SampledDirectLighting");
//
//	FSceneTextureParameters SceneTextures = GetSceneTextureParameters(GraphBuilder, SceneTexturesUniformBuffer);
//
//	const FViewInfo& ReferenceView = Views[0];
//
//	//create/update light structure, could do per view rather than via reference view
//	TUniformBufferRef<FSampledLightData> SampledLightDataUniformBuffer = CreateSampledLightDatadUniformBuffer(
//		GraphBuilder.RHICmdList,
//		Scene,
//		SampledLights,
//		SimpleLights,
//		ReferenceView,
//		ReferenceView.PrevViewInfo.SampledLightHistory,
//		EUniformBufferUsage::UniformBuffer_SingleFrame,
//		ReferenceView.ViewState->PrevFrameViewInfo.SampledLightHistory);
//
//	const int32 PresampleLightsMode = FMath::Clamp(CVarRayTracingSampledLightingPresampleLocalLights.GetValueOnRenderThread(),0,2);
//	const int32 RisTileSize = PresampleLightsMode == 1 ? CVarRayTracingSampledLightingRISTileSize.GetValueOnRenderThread() : 0;
//	const int32 RisTileCount = PresampleLightsMode == 1 ? CVarRayTracingSampledLightingRISTiles.GetValueOnRenderThread() : 0;
//	const int32 ReGIRTileSize = PresampleLightsMode == 2 ? CVarRayTracingSampledLightingReGIRSamplesPerCell.GetValueOnRenderThread(): 0;
//	FIntVector ReGIRCellCount;
//	ReGIRCellCount.X = FMath::Max(1, CVarRayTracingSampledLightingReGIRCellsX.GetValueOnRenderThread());
//	ReGIRCellCount.Y = FMath::Max(1, CVarRayTracingSampledLightingReGIRCellsY.GetValueOnRenderThread());
//	ReGIRCellCount.Z = FMath::Max(1, CVarRayTracingSampledLightingReGIRCellsZ.GetValueOnRenderThread());
//
//	FRDGBufferRef RisBuffer = BuildRISStructures(GraphBuilder, RisTileSize, RisTileCount, SampledLights.Num(), ReferenceView, SampledLightDataUniformBuffer);
//	FRDGBufferRef ReGIRBuffer = BuildReGIRStructures(GraphBuilder, ReGIRTileSize, ReGIRCellCount, ReferenceView, SampledLightDataUniformBuffer);
//
//	const int32 SkyTiles = (Scene->SkyLight != nullptr && CVarRayTracingSampledLightingSkylight.GetValueOnRenderThread() != 0) ? 16 : 0;
//
//	FSkylightRIS SkylightRIS = BuildSkylightRISStructures(GraphBuilder, 256, SkyTiles, ReferenceView, SampledLightDataUniformBuffer);
//
//	// Intermediate lighting targets
//	FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
//		SceneTextures.SceneDepthTexture->Desc.Extent,
//		PF_FloatRGBA,
//		FClearValueBinding::None,
//		TexCreate_ShaderResource | TexCreate_UAV);
//
//	FRDGTextureRef Diffuse = GraphBuilder.CreateTexture(Desc, TEXT("SampledLightDiffuse"));
//	FRDGTextureRef Specular = GraphBuilder.CreateTexture(Desc, TEXT("SampledLightSpecular"));
//
//	Desc.Format = PF_G16R16F;
//	FRDGTextureRef RayHitDistance = GraphBuilder.CreateTexture(Desc, TEXT("SampledLightHitDistance"));
//
//	const int32 RequestedReservoirs = CVarSampledLightingNumReservoirs.GetValueOnAnyThread();
//	const int32 MinReservoirs = FMath::Max(CVarSampledLightingMinReservoirs.GetValueOnAnyThread(), 1);
//	const int32 MaxReservoirs = FMath::Max(CVarSampledLightingMaxReservoirs.GetValueOnAnyThread(), 1);
//	const bool SubsampledView = ReferenceView.GetSecondaryViewRectSize() != ReferenceView.ViewRect.Size();
//	const int32 AutoReservoirs = SubsampledView ? MaxReservoirs : MinReservoirs;
//	const int32 NumReservoirs = RequestedReservoirs < 0 ? AutoReservoirs : FMath::Max(RequestedReservoirs, 1);
//	FIntPoint PaddedSize = FMath::DivideAndRoundUp<FIntPoint>(SceneTextures.SceneDepthTexture->Desc.Extent, 4) * 4;
//
//	FIntVector ReservoirBufferDim = FIntVector(PaddedSize.X, PaddedSize.Y, NumReservoirs + 1);
//	FRDGBufferDesc ReservoirDesc = FRDGBufferDesc::CreateStructuredDesc(16, ReservoirBufferDim.X * ReservoirBufferDim.Y * ReservoirBufferDim.Z);
//
//	FRDGBufferRef LightReservoirs = GraphBuilder.CreateBuffer(ReservoirDesc, TEXT("LightReservoirs"));
//	
//	FIntVector ReservoirHistoryBufferDim = FIntVector(PaddedSize.X, PaddedSize.Y, NumReservoirs);
//	FRDGBufferDesc ReservoirHistoryDesc = FRDGBufferDesc::CreateStructuredDesc(16, ReservoirHistoryBufferDim.X * ReservoirHistoryBufferDim.Y * ReservoirHistoryBufferDim.Z);
//	FRDGBufferRef LightReservoirsHistory = GraphBuilder.CreateBuffer(ReservoirHistoryDesc, TEXT("LightReservoirsHistory"));
//
//	const int32 LightingMode = CVarSampledLightingMode.GetValueOnRenderThread();
//
//	// Hair parameters
//	const bool bEvaluateStrandBasedHair = CVarRayTracingSampledLightingHair.GetValueOnRenderThread() != 0;
//	const int32 HairSamples = FMath::Max(1, CVarRayTracingSampledLightingHairSamples.GetValueOnRenderThread());
//	const int32 HairCandidates = FMath::Max(1, CVarRayTracingSampledLightingHairCandidates.GetValueOnRenderThread());
//
//	// evaluate lighting
//	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
//	{
//		const FViewInfo& View = Views[ViewIndex];
//		FIntPoint LightingResolution = View.ViewRect.Size();
//
//		const bool bUseHairLighting = HairStrands::HasViewHairStrandsData(View) && HairStrands::HasViewHairStrandsVoxelData(View);
//		const bool bUseHairDeepShadow = HairStrands::HasViewHairStrandsData(View); // EHartNV ToDo: Light scene proxy technically allows hair to change behavior per light
//
//		static auto CVarSupportTranslucency = IConsoleManager::Get().FindConsoleVariable(TEXT("r.RayTracing.Shadows.SupportTranslucency"));
//		static auto CVarMaxInexactBias = IConsoleManager::Get().FindConsoleVariable(TEXT("r.RayTracing.Shadow.MaxBiasForInexactGeometry"));
//		static auto CVarEnableInexactBias = IConsoleManager::Get().FindConsoleVariable(TEXT("r.RayTracing.Shadow.UseBiasForSkipWPOEval"));
//
//		static auto CVarEnableScreenTrace = IConsoleManager::Get().FindConsoleVariable(TEXT("r.RayTracing.Shadows.ScreenTraceMode"));
//		static auto CVarMaxScreenTraceDistance = IConsoleManager::Get().FindConsoleVariable(TEXT("r.RayTracing.Shadows.MaxScreenTraceDistance"));
//
//		const int32 ScreenTraceMode = CVarEnableScreenTrace ? FMath::Clamp(CVarEnableScreenTrace->GetInt(), 0, 1) : 0;
//		const bool bEnableScreenTrace = ScreenTraceMode != 0;
//		const float ApproxMinDistance = (bEnableScreenTrace && CVarMaxScreenTraceDistance) ? FMath::Max(0.1f, CVarMaxScreenTraceDistance->GetFloat()) : 0.1f;
//
//		// Parameters shared by ray tracing passes
//		// EHartNV ToDo: possibly refactor to make direct sampled lighting not rely on a reservoir UAV
//		FSampledLightingCommonParameters CommonParameters;
//		CommonParameters.MaxNormalBias = GetRaytracingMaxNormalBias();
//		CommonParameters.TLAS = View.GetRayTracingSceneViewChecked();
//		CommonParameters.RWLightReservoirUAV = GraphBuilder.CreateUAV(LightReservoirs);
//		CommonParameters.ReservoirBufferDim = ReservoirBufferDim;
//		CommonParameters.VisibilityApproximateTestMode = CVarSampledLightingApproximateVisibilityMode.GetValueOnRenderThread();
//		CommonParameters.VisibilityFaceCull = CVarSampledLightingFaceCull.GetValueOnRenderThread();
//		CommonParameters.SupportTranslucency = CVarSupportTranslucency ? CVarSupportTranslucency->GetInt() : 0;
//		CommonParameters.InexactShadows = CVarEnableInexactBias ? CVarEnableInexactBias->GetInt() : 0;
//		CommonParameters.MaxBiasForInexactGeometry = CVarMaxInexactBias ? CVarMaxInexactBias->GetFloat() : 0.0f;
//		CommonParameters.ApproxVisibilityMinDistance = ApproxMinDistance;
//		CommonParameters.MaxTemporalHistory = FMath::Max(1, CVarSampledLightingTemporalMaxHistory.GetValueOnRenderThread());
//		CommonParameters.RISBuffer = GraphBuilder.CreateSRV(RisBuffer, PF_R32G32_UINT);
//		CommonParameters.RISBufferTiles = RisTileCount;
//		CommonParameters.RISBufferTileSize = RisTileSize;
//		CommonParameters.ReGIRBuffer = GraphBuilder.CreateSRV(ReGIRBuffer, PF_R32G32_UINT);
//		CommonParameters.CellSize = CVarRayTracingSampledLightingReGIRCellSize.GetValueOnRenderThread();
//		CommonParameters.CellCount = ReGIRCellCount;
//		CommonParameters.ReGIRTileSize = ReGIRTileSize;
//
//		CommonParameters.RISSkylightBuffer = GraphBuilder.CreateSRV(SkylightRIS.RISBuffer, PF_R32G32_UINT);
//		CommonParameters.RISSkylightBufferTiles = SkyTiles;
//		CommonParameters.RISSkylightBufferTileSize = 256;
//		CommonParameters.InvSkylightSize = SkylightRIS.InvSize;
//		CommonParameters.SkylightTexture = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(SkylightRIS.EnvTexture));
//		CommonParameters.SkylightSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
//
//
//		FSampledLightingSceenTraceParameters ScreenTraceParameters;
//		{
//			static auto CVarUseClosestHZB = IConsoleManager::Get().FindConsoleVariable(TEXT("r.RayTracing.Shadows.UseClosestHZB"));
//			static auto CVarMaxScreenTraceIterations = IConsoleManager::Get().FindConsoleVariable(TEXT("r.RayTracing.Shadows.MaxScreenTraceIterations"));
//			static auto CVarScreenRelativeDepthThickness = IConsoleManager::Get().FindConsoleVariable(TEXT("r.RayTracing.Shadows.ScreenRelativeDepthThickness"));
//
//			const float MaxScreenTraceDistance = CVarMaxScreenTraceDistance ? CVarMaxScreenTraceDistance->GetFloat() : 0.0f;
//			FRDGTextureRef HZB = (CVarUseClosestHZB && CVarUseClosestHZB->GetInt() != 0) ? View.ClosestHZB : View.HZB;
//
//			if (HZB)
//			{
//				ScreenTraceParameters.ClosestHZBTexture = HZB; // Not valid in certain render modes
//				ScreenTraceParameters.HZBBaseTexelSize = FVector2f(1.0f / HZB->Desc.Extent.X, 1.0f / HZB->Desc.Extent.Y);
//
//				ScreenTraceParameters.MaxHierarchicalScreenTraceIterations = CVarMaxScreenTraceIterations ? CVarMaxScreenTraceIterations->GetInt() : 0;
//				ScreenTraceParameters.MaxScreenTraceDistance = MaxScreenTraceDistance;
//			}
//			else
//			{
//				ScreenTraceParameters.ClosestHZBTexture = GSystemTextures.GetBlackDummy(GraphBuilder);
//				ScreenTraceParameters.HZBBaseTexelSize = FVector2f(1.0f, 1.0f);
//
//				// force scrren tracing off
//				ScreenTraceParameters.MaxHierarchicalScreenTraceIterations = 0;
//				ScreenTraceParameters.MaxScreenTraceDistance = 0.0f;
//			}
//
//			const FVector2D HZBUvFactor(
//				float(View.ViewRect.Width()) / float(2 * View.HZBMipmap0Size.X),
//				float(View.ViewRect.Height()) / float(2 * View.HZBMipmap0Size.Y));
//			ScreenTraceParameters.HZBUvFactorAndInvFactor = FVector4f(
//				HZBUvFactor.X,
//				HZBUvFactor.Y,
//				1.0f / HZBUvFactor.X,
//				1.0f / HZBUvFactor.Y);
//
//			const FVector4f ScreenPositionScaleBias = View.GetScreenPositionScaleBias(SceneTexturesConfig.Extent, View.ViewRect);
//			const FVector2f HZBUVToScreenUVScale = FVector2f(1.0f / HZBUvFactor.X, 1.0f / HZBUvFactor.Y) * FVector2f(2.0f, -2.0f) * FVector2f(ScreenPositionScaleBias.X, ScreenPositionScaleBias.Y);
//			const FVector2f HZBUVToScreenUVBias = FVector2f(-1.0f, 1.0f) * FVector2f(ScreenPositionScaleBias.X, ScreenPositionScaleBias.Y) + FVector2f(ScreenPositionScaleBias.W, ScreenPositionScaleBias.Z);
//			ScreenTraceParameters.HZBUVToScreenUVScaleBias = FVector4f(HZBUVToScreenUVScale, HZBUVToScreenUVBias);
//
//			ScreenTraceParameters.RelativeDepthThickness = CVarScreenRelativeDepthThickness ? CVarScreenRelativeDepthThickness->GetFloat() : 0.0f;
//		}
//
//		if (LightingMode == 0)
//		{
//			// single pass mode sampling independently per pixel
//			FDirectLightRGS::FParameters* PassParameters = GraphBuilder.AllocParameters<FDirectLightRGS::FParameters>();
//
//			PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
//			PassParameters->SampledLightData = SampledLightDataUniformBuffer;
//			PassParameters->SceneTextures = SceneTextures;
//			PassParameters->SceneLightingChannels = GetSceneLightingChannelParameters(GraphBuilder, LightingChannelsTexture);
//			PassParameters->SSProfilesTexture = View.RayTracingSubSurfaceProfileTexture ? View.RayTracingSubSurfaceProfileTexture : GSystemTextures.BlackDummy->GetRHI();
//
//			PassParameters->RWDiffuseUAV = GraphBuilder.CreateUAV(Diffuse);
//			PassParameters->RWSpecularUAV = GraphBuilder.CreateUAV(Specular);
//			PassParameters->RWRayDistanceUAV = GraphBuilder.CreateUAV(RayHitDistance);
//
//			PassParameters->SampledLightingCommonParameters = CommonParameters;
//			PassParameters->ScreenTraceParameters = ScreenTraceParameters;
//
//			PassParameters->BruteForceSamples = CVarRayTracingSampledLightingDirectNumSamples.GetValueOnRenderThread();
//			PassParameters->BruteForceCandidates = CVarRayTracingSampledLightingDirectNumCandidates.GetValueOnRenderThread();
//			PassParameters->DemodulateMaterials = CVarSampledLightingDemodulateMaterials.GetValueOnRenderThread();
//
//			FDirectLightRGS::FPermutationDomain PermutationVector;
//			PermutationVector.Set<FDirectLightRGS::FHairShadingDim>(false);
//			PermutationVector.Set<FDirectLightRGS::FScreenTraceDim>(ScreenTraceMode);
//
//			auto RayGenShader = View.ShaderMap->GetShader<FDirectLightRGS>(PermutationVector);
//
//			ClearUnusedGraphResources(RayGenShader, PassParameters);
//
//			GraphBuilder.AddPass(
//				RDG_EVENT_NAME("SinglePassSampledLighting"),
//				PassParameters,
//				ERDGPassFlags::Compute,
//				[PassParameters, this, &View, RayGenShader, LightingResolution](FRHIRayTracingCommandList& RHICmdList)
//			{
//				FRayTracingShaderBindingsWriter GlobalResources;
//				SetShaderParameters(GlobalResources, RayGenShader, *PassParameters);
//
//				FRHIRayTracingScene* RayTracingSceneRHI = View.GetRayTracingSceneChecked();
//				RHICmdList.RayTraceDispatch(View.RayTracingMaterialPipeline, RayGenShader.GetRayTracingShader(), RayTracingSceneRHI, GlobalResources, LightingResolution.X, LightingResolution.Y);
//
//
//			});
//		}
//		else
//		{
//			// detect camera cuts/history invalidation and boost initial/spatial samples to compensate
//			const bool bCameraCut = !ReferenceView.PrevViewInfo.SampledLightHistory.LightReservoirs.IsValid() || ReferenceView.bCameraCut;
//			const int32 PrevHistoryCount = ReferenceView.PrevViewInfo.SampledLightHistory.ReservoirDimensions.Z;
//
//			// Global permutation options
//			const SampledLightingPermutation Options = GetPermutationOptions();
//
//			const int32 InitialCandidates = bCameraCut ? CVarSampledLightingInitialCandidatesBoost.GetValueOnRenderThread() : CVarSampledLightingInitialCandidates.GetValueOnRenderThread();
//
//			int32 InitialSlice = 0;
//			const bool bEnableFusedSampling = CVarSampledLightingFusedSampling.GetValueOnRenderThread() != 0;
//
//
//			for (int32 Reservoir = 0; Reservoir < NumReservoirs; Reservoir++)
//			{
//				const bool bUseFusedSampling = CVarSampledLightingTemporal.GetValueOnRenderThread() != 0 && !bCameraCut && Reservoir < PrevHistoryCount && bEnableFusedSampling;
//
//				// Initial sampling pass to select a light candidate
//				if (!bUseFusedSampling)
//				{
//					FGenerateInitialSamplesRGS::FParameters* PassParameters = GraphBuilder.AllocParameters<FGenerateInitialSamplesRGS::FParameters>();
//
//					PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
//					PassParameters->SampledLightData = SampledLightDataUniformBuffer;
//					PassParameters->SceneTextures = SceneTextures; //SceneTextures;
//					PassParameters->SSProfilesTexture = View.RayTracingSubSurfaceProfileTexture;
//					PassParameters->SceneLightingChannels = GetSceneLightingChannelParameters(GraphBuilder, LightingChannelsTexture);
//
//					PassParameters->OutputSlice = Reservoir;
//					PassParameters->HistoryReservoir = Reservoir;
//					PassParameters->InitialCandidates = FMath::Max(1, InitialCandidates);
//					PassParameters->InitialSampleVisibility = CVarRayTracingSampledLightingTestInitialVisibility.GetValueOnRenderThread();
//
//					PassParameters->SampledLightingCommonParameters = CommonParameters;
//
//					FGenerateInitialSamplesRGS::FPermutationDomain PermutationVector;
//					PermutationVector.Set<FGenerateInitialSamplesRGS::FRisSampleLocalLightsDim>(PresampleLightsMode);
//					auto RayGenShader = GetShaderPermutation<FGenerateInitialSamplesRGS>(PermutationVector,Options, View);
//
//					ClearUnusedGraphResources(RayGenShader, PassParameters);
//
//					GraphBuilder.AddPass(
//						RDG_EVENT_NAME("CreateInitialSamples"),
//						PassParameters,
//						ERDGPassFlags::Compute,
//						[PassParameters, this, &View, RayGenShader, LightingResolution](FRHIRayTracingCommandList& RHICmdList)
//					{
//						FRayTracingShaderBindingsWriter GlobalResources;
//						SetShaderParameters(GlobalResources, RayGenShader, *PassParameters);
//
//						FRHIRayTracingScene* RayTracingSceneRHI = View.GetRayTracingSceneChecked();
//						RHICmdList.RayTraceDispatch(View.RayTracingMaterialPipeline, RayGenShader.GetRayTracingShader(), RayTracingSceneRHI, GlobalResources, LightingResolution.X, LightingResolution.Y);
//					});
//				}
//
//				// Temporal candidate merge pass, optionally merged with initial candidate pass
//				if (CVarSampledLightingTemporal.GetValueOnRenderThread() != 0 && !bCameraCut && Reservoir < PrevHistoryCount)
//				{
//					{
//						FApplyTemporalResamplingRGS::FParameters* PassParameters = GraphBuilder.AllocParameters<FApplyTemporalResamplingRGS::FParameters>();
//
//						PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
//						PassParameters->SampledLightData = SampledLightDataUniformBuffer;
//						PassParameters->SceneTextures = SceneTextures; //SceneTextures;
//						PassParameters->SSProfilesTexture = View.RayTracingSubSurfaceProfileTexture;
//						PassParameters->SceneLightingChannels = GetSceneLightingChannelParameters(GraphBuilder, LightingChannelsTexture);
//
//						PassParameters->ReservoirHistoryBufferDim = ReservoirHistoryBufferDim;
//						PassParameters->InputSlice = Reservoir;
//						PassParameters->OutputSlice = Reservoir;
//						PassParameters->HistoryReservoir = Reservoir;
//						PassParameters->TemporalDepthRejectionThreshold = FMath::Clamp(CVarSampledLightingTemporalDepthRejectionThreshold.GetValueOnRenderThread(), 0.0f, 1.0f);
//						PassParameters->TemporalNormalRejectionThreshold = FMath::Clamp(CVarSampledLightingTemporalNormalRejectionThreshold.GetValueOnRenderThread(), -1.0f, 1.0f);
//						PassParameters->ApplyApproximateVisibilityTest = CVarSampledLightingTemporalApplyApproxVisibility.GetValueOnAnyThread();
//						PassParameters->InitialCandidates = FMath::Max(1, InitialCandidates);
//						PassParameters->InitialSampleVisibility = CVarRayTracingSampledLightingTestInitialVisibility.GetValueOnRenderThread();
//
//						PassParameters->SpatiallyHashTemporalReprojection = FMath::Clamp(CVarSampledLightingTemporalApplySpatialHash.GetValueOnRenderThread(), 0, 1);
//
//						PassParameters->LightReservoirHistory = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(ReferenceView.PrevViewInfo.SampledLightHistory.LightReservoirs));
//						PassParameters->NormalHistory = RegisterExternalTextureWithFallback(GraphBuilder, ReferenceView.PrevViewInfo.GBufferA, GSystemTextures.BlackDummy);
//						PassParameters->DepthHistory = RegisterExternalTextureWithFallback(GraphBuilder, ReferenceView.PrevViewInfo.DepthBuffer, GSystemTextures.BlackDummy);
//
//						PassParameters->SampledLightingCommonParameters = CommonParameters;
//
//						FApplyTemporalResamplingRGS::FPermutationDomain PermutationVector;
//						PermutationVector.Set<FApplyTemporalResamplingRGS::FFuseInitialSamplingDim>(bEnableFusedSampling);
//						PermutationVector.Set<FApplyTemporalResamplingRGS::FRisSampleLocalLightsDim>(PresampleLightsMode);
//
//						//auto RayGenShader = View.ShaderMap->GetShader<FApplyTemporalResamplingRGS>(PermutationVector);
//						auto RayGenShader = GetShaderPermutation<FApplyTemporalResamplingRGS>(PermutationVector,Options, View);
//
//						ClearUnusedGraphResources(RayGenShader, PassParameters);
//
//						GraphBuilder.AddPass(
//							RDG_EVENT_NAME("%sTemporalResample", bEnableFusedSampling ? TEXT("FusedInitialCandidateAnd") : TEXT("")),
//							PassParameters,
//							ERDGPassFlags::Compute,
//							[PassParameters, this, &View, RayGenShader, LightingResolution](FRHIRayTracingCommandList& RHICmdList)
//						{
//							FRayTracingShaderBindingsWriter GlobalResources;
//							SetShaderParameters(GlobalResources, RayGenShader, *PassParameters);
//
//							FRHIRayTracingScene* RayTracingSceneRHI = View.GetRayTracingSceneChecked();
//							RHICmdList.RayTraceDispatch(View.RayTracingMaterialPipeline, RayGenShader.GetRayTracingShader(), RayTracingSceneRHI, GlobalResources, LightingResolution.X, LightingResolution.Y);
//
//						});
//					}
//
//					// Boiling filter pass to prevent runaway samples
//					if (CVarSampledLightingApplyBoilingFilter.GetValueOnRenderThread() != 0)
//					{
//						FApplyBoilingFilterCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FApplyBoilingFilterCS::FParameters>();
//
//						PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
//
//						PassParameters->RWLightReservoirUAV = GraphBuilder.CreateUAV(LightReservoirs);
//						PassParameters->ReservoirBufferDim = ReservoirBufferDim;
//						PassParameters->InputSlice = Reservoir;
//						PassParameters->OutputSlice = Reservoir;
//						PassParameters->BoilingFilterStrength = FMath::Clamp(CVarSampledLightingBoilingFilterStrength.GetValueOnRenderThread(), 0.00001f, 1.0f);
//
//						auto ComputeShader = View.ShaderMap->GetShader<FApplyBoilingFilterCS>();
//
//						ClearUnusedGraphResources(ComputeShader, PassParameters);
//						FIntPoint GridSize = FMath::DivideAndRoundUp<FIntPoint>(View.ViewRect.Size(), 16);
//
//						FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("BoilingFilter"), ComputeShader, PassParameters, FIntVector(GridSize.X, GridSize.Y, 1));
//					}
//				}
//			}
//
//			// Spatial resampling passes, one per reservoir
//			for (int32 Reservoir = NumReservoirs; Reservoir > 0; Reservoir--)
//			{
//				if (CVarSampledLightingSpatial.GetValueOnRenderThread() != 0)
//				{
//					FApplySpatialResamplingRGS::FParameters* PassParameters = GraphBuilder.AllocParameters<FApplySpatialResamplingRGS::FParameters>();
//
//					PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
//					PassParameters->SampledLightData = SampledLightDataUniformBuffer;
//					PassParameters->SceneTextures = SceneTextures; //SceneTextures;
//					PassParameters->SSProfilesTexture = View.RayTracingSubSurfaceProfileTexture;
//					PassParameters->SceneLightingChannels = GetSceneLightingChannelParameters(GraphBuilder, LightingChannelsTexture);
//
//					PassParameters->InputSlice = Reservoir - 1;
//					PassParameters->OutputSlice = Reservoir;
//					PassParameters->HistoryReservoir = Reservoir - 1;
//					PassParameters->SpatialSamples = FMath::Max(CVarSampledLightingSpatialSamples.GetValueOnRenderThread(), 1);
//					PassParameters->SpatialSamplesBoost = FMath::Max(CVarSampledLightingSpatialSamplesBoost.GetValueOnRenderThread(), 1);
//					PassParameters->SpatialSamplingRadius = FMath::Max(1.0f, CVarSampledLightingSpatialSamplingRadius.GetValueOnRenderThread());
//					PassParameters->SpatialDepthRejectionThreshold = FMath::Clamp(CVarSampledLightingSpatialDepthRejectionThreshold.GetValueOnRenderThread(), 0.0f, 1.0f);
//					PassParameters->SpatialNormalRejectionThreshold = FMath::Clamp(CVarSampledLightingSpatialNormalRejectionThreshold.GetValueOnRenderThread(), -1.0f, 1.0f);
//					PassParameters->ApplyApproximateVisibilityTest = CVarSampledLightingSpatialApplyApproxVisibility.GetValueOnRenderThread();
//					PassParameters->DiscountNaiveSamples = CVarSampledLightingSpatialDiscountNaiveSamples.GetValueOnRenderThread();
//
//					PassParameters->NeighborOffsetMask = GDiscSampleBuffer.NumSamples - 1;
//					PassParameters->NeighborOffsets = GDiscSampleBuffer.DiscSampleBufferSRV;
//
//					PassParameters->SampledLightingCommonParameters = CommonParameters;
//
//					//auto RayGenShader = View.ShaderMap->GetShader<FApplySpatialResamplingRGS>();
//					auto RayGenShader = GetShaderPermutation<FApplySpatialResamplingRGS>(Options, View);
//
//					ClearUnusedGraphResources(RayGenShader, PassParameters);
//
//					GraphBuilder.AddPass(
//						RDG_EVENT_NAME("SpatialResample"),
//						PassParameters,
//						ERDGPassFlags::Compute,
//						[PassParameters, this, &View, RayGenShader, LightingResolution](FRHIRayTracingCommandList& RHICmdList)
//					{
//						FRayTracingShaderBindingsWriter GlobalResources;
//						SetShaderParameters(GlobalResources, RayGenShader, *PassParameters);
//
//						FRHIRayTracingScene* RayTracingSceneRHI = View.GetRayTracingSceneChecked();
//						RHICmdList.RayTraceDispatch(View.RayTracingMaterialPipeline, RayGenShader.GetRayTracingShader(), RayTracingSceneRHI, GlobalResources, LightingResolution.X, LightingResolution.Y);
//
//					});
//					InitialSlice = Reservoir;
//				}
//			}
//
//			// Shading evaluation pass
//			{
//
//				FEvaluateSampledLightingRGS::FParameters* PassParameters = GraphBuilder.AllocParameters<FEvaluateSampledLightingRGS::FParameters>();
//
//				PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
//				PassParameters->SampledLightData = SampledLightDataUniformBuffer;
//				PassParameters->SceneTextures = SceneTextures; //SceneTextures;
//				PassParameters->SSProfilesTexture = View.RayTracingSubSurfaceProfileTexture;
//				PassParameters->SceneLightingChannels = GetSceneLightingChannelParameters(GraphBuilder, LightingChannelsTexture);
//
//				PassParameters->RWDiffuseUAV = GraphBuilder.CreateUAV(Diffuse);
//				PassParameters->RWSpecularUAV = GraphBuilder.CreateUAV(Specular);
//				PassParameters->RWRayDistanceUAV = GraphBuilder.CreateUAV(RayHitDistance);
//				PassParameters->ReservoirHistoryBufferDim = ReservoirHistoryBufferDim;
//				PassParameters->RWLightReservoirHistoryUAV = GraphBuilder.CreateUAV(LightReservoirsHistory);
//				PassParameters->InputSlice = InitialSlice;
//				PassParameters->NumReservoirs = NumReservoirs;
//				PassParameters->DemodulateMaterials = CVarSampledLightingDemodulateMaterials.GetValueOnRenderThread();
//				PassParameters->DebugOutput = CVarRayTracingSampledLightingDebugMode.GetValueOnRenderThread();
//				PassParameters->FeedbackVisibility = CVarRayTracingSampledLightingFeedbackVisibility.GetValueOnRenderThread();
//
//				if (bUseHairLighting)
//				{
//					const bool bUseHairVoxel = CVarRayTracingSampledLightingEnableHairVoxel.GetValueOnRenderThread() > 0;
//					PassParameters->bUseHairVoxel = (bUseHairDeepShadow && bUseHairVoxel) ? 1 : 0;
//					
//					//EHartNV ToDo: change to strand data
//					// old PassParameters->HairCategorizationTexture = HairResources.CategorizationTexture;
//					// new? PassParameters->HairStrands = HairStrands::BindHairStrandsViewUniformParameters(View);
//					PassParameters->HairLightChannelMaskTexture = View.HairStrandsViewData.VisibilityData.LightChannelMaskTexture;
//					PassParameters->VirtualVoxel = HairStrands::BindHairStrandsVoxelUniformParameters(View);
//				}
//
//				PassParameters->SampledLightingCommonParameters = CommonParameters;
//				PassParameters->ScreenTraceParameters = ScreenTraceParameters;
//
//				FEvaluateSampledLightingRGS::FPermutationDomain PermutationVector;
//				PermutationVector.Set<FEvaluateSampledLightingRGS::FHairLightingDim>(bUseHairLighting);
//				PermutationVector.Set<FEvaluateSampledLightingRGS::FScreenTraceDim>(ScreenTraceMode);
//				auto RayGenShader = GetShaderPermutation<FEvaluateSampledLightingRGS>(PermutationVector, Options, View);
//
//				ClearUnusedGraphResources(RayGenShader, PassParameters);
//
//				GraphBuilder.AddPass(
//					RDG_EVENT_NAME("ShadeSamples"),
//					PassParameters,
//					ERDGPassFlags::Compute,
//					[PassParameters, this, &View, RayGenShader, LightingResolution](FRHIRayTracingCommandList& RHICmdList)
//				{
//					FRayTracingShaderBindingsWriter GlobalResources;
//					SetShaderParameters(GlobalResources, RayGenShader, *PassParameters);
//
//					FRHIRayTracingScene* RayTracingSceneRHI = View.GetRayTracingSceneChecked();
//					RHICmdList.RayTraceDispatch(View.RayTracingMaterialPipeline, RayGenShader.GetRayTracingShader(), RayTracingSceneRHI, GlobalResources, LightingResolution.X, LightingResolution.Y);
//				});
//			}
//		}
//
//		// render strand-based hair as a separate pass, as it doesn't live in the gbuffer or keep its illumination in SceneColor
//		if (bEvaluateStrandBasedHair && bUseHairLighting)
//		{
//			const FHairStrandsVisibilityData& HairVisibilityData = View.HairStrandsViewData.VisibilityData; 
//			if (HairVisibilityData.SampleLightingTexture)
//			{
//				FDirectLightRGS::FParameters* PassParameters = GraphBuilder.AllocParameters<FDirectLightRGS::FParameters>();
//
//				PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
//				PassParameters->SampledLightData = SampledLightDataUniformBuffer;
//				PassParameters->SceneTextures = SceneTextures;
//				PassParameters->SceneLightingChannels = GetSceneLightingChannelParameters(GraphBuilder, LightingChannelsTexture);
//				PassParameters->SSProfilesTexture = View.RayTracingSubSurfaceProfileTexture;
//
//				PassParameters->SampledLightingCommonParameters = CommonParameters;
//				PassParameters->ScreenTraceParameters = ScreenTraceParameters;
//
//				PassParameters->BruteForceSamples = HairSamples;
//				PassParameters->BruteForceCandidates = HairCandidates;
//				PassParameters->DemodulateMaterials = 0; // hair is never demodulated
//
//				//
//				// Hair only parameters
//				//
//				PassParameters->HairDualScatteringRoughnessOverride = GetHairDualScatteringRoughnessOverride();
//
//				// deep shadow maps presently unused due to the per-light source nature
//				PassParameters->HairTransmittanceBufferMaxCount = 0;
//				PassParameters->HairTransmittanceBuffer = nullptr;
//
//				PassParameters->HairVisibilityNodeOffsetAndCount = HairVisibilityData.NodeIndex;
//				PassParameters->HairVisibilityNodeData = GraphBuilder.CreateSRV(HairVisibilityData.NodeData);
//				PassParameters->HairVisibilityNodeCoords = GraphBuilder.CreateSRV(HairVisibilityData.NodeCoord);
//
//				PassParameters->HairVisibilityNodeCount = HairVisibilityData.NodeCount;
//
//				// unused as all lighting for hair counts as specular and no denoiser pass is run against the hair illumination
//				PassParameters->RWDiffuseUAV = nullptr;
//				PassParameters->RWRayDistanceUAV = nullptr;
//
//				PassParameters->RWSpecularUAV = GraphBuilder.CreateUAV(HairVisibilityData.SampleLightingTexture);
//
//				FDirectLightRGS::FPermutationDomain PermutationVector;
//				PermutationVector.Set<FDirectLightRGS::FHairShadingDim>(true);
//				PermutationVector.Set<FDirectLightRGS::FScreenTraceDim>(ScreenTraceMode);
//				auto RayGenShader = View.ShaderMap->GetShader<FDirectLightRGS>(PermutationVector);
//
//				ClearUnusedGraphResources(RayGenShader, PassParameters);
//
//				FIntPoint HairLightingResolution = HairVisibilityData.SampleLightingViewportResolution;
//
//				GraphBuilder.AddPass(
//					RDG_EVENT_NAME("HairSampledLighting"),
//					PassParameters,
//					ERDGPassFlags::Compute,
//					[PassParameters, this, &View, RayGenShader, HairLightingResolution](FRHIRayTracingCommandList& RHICmdList)
//				{
//					FRayTracingShaderBindingsWriter GlobalResources;
//					SetShaderParameters(GlobalResources, RayGenShader, *PassParameters);
//
//					FRHIRayTracingScene* RayTracingSceneRHI = View.GetRayTracingSceneChecked();
//					RHICmdList.RayTraceDispatch(View.RayTracingMaterialPipeline, RayGenShader.GetRayTracingShader(), RayTracingSceneRHI, GlobalResources, HairLightingResolution.X, HairLightingResolution.Y);
//
//
//				});
//			}
//		}
//		
//		// evaluate denoiser
//		{
//			const int32 DenoiserMode = CVarSampledLightingDenoiser.GetValueOnRenderThread();
//			const IScreenSpaceDenoiser* DefaultDenoiser = IScreenSpaceDenoiser::GetDefaultDenoiser();
//			const IScreenSpaceDenoiser* DenoiserToUse = DenoiserMode == 1 ? DefaultDenoiser : GScreenSpaceDenoiser;
//
//
//			// this needs the NRD plugin since we are using DenoisePolychromaticPenumbraHarmonics differently than the default denoiser
//			// the default DenoisePolychromaticPenumbraHarmonics denoiser also is missing shaders
//			// we can't check for FNRDDenoiser in GScreenSpaceDenoiser->GetDebugName() directly since the DLSS plugin put itself into GScreenSpaceDenoiser and then passes through to the FNRDDenoiser
//			// so we check for the NRD module that's part of the NRD plugin
//
//			static IModuleInterface* NRDModule = FModuleManager::GetModulePtr<IModuleInterface>(TEXT("NRD"));
//			const bool bHasNRDPluginEnabled = NRDModule != nullptr;
//
//#if WITH_EDITOR
//			if (DenoiserMode == 2 && !bHasNRDPluginEnabled)
//			{
//				static bool bMessageBoxShown = false;
//				const bool IsUnattended = FApp::IsUnattended() || IsRunningCommandlet() || GIsRunningUnattendedScript;
//				if (!IsUnattended && !bMessageBoxShown)
//				{
//					const FText DialogTitle(NSLOCTEXT("RaytracingRTXDISampledLighting","RTXDINRDPluginRequiredTitle", "Error - RTXDI sampled lighting requires the NRD Denoiser plugin"));
//					const FTextFormat Format(NSLOCTEXT("RaytracingRTXDISampledLighting","RTXDINRDPluginRequiredMessage",
//						"r.RayTracing.SampledDirectLighting (RTXDI), requires the NVIDIA Realtime Denoiser (NRD) plugin.\n\n"
//						"Please enable the NRD plugin for your project and restart the engine"));
//					const FText WarningMessage = FText::Format(Format, FText::FromString((TEXT(""))));
//					FMessageDialog::Open(EAppMsgType::Ok, WarningMessage, &DialogTitle);
//					bMessageBoxShown = true;
//				}
//			}
//#endif //WITH_EDITOR
//
//			if(DenoiserMode == 2 && bHasNRDPluginEnabled && DenoiserToUse != DefaultDenoiser)
//			{
//				RDG_EVENT_SCOPE(GraphBuilder, "%s%s(Diffuse + Specular) %dx%d",
//					DenoiserToUse != DefaultDenoiser ? TEXT("ThirdParty ") : TEXT(""),
//					DenoiserToUse->GetDebugName(),
//					View.ViewRect.Width(), View.ViewRect.Height());
//
//				IScreenSpaceDenoiser::FPolychromaticPenumbraHarmonics DenoiserInputs;
//				DenoiserInputs.Diffuse.Harmonics[0] = Diffuse;
//				DenoiserInputs.Diffuse.Harmonics[1] = RayHitDistance;
//				DenoiserInputs.Specular.Harmonics[0] = Specular;
//				DenoiserInputs.Specular.Harmonics[1] = RayHitDistance;
//
//				IScreenSpaceDenoiser::FPolychromaticPenumbraOutputs DenoiserOutputs = DenoiserToUse->DenoisePolychromaticPenumbraHarmonics(
//					GraphBuilder,
//					View,
//					(FPreviousViewInfo*)&View.PrevViewInfo,
//					SceneTextures,
//					DenoiserInputs
//					);
//
//				Diffuse = DenoiserOutputs.Diffuse;
//				Specular = DenoiserOutputs.Specular;
//			}
//		}
//		
//		// composite
//		{
//
//			const FScreenPassRenderTarget Output(SceneColorTexture, View.ViewRect, ERenderTargetLoadAction::ELoad);
//
//			const FScreenPassTexture SceneColor(Diffuse, View.ViewRect);
//
//			TShaderMapRef<FScreenPassVS> VertexShader(View.ShaderMap);
//			TShaderMapRef<FCompositeSampledLightingPS> PixelShader(View.ShaderMap);
//			const bool bCompositeReplace = CVarSampledLightingCompositeMode.GetValueOnRenderThread() != 0;
//			FRHIBlendState* BlendState = bCompositeReplace ?
//				TStaticBlendState<CW_RGBA>::GetRHI()  :
//				TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One>::GetRHI();
//			const FScreenPassTextureViewport InputViewport(SceneColorTexture->Desc.Extent, View.ViewRect);
//			const FScreenPassTextureViewport OutputViewport(SceneColorTexture->Desc.Extent, View.ViewRect);
//
//			auto Parameters = GraphBuilder.AllocParameters<FCompositeSampledLightingPS::FParameters>();
//
//			Parameters->ApplyDiffuse = CVarSampledLightingCompositeDiffuse.GetValueOnRenderThread();
//			Parameters->ApplySpecular = CVarSampledLightingCompositeSpecular.GetValueOnRenderThread();
//			Parameters->ModulateMaterials = CVarSampledLightingDemodulateMaterials.GetValueOnRenderThread();
//
//			Parameters->Diffuse = Diffuse;
//			Parameters->Specular = Specular;
//			Parameters->InputSampler = TStaticSamplerState<>::GetRHI();
//
//			Parameters->SceneTextures = SceneTexturesUniformBuffer;
//			Parameters->ViewUniformBuffer = View.ViewUniformBuffer;
//			Parameters->RenderTargets[0] = FRenderTargetBinding(SceneColorTexture, ERenderTargetLoadAction::ELoad);
//
//			ClearUnusedGraphResources(PixelShader, Parameters);
//
//			AddDrawScreenPass(
//				GraphBuilder,
//				RDG_EVENT_NAME("CompositeSampledLighting"),
//				View,
//				OutputViewport,
//				InputViewport,
//				VertexShader,
//				PixelShader,
//				BlendState,
//				Parameters);
//		}
//	}
//
//	if (LightingMode == 1 && !ReferenceView.bStatePrevViewInfoIsReadOnly)
//	{
//		//Extract history feedback here
//		GraphBuilder.QueueBufferExtraction(LightReservoirsHistory, &ReferenceView.ViewState->PrevFrameViewInfo.SampledLightHistory.LightReservoirs);
//
//		//Extract scene textures as each effect potentially using them most do so to ensure it happens
//		GraphBuilder.QueueTextureExtraction(SceneTextures.GBufferATexture, &ReferenceView.ViewState->PrevFrameViewInfo.GBufferA);
//		GraphBuilder.QueueTextureExtraction(SceneTextures.SceneDepthTexture, &ReferenceView.ViewState->PrevFrameViewInfo.DepthBuffer);
//
//		ReferenceView.ViewState->PrevFrameViewInfo.SampledLightHistory.ReservoirDimensions = ReservoirHistoryBufferDim;
//	}
//
//	//ToDo - revist light buffer lifetimes. Maybe they should be made as explict allocations from the RDG
//}
//#else
//
//void FDeferredShadingSceneRenderer::RenderSampledDirectLighting(
//	FRDGBuilder& GraphBuilder,
//	TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
//	const FSceneTexturesConfig& SceneTexturesConfig,
//	const TArray<FSortedLightSceneInfo, SceneRenderingAllocator> &SampledLights,
//	const FSimpleLightArray& SimpleLights,
//	FRDGTextureRef SceneColorTexture,
//	FRDGTextureRef LightingChannelsTexture)
//{
//	// presently unsupported on platforms without ray tracing
//	check(0);
//}
//
//#endif