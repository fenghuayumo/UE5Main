// Copyright Epic Games, Inc. All Rights Reserved.

#include "NaniteShared.h"
#include "RHI.h"
#include "SceneUtils.h"
#include "ScenePrivate.h"
#include "Rendering/NaniteStreamingManager.h"
#include "SceneRelativeViewMatrices.h"

DEFINE_LOG_CATEGORY(LogNanite);
DEFINE_GPU_STAT(NaniteDebug);

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FNaniteUniformParameters, "Nanite");

extern float GNaniteMaxPixelsPerEdge;
extern float GNaniteMinPixelsPerEdgeHW;

// Optimized compute dual depth export pass on supported platforms.
int32 GNaniteExportDepth = 1;
static FAutoConsoleVariableRef CVarNaniteExportDepth(
	TEXT("r.Nanite.ExportDepth"),
	GNaniteExportDepth,
	TEXT("")
);

int32 GNaniteMaxNodes = 2 * 1048576;
FAutoConsoleVariableRef CVarNaniteMaxNodes(
	TEXT("r.Nanite.MaxNodes"),
	GNaniteMaxNodes,
	TEXT("Maximum number of Nanite nodes traversed during a culling pass."),
	ECVF_ReadOnly
);

int32 GNaniteMaxCandidateClusters = 16 * 1048576;
FAutoConsoleVariableRef CVarNaniteMaxCandidateClusters(
	TEXT("r.Nanite.MaxCandidateClusters"),
	GNaniteMaxCandidateClusters,
	TEXT("Maximum number of Nanite clusters before cluster culling."),
	ECVF_ReadOnly
);

int32 GNaniteMaxVisibleClusters = 4 * 1048576;
FAutoConsoleVariableRef CVarNaniteMaxVisibleClusters(
	TEXT("r.Nanite.MaxVisibleClusters"),
	GNaniteMaxVisibleClusters,
	TEXT("Maximum number of visible Nanite clusters."),
	ECVF_ReadOnly
);

#define MAX_CLUSTERS	(16 * 1024 * 1024)


