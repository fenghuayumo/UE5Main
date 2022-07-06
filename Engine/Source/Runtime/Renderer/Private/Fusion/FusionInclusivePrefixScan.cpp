#include "Fusion.h"
#include "ScenePrivate.h"
#include "SceneRenderTargets.h"
#include "RenderGraphBuilder.h"
#include "RenderTargetPool.h"
#include "RHIResources.h"
#include "UniformBuffer.h"


class FPrefixScanCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FPrefixScanCS)
	SHADER_USE_PARAMETER_STRUCT(FPrefixScanCS, FGlobalShader)

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
		OutEnvironment.SetDefine(TEXT("THREAD_GROUP_SIZE"), GetThreadBlockSize());
		OutEnvironment.SetDefine(TEXT("PREFIX_SCAN"), 1);
	}

	static uint32 GetThreadBlockSize()
	{
		return 512;
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWByteAddressBuffer, InoutBuf)
		END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FPrefixScanCS, "/Engine/Private/PrefixScan/PrefixSum.usf", "PrefixScan", SF_Compute);

class FPrefixScanSegmentCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FPrefixScanSegmentCS)
	SHADER_USE_PARAMETER_STRUCT(FPrefixScanSegmentCS, FGlobalShader)

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
		OutEnvironment.SetDefine(TEXT("THREAD_GROUP_SIZE"), GetThreadBlockSize());
		OutEnvironment.SetDefine(TEXT("PREFIX_SCAN_SEGMENT"), 1);
	}

	static uint32 GetThreadBlockSize()
	{
		return 512;
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )

		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, InputBuf)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWByteAddressBuffer, OutputBuf)
		END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FPrefixScanSegmentCS, "/Engine/Private/PrefixScan/PrefixSum.usf", "PrefixScanSegment", SF_Compute);

class FPrefixScanMergeCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FPrefixScanMergeCS)
	SHADER_USE_PARAMETER_STRUCT(FPrefixScanMergeCS, FGlobalShader)

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
		OutEnvironment.SetDefine(TEXT("THREAD_GROUP_SIZE"), GetThreadBlockSize());
		OutEnvironment.SetDefine(TEXT("PREFIX_SCAN_MERGE"), 1);
	}

	static uint32 GetThreadBlockSize()
	{
		return 512;
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWByteAddressBuffer, InoutBuf)
		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, SegmentSumBuf)
		END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FPrefixScanMergeCS, "/Engine/Private/PrefixScan/PrefixSum.usf", "PrefixScanMerge", SF_Compute);



void InclusivePrefixScan(FRDGBuilder& GraphBuilder, FRDGBufferRef& InputBuf)
{
    const int32 SEGMENT_SIZE = 1024;

	{
		TShaderMapRef<FPrefixScanCS> ComputeShader(GetGlobalShaderMap(ERHIFeatureLevel::SM5));
		FPrefixScanCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FPrefixScanCS::FParameters>();
		PassParameters->InoutBuf = GraphBuilder.CreateUAV(InputBuf);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("PrefixScanCS"),
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(FIntVector(SEGMENT_SIZE * SEGMENT_SIZE / 2, 1, 1), FIntVector(FPrefixScanCS::GetThreadBlockSize())));
	}

	auto SegmentBuf = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateByteAddressDesc(sizeof(uint32) * SEGMENT_SIZE), TEXT("SegmentBuf"));
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(SegmentBuf), 0);
	{
		TShaderMapRef<FPrefixScanSegmentCS> ComputeShader(GetGlobalShaderMap(ERHIFeatureLevel::SM5));
		FPrefixScanSegmentCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FPrefixScanSegmentCS::FParameters>();
		PassParameters->InputBuf = GraphBuilder.CreateSRV(InputBuf);
		PassParameters->OutputBuf = GraphBuilder.CreateUAV(SegmentBuf);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("PrefixScanSegmentCS"),
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(FIntVector(SEGMENT_SIZE / 2, 1, 1), FIntVector(FPrefixScanSegmentCS::GetThreadBlockSize())));
	}
	{
		TShaderMapRef<FPrefixScanMergeCS> ComputeShader(GetGlobalShaderMap(ERHIFeatureLevel::SM5));
		FPrefixScanMergeCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FPrefixScanMergeCS::FParameters>();
		PassParameters->InoutBuf = GraphBuilder.CreateUAV(InputBuf);
		PassParameters->SegmentSumBuf = GraphBuilder.CreateSRV(SegmentBuf);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("PrefixScanMergeCS"),
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(FIntVector(SEGMENT_SIZE * SEGMENT_SIZE / 2, 1, 1), FIntVector(FPrefixScanMergeCS::GetThreadBlockSize())));
	}
}