#include "BitonicSortUtils.h"
#include "RHI.h"

#include "RendererPrivate.h"
#include "GlobalShader.h"
#include "DeferredShadingRenderer.h"
#include "HAL/PlatformApplicationMisc.h"
#include <limits>

BEGIN_SHADER_PARAMETER_STRUCT(FBitonicSortParameters, )

SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, g_CounterBuffer)
SHADER_PARAMETER(uint32, CounterOffset)
SHADER_PARAMETER(uint32, NullItem)
RDG_BUFFER_ACCESS(IndirectDispatchArgs, ERHIAccess::IndirectArgs)
END_SHADER_PARAMETER_STRUCT()

class FBitonicSortBitDim : SHADER_PERMUTATION_BOOL("BITONICSORT_64BIT");

class FBitonicSortInDirectArgsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FBitonicSortInDirectArgsCS)
	SHADER_USE_PARAMETER_STRUCT(FBitonicSortInDirectArgsCS, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		//OutEnvironment.CompilerFlags.Add(CFLAG_WarningsAsErrors);
		OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
		OutEnvironment.SetDefine(TEXT("SORT_INDIRECT"), 1);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, g_CounterBuffer)
		SHADER_PARAMETER(uint32, CounterOffset)
		SHADER_PARAMETER(uint32, NullItem)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWByteAddressBuffer, g_IndirectArgsBuffer)
		SHADER_PARAMETER(uint32, MaxIterations)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_SHADER_TYPE(, FBitonicSortInDirectArgsCS, TEXT("/Engine/Private/BitonicSort.usf"), TEXT("BitonicSortInDirectArgs"), SF_Compute);

class FBitonicSortPreCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FBitonicSortPreCS)
	SHADER_USE_PARAMETER_STRUCT(FBitonicSortPreCS, FGlobalShader)
	using FPermutationDomain = TShaderPermutationDomain<FBitonicSortBitDim>;
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		//OutEnvironment.CompilerFlags.Add(CFLAG_WarningsAsErrors);
		OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
		OutEnvironment.SetDefine(TEXT("SORT_PRE"), 1);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FBitonicSortParameters, SortCommonParameters)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWByteAddressBuffer, g_SortBuffer)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_SHADER_TYPE(, FBitonicSortPreCS, TEXT("/Engine/Private/BitonicSort.usf"), TEXT("BitonicSortPre"), SF_Compute);


class FBitonicInnerSortCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FBitonicInnerSortCS)
	SHADER_USE_PARAMETER_STRUCT(FBitonicInnerSortCS, FGlobalShader)

		using FPermutationDomain = TShaderPermutationDomain<FBitonicSortBitDim>;
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		//OutEnvironment.CompilerFlags.Add(CFLAG_WarningsAsErrors);
		OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
		OutEnvironment.SetDefine(TEXT("INNER_SORT"), 1);
		//OutEnvironment.SetDefine(TEXT("BITONICSORT_64BIT"),0)
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FBitonicSortParameters, SortCommonParameters)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWByteAddressBuffer, g_SortBuffer)
		SHADER_PARAMETER(uint32, k)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_SHADER_TYPE(, FBitonicInnerSortCS, TEXT("/Engine/Private/BitonicSort.usf"), TEXT("BitonicInnerSort"), SF_Compute);


class FBitonicOutterSortCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FBitonicOutterSortCS)
	SHADER_USE_PARAMETER_STRUCT(FBitonicOutterSortCS, FGlobalShader)

		using FPermutationDomain = TShaderPermutationDomain<FBitonicSortBitDim>;
		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		//OutEnvironment.CompilerFlags.Add(CFLAG_WarningsAsErrors);
		OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
		OutEnvironment.SetDefine(TEXT("OUT_SORT"), 1);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FBitonicSortParameters, SortCommonParameters)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWByteAddressBuffer, g_SortBuffer)
		SHADER_PARAMETER(uint32, k)
		SHADER_PARAMETER(uint32, j)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_SHADER_TYPE(, FBitonicOutterSortCS, TEXT("/Engine/Private/BitonicSort.usf"), TEXT("BitonicOuterSort"), SF_Compute);


template <typename T> __forceinline T AlignPowerOfTwo(T value)
{
	return value == 0 ? 0 : 1 << FMath::CeilLogTwo(value);
}

