// Copyright Epic Games, Inc. All Rights Reserved.

#include "SlatePostProcessor.h"
#include "SlatePostProcessResource.h"
#include "SlateShaders.h"
#include "ScreenRendering.h"
#include "SceneUtils.h"
#include "RendererInterface.h"
#include "StaticBoundShaderState.h"
#include "PipelineStateCache.h"
#include "CommonRenderResources.h"

DECLARE_CYCLE_STAT(TEXT("Slate PostProcessing RT"), STAT_SlatePostProcessingRTTime, STATGROUP_Slate);
DECLARE_CYCLE_STAT(TEXT("Slate ColorDeficiency RT"), STAT_SlateColorDeficiencyRTTime, STATGROUP_Slate);

FSlatePostProcessor::FSlatePostProcessor()
{
	const int32 NumIntermediateTargets = 2;
	IntermediateTargets = new FSlatePostProcessResource(NumIntermediateTargets);
	BeginInitResource(IntermediateTargets);
}

FSlatePostProcessor::~FSlatePostProcessor()
{
	// Note this is deleted automatically because it implements FDeferredCleanupInterface.
	IntermediateTargets->CleanUp();
}

void FSlatePostProcessor::BlurRect(FRHICommandListImmediate& RHICmdList, IRendererModule& RendererModule, const FBlurRectParams& Params, const FPostProcessRectParams& RectParams)
{
	SCOPE_CYCLE_COUNTER(STAT_SlatePostProcessingRTTime);
	check(RHICmdList.IsOutsideRenderPass());

	TArray<FVector4f> WeightsAndOffsets;
	const int32 SampleCount = ComputeBlurWeights(Params.KernelSize, Params.Strength, WeightsAndOffsets);


	const bool bDownsample = Params.DownsampleAmount > 0;

	FIntPoint DestRectSize = RectParams.DestRect.GetSize().IntPoint();
	FIntPoint RequiredSize = bDownsample
										? FIntPoint(FMath::DivideAndRoundUp(DestRectSize.X, Params.DownsampleAmount), FMath::DivideAndRoundUp(DestRectSize.Y, Params.DownsampleAmount))
										: DestRectSize;

	// The max size can get ridiculous with large scale values.  Clamp to size of the backbuffer
	RequiredSize.X = FMath::Min(RequiredSize.X, RectParams.SourceTextureSize.X);
	RequiredSize.Y = FMath::Min(RequiredSize.Y, RectParams.SourceTextureSize.Y);
	
	SCOPED_DRAW_EVENTF(RHICmdList, SlatePostProcess, TEXT("Slate Post Process Blur Background Kernel: %dx%d Size: %dx%d"), SampleCount, SampleCount, RequiredSize.X, RequiredSize.Y);


	const FIntPoint DownsampleSize = RequiredSize;

	IntermediateTargets->Update(RequiredSize);

	if(bDownsample)
	{
		DownsampleRect(RHICmdList, RendererModule, RectParams, DownsampleSize);
	}

	FSamplerStateRHIRef BilinearClamp = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

#if 1
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	check(ShaderMap);

	TShaderMapRef<FScreenVS> VertexShader(ShaderMap);
	TShaderMapRef<FSlatePostProcessBlurPS> PixelShader(ShaderMap);

	const int32 SrcTextureWidth = RectParams.SourceTextureSize.X;
	const int32 SrcTextureHeight = RectParams.SourceTextureSize.Y;

	const int32 DestTextureWidth = IntermediateTargets->GetWidth();
	const int32 DestTextureHeight = IntermediateTargets->GetHeight();

	const FSlateRect& SourceRect = RectParams.SourceRect;
	const FSlateRect& DestRect = RectParams.DestRect;

	FVertexDeclarationRHIRef VertexDecl = GFilterVertexDeclaration.VertexDeclarationRHI;
	check(IsValidRef(VertexDecl));
	
	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

	RHICmdList.SetScissorRect(false, 0, 0, 0, 0);
	RHICmdList.SetViewport(0, 0, 0, DestTextureWidth, DestTextureHeight, 0.0f);
	
	const FVector2D InvBufferSize = FVector2D(1.0f / DestTextureWidth, 1.0f / DestTextureHeight);
	const FVector2D HalfTexelOffset = FVector2D(0.5f/ DestTextureWidth, 0.5f/ DestTextureHeight);

	for (int32 PassIndex = 0; PassIndex < 2; ++PassIndex)
	{
		// First pass render to the render target with the post process fx
		if (PassIndex == 0)
		{
			FTexture2DRHIRef SourceTexture = bDownsample ? IntermediateTargets->GetRenderTarget(0) : RectParams.SourceTexture;
			FTexture2DRHIRef DestTexture = IntermediateTargets->GetRenderTarget(1);

			RHICmdList.Transition(FRHITransitionInfo(SourceTexture, ERHIAccess::Unknown, ERHIAccess::SRVGraphics));
			RHICmdList.Transition(FRHITransitionInfo(DestTexture, ERHIAccess::Unknown, ERHIAccess::RTV));

			FRHIRenderPassInfo RPInfo(DestTexture, ERenderTargetActions::Load_Store);
			RHICmdList.BeginRenderPass(RPInfo, TEXT("SlateBlurRectPass0"));
			{
				RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = VertexDecl;
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
				GraphicsPSOInit.PrimitiveType = PT_TriangleList;
				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);

				PixelShader->SetWeightsAndOffsets(RHICmdList, WeightsAndOffsets, SampleCount);
				PixelShader->SetTexture(RHICmdList, SourceTexture, BilinearClamp);

				if (bDownsample)
				{
					PixelShader->SetUVBounds(RHICmdList, FVector4f(FVector2f::ZeroVector, FVector2f((float)DownsampleSize.X / DestTextureWidth, (float)DownsampleSize.Y / DestTextureHeight) - FVector2f(HalfTexelOffset)));
					PixelShader->SetBufferSizeAndDirection(RHICmdList, InvBufferSize, FVector2D(1, 0));

					RendererModule.DrawRectangle(
						RHICmdList,
						0, 0,
						DownsampleSize.X, DownsampleSize.Y,
						0, 0,
						DownsampleSize.X, DownsampleSize.Y,
						FIntPoint(DestTextureWidth, DestTextureHeight),
						FIntPoint(DestTextureWidth, DestTextureHeight),
						VertexShader,
						EDRF_Default);
				}
				else
				{
					const FVector2f InvSrcTextureSize(1.f / SrcTextureWidth, 1.f / SrcTextureHeight);

					const FVector2f UVStart = FVector2f(DestRect.Left, DestRect.Top) * InvSrcTextureSize;
					const FVector2f UVEnd = FVector2f(DestRect.Right, DestRect.Bottom) * InvSrcTextureSize;
					const FVector2f SizeUV = UVEnd - UVStart;

					PixelShader->SetUVBounds(RHICmdList, FVector4f(UVStart, UVEnd));
					PixelShader->SetBufferSizeAndDirection(RHICmdList, FVector2D(InvSrcTextureSize), FVector2D(1, 0));

					RendererModule.DrawRectangle(
						RHICmdList,
						0, 0,
						RequiredSize.X, RequiredSize.Y,
						UVStart.X, UVStart.Y,
						SizeUV.X, SizeUV.Y,
						FIntPoint(DestTextureWidth, DestTextureHeight),
						FIntPoint(1, 1),
						VertexShader,
						EDRF_Default);
				}
			}
			RHICmdList.EndRenderPass();
		}
		else
		{
			FTexture2DRHIRef SourceTexture = IntermediateTargets->GetRenderTarget(1);
			FTexture2DRHIRef DestTexture = IntermediateTargets->GetRenderTarget(0);

			RHICmdList.Transition(FRHITransitionInfo(SourceTexture, ERHIAccess::Unknown, ERHIAccess::SRVGraphics));
			RHICmdList.Transition(FRHITransitionInfo(DestTexture, ERHIAccess::Unknown, ERHIAccess::RTV));

			FRHIRenderPassInfo RPInfo(DestTexture, ERenderTargetActions::Load_Store);
			RHICmdList.BeginRenderPass(RPInfo, TEXT("SlateBlurRect"));
			{
				RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = VertexDecl;
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
				GraphicsPSOInit.PrimitiveType = PT_TriangleList;
				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);

				PixelShader->SetWeightsAndOffsets(RHICmdList, WeightsAndOffsets, SampleCount);
				PixelShader->SetUVBounds(RHICmdList, FVector4f(FVector2f::ZeroVector, FVector2f((float)DownsampleSize.X / DestTextureWidth, (float)DownsampleSize.Y / DestTextureHeight) - FVector2f(HalfTexelOffset)));
				PixelShader->SetTexture(RHICmdList, SourceTexture, BilinearClamp);
				PixelShader->SetBufferSizeAndDirection(RHICmdList, InvBufferSize, FVector2D(0, 1));

				RendererModule.DrawRectangle(
					RHICmdList,
					0, 0,
					DownsampleSize.X, DownsampleSize.Y,
					0, 0,
					DownsampleSize.X, DownsampleSize.Y,
					FIntPoint(DestTextureWidth, DestTextureHeight),
					FIntPoint(DestTextureWidth, DestTextureHeight),
					VertexShader,
					EDRF_Default);
			}
			RHICmdList.EndRenderPass();
		}	
	}