namespace Nanite
{

void FPackedView::UpdateLODScales()
{
	const float ViewToPixels = 0.5f * ViewToClip.M[1][1] * ViewSizeAndInvSize.Y;

	const float LODScale = ViewToPixels / GNaniteMaxPixelsPerEdge;
	const float LODScaleHW = ViewToPixels / GNaniteMinPixelsPerEdgeHW;

	LODScales = FVector2f(LODScale, LODScaleHW);
}

FPackedView CreatePackedView( const FPackedViewParams& Params )
{
	// NOTE: There is some overlap with the logic - and this should stay consistent with - FSceneView::SetupViewRectUniformBufferParameters
	// Longer term it would be great to refactor a common place for both of this logic, but currently FSceneView has a lot of heavy-weight
	// stuff in it beyond the relevant parameters to SetupViewRectUniformBufferParameters (and Nanite has a few of its own parameters too).

	const FRelativeViewMatrices RelativeMatrices = FRelativeViewMatrices::Create(Params.ViewMatrices, Params.PrevViewMatrices);
	const FLargeWorldRenderPosition AbsoluteViewOrigin(Params.ViewMatrices.GetViewOrigin());
	const FVector ViewTileOffset = AbsoluteViewOrigin.GetTileOffset();

	FPackedView PackedView;

	PackedView.TranslatedWorldToView		= FMatrix44f(Params.ViewMatrices.GetOverriddenTranslatedViewMatrix());	// LWC_TODO: Precision loss? (and below)
	PackedView.TranslatedWorldToClip		= FMatrix44f(Params.ViewMatrices.GetTranslatedViewProjectionMatrix());
	PackedView.ViewToClip					= RelativeMatrices.ViewToClip;
	PackedView.ClipToRelativeWorld			= RelativeMatrices.ClipToRelativeWorld;
	PackedView.PreViewTranslation			= FVector4f(FVector3f(Params.ViewMatrices.GetPreViewTranslation() + ViewTileOffset)); // LWC_TODO: precision loss
	PackedView.WorldCameraOrigin			= FVector4f(FVector3f(Params.ViewMatrices.GetViewOrigin() - ViewTileOffset), 0.0f);
	PackedView.ViewForwardAndNearPlane		= FVector4f((FVector3f)Params.ViewMatrices.GetOverriddenTranslatedViewMatrix().GetColumn(2), Params.ViewMatrices.ComputeNearPlane());
	PackedView.ViewTilePosition				= AbsoluteViewOrigin.GetTile();
	PackedView.Padding0						= 0u;
	PackedView.MatrixTilePosition			= RelativeMatrices.TilePosition;
	PackedView.Padding1						= 0u;

	PackedView.PrevTranslatedWorldToView	= FMatrix44f(Params.PrevViewMatrices.GetOverriddenTranslatedViewMatrix()); // LWC_TODO: Precision loss? (and below)
	PackedView.PrevTranslatedWorldToClip	= FMatrix44f(Params.PrevViewMatrices.GetTranslatedViewProjectionMatrix());
	PackedView.PrevViewToClip				= FMatrix44f(Params.PrevViewMatrices.GetProjectionMatrix());
	PackedView.PrevClipToRelativeWorld		= RelativeMatrices.PrevClipToRelativeWorld;
	PackedView.PrevPreViewTranslation		= FVector4f(FVector3f(Params.PrevViewMatrices.GetPreViewTranslation() + ViewTileOffset)); // LWC_TODO: precision loss

	const FIntRect& ViewRect = Params.ViewRect;
	const FVector4f ViewSizeAndInvSize(ViewRect.Width(), ViewRect.Height(), 1.0f / float(ViewRect.Width()), 1.0f / float(ViewRect.Height()));

	PackedView.ViewRect = FIntVector4(ViewRect.Min.X, ViewRect.Min.Y, ViewRect.Max.X, ViewRect.Max.Y);
	PackedView.ViewSizeAndInvSize = ViewSizeAndInvSize;

	// Transform clip from full screen to viewport.
	FVector2D RcpRasterContextSize = FVector2D(1.0f / Params.RasterContextSize.X, 1.0f / Params.RasterContextSize.Y);
	PackedView.ClipSpaceScaleOffset = FVector4f(ViewSizeAndInvSize.X * RcpRasterContextSize.X,
		ViewSizeAndInvSize.Y * RcpRasterContextSize.Y,
		(ViewSizeAndInvSize.X + 2.0f * ViewRect.Min.X) * RcpRasterContextSize.X - 1.0f,
		-(ViewSizeAndInvSize.Y + 2.0f * ViewRect.Min.Y) * RcpRasterContextSize.Y + 1.0f);

	const float Mx = 2.0f * ViewSizeAndInvSize.Z;
	const float My = -2.0f * ViewSizeAndInvSize.W;
	const float Ax = -1.0f - 2.0f * ViewRect.Min.X * ViewSizeAndInvSize.Z;
	const float Ay = 1.0f + 2.0f * ViewRect.Min.Y * ViewSizeAndInvSize.W;

	PackedView.SVPositionToTranslatedWorld = FMatrix44f(			// LWC_TODO: Precision loss? (and below)
		FMatrix(FPlane(Mx, 0, 0, 0),
			FPlane(0, My, 0, 0),
			FPlane(0, 0, 1, 0),
			FPlane(Ax, Ay, 0, 1)) * Params.ViewMatrices.GetInvTranslatedViewProjectionMatrix());
	PackedView.ViewToTranslatedWorld = FMatrix44f(Params.ViewMatrices.GetOverriddenInvTranslatedViewMatrix());	

	check(Params.StreamingPriorityCategory <= NANITE_STREAMING_PRIORITY_CATEGORY_MASK);
	PackedView.StreamingPriorityCategory_AndFlags = (Params.Flags << NANITE_NUM_STREAMING_PRIORITY_CATEGORY_BITS) | Params.StreamingPriorityCategory;
	PackedView.MinBoundsRadiusSq = Params.MinBoundsRadius * Params.MinBoundsRadius;
	PackedView.UpdateLODScales();

	PackedView.LODScales.X *= Params.LODScaleFactor;

	PackedView.TargetLayerIdX_AndMipLevelY_AndNumMipLevelsZ.X = Params.TargetLayerIndex;
	PackedView.TargetLayerIdX_AndMipLevelY_AndNumMipLevelsZ.Y = Params.TargetMipLevel;
	PackedView.TargetLayerIdX_AndMipLevelY_AndNumMipLevelsZ.Z = Params.TargetMipCount;
	PackedView.TargetLayerIdX_AndMipLevelY_AndNumMipLevelsZ.W = Params.PrevTargetLayerIndex;

	PackedView.HZBTestViewRect = FIntVector4(Params.HZBTestViewRect.Min.X, Params.HZBTestViewRect.Min.Y, Params.HZBTestViewRect.Max.X, Params.HZBTestViewRect.Max.Y);

	return PackedView;

}

FPackedView CreatePackedViewFromViewInfo
(
	const FViewInfo& View,
	FIntPoint RasterContextSize,
	uint32 Flags,
	uint32 StreamingPriorityCategory,
	float MinBoundsRadius,
	float LODScaleFactor
)
{
	FPackedViewParams Params;
	Params.ViewMatrices = View.ViewMatrices;
	Params.PrevViewMatrices = View.PrevViewInfo.ViewMatrices;
	Params.ViewRect = View.ViewRect;
	Params.RasterContextSize = RasterContextSize;
	Params.Flags = Flags;
	Params.StreamingPriorityCategory = StreamingPriorityCategory;
	Params.MinBoundsRadius = MinBoundsRadius;
	Params.LODScaleFactor = LODScaleFactor;
	Params.HZBTestViewRect = View.PrevViewInfo.ViewRect;
	return CreatePackedView(Params);
}

void FGlobalResources::InitRHI()
{
	if (DoesPlatformSupportNanite(GMaxRHIShaderPlatform))
	{
		LLM_SCOPE_BYTAG(Nanite);
#if !UE_BUILD_SHIPPING
		FeedbackManager = new FFeedbackManager();
#endif
	}
}

void FGlobalResources::ReleaseRHI()
{
	if (DoesPlatformSupportNanite(GMaxRHIShaderPlatform))
	{
		LLM_SCOPE_BYTAG(Nanite);

		MainPassBuffers.StatsRasterizeArgsSWHWBuffer.SafeRelease();
		PostPassBuffers.StatsRasterizeArgsSWHWBuffer.SafeRelease();

		MainAndPostNodesAndClusterBatchesBuffer.SafeRelease();

		StatsBuffer.SafeRelease();

#if !UE_BUILD_SHIPPING
		delete FeedbackManager;
		FeedbackManager = nullptr;
#endif
	}
}

void FGlobalResources::Update(FRDGBuilder& GraphBuilder)
{
	check(DoesPlatformSupportNanite(GMaxRHIShaderPlatform));
}

uint32 FGlobalResources::GetMaxCandidateClusters()
{
	checkf(GNaniteMaxCandidateClusters <= MAX_CLUSTERS, TEXT("r.Nanite.MaxCandidateClusters must be <= MAX_CLUSTERS"));
	const uint32 MaxCandidateClusters = GNaniteMaxCandidateClusters & -NANITE_PERSISTENT_CLUSTER_CULLING_GROUP_SIZE;
	return MaxCandidateClusters;
}

uint32 FGlobalResources::GetMaxClusterBatches()
{
	const uint32 MaxCandidateClusters = GetMaxCandidateClusters();
	check(MaxCandidateClusters % NANITE_PERSISTENT_CLUSTER_CULLING_GROUP_SIZE == 0);
	return MaxCandidateClusters / NANITE_PERSISTENT_CLUSTER_CULLING_GROUP_SIZE;
}

uint32 FGlobalResources::GetMaxVisibleClusters()
{
	checkf(GNaniteMaxVisibleClusters <= MAX_CLUSTERS, TEXT("r.Nanite.MaxVisibleClusters must be <= MAX_CLUSTERS"));
	return GNaniteMaxVisibleClusters;
}

uint32 FGlobalResources::GetMaxNodes()
{
	return GNaniteMaxNodes & -NANITE_MAX_BVH_NODES_PER_GROUP;
}

TGlobalResource< FGlobalResources > GGlobalResources;

} // namespace Nanite