void FBitonicSortUtils::Sort(FRDGBuilder& GraphBuilder,
	FRDGBufferRef& KeyIndexList,
	FRDGBufferRef& CounterBuffer,
	uint32_t CounterOffset,
	bool IsPartiallyPreSorted,
	bool SortAscending)
{
	const uint32_t ElementSizeBytes = KeyIndexList->Desc.BytesPerElement;
	const uint32_t MaxNumElements = KeyIndexList->Desc.NumElements;
	const uint32_t AlignedMaxNumElements = AlignPowerOfTwo(MaxNumElements);
	const uint32_t MaxIterations = FMath::CeilLogTwo(FMath::Max(2048u, AlignedMaxNumElements)) - 10;
	
	
	auto DispatchArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(22 * 23 / 2), TEXT("BitSort IndirectArgs"));

	FBitonicSortParameters CommonParameters;
	CommonParameters.g_CounterBuffer = GraphBuilder.CreateSRV(CounterBuffer, EPixelFormat::PF_R8_UINT);
	CommonParameters.CounterOffset = CounterOffset;
	CommonParameters.NullItem = SortAscending ? 0xffffffff : 0;
	// This controls two things.  It is a key that will sort to the end, and it is a mask used to
	// determine whether the current group should sort ascending or descending.
	TShaderMapRef<FBitonicSortInDirectArgsCS> ComputeShader(GetGlobalShaderMap(ERHIFeatureLevel::SM5));
	//auto ComputeShader = View.ShaderMap->GetShader<FBitonicSortInDirectArgsCS>(0);
	FBitonicSortInDirectArgsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FBitonicSortInDirectArgsCS::FParameters>();
	PassParameters->g_CounterBuffer = CommonParameters.g_CounterBuffer;
	PassParameters->CounterOffset = CommonParameters.CounterOffset;
	PassParameters->NullItem = CommonParameters.NullItem;
	//PassParameters->SortCommonParameters = CommonParameters;
	PassParameters->g_IndirectArgsBuffer = GraphBuilder.CreateUAV(DispatchArgs, EPixelFormat::PF_R8_UINT);
	PassParameters->MaxIterations = MaxIterations;
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("BitonicSortInDirectArgs"),
		ComputeShader,
		PassParameters,
		FIntVector(1,1,1));

	CommonParameters.IndirectDispatchArgs = DispatchArgs;
	
	if (!IsPartiallyPreSorted)
	{
		FBitonicSortPreCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FBitonicSortBitDim>(ElementSizeBytes == 8);
		//auto PreComputeShader = View.ShaderMap->GetShader<FBitonicSortPreCS>(0);
		//TShaderMapRef<FBitonicSortPreCS> PreComputeShader(GetGlobalShaderMap(ERHIFeatureLevel::SM5));
		auto PreComputeShader = GetGlobalShaderMap(ERHIFeatureLevel::SM5)->GetShader<FBitonicSortPreCS>(PermutationVector);

		FBitonicSortPreCS::FParameters* PrePassParameters = GraphBuilder.AllocParameters<FBitonicSortPreCS::FParameters>();
		PrePassParameters->SortCommonParameters = CommonParameters;
		PrePassParameters->g_SortBuffer = GraphBuilder.CreateUAV(KeyIndexList, EPixelFormat::PF_R8_UINT);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("BitonicSort Pre"),
			PreComputeShader,
			PrePassParameters,
			PrePassParameters->SortCommonParameters.IndirectDispatchArgs,
			0 );
	}

	uint32_t IndirectArgsOffset = 12;

	 //We have already pre-sorted up through k = 2048 when first writing our list, so
	 //we continue sorting with k = 4096.  For unnecessarily large values of k, these
	 //indirect dispatches will be skipped over with thread counts of 0.
	for (uint32_t k = 4096; k <= AlignedMaxNumElements; k *= 2)
	{
		for (uint32_t j = k / 2; j >= 2048; j /= 2)
		{
			FBitonicOutterSortCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FBitonicSortBitDim>(ElementSizeBytes == 8);
			auto OutterComputeShader = GetGlobalShaderMap(ERHIFeatureLevel::SM5)->GetShader<FBitonicOutterSortCS>(PermutationVector);
			//TShaderMapRef<FBitonicOutterSortCS> OutterComputeShader(GetGlobalShaderMap(ERHIFeatureLevel::SM5));
			FBitonicOutterSortCS::FParameters* OutPassParameters = GraphBuilder.AllocParameters<FBitonicOutterSortCS::FParameters>();

			OutPassParameters->SortCommonParameters = CommonParameters;
			OutPassParameters->g_SortBuffer = GraphBuilder.CreateUAV(KeyIndexList, EPixelFormat::PF_R8_UINT);
			OutPassParameters->k = k;
			OutPassParameters->j = j;
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("BitonicSortOutter"),
				OutterComputeShader,
				OutPassParameters,
				OutPassParameters->SortCommonParameters.IndirectDispatchArgs,
				IndirectArgsOffset);

			IndirectArgsOffset += 12;
		}

		FBitonicInnerSortCS::FPermutationDomain InnerPermutationVector;
		InnerPermutationVector.Set<FBitonicSortBitDim>(ElementSizeBytes == 8);
		//TShaderMapRef<FBitonicInnerSortCS> InnerComputeShader(GetGlobalShaderMap(ERHIFeatureLevel::SM5));
		auto InnerComputeShader = GetGlobalShaderMap(ERHIFeatureLevel::SM5)->GetShader<FBitonicInnerSortCS>(InnerPermutationVector);
		FBitonicInnerSortCS::FParameters* InnerPassParameters = GraphBuilder.AllocParameters<FBitonicInnerSortCS::FParameters>();
		InnerPassParameters->SortCommonParameters = CommonParameters;
		InnerPassParameters->k = k;
		InnerPassParameters->g_SortBuffer = GraphBuilder.CreateUAV(KeyIndexList, EPixelFormat::PF_R8_UINT);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("BitonicSortInner"),
			InnerComputeShader,
			InnerPassParameters,
			InnerPassParameters->SortCommonParameters.IndirectDispatchArgs,
			IndirectArgsOffset);
		IndirectArgsOffset += 12;
	}
}