#endif

	UpsampleRect(RHICmdList, RendererModule, RectParams, DownsampleSize, BilinearClamp);
}

void FSlatePostProcessor::ColorDeficiency(FRHICommandListImmediate& RHICmdList, IRendererModule& RendererModule, const FPostProcessRectParams& RectParams)
{
	SCOPE_CYCLE_COUNTER(STAT_SlateColorDeficiencyRTTime);

	FIntPoint DestRectSize = RectParams.DestRect.GetSize().IntPoint();
	FIntPoint RequiredSize = DestRectSize;

	IntermediateTargets->Update(RequiredSize);

#if 1
	FSamplerStateRHIRef PointClamp = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	check(ShaderMap);

	TShaderMapRef<FScreenVS> VertexShader(ShaderMap);
	TShaderMapRef<FSlatePostProcessColorDeficiencyPS> PixelShader(ShaderMap);

	const int32 SrcTextureWidth = RectParams.SourceTextureSize.X;
	const int32 SrcTextureHeight = RectParams.SourceTextureSize.Y;

	const int32 DestTextureWidth = IntermediateTargets->GetWidth();
	const int32 DestTextureHeight = IntermediateTargets->GetHeight();

	const FSlateRect& SourceRect = RectParams.SourceRect;
	const FSlateRect& DestRect = RectParams.DestRect;

	FVertexDeclarationRHIRef VertexDecl = GFilterVertexDeclaration.VertexDeclarationRHI;
	check(IsValidRef(VertexDecl));

	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

	RHICmdList.SetScissorRect(false, 0, 0, 0, 0);
	RHICmdList.SetViewport(0, 0, 0, DestTextureWidth, DestTextureHeight, 0.0f);

	// 
	{
		FTexture2DRHIRef SourceTexture = RectParams.SourceTexture;
		FTexture2DRHIRef DestTexture = IntermediateTargets->GetRenderTarget(0);

		RHICmdList.Transition(FRHITransitionInfo(SourceTexture, ERHIAccess::Unknown, ERHIAccess::SRVGraphics));
		RHICmdList.Transition(FRHITransitionInfo(DestTexture, ERHIAccess::Unknown, ERHIAccess::RTV));

		FRHIRenderPassInfo RPInfo(DestTexture, ERenderTargetActions::Load_Store);
		RHICmdList.BeginRenderPass(RPInfo, TEXT("ColorDeficiency"));
		{
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = VertexDecl;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);

			PixelShader->SetColorRules(RHICmdList, GSlateColorDeficiencyCorrection, GSlateColorDeficiencyType, GSlateColorDeficiencySeverity);
			PixelShader->SetShowCorrectionWithDeficiency(RHICmdList, GSlateShowColorDeficiencyCorrectionWithDeficiency);
			PixelShader->SetTexture(RHICmdList, SourceTexture, PointClamp);

			RendererModule.DrawRectangle(
				RHICmdList,
				0, 0,
				RequiredSize.X, RequiredSize.Y,
				0, 0,
				1, 1,
				FIntPoint(DestTextureWidth, DestTextureHeight),
				FIntPoint(1, 1),
				VertexShader,
				EDRF_Default);
		}
		RHICmdList.EndRenderPass();
	}

	const FIntPoint DownsampleSize = RequiredSize;
	UpsampleRect(RHICmdList, RendererModule, RectParams, DownsampleSize, PointClamp);