bool ShouldRenderNanite(const FScene* Scene, const FViewInfo& View, bool bCheckForAtomicSupport)
{
	// Does the platform support Nanite (with 64bit image atomics), and is it enabled?
	if (Scene && UseNanite(Scene->GetShaderPlatform(), bCheckForAtomicSupport))
	{
		// Any resources registered to the streaming manager?
		if (Nanite::GStreamingManager.HasResourceEntries())
		{
			// Is the view family showing Nanite meshes?
			return View.Family->EngineShowFlags.NaniteMeshes;
		}
	}

	// Nanite should not render for this view
	return false;
}

bool WouldRenderNanite(const FScene* Scene, const FViewInfo& View, bool bCheckForAtomicSupport, bool bCheckForProjectSetting)
{
	// Does the platform support Nanite (with 64bit image atomics), and is it enabled?
	if (Scene && UseNanite(Scene->GetShaderPlatform(), bCheckForAtomicSupport, bCheckForProjectSetting))
	{
		// Is the view family showing would-be Nanite meshes?
		return View.Family->EngineShowFlags.NaniteMeshes;
	}

	// Nanite would not render for this view
	return false;
}


bool UseComputeDepthExport()
{
	return (GRHISupportsDepthUAV && GRHISupportsExplicitHTile && GNaniteExportDepth != 0);
}