#endif
}

void FSlatePostProcessor::ReleaseRenderTargets()
{
	check(IsInGameThread());
	// Only release the resource not delete it.  Deleting it could cause issues on any RHI thread
	BeginReleaseResource(IntermediateTargets);
}

void FSlatePostProcessor::DownsampleRect(FRHICommandListImmediate& RHICmdList, IRendererModule& RendererModule, const FPostProcessRectParams& Params, const FIntPoint& DownsampleSize)
{
	SCOPED_DRAW_EVENT(RHICmdList, SlatePostProcessDownsample);

	// Source is the viewport.  This is the width and height of the viewport backbuffer
	const int32 SrcTextureWidth = Params.SourceTextureSize.X;
	const int32 SrcTextureHeight = Params.SourceTextureSize.Y;

	// Dest is the destination quad for the downsample
	const int32 DestTextureWidth = IntermediateTargets->GetWidth();
	const int32 DestTextureHeight = IntermediateTargets->GetHeight();

	// Rect of the viewport
	const FSlateRect& SourceRect = Params.SourceRect;

	// Rect of the final destination post process effect (not downsample rect).  This is the area we sample from
	const FSlateRect& DestRect = Params.DestRect;

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	TShaderMapRef<FScreenVS> VertexShader(ShaderMap);

	FSamplerStateRHIRef BilinearClamp = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	FTexture2DRHIRef DestTexture = IntermediateTargets->GetRenderTarget(0);

	// Downsample and store in intermediate texture
	{
		TShaderMapRef<FSlatePostProcessDownsamplePS> PixelShader(ShaderMap);

		RHICmdList.Transition(FRHITransitionInfo(Params.SourceTexture, ERHIAccess::Unknown, ERHIAccess::SRVGraphics));
		RHICmdList.Transition(FRHITransitionInfo(DestTexture, ERHIAccess::Unknown, ERHIAccess::RTV));

		const FVector2f InvSrcTextureSize(1.f/SrcTextureWidth, 1.f/SrcTextureHeight);

		const FVector2f UVStart = FVector2f(DestRect.Left, DestRect.Top) * InvSrcTextureSize;
		const FVector2f UVEnd = FVector2f(DestRect.Right, DestRect.Bottom) * InvSrcTextureSize;
		const FVector2f SizeUV = UVEnd - UVStart;
		
		RHICmdList.SetViewport(0, 0, 0, DestTextureWidth, DestTextureHeight, 0.0f);
		RHICmdList.SetScissorRect(false, 0, 0, 0, 0);

		FRHIRenderPassInfo RPInfo(DestTexture, ERenderTargetActions::Load_Store);
		RHICmdList.BeginRenderPass(RPInfo, TEXT("DownsampleRect"));
		{
			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);

			PixelShader->SetShaderParams(RHICmdList, FShaderParams::MakePixelShaderParams(FVector4f(InvSrcTextureSize.X, InvSrcTextureSize.Y, 0, 0)));
			PixelShader->SetUVBounds(RHICmdList, FVector4f(UVStart, UVEnd));
			PixelShader->SetTexture(RHICmdList, Params.SourceTexture, BilinearClamp);

			RendererModule.DrawRectangle(
				RHICmdList,
				0, 0,
				DownsampleSize.X, DownsampleSize.Y,
				UVStart.X, UVStart.Y,
				SizeUV.X, SizeUV.Y,
				FIntPoint(DestTextureWidth, DestTextureHeight),
				FIntPoint(1, 1),
				VertexShader,
				EDRF_Default);
		}
		RHICmdList.EndRenderPass();
	}
	
	// Testing only
#if 0
	UpsampleRect(RHICmdList, RendererModule, Params, DownsampleSize);
#endif
}

void FSlatePostProcessor::UpsampleRect(FRHICommandListImmediate& RHICmdList, IRendererModule& RendererModule, const FPostProcessRectParams& Params, const FIntPoint& DownsampleSize, FSamplerStateRHIRef& Sampler)
{
	SCOPED_DRAW_EVENT(RHICmdList, SlatePostProcessUpsample);

	const FVector4 Zero(0, 0, 0, 0);

	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	GraphicsPSOInit.BlendState = Params.CornerRadius == Zero ? TStaticBlendState<>::GetRHI() : TStaticBlendState<CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_One, BF_InverseSourceAlpha>::GetRHI();
	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

	// Original source texture is now the destination texture
	FTexture2DRHIRef DestTexture = Params.SourceTexture;
	const int32 DestTextureWidth = Params.SourceTextureSize.X;
	const int32 DestTextureHeight = Params.SourceTextureSize.Y;

	const int32 DownsampledWidth = DownsampleSize.X;
	const int32 DownsampledHeight = DownsampleSize.Y;

	// Source texture is the texture that was originally downsampled
	FTexture2DRHIRef SrcTexture = IntermediateTargets->GetRenderTarget(0);
	const int32 SrcTextureWidth = IntermediateTargets->GetWidth();
	const int32 SrcTextureHeight = IntermediateTargets->GetHeight();

	const FSlateRect& SourceRect = Params.SourceRect;
	const FSlateRect& DestRect = Params.DestRect;

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	TShaderMapRef<FScreenVS> VertexShader(ShaderMap);

	RHICmdList.SetViewport(0, 0, 0, DestTextureWidth, DestTextureHeight, 0.0f);

	// Perform Writable transitions first

	RHICmdList.Transition(FRHITransitionInfo(SrcTexture, ERHIAccess::Unknown, ERHIAccess::SRVGraphics));
	RHICmdList.Transition(FRHITransitionInfo(DestTexture, ERHIAccess::Unknown, ERHIAccess::RTV));

	FRHIRenderPassInfo RPInfo(DestTexture, ERenderTargetActions::Load_Store);
	RHICmdList.BeginRenderPass(RPInfo, TEXT("UpsampleRect"));
	{
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

		if (Params.RestoreStateFunc)
		{
			// This can potentially end and restart a renderpass.
			// #todo refactor so that we only start one renderpass here. Right now RestoreStateFunc may call UpdateScissorRect which requires an open renderpass.
			Params.RestoreStateFunc(RHICmdList, GraphicsPSOInit);
		}

		TShaderMapRef<FSlatePostProcessUpsamplePS> PixelShader(ShaderMap);

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, Params.StencilRef);

		const FVector2f SizeUV(
			DownsampledWidth == SrcTextureWidth ? 1.0f : (DownsampledWidth / (float)SrcTextureWidth) - (1.0f / (float)SrcTextureWidth),
			DownsampledHeight == SrcTextureHeight ? 1.0f : (DownsampledHeight / (float)SrcTextureHeight) - (1.0f / (float)SrcTextureHeight)
			);

		const FVector2f Size(DestRect.Right - DestRect.Left, DestRect.Bottom - DestRect.Top);
		FShaderParams ShaderParams = FShaderParams::MakePixelShaderParams(FVector4f(Size, SizeUV), (FVector4f)Params.CornerRadius); // LWC_TODO: precision loss


		PixelShader->SetShaderParams(RHICmdList, ShaderParams);
		PixelShader->SetTexture(RHICmdList, SrcTexture, Sampler);


		RendererModule.DrawRectangle(RHICmdList,
			DestRect.Left, DestRect.Top,
			Size.X, Size.Y,
			0, 0,
			SizeUV.X, SizeUV.Y,
			Params.SourceTextureSize,
			FIntPoint(1, 1),
			VertexShader,
			EDRF_Default);
	}
	RHICmdList.EndRenderPass();

}
#define BILINEAR_FILTER_METHOD 1

#if !BILINEAR_FILTER_METHOD

static int32 ComputeWeights(int32 KernelSize, float Sigma, TArray<FVector4f>& OutWeightsAndOffsets)
{
	OutWeightsAndOffsets.AddUninitialized(KernelSize / 2 + 1);

	int32 SampleIndex = 0;
	for (int32 X = 0; X < KernelSize; X += 2)
	{
		float Dist = X;
		FVector4f WeightAndOffset;
		WeightAndOffset.X = (1.0f / FMath::Sqrt(2 * PI*Sigma*Sigma))*FMath::Exp(-(Dist*Dist) / (2 * Sigma*Sigma));
		WeightAndOffset.Y = Dist;

		Dist = X + 1;
		WeightAndOffset.Z = (1.0f / FMath::Sqrt(2 * PI*Sigma*Sigma))*FMath::Exp(-(Dist*Dist) / (2 * Sigma*Sigma));
		WeightAndOffset.W = Dist;

		OutWeightsAndOffsets[SampleIndex] = WeightAndOffset;

		++SampleIndex;
	}

	return KernelSize;
};

#else

static float GetWeight(float Dist, float Strength)
{
	// from https://en.wikipedia.org/wiki/Gaussian_blur
	float Strength2 = Strength*Strength;
	return (1.0f / FMath::Sqrt(2 * PI*Strength2))*FMath::Exp(-(Dist*Dist) / (2 * Strength2));
}

static FVector2D GetWeightAndOffset(float Dist, float Sigma)
{
	float Offset1 = Dist;
	float Weight1 = GetWeight(Offset1, Sigma);

	float Offset2 = Dist + 1;
	float Weight2 = GetWeight(Offset2, Sigma);

	float TotalWeight = Weight1 + Weight2;

	float Offset = 0;
	if (TotalWeight > 0)
	{
		Offset = (Weight1*Offset1 + Weight2*Offset2) / TotalWeight;
	}


	return FVector2D(TotalWeight, Offset);
}

static int32 ComputeWeights(int32 KernelSize, float Sigma, TArray<FVector4f>& OutWeightsAndOffsets)
{
	int32 NumSamples = FMath::DivideAndRoundUp(KernelSize, 2);

	// We need half of the sample count array because we're packing two samples into one float4

	OutWeightsAndOffsets.AddUninitialized(NumSamples%2 == 0 ? NumSamples / 2 : NumSamples/2+1);

	OutWeightsAndOffsets[0] = FVector4f(FVector2f(GetWeight(0,Sigma), 0), FVector2f(GetWeightAndOffset(1, Sigma)) );
	int32 SampleIndex = 1;
	for (int32 X = 3; X < KernelSize; X += 4)
	{
		OutWeightsAndOffsets[SampleIndex] = FVector4f(FVector2f(GetWeightAndOffset(X, Sigma)), FVector2f(GetWeightAndOffset(X + 2, Sigma)));

		++SampleIndex;
	}

	return NumSamples;
};

#endif

int32 FSlatePostProcessor::ComputeBlurWeights(int32 KernelSize, float StdDev, TArray<FVector4f>& OutWeightsAndOffsets)
{
	return ComputeWeights(KernelSize, StdDev, OutWeightsAndOffsets);
}
