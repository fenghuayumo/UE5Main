// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShaderCompilerCommon.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "HlslccDefinitions.h"
#include "HAL/FileManager.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, ShaderCompilerCommon);


int16 GetNumUniformBuffersUsed(const FShaderCompilerResourceTable& InSRT)
{
	auto CountLambda = [&](const TArray<uint32>& In)
					{
						int16 LastIndex = -1;
						for (int32 i = 0; i < In.Num(); ++i)
						{
							auto BufferIndex = FRHIResourceTableEntry::GetUniformBufferIndex(In[i]);
							if (BufferIndex != static_cast<uint16>(FRHIResourceTableEntry::GetEndOfStreamToken()) )
							{
								LastIndex = FMath::Max(LastIndex, (int16)BufferIndex);
							}
						}

						return LastIndex + 1;
					};
	int16 Num = CountLambda(InSRT.SamplerMap);
	Num = FMath::Max(Num, (int16)CountLambda(InSRT.ShaderResourceViewMap));
	Num = FMath::Max(Num, (int16)CountLambda(InSRT.TextureMap));
	Num = FMath::Max(Num, (int16)CountLambda(InSRT.UnorderedAccessViewMap));
	return Num;
}


void BuildResourceTableTokenStream(const TArray<uint32>& InResourceMap, int32 MaxBoundResourceTable, TArray<uint32>& OutTokenStream, bool bGenerateEmptyTokenStreamIfNoResources)
{
	if (bGenerateEmptyTokenStreamIfNoResources)
	{
		if (InResourceMap.Num() == 0)
		{
			return;
		}
	}

	// First we sort the resource map.
	TArray<uint32> SortedResourceMap = InResourceMap;
	SortedResourceMap.Sort();

	// The token stream begins with a table that contains offsets per bound uniform buffer.
	// This offset provides the start of the token stream.
	OutTokenStream.AddZeroed(MaxBoundResourceTable+1);
	auto LastBufferIndex = FRHIResourceTableEntry::GetEndOfStreamToken();
	for (int32 i = 0; i < SortedResourceMap.Num(); ++i)
	{
		auto BufferIndex = FRHIResourceTableEntry::GetUniformBufferIndex(SortedResourceMap[i]);
		if (BufferIndex != LastBufferIndex)
		{
			// Store the offset for resources from this buffer.
			OutTokenStream[BufferIndex] = OutTokenStream.Num();
			LastBufferIndex = BufferIndex;
		}
		OutTokenStream.Add(SortedResourceMap[i]);
	}

	// Add a token to mark the end of the stream. Not needed if there are no bound resources.
	if (OutTokenStream.Num())
	{
		OutTokenStream.Add(FRHIResourceTableEntry::GetEndOfStreamToken());
	}
}


bool BuildResourceTableMapping(
	const TMap<FString, FResourceTableEntry>& ResourceTableMap,
	const TMap<FString, FUniformBufferEntry>& UniformBufferMap,
	TBitArray<>& UsedUniformBufferSlots,
	FShaderParameterMap& ParameterMap,
	FShaderCompilerResourceTable& OutSRT)
{
	check(OutSRT.ResourceTableBits == 0);
	check(OutSRT.ResourceTableLayoutHashes.Num() == 0);

	// Build resource table mapping
	int32 MaxBoundResourceTable = -1;
	TArray<uint32> ResourceTableSRVs;
	TArray<uint32> ResourceTableSamplerStates;
	TArray<uint32> ResourceTableUAVs;

	// Go through ALL the members of ALL the UB resources
	for( auto MapIt = ResourceTableMap.CreateConstIterator(); MapIt; ++MapIt )
	{
		const FString& Name	= MapIt->Key;
		const FResourceTableEntry& Entry = MapIt->Value;

		uint16 BufferIndex, BaseIndex, Size;

		// If the shaders uses this member (eg View_PerlinNoise3DTexture)...
		if (ParameterMap.FindParameterAllocation( *Name, BufferIndex, BaseIndex, Size ) )
		{
			ParameterMap.RemoveParameterAllocation(*Name);

			uint16 UniformBufferIndex = INDEX_NONE;
			uint16 UBBaseIndex, UBSize;

			// Add the UB itself as a parameter if not there
			if (!ParameterMap.FindParameterAllocation(*Entry.UniformBufferName, UniformBufferIndex, UBBaseIndex, UBSize))
			{
				UniformBufferIndex = UsedUniformBufferSlots.FindAndSetFirstZeroBit();
				ParameterMap.AddParameterAllocation(*Entry.UniformBufferName,UniformBufferIndex,0,0,EShaderParameterType::UniformBuffer);
			}

			// Mark used UB index
			if (UniformBufferIndex >= sizeof(OutSRT.ResourceTableBits) * 8)
			{
				return false;
			}
			OutSRT.ResourceTableBits |= (1 << UniformBufferIndex);

			// How many resource tables max we'll use, and fill it with zeroes
			MaxBoundResourceTable = FMath::Max<int32>(MaxBoundResourceTable, (int32)UniformBufferIndex);

			auto ResourceMap = FRHIResourceTableEntry::Create(UniformBufferIndex, Entry.ResourceIndex, BaseIndex);
			switch( Entry.Type )
			{
			case UBMT_TEXTURE:
			case UBMT_RDG_TEXTURE:
				OutSRT.TextureMap.Add(ResourceMap);
				break;
			case UBMT_SAMPLER:
				OutSRT.SamplerMap.Add(ResourceMap);
				break;
			case UBMT_SRV:
			case UBMT_RDG_TEXTURE_SRV:
			case UBMT_RDG_BUFFER_SRV:
				OutSRT.ShaderResourceViewMap.Add(ResourceMap);
				break;
			case UBMT_UAV:
			case UBMT_RDG_TEXTURE_UAV:
			case UBMT_RDG_BUFFER_UAV:
				OutSRT.UnorderedAccessViewMap.Add(ResourceMap);
				break;
			default:
				return false;
			}
		}
	}

	// Emit hashes for all uniform buffers in the parameter map. We need to include the ones without resources as well
	// (i.e. just constants), since the global uniform buffer bindings rely on valid hashes.
	for (const auto& KeyValue : ParameterMap.GetParameterMap())
	{
		const FString& UniformBufferName = KeyValue.Key;
		const FParameterAllocation& UniformBufferParameter = KeyValue.Value;

		if (UniformBufferParameter.Type == EShaderParameterType::UniformBuffer)
		{
			if (OutSRT.ResourceTableLayoutHashes.Num() <= UniformBufferParameter.BufferIndex)
			{
				OutSRT.ResourceTableLayoutHashes.SetNumZeroed(UniformBufferParameter.BufferIndex + 1);
			}

			// Data-driven uniform buffers will not have registered this information.
			if (const FUniformBufferEntry* UniformBufferEntry = UniformBufferMap.Find(UniformBufferName))
			{
				OutSRT.ResourceTableLayoutHashes[UniformBufferParameter.BufferIndex] = UniformBufferEntry->LayoutHash;
			}
		}
	}

	OutSRT.MaxBoundResourceTable = MaxBoundResourceTable;
	return true;
}

void CullGlobalUniformBuffers(const TMap<FString, FUniformBufferEntry>& UniformBufferMap, FShaderParameterMap& ParameterMap)
{
	TArray<FString> ParameterNames;
	ParameterMap.GetAllParameterNames(ParameterNames);

	for (const FString& Name : ParameterNames)
	{
		if (const FUniformBufferEntry* UniformBufferEntry = UniformBufferMap.Find(*Name))
		{
			// A uniform buffer that is bound per-shader keeps its allocation in the map.
			if (EnumHasAnyFlags(UniformBufferEntry->BindingFlags, EUniformBufferBindingFlags::Shader))
			{
				continue;
			}

			ParameterMap.RemoveParameterAllocation(*Name);
		}
	}
}

const TCHAR* FindNextWhitespace(const TCHAR* StringPtr)
{
	while (*StringPtr && !FChar::IsWhitespace(*StringPtr))
	{
		StringPtr++;
	}

	if (*StringPtr && FChar::IsWhitespace(*StringPtr))
	{
		return StringPtr;
	}
	else
	{
		return nullptr;
	}
}

const TCHAR* FindNextNonWhitespace(const TCHAR* StringPtr)
{
	bool bFoundWhitespace = false;

	while (*StringPtr && (FChar::IsWhitespace(*StringPtr) || !bFoundWhitespace))
	{
		bFoundWhitespace = true;
		StringPtr++;
	}

	if (bFoundWhitespace && *StringPtr && !FChar::IsWhitespace(*StringPtr))
	{
		return StringPtr;
	}
	else
	{
		return nullptr;
	}
}

const TCHAR* FindMatchingBlock(const TCHAR* OpeningCharPtr, char OpenChar, char CloseChar)
{
	const TCHAR* SearchPtr = OpeningCharPtr;
	int32 Depth = 0;

	while (*SearchPtr)
	{
		if (*SearchPtr == OpenChar)
		{
			Depth++;
		}
		else if (*SearchPtr == CloseChar)
		{
			if (Depth == 0)
			{
				return SearchPtr;
			}

			Depth--;
		}
		SearchPtr++;
	}

	return nullptr;
}
const TCHAR* FindMatchingClosingBrace(const TCHAR* OpeningCharPtr)			{ return FindMatchingBlock(OpeningCharPtr, '{', '}'); };
const TCHAR* FindMatchingClosingParenthesis(const TCHAR* OpeningCharPtr)	{ return FindMatchingBlock(OpeningCharPtr, '(', ')'); };

// See MSDN HLSL 'Symbol Name Restrictions' doc
inline bool IsValidHLSLIdentifierCharacter(TCHAR Char)
{
	return (Char >= 'a' && Char <= 'z') ||
		(Char >= 'A' && Char <= 'Z') ||
		(Char >= '0' && Char <= '9') ||
		Char == '_';
}

void ParseHLSLTypeName(const TCHAR* SearchString, const TCHAR*& TypeNameStartPtr, const TCHAR*& TypeNameEndPtr)
{
	TypeNameStartPtr = FindNextNonWhitespace(SearchString);
	check(TypeNameStartPtr);

	TypeNameEndPtr = TypeNameStartPtr;
	int32 Depth = 0;

	const TCHAR* NextWhitespace = FindNextWhitespace(TypeNameStartPtr);
	const TCHAR* PotentialExtraTypeInfoPtr = NextWhitespace ? FindNextNonWhitespace(NextWhitespace) : nullptr;

	// Find terminating whitespace, but skip over trailing ' < float4 >'
	while (*TypeNameEndPtr)
	{
		if (*TypeNameEndPtr == '<')
		{
			Depth++;
		}
		else if (*TypeNameEndPtr == '>')
		{
			Depth--;
		}
		else if (Depth == 0 
			&& FChar::IsWhitespace(*TypeNameEndPtr)
			// If we found a '<', we must not accept any whitespace before it
			&& (!PotentialExtraTypeInfoPtr || *PotentialExtraTypeInfoPtr != '<' || TypeNameEndPtr > PotentialExtraTypeInfoPtr))
		{
			break;
		}

		TypeNameEndPtr++;
	}

	check(TypeNameEndPtr);
}

const TCHAR* ParseHLSLSymbolName(const TCHAR* SearchString, FString& SymboName)
{
	const TCHAR* SymbolNameStartPtr = FindNextNonWhitespace(SearchString);
	check(SymbolNameStartPtr);

	const TCHAR* SymbolNameEndPtr = SymbolNameStartPtr;
	while (*SymbolNameEndPtr && IsValidHLSLIdentifierCharacter(*SymbolNameEndPtr))
	{
		SymbolNameEndPtr++;
	}

	SymboName = FString(SymbolNameEndPtr - SymbolNameStartPtr, SymbolNameStartPtr);

	return SymbolNameEndPtr;
}

class FUniformBufferMemberInfo
{
public:
	// eg View.WorldToClip
	FString NameAsStructMember;
	// eg View_WorldToClip
	FString GlobalName;
};

const TCHAR* ParseStructRecursive(
	const TCHAR* StructStartPtr,
	FString& UniformBufferName,
	int32 StructDepth,
	const FString& StructNamePrefix, 
	const FString& GlobalNamePrefix, 
	TMap<FString, TArray<FUniformBufferMemberInfo>>& UniformBufferNameToMembers)
{
	const TCHAR* OpeningBracePtr = FCString::Strstr(StructStartPtr, TEXT("{"));
	check(OpeningBracePtr);

	const TCHAR* ClosingBracePtr = FindMatchingClosingBrace(OpeningBracePtr + 1);
	check(ClosingBracePtr);

	FString StructName;
	const TCHAR* StructNameEndPtr = ParseHLSLSymbolName(ClosingBracePtr + 1, StructName);
	check(StructName.Len() > 0);

	FString NestedStructNamePrefix = StructNamePrefix + StructName + TEXT(".");
	FString NestedGlobalNamePrefix = GlobalNamePrefix + StructName + TEXT("_");

	if (StructDepth == 0)
	{
		UniformBufferName = StructName;
	}

	const TCHAR* LastMemberSemicolon = ClosingBracePtr;

	// Search backward to find the last member semicolon so we know when to stop parsing members
	while (LastMemberSemicolon > OpeningBracePtr && *LastMemberSemicolon != ';')
	{
		LastMemberSemicolon--;
	}

	const TCHAR* MemberSearchPtr = OpeningBracePtr + 1;

	do
	{
		const TCHAR* MemberTypeStartPtr = nullptr;
		const TCHAR* MemberTypeEndPtr = nullptr;
		ParseHLSLTypeName(MemberSearchPtr, MemberTypeStartPtr, MemberTypeEndPtr);
		FString MemberTypeName(MemberTypeEndPtr - MemberTypeStartPtr, MemberTypeStartPtr);

		if (FCString::Strcmp(*MemberTypeName, TEXT("struct")) == 0)
		{
			MemberSearchPtr = ParseStructRecursive(MemberTypeStartPtr, UniformBufferName, StructDepth + 1, NestedStructNamePrefix, NestedGlobalNamePrefix, UniformBufferNameToMembers);
		}
		else
		{
			FString MemberName;
			const TCHAR* SymbolEndPtr = ParseHLSLSymbolName(MemberTypeEndPtr, MemberName);
			check(MemberName.Len() > 0);
			
			MemberSearchPtr = SymbolEndPtr;

			// Skip over trailing tokens '[1];'
			while (*MemberSearchPtr && *MemberSearchPtr != ';')
			{
				MemberSearchPtr++;
			}

			// Add this member to the map
			TArray<FUniformBufferMemberInfo>& UniformBufferMembers = UniformBufferNameToMembers.FindOrAdd(UniformBufferName);

			FUniformBufferMemberInfo NewMemberInfo;
			NewMemberInfo.NameAsStructMember = NestedStructNamePrefix + MemberName;
			NewMemberInfo.GlobalName = NestedGlobalNamePrefix + MemberName;
			UniformBufferMembers.Add(MoveTemp(NewMemberInfo));
		}
	} 
	while (MemberSearchPtr < LastMemberSemicolon);

	const TCHAR* StructEndPtr = StructNameEndPtr;

	// Skip over trailing tokens '[1];'
	while (*StructEndPtr && *StructEndPtr != ';')
	{
		StructEndPtr++;
	}

	return StructEndPtr;
}

bool MatchStructMemberName(const FString& SymbolName, const TCHAR* SearchPtr, const FString& PreprocessedShaderSource)
{
	// Only match whole symbol
	if (IsValidHLSLIdentifierCharacter(*(SearchPtr - 1)) || *(SearchPtr - 1) == '.')
	{
		return false;
	}

	for (int32 i = 0; i < SymbolName.Len(); i++)
	{
		if (*SearchPtr != SymbolName[i])
		{
			return false;
		}
		
		SearchPtr++;

		if (i < SymbolName.Len() - 1)
		{
			// Skip whitespace within the struct member reference before the end
			// eg 'View. ViewToClip'
			while (FChar::IsWhitespace(*SearchPtr))
			{
				SearchPtr++;
			}
		}
	}

	// Only match whole symbol
	if (IsValidHLSLIdentifierCharacter(*SearchPtr))
	{
		return false;
	}

	return true;
}

// Searches string SearchPtr for 'SearchString.' or 'SearchString .' and returns a pointer to the first character of the match.
TCHAR* FindNextUniformBufferReference(TCHAR* SearchPtr, const TCHAR* SearchString, uint32 SearchStringLength)
{
	TCHAR* FoundPtr = FCString::Strstr(SearchPtr, SearchString);
	
	while(FoundPtr)
	{
		if (FoundPtr == nullptr)
		{
			return nullptr;
		}
		else if (FoundPtr[SearchStringLength] == '.' || (FoundPtr[SearchStringLength] == ' ' && FoundPtr[SearchStringLength+1] == '.'))
		{
			return FoundPtr;
		}
		
		FoundPtr = FCString::Strstr(FoundPtr + SearchStringLength, SearchString);
	}
	
	return nullptr;
}

void HandleReflectedGlobalConstantBufferMember(
	const FString& MemberName,
	uint32 ConstantBufferIndex,
	int32 ReflectionOffset,
	int32 ReflectionSize,
	FShaderCompilerOutput& Output
)
{
	Output.ParameterMap.AddParameterAllocation(
		*MemberName,
		ConstantBufferIndex,
		ReflectionOffset,
		ReflectionSize,
		EShaderParameterType::LooseData);
}

void HandleReflectedRootConstantBufferMember(
	const FShaderCompilerInput& Input,
	const FShaderParameterParser& ShaderParameterParser,
	const FString& MemberName,
	int32 ReflectionOffset,
	int32 ReflectionSize,
	FShaderCompilerOutput& Output
)
{
	ShaderParameterParser.ValidateShaderParameterType(Input, MemberName, ReflectionOffset, ReflectionSize, Output);
}

void HandleReflectedRootConstantBuffer(
	int32 ConstantBufferSize,
	FShaderCompilerOutput& CompilerOutput
)
{
	CompilerOutput.ParameterMap.AddParameterAllocation(
		FShaderParametersMetadata::kRootUniformBufferBindingName,
		FShaderParametersMetadata::kRootCBufferBindingIndex,
		0,
		static_cast<uint16>(ConstantBufferSize),
		EShaderParameterType::LooseData);
}

void HandleReflectedUniformBuffer(
	const FString& UniformBufferName,
	int32 ReflectionSlot,
	int32 BaseIndex,
	int32 BufferSize,
	FShaderCompilerOutput& CompilerOutput
)
{
	CompilerOutput.ParameterMap.AddParameterAllocation(
		*UniformBufferName,
		ReflectionSlot,
		BaseIndex,
		BufferSize,
		EShaderParameterType::UniformBuffer
	);
}

void HandleReflectedShaderResource(
	const FString& ResourceName,
	int32 BindOffset,
	int32 ReflectionSlot,
	int32 BindCount,
	FShaderCompilerOutput& CompilerOutput
)
{
	CompilerOutput.ParameterMap.AddParameterAllocation(
		*ResourceName,
		BindOffset,
		ReflectionSlot,
		BindCount,
		EShaderParameterType::SRV
	);
}

void HandleReflectedShaderUAV(
	const FString& UAVName,
	int32 BindOffset,
	int32 ReflectionSlot,
	int32 BindCount,
	FShaderCompilerOutput& CompilerOutput
)
{
	CompilerOutput.ParameterMap.AddParameterAllocation(
		*UAVName,
		BindOffset,
		ReflectionSlot,
		BindCount,
		EShaderParameterType::UAV
	);
}

void HandleReflectedShaderSampler(
	const FString& SamplerName,
	int32 BindOffset,
	int32 ReflectionSlot,
	int32 BindCount,
	FShaderCompilerOutput& CompilerOutput
)
{
	CompilerOutput.ParameterMap.AddParameterAllocation(
		*SamplerName,
		BindOffset,
		ReflectionSlot,
		BindCount,
		EShaderParameterType::Sampler
	);
}

void AddNoteToDisplayShaderParameterStructureOnCppSide(
	const FShaderParametersMetadata* ParametersStructure,
	FShaderCompilerOutput& CompilerOutput)
{
	FShaderCompilerError Error;
	Error.StrippedErrorMessage = FString::Printf(
		TEXT("Note: Definition of structure %s"),
		ParametersStructure->GetStructTypeName());
	Error.ErrorVirtualFilePath = ANSI_TO_TCHAR(ParametersStructure->GetFileName());
	Error.ErrorLineString = FString::FromInt(ParametersStructure->GetFileLine());

	CompilerOutput.Errors.Add(Error);
}

void AddNoteToDisplayShaderParameterMemberOnCppSide(
	const FShaderCompilerInput& CompilerInput,
	const FShaderParameterParser::FParsedShaderParameter& ParsedParameter,
	FShaderCompilerOutput& CompilerOutput)
{
	const FShaderParametersMetadata* MemberContainingStruct = nullptr;
	const FShaderParametersMetadata::FMember* Member = nullptr;
	{
		int32 ArrayElementId = 0;
		FString NamePrefix;
		CompilerInput.RootParametersStructure->FindMemberFromOffset(ParsedParameter.ConstantBufferOffset, &MemberContainingStruct, &Member, &ArrayElementId, &NamePrefix);
	}

	FString CppCodeName = CompilerInput.RootParametersStructure->GetFullMemberCodeName(ParsedParameter.ConstantBufferOffset);

	FShaderCompilerError Error;
	Error.StrippedErrorMessage = FString::Printf(
		TEXT("Note: Definition of %s"),
		*CppCodeName);
	Error.ErrorVirtualFilePath = ANSI_TO_TCHAR(MemberContainingStruct->GetFileName());
	Error.ErrorLineString = FString::FromInt(Member->GetFileLine());

	CompilerOutput.Errors.Add(Error);
}

void AddUnboundShaderParameterError(
	const FShaderCompilerInput& CompilerInput,
	const FShaderParameterParser& ShaderParameterParser,
	const FString& ParameterBindingName,
	FShaderCompilerOutput& CompilerOutput)
{
	check(CompilerInput.RootParametersStructure);

	const FShaderParameterParser::FParsedShaderParameter& Member = ShaderParameterParser.FindParameterInfos(ParameterBindingName);
	check(!Member.IsBindable());

	FShaderCompilerError Error(FString::Printf(
		TEXT("Error: Shader parameter %s could not be bound to %s's shader parameter structure %s."),
		*ParameterBindingName,
		*CompilerInput.ShaderName,
		CompilerInput.RootParametersStructure->GetStructTypeName()));
	ShaderParameterParser.GetParameterFileAndLine(Member, Error.ErrorVirtualFilePath, Error.ErrorLineString);

	CompilerOutput.Errors.Add(Error);
	CompilerOutput.bSucceeded = false;

	AddNoteToDisplayShaderParameterStructureOnCppSide(CompilerInput.RootParametersStructure, CompilerOutput);
}

inline bool MemberWasPotentiallyMoved(const FShaderParametersMetadata::FMember& InMember)
{
	const EUniformBufferBaseType BaseType = InMember.GetBaseType();
	return BaseType == UBMT_INT32 || BaseType == UBMT_UINT32 || BaseType == UBMT_FLOAT32;
}

bool FShaderParameterParser::ParseAndMoveShaderParametersToRootConstantBuffer(
	const FShaderCompilerInput& CompilerInput,
	FShaderCompilerOutput& CompilerOutput,
	FString& PreprocessedShaderSource,
	const TCHAR* ConstantBufferType)
{
	// The shader doesn't have any parameter binding through shader structure, therefore don't do anything.
	if (!CompilerInput.RootParametersStructure)
	{
		return true;
	}

	const bool bMoveToRootConstantBuffer = ConstantBufferType != nullptr;
	OriginalParsedShader = PreprocessedShaderSource;

	// Reserves the number of parameters up front.
	ParsedParameters.Reserve(CompilerInput.RootParametersStructure->GetSize() / sizeof(int32));

	CompilerInput.RootParametersStructure->IterateShaderParameterMembers(
		[&](const FShaderParametersMetadata& ParametersMetadata,
			const FShaderParametersMetadata::FMember& Member,
			const TCHAR* ShaderBindingName,
			uint16 ByteOffset)
	{
		FParsedShaderParameter ParsedParameter;
		ParsedParameter.Member = &Member;
		ParsedParameter.ConstantBufferOffset = ByteOffset;
		check(ParsedParameter.IsBindable());

		ParsedParameters.Add(ShaderBindingName, ParsedParameter);
	});

	bool bSuccess = true;

	// Browse the code for global shader parameter, Save their type and erase them white spaces.
	{
		enum class EState
		{
			// When to look for something to scan.
			Scanning,

			// When going to next ; in the global scope and reset.
			GoToNextSemicolonAndReset,

			// Parsing what might be a type of the parameter.
			ParsingPotentialType,
			FinishedPotentialType,

			// Parsing what might be a name of the parameter.
			ParsingPotentialName,
			FinishedPotentialName,

			// Parsing what looks like array of the parameter.
			ParsingPotentialArraySize,
			FinishedArraySize,

			// Found a parameter, just finish to it's semi colon.
			FoundParameter,
		};

		const int32 ShaderSourceLen = PreprocessedShaderSource.Len();

		int32 CurrentPragamLineoffset = -1;
		int32 CurrentLineoffset = 0;

		int32 TypeQualifierStartPos = -1;
		int32 TypeStartPos = -1;
		int32 TypeEndPos = -1;
		int32 NameStartPos = -1;
		int32 NameEndPos = -1;
		int32 ArrayStartPos = -1;
		int32 ArrayEndPos = -1;
		int32 ScopeIndent = 0;

		EState State = EState::Scanning;
		bool bGoToNextLine = false;

		auto ResetState = [&]()
		{
			TypeQualifierStartPos = -1;
			TypeStartPos = -1;
			TypeEndPos = -1;
			NameStartPos = -1;
			NameEndPos = -1;
			ArrayStartPos = -1;
			ArrayEndPos = -1;
			State = EState::Scanning;
		};

		auto EmitError = [&](const FString& ErrorMessage)
		{
			FShaderCompilerError Error;
			Error.StrippedErrorMessage = ErrorMessage;
			ExtractFileAndLine(CurrentPragamLineoffset, CurrentLineoffset, Error.ErrorVirtualFilePath, Error.ErrorLineString);
			CompilerOutput.Errors.Add(Error);
			bSuccess = false;
		};

		auto EmitUnpextectedHLSLSyntaxError = [&]()
		{
			EmitError(TEXT("Unexpected syntax when parsing shader parameters from shader code."));
			State = EState::GoToNextSemicolonAndReset;
		};

		for (int32 Cursor = 0; Cursor < ShaderSourceLen; Cursor++)
		{
			const TCHAR Char = PreprocessedShaderSource[Cursor];

			auto FoundShaderParameter = [&]()
			{
				check(Char == ';');
				check(TypeStartPos != -1);
				check(TypeEndPos != -1);
				check(NameStartPos != -1);
				check(NameEndPos != -1);

				FString Type = PreprocessedShaderSource.Mid(TypeStartPos, TypeEndPos - TypeStartPos + 1);
				FString Name = PreprocessedShaderSource.Mid(NameStartPos, NameEndPos - NameStartPos + 1);

				FParsedShaderParameter ParsedParameter;
				bool bUpdateParsedParameters = false;
				bool bEraseOriginalParameter = false;
				if (ParsedParameters.Contains(Name))
				{
					if (ParsedParameters.FindChecked(Name).IsFound())
					{
						// If it has already been found, it means it is duplicated. Do nothing and let the shader compiler throw the error.
					}
					else
					{
						// Update the parsed parameters
						bUpdateParsedParameters = true;
						ParsedParameter = ParsedParameters.FindChecked(Name);

						// Erase the parameter to move it into the root constant buffer.
						if (bMoveToRootConstantBuffer && ParsedParameter.IsBindable())
						{
							EUniformBufferBaseType BaseType = ParsedParameter.Member->GetBaseType();
							bEraseOriginalParameter = BaseType == UBMT_INT32 || BaseType == UBMT_UINT32 || BaseType == UBMT_FLOAT32;
						}
					}
				}
				else
				{
					// Update the parsed parameters to still have file and line number.
					bUpdateParsedParameters = true;
				}

				// Update 
				if (bUpdateParsedParameters)
				{
					ParsedParameter.ParsedType = Type;
					ParsedParameter.ParsedPragmaLineoffset = CurrentPragamLineoffset;
					ParsedParameter.ParsedLineOffset = CurrentLineoffset;

					if (ArrayStartPos != -1 && ArrayEndPos != -1)
					{
						ParsedParameter.ParsedArraySize = PreprocessedShaderSource.Mid(ArrayStartPos + 1, ArrayEndPos - ArrayStartPos - 1);
					}

					ParsedParameters.Add(Name, ParsedParameter);
				}

				// Erases this shader parameter conserving the same line numbers.
				if (bEraseOriginalParameter)
				{
					for (int32 j = (TypeQualifierStartPos != -1 ? TypeQualifierStartPos : TypeStartPos); j <= Cursor; j++)
					{
						if (PreprocessedShaderSource[j] != '\r' && PreprocessedShaderSource[j] != '\n')
							PreprocessedShaderSource[j] = ' ';
					}
				}

				ResetState();
			};

			const bool bIsWhiteSpace = Char == ' ' || Char == '\t' || Char == '\r' || Char == '\n';
			const bool bIsLetter = (Char >= 'a' && Char <= 'z') || (Char >= 'A' && Char <= 'Z');
			const bool bIsNumber = Char >= '0' && Char <= '9';

			const TCHAR* UpComing = (*PreprocessedShaderSource) + Cursor;
			const int32 RemainingSize = ShaderSourceLen - Cursor;

			CurrentLineoffset += Char == '\n';

			// Go to the next line if this is a preprocessor macro.
			if (bGoToNextLine)
			{
				if (Char == '\n')
				{
					bGoToNextLine = false;
				}
				continue;
			}
			else if (Char == '#')
			{
				if (RemainingSize > 6 && FCString::Strncmp(UpComing, TEXT("#line "), 6) == 0)
				{
					CurrentPragamLineoffset = Cursor;
					CurrentLineoffset = -1; // that will be incremented to 0 when reaching the \n at the end of the #line
				}

				bGoToNextLine = true;
				continue;
			}

			// If within a scope, just carry on until outside the scope.
			if (ScopeIndent > 0 || Char == '{')
			{
				if (Char == '{')
				{
					ScopeIndent++;
				}
				else if (Char == '}')
				{
					ScopeIndent--;
					if (ScopeIndent == 0)
					{
						ResetState();
					}
				}
				continue;
			}

			if (State == EState::Scanning)
			{
				if (bIsLetter)
				{
					static const TCHAR* KeywordTable[] = {
						TEXT("enum"),
						TEXT("class"),
						TEXT("const"),
						TEXT("struct"),
						TEXT("static"),
					};
					static int32 KeywordTableSize[] = {4, 5, 5, 6, 6};

					int32 RecognisedKeywordId = -1;
					for (int32 KeywordId = 0; KeywordId < UE_ARRAY_COUNT(KeywordTable); KeywordId++)
					{
						const TCHAR* Keyword = KeywordTable[KeywordId];
						const int32 KeywordSize = KeywordTableSize[KeywordId];

						if (RemainingSize > KeywordSize)
						{
							TCHAR KeywordEndTestChar = UpComing[KeywordSize];

							if ((KeywordEndTestChar == ' ' || KeywordEndTestChar == '\r' || KeywordEndTestChar == '\n' || KeywordEndTestChar == '\t') &&
								FCString::Strncmp(UpComing, Keyword, KeywordSize) == 0)
							{
								RecognisedKeywordId = KeywordId;
								break;
							}
						}
					}

					if (RecognisedKeywordId == -1)
					{
						// Might have found beginning of the type of a parameter.
						State = EState::ParsingPotentialType;
						TypeStartPos = Cursor;
					}
					else if (RecognisedKeywordId == 2)
					{
						// Ignore the const keywords, but still parse given it might still be a shader parameter.
						if (TypeQualifierStartPos == -1)
						{
							// If the parameter is erased, we also have to erase *all* 'const'-qualifiers, e.g. "const int Foo" or "const const int Foo".
							TypeQualifierStartPos = Cursor;
						}
						Cursor += KeywordTableSize[RecognisedKeywordId];
					}
					else
					{
						// Purposefully ignore enum, class, struct, static
						State = EState::GoToNextSemicolonAndReset;
					}
				}
				else if (bIsWhiteSpace)
				{
					// Keep parsing void.
				}
				else if (Char == ';')
				{
					// Looks like redundant semicolon, just ignore and keep scanning.
				}
				else
				{
					// No idea what this is, just go to next semi colon.
					State = EState::GoToNextSemicolonAndReset;
				}
			}
			else if (State == EState::GoToNextSemicolonAndReset)
			{
				// If need to go to next global semicolon and reach it. Resume browsing.
				if (Char == ';')
				{
					ResetState();
				}
			}
			else if (State == EState::ParsingPotentialType)
			{
				// Found character legal for a type...
				if (bIsLetter ||
					bIsNumber ||
					Char == '<' || Char == '>' || Char == '_')
				{
					// Keep browsing what might be type of the parameter.
				}
				else if (bIsWhiteSpace)
				{
					// Might have found a type.
					State = EState::FinishedPotentialType;
					TypeEndPos = Cursor - 1;
				}
				else
				{
					// Found unexpected character in the type.
					State = EState::GoToNextSemicolonAndReset;
				}
			}
			else if (State == EState::FinishedPotentialType)
			{
				if (bIsLetter)
				{
					// Might have found beginning of the name of a parameter.
					State = EState::ParsingPotentialName;
					NameStartPos = Cursor;
				}
				else if (bIsWhiteSpace)
				{
					// Keep parsing void.
				}
				else
				{
					// No idea what this is, just go to next semi colon.
					State = EState::GoToNextSemicolonAndReset;
				}
			}
			else if (State == EState::ParsingPotentialName)
			{
				// Found character legal for a name...
				if (bIsLetter ||
					bIsNumber ||
					Char == '_')
				{
					// Keep browsing what might be name of the parameter.
				}
				else if (Char == ':' || Char == '=')
				{
					// Found a parameter with syntax:
					// uint MyParameter : <whatever>;
					// uint MyParameter = <DefaultValue>;
					NameEndPos = Cursor - 1;
					State = EState::FoundParameter;
				}
				else if (Char == ';')
				{
					// Found a parameter with syntax:
					// uint MyParameter;
					NameEndPos = Cursor - 1;
					FoundShaderParameter();
				}
				else if (Char == '[')
				{
					// Syntax:
					//  uint MyArray[
					NameEndPos = Cursor - 1;
					ArrayStartPos = Cursor;
					State = EState::ParsingPotentialArraySize;
				}
				else if (bIsWhiteSpace)
				{
					// Might have found a name.
					// uint MyParameter <Still need to know what is after>;
					NameEndPos = Cursor - 1;
					State = EState::FinishedPotentialName;
				}
				else
				{
					// Found unexpected character in the name.
					// syntax:
					// uint MyFunction(<Don't care what is after>
					State = EState::GoToNextSemicolonAndReset;
				}
			}
			else if (
				State == EState::FinishedPotentialName ||
				State == EState::FinishedArraySize)
			{
				if (Char == ';')
				{
					// Found a parameter with syntax:
					// uint MyParameter <a bit of OK stuf>;
					FoundShaderParameter();
				}
				else if (Char == ':')
				{
					// Found a parameter with syntax:
					// uint MyParameter <a bit of OK stuf> : <Ignore all this crap>;
					State = EState::FoundParameter;
				}
				else if (Char == '=')
				{
					// Found syntax that doesn't make any sens:
					// uint MyParameter <a bit of OK stuf> = <Ignore all this crap>;
					State = EState::FoundParameter;
					// TDOO: should error out that this is useless.
				}
				else if (Char == '[')
				{
					if (State == EState::FinishedPotentialName)
					{
						// Syntax:
						//  uint MyArray [
						ArrayStartPos = Cursor;
						State = EState::ParsingPotentialArraySize;
					}
					else
					{
						EmitError(TEXT("Shader parameters can only support one dimensional array"));
					}
				}
				else if (bIsWhiteSpace)
				{
					// Keep parsing void.
				}
				else
				{
					// Found unexpected stuff.
					State = EState::GoToNextSemicolonAndReset;
				}
			}
			else if (State == EState::ParsingPotentialArraySize)
			{
				if (Char == ']')
				{
					ArrayEndPos = Cursor;
					State = EState::FinishedArraySize;
				}
				else if (Char == ';')
				{
					EmitUnpextectedHLSLSyntaxError();
				}
				else
				{
					// Keep going through the array size that might be a complex expression.
				}
			}
			else if (State == EState::FoundParameter)
			{
				if (Char == ';')
				{
					FoundShaderParameter();
				}
				else
				{
					// Cary on skipping all crap we don't care about shader parameter until we find it's semi colon.
				}
			}
			else
			{
				unimplemented();
			}
		} // for (int32 Cursor = 0; Cursor < PreprocessedShaderSource.Len(); Cursor++)
	}

	// Generate the root cbuffer content.
	if (bMoveToRootConstantBuffer)
	{
		FString RootCBufferContent;

		CompilerInput.RootParametersStructure->IterateShaderParameterMembers(
			[&](const FShaderParametersMetadata& ParametersMetadata,
				const FShaderParametersMetadata::FMember& Member,
				const TCHAR* ShaderBindingName,
				uint16 ByteOffset)
		{
			if (MemberWasPotentiallyMoved(Member))
			{
				FParsedShaderParameter* ParsedParameter = ParsedParameters.Find(ShaderBindingName);
				if (ParsedParameter && ParsedParameter->IsFound())
				{
					uint32 ConstantRegister = ByteOffset / 16;
					const TCHAR* ConstantSwizzle = [ByteOffset]()
						{
							switch (ByteOffset % 16)
							{
							default: unimplemented();
							case 0:  return TEXT("");
							case 4:  return TEXT(".y");
							case 8:  return TEXT(".z");
							case 12: return TEXT(".w");
							}
						}();

					if (!ParsedParameter->ParsedArraySize.IsEmpty())
					{
						RootCBufferContent.Append(FString::Printf(
							TEXT("%s %s[%s] : packoffset(c%d%s);\r\n"),
							*ParsedParameter->ParsedType,
							ShaderBindingName,
							*ParsedParameter->ParsedArraySize,
							ConstantRegister,
							ConstantSwizzle));
					}
					else
					{
						RootCBufferContent.Append(FString::Printf(
							TEXT("%s %s : packoffset(c%d%s);\r\n"),
							*ParsedParameter->ParsedType,
							ShaderBindingName,
							ConstantRegister,
							ConstantSwizzle));
					}
				}
			}
		});

		FString CBufferCodeBlock = FString::Printf(
			TEXT("%s %s\r\n")
			TEXT("{\r\n")
			TEXT("%s")
			TEXT("}\r\n\r\n"),
			ConstantBufferType,
			FShaderParametersMetadata::kRootUniformBufferBindingName,
			*RootCBufferContent);

		FString NewShaderCode = (
			MakeInjectedShaderCodeBlock(TEXT("ParseAndMoveShaderParametersToRootConstantBuffer"), CBufferCodeBlock) +
			PreprocessedShaderSource);

		PreprocessedShaderSource = MoveTemp(NewShaderCode);

		bMovedLoosedParametersToRootConstantBuffer = true;
	} // if (bMoveToRootConstantBuffer)

	return bSuccess;
}

void FShaderParameterParser::ValidateShaderParameterType(
	const FShaderCompilerInput& CompilerInput,
	const FString& ShaderBindingName,
	int32 ReflectionOffset,
	int32 ReflectionSize,
	bool bPlatformSupportsPrecisionModifier,
	FShaderCompilerOutput& CompilerOutput) const
{
	const FShaderParameterParser::FParsedShaderParameter& ParsedParameter = FindParameterInfos(ShaderBindingName);

	check(ParsedParameter.IsFound());
	check(CompilerInput.RootParametersStructure);

	if (ReflectionSize > 0 && bMovedLoosedParametersToRootConstantBuffer)
	{
		// Verify the offset of the parameter coming from shader reflections honor the packoffset()
		check(ReflectionOffset == ParsedParameter.ConstantBufferOffset);
	}

	// Validate the shader type.
	{
		FString ExpectedShaderType;
		ParsedParameter.Member->GenerateShaderParameterType(ExpectedShaderType, bPlatformSupportsPrecisionModifier);

		const bool bShouldBeInt = ParsedParameter.Member->GetBaseType() == UBMT_INT32;
		const bool bShouldBeUint = ParsedParameter.Member->GetBaseType() == UBMT_UINT32;

		// Match parsed type with expected shader type
		bool bIsTypeCorrect = ParsedParameter.ParsedType == ExpectedShaderType;

		if (!bIsTypeCorrect)
		{
			// Accept half-precision floats when single-precision was requested
			if (ParsedParameter.ParsedType.StartsWith(TEXT("half")) && ParsedParameter.Member->GetBaseType() == UBMT_FLOAT32)
			{
				bIsTypeCorrect = (FCString::Strcmp(*ParsedParameter.ParsedType + 4, *ExpectedShaderType + 5) == 0);
			}
			// Accept single-precision floats when half-precision was expected
			else if (ParsedParameter.ParsedType.StartsWith(TEXT("float")) && ExpectedShaderType.StartsWith(TEXT("half")))
			{
				bIsTypeCorrect = (FCString::Strcmp(*ParsedParameter.ParsedType + 5, *ExpectedShaderType + 4) == 0);
			}
			// support for min16float
			else if (ParsedParameter.ParsedType.StartsWith(TEXT("min16float")) && ExpectedShaderType.StartsWith(TEXT("float")))
			{
				bIsTypeCorrect = (FCString::Strcmp(*ParsedParameter.ParsedType + 10, *ExpectedShaderType + 5) == 0);
			}
			else if (ParsedParameter.ParsedType.StartsWith(TEXT("min16float")) && ExpectedShaderType.StartsWith(TEXT("half")))
			{
				bIsTypeCorrect = (FCString::Strcmp(*ParsedParameter.ParsedType + 10, *ExpectedShaderType + 4) == 0);
			}
		}

		// Allow silent casting between signed and unsigned on shader bindings.
		if (!bIsTypeCorrect && (bShouldBeInt || bShouldBeUint))
		{
			FString NewExpectedShaderType;
			if (bShouldBeInt)
			{
				// tries up with an uint.
				NewExpectedShaderType = TEXT("u") + ExpectedShaderType;
			}
			else
			{
				// tries up with an int.
				NewExpectedShaderType = ExpectedShaderType;
				NewExpectedShaderType.RemoveAt(0);
			}

			bIsTypeCorrect = ParsedParameter.ParsedType == NewExpectedShaderType;
		}

		if (!bIsTypeCorrect)
		{
			FString CppCodeName = CompilerInput.RootParametersStructure->GetFullMemberCodeName(ParsedParameter.ConstantBufferOffset);

			FShaderCompilerError Error;
			Error.StrippedErrorMessage = FString::Printf(
				TEXT("Error: Type %s of shader parameter %s in shader mismatch the shader parameter structure: %s expects a %s"),
				*ParsedParameter.ParsedType,
				*ShaderBindingName,
				*CppCodeName,
				*ExpectedShaderType);
			GetParameterFileAndLine(ParsedParameter, Error.ErrorVirtualFilePath, Error.ErrorLineString);

			CompilerOutput.Errors.Add(Error);
			CompilerOutput.bSucceeded = false;

			AddNoteToDisplayShaderParameterMemberOnCppSide(CompilerInput, ParsedParameter, CompilerOutput);
		}
	}

	// Validate parameter size, in case this is an array.
	if (ReflectionSize > int32(ParsedParameter.Member->GetMemberSize()))
	{
		FString CppCodeName = CompilerInput.RootParametersStructure->GetFullMemberCodeName(ParsedParameter.ConstantBufferOffset);

		FShaderCompilerError Error;
		Error.StrippedErrorMessage = FString::Printf(
			TEXT("Error: The size required to bind shader parameter %s is %i bytes, smaller than %s that is %i bytes in the parameter structure."),
			*ShaderBindingName,
			ReflectionSize,
			*CppCodeName,
			ParsedParameter.Member->GetMemberSize());
		GetParameterFileAndLine(ParsedParameter, Error.ErrorVirtualFilePath, Error.ErrorLineString);

		CompilerOutput.Errors.Add(Error);
		CompilerOutput.bSucceeded = false;

		AddNoteToDisplayShaderParameterMemberOnCppSide(CompilerInput, ParsedParameter, CompilerOutput);
	}
}

void FShaderParameterParser::ValidateShaderParameterTypes(
	const FShaderCompilerInput& CompilerInput,
	bool bPlatformSupportsPrecisionModifier,
	FShaderCompilerOutput& CompilerOutput) const
{
	// The shader doesn't have any parameter binding through shader structure, therefore don't do anything.
	if (!CompilerInput.RootParametersStructure)
	{
		return;
	}

	if (!CompilerOutput.bSucceeded)
	{
		return;
	}

	const TMap<FString, FParameterAllocation>& ParametersFoundByCompiler = CompilerOutput.ParameterMap.GetParameterMap();

	CompilerInput.RootParametersStructure->IterateShaderParameterMembers(
		[&](const FShaderParametersMetadata& ParametersMetadata,
			const FShaderParametersMetadata::FMember& Member,
			const TCHAR* ShaderBindingName,
			uint16 ByteOffset)
	{
		if (
			Member.GetBaseType() != UBMT_INT32 &&
			Member.GetBaseType() != UBMT_UINT32 &&
			Member.GetBaseType() != UBMT_FLOAT32)
		{
			return;
		}

		const FParsedShaderParameter& ParsedParameter = ParsedParameters[ShaderBindingName];

		// Did not find shader parameter in code.
		if (!ParsedParameter.IsFound())
		{
			// Verify the shader compiler also did not find this parameter to make sure there is no bug in the parser.
			checkf(
				!ParametersFoundByCompiler.Contains(ShaderBindingName),
				TEXT("Looks like there is a bug in FShaderParameterParser ParameterName=%s DumpDebugInfoPath=%s"),
				ShaderBindingName,
				*CompilerInput.DumpDebugInfoPath);
			return;
		}

		int32 BoundOffset = 0;
		int32 BoundSize = 0;
		if (const FParameterAllocation* ParameterAllocation = ParametersFoundByCompiler.Find(ShaderBindingName))
		{
			BoundOffset = ParameterAllocation->BaseIndex;
			BoundSize = ParameterAllocation->Size;
		}

		ValidateShaderParameterType(CompilerInput, ShaderBindingName, BoundOffset, BoundSize, bPlatformSupportsPrecisionModifier, CompilerOutput);
	});
}

void FShaderParameterParser::ExtractFileAndLine(int32 PragamLineoffset, int32 LineOffset, FString& OutFile, FString& OutLine) const
{
	if (PragamLineoffset == -1)
	{
		return;
	}

	check(FCString::Strncmp((*OriginalParsedShader) + PragamLineoffset, TEXT("#line "), 6) == 0);

	const int32 ShaderSourceLen = OriginalParsedShader.Len();

	int32 StartFilePos = -1;
	int32 EndFilePos = -1;
	int32 StartLinePos = PragamLineoffset + 6;
	int32 EndLinePos = -1;

	for (int32 Cursor = StartLinePos; Cursor < ShaderSourceLen; Cursor++)
	{
		const TCHAR Char = OriginalParsedShader[Cursor];

		if (Char == '\n')
		{
			break;
		}

		if (EndLinePos == -1)
		{
			if (Char > '9' || Char < '0')
			{
				EndLinePos = Cursor - 1;
			}
		}
		else if (StartFilePos == -1)
		{
			if (Char == '"')
			{
				StartFilePos = Cursor + 1;
			}
		}
		else if (EndFilePos == -1)
		{
			if (Char == '"')
			{
				EndFilePos = Cursor - 1;
				break;
			}
		}
	}

	check(StartFilePos != -1);
	check(EndFilePos != -1);
	check(EndLinePos != -1);

	OutFile = OriginalParsedShader.Mid(StartFilePos, EndFilePos - StartFilePos + 1);
	FString LineBasis = OriginalParsedShader.Mid(StartLinePos, EndLinePos - StartLinePos + 1);

	int32 FinalLine = FCString::Atoi(*LineBasis) + LineOffset;
	OutLine = FString::FromInt(FinalLine);
}

// The cross compiler doesn't yet support struct initializers needed to construct static structs for uniform buffers
// Replace all uniform buffer struct member references (View.WorldToClip) with a flattened name that removes the struct dependency (View_WorldToClip)
void RemoveUniformBuffersFromSource(const FShaderCompilerEnvironment& Environment, FString& PreprocessedShaderSource)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RemoveUniformBuffersFromSource);

	TMap<FString, TArray<FUniformBufferMemberInfo>> UniformBufferNameToMembers;
	UniformBufferNameToMembers.Reserve(Environment.UniformBufferMap.Num());

	// Build a mapping from uniform buffer name to its members
	{
		const TCHAR* UniformBufferStructIdentifier = TEXT("static const struct");
		const int32 StructPrefixLen = FCString::Strlen(TEXT("static const "));
		const int32 StructIdentifierLen = FCString::Strlen(UniformBufferStructIdentifier);
		TCHAR* SearchPtr = FCString::Strstr(&PreprocessedShaderSource[0], UniformBufferStructIdentifier);

		while (SearchPtr)
		{
			FString UniformBufferName;
			const TCHAR* ConstStructEndPtr = ParseStructRecursive(SearchPtr + StructPrefixLen, UniformBufferName, 0, TEXT(""), TEXT(""), UniformBufferNameToMembers);
			TCHAR* StructEndPtr = &PreprocessedShaderSource[ConstStructEndPtr - &PreprocessedShaderSource[0]];

			// Comment out the uniform buffer struct and initializer
			*SearchPtr = '/';
			*(SearchPtr + 1) = '*';
			*(StructEndPtr - 1) = '*';
			*StructEndPtr = '/';

			SearchPtr = FCString::Strstr(StructEndPtr, UniformBufferStructIdentifier);
		}
	}

	// Replace all uniform buffer struct member references (View.WorldToClip) with a flattened name that removes the struct dependency (View_WorldToClip)
	for (TMap<FString, TArray<FUniformBufferMemberInfo>>::TConstIterator It(UniformBufferNameToMembers); It; ++It)
	{
		const FString& UniformBufferName = It.Key();
		FString UniformBufferAccessString = UniformBufferName + TEXT(".");
		// MCPP inserts spaces after defines
		FString UniformBufferAccessStringWithSpace = UniformBufferName + TEXT(" .");

		// Search for the uniform buffer name first, as an optimization (instead of searching the entire source for every member)
		TCHAR* SearchPtr = FindNextUniformBufferReference(&PreprocessedShaderSource[0], *UniformBufferName, UniformBufferName.Len());

		while (SearchPtr)
		{
			const TArray<FUniformBufferMemberInfo>& UniformBufferMembers = It.Value();

			// Find the matching member we are replacing
			for (int32 MemberIndex = 0; MemberIndex < UniformBufferMembers.Num(); MemberIndex++)
			{
				const FString& MemberNameAsStructMember = UniformBufferMembers[MemberIndex].NameAsStructMember;

				if (MatchStructMemberName(MemberNameAsStructMember, SearchPtr, PreprocessedShaderSource))
				{
					const FString& MemberNameGlobal = UniformBufferMembers[MemberIndex].GlobalName;
					int32 NumWhitespacesToAdd = 0;

					for (int32 i = 0; i < MemberNameAsStructMember.Len(); i++)
					{
						if (i < MemberNameAsStructMember.Len() - 1)
						{
							if (FChar::IsWhitespace(SearchPtr[i]))
							{
								NumWhitespacesToAdd++;
							}
						}

						SearchPtr[i] = MemberNameGlobal[i];
					}

					// MCPP inserts spaces after defines
					// #define ReflectionStruct OpaqueBasePass.Shared.Reflection
					// 'ReflectionStruct.SkyLightCubemapBrightness' becomes 'OpaqueBasePass.Shared.Reflection .SkyLightCubemapBrightness' after MCPP
					// In order to convert this struct member reference into a globally unique variable we move the spaces to the end
					// 'OpaqueBasePass.Shared.Reflection .SkyLightCubemapBrightness' -> 'OpaqueBasePass_Shared_Reflection_SkyLightCubemapBrightness '
					for (int32 i = 0; i < NumWhitespacesToAdd; i++)
					{
						// If we passed MatchStructMemberName, it should not be possible to overwrite the null terminator
						check(SearchPtr[MemberNameAsStructMember.Len() + i] != 0);
						SearchPtr[MemberNameAsStructMember.Len() + i] = ' ';
					}
							
					break;
				}
			}

			SearchPtr = FindNextUniformBufferReference(SearchPtr + UniformBufferAccessString.Len(), *UniformBufferName, UniformBufferName.Len());
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Process TEXT() macro to convert them into GPU ASCII characters

FString ParseText(const TCHAR* StartPtr, const TCHAR*& EndPtr)
{
	const TCHAR* OpeningBracePtr = FCString::Strstr(StartPtr, TEXT("("));
	check(OpeningBracePtr);

	const TCHAR* ClosingBracePtr = FindMatchingClosingParenthesis(OpeningBracePtr + 1);
	check(ClosingBracePtr);

	FString Out;
	if (OpeningBracePtr && ClosingBracePtr)
	{
		const TCHAR* CurrPtr = OpeningBracePtr;
		do
		{
			Out += *CurrPtr;
			CurrPtr++;
		} while (CurrPtr != ClosingBracePtr+1);
	}
	EndPtr = ClosingBracePtr;
	return Out;
}

void ConvertTextToAsciiCharacter(const FString& InText, FString& OutText, FString& OutEncodedText)
{
	const uint32 CharCount = InText.Len();
	OutEncodedText.Reserve(CharCount * 3); // ~2 digits per character + a comma
	OutText = InText;
	for (uint32 CharIt = 0; CharIt < CharCount; ++CharIt)
	{
		const char C = InText[CharIt];
		OutEncodedText.AppendInt(uint8(C));
		if (CharIt + 1 != CharCount)
		{
			OutEncodedText += ',';
		}
	}
}

// Simple token matching and expansion to replace TEXT macro into supported character string
void TransformStringIntoCharacterArray(FString& PreprocessedShaderSource)
{
	struct FTextEntry
	{
		uint32  Index;
		uint32  Hash;
		uint32  Offset;
		FString SourceText;
		FString ConvertedText;
		FString EncodedText;
	};
	TArray<FTextEntry> Entries;

	// 1. Find all TEXT strings
	// 2. Add a text entry
	// 3. Replace TEXT by its entry number
	uint32 GlobalCount = 0;
	{
		const FString InitHashBegin(TEXT("InitShaderPrintText("));
		const FString InitHashEnd(TEXT(")"));

		const TCHAR* TextIdentifier = TEXT("TEXT(");
		const TCHAR* SearchPtr = FCString::Strstr(&PreprocessedShaderSource[0], TextIdentifier);
		while (SearchPtr)
		{
			const TCHAR* EndPtr = nullptr;
			FString Text = ParseText(SearchPtr, EndPtr);
			if (EndPtr)
			{
				// Trim enclosing
				Text.RemoveFromEnd("\")");
				Text.RemoveFromStart("(\"");

				// Register entry and convert text
				const uint32 EntryIndex = Entries.Num();
				uint32 ValidCharCount = 0;
				FTextEntry& Entry = Entries.AddDefaulted_GetRef();
				Entry.Index			= EntryIndex;
				Entry.Offset		= GlobalCount;
				Entry.SourceText	= Text;
				ConvertTextToAsciiCharacter(Entry.SourceText, Entry.ConvertedText, Entry.EncodedText);
				Entry.Hash			= CityHash32((const char*)&Entry.SourceText.GetCharArray(), sizeof(FString::ElementType) * Entry.SourceText.Len());

				GlobalCount += Entry.ConvertedText.Len();

				// Replace string
				const TCHAR* StartPtr = &PreprocessedShaderSource[0];
				const uint32 StartIndex = SearchPtr - StartPtr;
				const uint32 CharCount = (EndPtr - SearchPtr) + 1;
				PreprocessedShaderSource.RemoveAt(StartIndex, CharCount);

				const FString HashText = InitHashBegin + FString::FromInt(EntryIndex) + InitHashEnd;
				PreprocessedShaderSource.InsertAt(StartIndex, HashText);

				// Update SearchPtr, as PreprocessedShaderSource has been modified, and its memory could have been reallocated, causing SearchPtr to be invalid.
				SearchPtr = &PreprocessedShaderSource[0] + StartIndex;
			}
			SearchPtr = FCString::Strstr(SearchPtr, TextIdentifier);
		}
	}

	// 4. Write a global struct containing all the entries
	// 5. Write the function for fetching character for a given entry index
	const uint32 EntryCount = Entries.Num();
	FString TextChars;
	if (EntryCount)
	{
		// 1. Encoded character for each text entry within a single global char array
		TextChars = FString::Printf(TEXT("static const uint TEXT_CHARS[%d] = {\n"), GlobalCount);
		for (FTextEntry& Entry : Entries)
		{
			TextChars += FString::Printf(TEXT("\t%s%s // %d: \"%s\"\n"), *Entry.EncodedText, Entry.Index < EntryCount - 1 ? TEXT(",") : TEXT(""), Entry.Index, * Entry.SourceText);
		}
		TextChars += TEXT("};\n\n");

		// 2. Offset within the global array
		TextChars += FString::Printf(TEXT("static const uint TEXT_OFFSETS[%d] = {\n"), EntryCount+1);
		for (FTextEntry& Entry : Entries)
		{
			TextChars += FString::Printf(TEXT("\t%d, // %d: \"%s\"\n"), Entry.Offset, Entry.Index, *Entry.SourceText);
		}
		TextChars += FString::Printf(TEXT("\t%d // end\n"), GlobalCount);
		TextChars += TEXT("};\n\n");

		// 3. Entry hashes
		TextChars += TEXT("// Hashes are computed using the CityHash32 function\n");
		TextChars += FString::Printf(TEXT("static const uint TEXT_HASHES[%d] = {\n"), EntryCount);
		for (FTextEntry& Entry : Entries)
		{
			TextChars += FString::Printf(TEXT("\t0x%x%s // %d: \"%s\"\n"), Entry.Hash, Entry.Index < EntryCount - 1 ? TEXT(",") : TEXT(""), Entry.Index, * Entry.SourceText);
		}
		TextChars += TEXT("};\n\n");

		TextChars += TEXT("uint ShaderPrintGetChar(uint InIndex)              { return TEXT_CHARS[InIndex]; }\n");
		TextChars += TEXT("uint ShaderPrintGetOffset(FShaderPrintText InText) { return TEXT_OFFSETS[InText.Index]; }\n");
		TextChars += TEXT("uint ShaderPrintGetHash(FShaderPrintText InText)   { return TEXT_HASHES[InText.Index]; }\n");
	}
	else
	{	
		TextChars += TEXT("uint ShaderPrintGetChar(uint Index)                { return 0; }\n");
		TextChars += TEXT("uint ShaderPrintGetOffset(FShaderPrintText InText) { return 0; }\n");
		TextChars += TEXT("uint ShaderPrintGetHash(FShaderPrintText InText)   { return 0; }\n");
	}
	
	// 6. Insert global struct data + print function
	{
		const TCHAR* InsertToken = TEXT("GENERATED_SHADER_PRINT");
		const TCHAR* SearchPtr = FCString::Strstr(&PreprocessedShaderSource[0], InsertToken);
		if (SearchPtr)
		{
			// Replace string
			const TCHAR* StartPtr = &PreprocessedShaderSource[0];
			const uint32 StartIndex = SearchPtr - StartPtr;
			const uint32 CharCount = FCString::Strlen(InsertToken);
			PreprocessedShaderSource.RemoveAt(StartIndex, CharCount);
			PreprocessedShaderSource.InsertAt(StartIndex, TextChars);
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FString CreateShaderCompilerWorkerDirectCommandLine(const FShaderCompilerInput& Input, uint32 CCFlags)
{
	FString Text(TEXT("-directcompile -format="));
	Text += Input.ShaderFormat.GetPlainNameString();
	Text += TEXT(" -entry=");
	Text += Input.EntryPointName;
	switch (Input.Target.Frequency)
	{
	case SF_Vertex:			Text += TEXT(" -vs"); break;
	case SF_Mesh:			Text += TEXT(" -ms"); break;
	case SF_Amplification:	Text += TEXT(" -as"); break;
	case SF_Geometry:		Text += TEXT(" -gs"); break;
	case SF_Pixel:			Text += TEXT(" -ps"); break;
	case SF_Compute:		Text += TEXT(" -cs"); break;
#if RHI_RAYTRACING
	case SF_RayGen:			Text += TEXT(" -rgs"); break;
	case SF_RayMiss:		Text += TEXT(" -rms"); break;
	case SF_RayHitGroup:	Text += TEXT(" -rhs"); break;
	case SF_RayCallable:	Text += TEXT(" -rcs"); break;
#endif // RHI_RAYTRACING
	default: break;
	}
	if (Input.bCompilingForShaderPipeline)
	{
		Text += TEXT(" -pipeline");
	}
	if (Input.bIncludeUsedOutputs)
	{
		Text += TEXT(" -usedoutputs=");
		for (int32 Index = 0; Index < Input.UsedOutputs.Num(); ++Index)
		{
			if (Index != 0)
			{
				Text += TEXT("+");
			}
			Text += Input.UsedOutputs[Index];
		}
	}

	Text += TEXT(" ");
	Text += Input.DumpDebugInfoPath / Input.GetSourceFilename();

	Text += TEXT(" -cflags=");
	Text += FString::Printf(TEXT("%llu"), Input.Environment.CompilerFlags.GetData());

	if (CCFlags)
	{
		Text += TEXT(" -hlslccflags=");
		Text += FString::Printf(TEXT("%llu"), CCFlags);
	}
	// When we're running in directcompile mode, we don't to spam the crash reporter
	Text += TEXT(" -nocrashreports");
	return Text;
}

static FString CreateShaderConductorCommandLine(const FShaderCompilerInput& Input, const FString& SourceFilename, EShaderConductorTarget SCTarget)
{
	const TCHAR* Stage = nullptr;
	switch (Input.Target.GetFrequency())
	{
	case SF_Vertex:			Stage = TEXT("vs"); break;
	case SF_Pixel:			Stage = TEXT("ps"); break;
	case SF_Geometry:		Stage = TEXT("gs"); break;
	case SF_Compute:		Stage = TEXT("cs"); break;
	default:				return FString();
	}

	const TCHAR* Target = nullptr;
	switch (SCTarget)
	{
	case EShaderConductorTarget::Dxil:		Target = TEXT("dxil"); break;
	case EShaderConductorTarget::Spirv:		Target = TEXT("spirv"); break;
	default:								return FString();
	}

	FString CmdLine = TEXT("-E ") + Input.EntryPointName;
	//CmdLine += TEXT("-O ") + *(CompilerInfo.Input.D);
	CmdLine += TEXT(" -S ") + FString(Stage);
	CmdLine += TEXT(" -T ");
	CmdLine += Target;
	CmdLine += TEXT(" -I ") + (Input.DumpDebugInfoPath / SourceFilename);

	return CmdLine;
}

SHADERCOMPILERCOMMON_API void WriteShaderConductorCommandLine(const FShaderCompilerInput& Input, const FString& SourceFilename, EShaderConductorTarget Target)
{
	FArchive* FileWriter = IFileManager::Get().CreateFileWriter(*(Input.DumpDebugInfoPath / TEXT("ShaderConductorCmdLine.txt")));
	if (FileWriter)
	{
		FString CmdLine = CreateShaderConductorCommandLine(Input, SourceFilename, Target);

		FileWriter->Serialize(TCHAR_TO_ANSI(*CmdLine), CmdLine.Len());
		FileWriter->Close();
		delete FileWriter;
	}
}

static int Mali_ExtractNumberInstructions(const FString &MaliOutput)
{
	int ReturnedNum = 0;

	// Parse the instruction count
	int32 InstructionStringLength = FPlatformString::Strlen(TEXT("Instructions Emitted:"));
	int32 InstructionsIndex = MaliOutput.Find(TEXT("Instructions Emitted:"));

	// new version of mali offline compiler uses a different string in its output
	if (InstructionsIndex == INDEX_NONE)
	{
		InstructionStringLength = FPlatformString::Strlen(TEXT("Total instruction cycles:"));
		InstructionsIndex = MaliOutput.Find(TEXT("Total instruction cycles:"));
	}

	if (InstructionsIndex != INDEX_NONE && InstructionsIndex + InstructionStringLength < MaliOutput.Len())
	{
		const int32 EndIndex = MaliOutput.Find(TEXT("\n"), ESearchCase::IgnoreCase, ESearchDir::FromStart, InstructionsIndex + InstructionStringLength);

		if (EndIndex != INDEX_NONE)
		{
			int32 StartIndex = InstructionsIndex + InstructionStringLength;

			bool bFoundNrStart = false;
			int32 NumberIndex = 0;

			while (StartIndex < EndIndex)
			{
				if (FChar::IsDigit(MaliOutput[StartIndex]) && !bFoundNrStart)
				{
					// found number's beginning
					bFoundNrStart = true;
					NumberIndex = StartIndex;
				}
				else if (FChar::IsWhitespace(MaliOutput[StartIndex]) && bFoundNrStart)
				{
					// found number's end
					bFoundNrStart = false;
					const FString NumberString = MaliOutput.Mid(NumberIndex, StartIndex - NumberIndex);
					const float fNrInstructions = FCString::Atof(*NumberString);
					ReturnedNum += ceil(fNrInstructions);
				}

				++StartIndex;
			}
		}
	}

	return ReturnedNum;
}

static FString Mali_ExtractErrors(const FString &MaliOutput)
{
	FString ReturnedErrors;

	const int32 GlobalErrorIndex = MaliOutput.Find(TEXT("Compilation failed."));

	// find each 'line' that begins with token "ERROR:" and copy it to the returned string
	if (GlobalErrorIndex != INDEX_NONE)
	{
		int32 CompilationErrorIndex = MaliOutput.Find(TEXT("ERROR:"));
		while (CompilationErrorIndex != INDEX_NONE)
		{
			int32 EndLineIndex = MaliOutput.Find(TEXT("\n"), ESearchCase::CaseSensitive, ESearchDir::FromStart, CompilationErrorIndex + 1);
			EndLineIndex = EndLineIndex == INDEX_NONE ? MaliOutput.Len() - 1 : EndLineIndex;

			ReturnedErrors += MaliOutput.Mid(CompilationErrorIndex, EndLineIndex - CompilationErrorIndex + 1);

			CompilationErrorIndex = MaliOutput.Find(TEXT("ERROR:"), ESearchCase::CaseSensitive, ESearchDir::FromStart, EndLineIndex);
		}
	}

	return ReturnedErrors;
}

void CompileOfflineMali(const FShaderCompilerInput& Input, FShaderCompilerOutput& ShaderOutput, const ANSICHAR* ShaderSource, const int32 SourceSize, bool bVulkanSpirV, const ANSICHAR* VulkanSpirVEntryPoint)
{
	const bool bCompilerExecutableExists = FPaths::FileExists(Input.ExtraSettings.OfflineCompilerPath);

	if (bCompilerExecutableExists)
	{
		const auto Frequency = (EShaderFrequency)Input.Target.Frequency;
		const FString WorkingDir(FPlatformProcess::ShaderDir());

		FString CompilerPath = Input.ExtraSettings.OfflineCompilerPath;

		FString CompilerCommand = "";

		// add process and thread ids to the file name to avoid collision between workers
		auto ProcID = FPlatformProcess::GetCurrentProcessId();
		auto ThreadID = FPlatformTLS::GetCurrentThreadId();
		FString GLSLSourceFile = WorkingDir / TEXT("GLSLSource#") + FString::FromInt(ProcID) + TEXT("#") + FString::FromInt(ThreadID);

		// setup compilation arguments
		TCHAR *FileExt = nullptr;
		switch (Frequency)
		{
			case SF_Vertex:
				GLSLSourceFile += bVulkanSpirV ? TEXT(".spv") : TEXT(".vert");
				CompilerCommand += TEXT(" -v");
			break;
			case SF_Pixel:
				GLSLSourceFile += bVulkanSpirV ? TEXT(".spv") : TEXT(".frag");
				CompilerCommand += TEXT(" -f");
			break;
			case SF_Geometry:
				GLSLSourceFile += bVulkanSpirV ? TEXT(".spv") : TEXT(".geom");
				CompilerCommand += TEXT(" -g");
			break;
			case SF_Compute:
				GLSLSourceFile += bVulkanSpirV ? TEXT(".spv") : TEXT(".comp");
				CompilerCommand += TEXT(" -C");
			break;

			default:
				GLSLSourceFile += TEXT(".shd");
			break;
		}

		if (bVulkanSpirV)
		{
			CompilerCommand += FString::Printf(TEXT(" -y %s -p"), ANSI_TO_TCHAR(VulkanSpirVEntryPoint));
		}
		else
		{
			CompilerCommand += TEXT(" -s");
		}

		FArchive* Ar = IFileManager::Get().CreateFileWriter(*GLSLSourceFile, FILEWRITE_EvenIfReadOnly);

		if (Ar == nullptr)
		{
			return;
		}

		// write out the shader source to a file and use it below as input for the compiler
		Ar->Serialize((void*)ShaderSource, SourceSize);
		delete Ar;

		FString StdOut;
		FString StdErr;
		int32 ReturnCode = 0;

		// Since v6.2.0, Mali compiler needs to be started in the executable folder or it won't find "external/glslangValidator" for Vulkan
		FString CompilerWorkingDirectory = FPaths::GetPath(CompilerPath);

		if (!CompilerWorkingDirectory.IsEmpty() && FPaths::DirectoryExists(CompilerWorkingDirectory))
		{
			// compiler command line contains flags and the GLSL source file name
			CompilerCommand += " " + FPaths::ConvertRelativePathToFull(GLSLSourceFile);

			// Run Mali shader compiler and wait for completion
			FPlatformProcess::ExecProcess(*CompilerPath, *CompilerCommand, &ReturnCode, &StdOut, &StdErr, *CompilerWorkingDirectory);
		}
		else
		{
			StdErr = "Couldn't find Mali offline compiler at " + CompilerPath;
		}

		// parse Mali's output and extract instruction count or eventual errors
		ShaderOutput.bSucceeded = (ReturnCode >= 0);
		if (ShaderOutput.bSucceeded)
		{
			// check for errors
			if (StdErr.Len())
			{
				ShaderOutput.bSucceeded = false;

				FShaderCompilerError* NewError = new(ShaderOutput.Errors) FShaderCompilerError();
				NewError->StrippedErrorMessage = TEXT("[Mali Offline Complier]\n") + StdErr;
			}
			else
			{
				FString Errors = Mali_ExtractErrors(StdOut);

				if (Errors.Len())
				{
					FShaderCompilerError* NewError = new(ShaderOutput.Errors) FShaderCompilerError();
					NewError->StrippedErrorMessage = TEXT("[Mali Offline Complier]\n") + Errors;
					ShaderOutput.bSucceeded = false;
				}
			}

			// extract instruction count
			if (ShaderOutput.bSucceeded)
			{
				ShaderOutput.NumInstructions = Mali_ExtractNumberInstructions(StdOut);
			}
		}

		// we're done so delete the shader file
		IFileManager::Get().Delete(*GLSLSourceFile, true, true);
	}
}


FString GetDumpDebugUSFContents(const FShaderCompilerInput& Input, const FString& Source, uint32 HlslCCFlags)
{
	FString Contents = Source;
	Contents += TEXT("\n");
	Contents += CrossCompiler::CreateResourceTableFromEnvironment(Input.Environment);
	Contents += TEXT("#if 0 /*DIRECT COMPILE*/\n");
	Contents += CreateShaderCompilerWorkerDirectCommandLine(Input, HlslCCFlags);
	Contents += TEXT("\n#endif /*DIRECT COMPILE*/\n");

	return Contents;
}

void DumpDebugUSF(const FShaderCompilerInput& Input, const ANSICHAR* Source, uint32 HlslCCFlags, const TCHAR* OverrideBaseFilename)
{
	FString NewSource = Source ? Source : "";
	FString Contents = GetDumpDebugUSFContents(Input, NewSource, HlslCCFlags);
	DumpDebugUSF(Input, NewSource, HlslCCFlags, OverrideBaseFilename);
}

void DumpDebugUSF(const FShaderCompilerInput& Input, const FString& Source, uint32 HlslCCFlags, const TCHAR* OverrideBaseFilename)
{
	FString BaseSourceFilename = (OverrideBaseFilename && *OverrideBaseFilename) ? OverrideBaseFilename : *Input.GetSourceFilename();
	FString Filename = Input.DumpDebugInfoPath / BaseSourceFilename;

	if (TUniquePtr<FArchive> FileWriter = TUniquePtr<FArchive>(IFileManager::Get().CreateFileWriter(*Filename)))
	{
		FString Contents = GetDumpDebugUSFContents(Input, Source, HlslCCFlags);
		FileWriter->Serialize(TCHAR_TO_ANSI(*Contents), Contents.Len());
		FileWriter->Close();
	}
}

void DumpDebugShaderText(const FShaderCompilerInput& Input, const FString& InSource, const FString& FileExtension)
{
	FTCHARToUTF8 StringConverter(InSource.GetCharArray().GetData(), InSource.Len());

	// Provide mutable container to pass string to FArchive inside inner function
	TArray<ANSICHAR> SourceAnsi;
	SourceAnsi.SetNum(InSource.Len() + 1);
	FCStringAnsi::Strncpy(SourceAnsi.GetData(), (ANSICHAR*)StringConverter.Get(), SourceAnsi.Num());

	// Forward temporary container to primary function
	DumpDebugShaderText(Input, SourceAnsi.GetData(), InSource.Len(), FileExtension);
}

void DumpDebugShaderText(const FShaderCompilerInput& Input, ANSICHAR* InSource, int32 InSourceLength, const FString& FileExtension)
{
	DumpDebugShaderBinary(Input, InSource, InSourceLength * sizeof(ANSICHAR), FileExtension);
}

void DumpDebugShaderText(const FShaderCompilerInput& Input, ANSICHAR* InSource, int32 InSourceLength, const FString& FileName, const FString& FileExtension)
{
	DumpDebugShaderBinary(Input, InSource, InSourceLength * sizeof(ANSICHAR), FileName, FileExtension);
}

void DumpDebugShaderBinary(const FShaderCompilerInput& Input, void* InData, int32 InDataByteSize, const FString& FileExtension)
{
	if (InData != nullptr && InDataByteSize > 0 && !FileExtension.IsEmpty())
	{
		const FString Filename = Input.DumpDebugInfoPath / FPaths::GetBaseFilename(Input.GetSourceFilename()) + TEXT(".") + FileExtension;
		if (TUniquePtr<FArchive> FileWriter = TUniquePtr<FArchive>(IFileManager::Get().CreateFileWriter(*Filename)))
		{
			FileWriter->Serialize(InData, InDataByteSize);
			FileWriter->Close();
		}
	}
}

void DumpDebugShaderBinary(const FShaderCompilerInput& Input, void* InData, int32 InDataByteSize, const FString& FileName, const FString& FileExtension)
{
	if (InData != nullptr && InDataByteSize > 0 && !FileExtension.IsEmpty())
	{
		const FString Filename = Input.DumpDebugInfoPath / FileName + TEXT(".") + FileExtension;
		if (TUniquePtr<FArchive> FileWriter = TUniquePtr<FArchive>(IFileManager::Get().CreateFileWriter(*Filename)))
		{
			FileWriter->Serialize(InData, InDataByteSize);
			FileWriter->Close();
		}
	}
}

static void DumpDebugShaderDisassembled(const FShaderCompilerInput& Input, CrossCompiler::EShaderConductorIR Language, void* InData, int32 InDataByteSize, const FString& FileExtension)
{
	if (InData != nullptr && InDataByteSize > 0 && !FileExtension.IsEmpty())
	{
		TArray<ANSICHAR> AssemblyText;
		if (CrossCompiler::FShaderConductorContext::Disassemble(Language, InData, InDataByteSize, AssemblyText))
		{
			// Assembly text contains NUL terminator, so text lenght is |array|-1
			DumpDebugShaderText(Input, AssemblyText.GetData(), AssemblyText.Num() - 1, FileExtension);
		}
	}
}

void DumpDebugShaderDisassembledSpirv(const FShaderCompilerInput& Input, void* InData, int32 InDataByteSize, const FString& FileExtension)
{
	DumpDebugShaderDisassembled(Input, CrossCompiler::EShaderConductorIR::Spirv, InData, InDataByteSize, FileExtension);
}

void DumpDebugShaderDisassembledDxil(const FShaderCompilerInput& Input, void* InData, int32 InDataByteSize, const FString& FileExtension)
{
	DumpDebugShaderDisassembled(Input, CrossCompiler::EShaderConductorIR::Dxil, InData, InDataByteSize, FileExtension);
}

namespace CrossCompiler
{
	FString CreateResourceTableFromEnvironment(const FShaderCompilerEnvironment& Environment)
	{
		FString Line = TEXT("\n#if 0 /*BEGIN_RESOURCE_TABLES*/\n");
		for (auto Pair : Environment.UniformBufferMap)
		{
			Line += FString::Printf(TEXT("%s, %d\n"), *Pair.Key, Pair.Value.LayoutHash);
		}
		Line += TEXT("NULL, 0\n");
		for (auto Pair : Environment.ResourceTableMap)
		{
			const FResourceTableEntry& Entry = Pair.Value;
			Line += FString::Printf(TEXT("%s, %s, %d, %d\n"), *Pair.Key, *Entry.UniformBufferName, Entry.Type, Entry.ResourceIndex);
		}
		Line += TEXT("NULL, NULL, 0, 0\n");

		Line += TEXT("#endif /*END_RESOURCE_TABLES*/\n");
		return Line;
	}

	void CreateEnvironmentFromResourceTable(const FString& String, FShaderCompilerEnvironment& OutEnvironment)
	{
		FString Prolog = TEXT("#if 0 /*BEGIN_RESOURCE_TABLES*/");
		int32 FoundBegin = String.Find(Prolog, ESearchCase::CaseSensitive);
		if (FoundBegin == INDEX_NONE)
		{
			return;
		}
		int32 FoundEnd = String.Find(TEXT("#endif /*END_RESOURCE_TABLES*/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, FoundBegin);
		if (FoundEnd == INDEX_NONE)
		{
			return;
		}

		// +1 for EOL
		const TCHAR* Ptr = &String[FoundBegin + 1 + Prolog.Len()];
		while (*Ptr == '\r' || *Ptr == '\n')
		{
			++Ptr;
		}
		const TCHAR* PtrEnd = &String[FoundEnd];
		while (Ptr < PtrEnd)
		{
			FString UB;
			if (!CrossCompiler::ParseIdentifier(Ptr, UB))
			{
				return;
			}
			if (!CrossCompiler::Match(Ptr, TEXT(", ")))
			{
				return;
			}
			int32 Hash;
			if (!CrossCompiler::ParseSignedNumber(Ptr, Hash))
			{
				return;
			}
			// Optional \r
			CrossCompiler::Match(Ptr, '\r');
			if (!CrossCompiler::Match(Ptr, '\n'))
			{
				return;
			}

			if (UB == TEXT("NULL") && Hash == 0)
			{
				break;
			}

			FUniformBufferEntry& UniformBufferEntry = OutEnvironment.UniformBufferMap.FindOrAdd(UB);
			UniformBufferEntry.LayoutHash = (uint32)Hash;
		}

		while (Ptr < PtrEnd)
		{
			FString Name;
			if (!CrossCompiler::ParseIdentifier(Ptr, Name))
			{
				return;
			}
			if (!CrossCompiler::Match(Ptr, TEXT(", ")))
			{
				return;
			}
			FString UB;
			if (!CrossCompiler::ParseIdentifier(Ptr, UB))
			{
				return;
			}
			if (!CrossCompiler::Match(Ptr, TEXT(", ")))
			{
				return;
			}
			int32 Type;
			if (!CrossCompiler::ParseSignedNumber(Ptr, Type))
			{
				return;
			}
			if (!CrossCompiler::Match(Ptr, TEXT(", ")))
			{
				return;
			}
			int32 ResourceIndex;
			if (!CrossCompiler::ParseSignedNumber(Ptr, ResourceIndex))
			{
				return;
			}
			// Optional
			CrossCompiler::Match(Ptr, '\r');
			if (!CrossCompiler::Match(Ptr, '\n'))
			{
				return;
			}

			if (Name == TEXT("NULL") && UB == TEXT("NULL") && Type == 0 && ResourceIndex == 0)
			{
				break;
			}
			FResourceTableEntry& Entry = OutEnvironment.ResourceTableMap.FindOrAdd(Name);
			Entry.UniformBufferName = UB;
			Entry.Type = Type;
			Entry.ResourceIndex = ResourceIndex;
		}
	}

	/**
	 * Parse an error emitted by the HLSL cross-compiler.
	 * @param OutErrors - Array into which compiler errors may be added.
	 * @param InLine - A line from the compile log.
	 */
	void ParseHlslccError(TArray<FShaderCompilerError>& OutErrors, const FString& InLine, bool bUseAbsolutePaths)
	{
		const TCHAR* p = *InLine;
		FShaderCompilerError* Error = new(OutErrors) FShaderCompilerError();

		// Copy the filename.
		while (*p && *p != TEXT('('))
		{
			Error->ErrorVirtualFilePath += (*p++);
		}

		if (!bUseAbsolutePaths)
		{
			Error->ErrorVirtualFilePath = ParseVirtualShaderFilename(Error->ErrorVirtualFilePath);
		}
		p++;

		// Parse the line number.
		int32 LineNumber = 0;
		while (*p && *p >= TEXT('0') && *p <= TEXT('9'))
		{
			LineNumber = 10 * LineNumber + (*p++ - TEXT('0'));
		}
		Error->ErrorLineString = *FString::Printf(TEXT("%d"), LineNumber);

		// Skip to the warning message.
		while (*p && (*p == TEXT(')') || *p == TEXT(':') || *p == TEXT(' ') || *p == TEXT('\t')))
		{
			p++;
		}
		Error->StrippedErrorMessage = p;
	}


	/** Map shader frequency -> string for messages. */
	static const TCHAR* FrequencyStringTable[] =
	{
		TEXT("Vertex"),
		TEXT("Mesh"),
		TEXT("Amplification"),
		TEXT("Pixel"),
		TEXT("Geometry"),
		TEXT("Compute"),
		TEXT("RayGen"),
		TEXT("RayMiss"),
		TEXT("RayHitGroup"),
		TEXT("RayCallable"),
	};

	/** Compile time check to verify that the GL mapping tables are up-to-date. */
	static_assert(SF_NumFrequencies == UE_ARRAY_COUNT(FrequencyStringTable), "NumFrequencies changed. Please update tables.");

	const TCHAR* GetFrequencyName(EShaderFrequency Frequency)
	{
		check((int32)Frequency >= 0 && Frequency < SF_NumFrequencies);
		return FrequencyStringTable[Frequency];
	}

	FHlslccHeader::FHlslccHeader() :
		Name(TEXT(""))
	{
		NumThreads[0] = NumThreads[1] = NumThreads[2] = 0;
	}

	bool FHlslccHeader::Read(const ANSICHAR*& ShaderSource, int32 SourceLen)
	{
#define DEF_PREFIX_STR(Str) \
		static const ANSICHAR* Str##Prefix = "// @" #Str ": "; \
		static const int32 Str##PrefixLen = FCStringAnsi::Strlen(Str##Prefix)
		DEF_PREFIX_STR(Inputs);
		DEF_PREFIX_STR(Outputs);
		DEF_PREFIX_STR(UniformBlocks);
		DEF_PREFIX_STR(Uniforms);
		DEF_PREFIX_STR(PackedGlobals);
		DEF_PREFIX_STR(PackedUB);
		DEF_PREFIX_STR(PackedUBCopies);
		DEF_PREFIX_STR(PackedUBGlobalCopies);
		DEF_PREFIX_STR(Samplers);
		DEF_PREFIX_STR(UAVs);
		DEF_PREFIX_STR(SamplerStates);
		DEF_PREFIX_STR(AccelerationStructures);
		DEF_PREFIX_STR(NumThreads);
#undef DEF_PREFIX_STR

		// Skip any comments that come before the signature.
		while (FCStringAnsi::Strncmp(ShaderSource, "//", 2) == 0 &&
			FCStringAnsi::Strncmp(ShaderSource + 2, " !", 2) != 0 &&
			FCStringAnsi::Strncmp(ShaderSource + 2, " @", 2) != 0)
		{
			ShaderSource += 2;
			while (*ShaderSource && *ShaderSource++ != '\n')
			{
				// Do nothing
			}
		}

		// Read shader name if any
		if (FCStringAnsi::Strncmp(ShaderSource, "// !", 4) == 0)
		{
			ShaderSource += 4;
			while (*ShaderSource && *ShaderSource != '\n')
			{
				Name += (TCHAR)*ShaderSource;
				++ShaderSource;
			}

			if (*ShaderSource == '\n')
			{
				++ShaderSource;
			}
		}

		// Skip any comments that come before the signature.
		while (FCStringAnsi::Strncmp(ShaderSource, "//", 2) == 0 &&
			FCStringAnsi::Strncmp(ShaderSource + 2, " @", 2) != 0)
		{
			ShaderSource += 2;
			while (*ShaderSource && *ShaderSource++ != '\n')
			{
				// Do nothing
			}
		}

		if (FCStringAnsi::Strncmp(ShaderSource, InputsPrefix, InputsPrefixLen) == 0)
		{
			ShaderSource += InputsPrefixLen;

			if (!ReadInOut(ShaderSource, Inputs))
			{
				return false;
			}
		}

		if (FCStringAnsi::Strncmp(ShaderSource, OutputsPrefix, OutputsPrefixLen) == 0)
		{
			ShaderSource += OutputsPrefixLen;

			if (!ReadInOut(ShaderSource, Outputs))
			{
				return false;
			}
		}

		if (FCStringAnsi::Strncmp(ShaderSource, UniformBlocksPrefix, UniformBlocksPrefixLen) == 0)
		{
			ShaderSource += UniformBlocksPrefixLen;

			while (*ShaderSource && *ShaderSource != '\n')
			{
				FAttribute UniformBlock;
				if (!ParseIdentifier(ShaderSource, UniformBlock.Name))
				{
					return false;
				}

				if (!Match(ShaderSource, '('))
				{
					return false;
				}
				
				if (!ParseIntegerNumber(ShaderSource, UniformBlock.Index))
				{
					return false;
				}

				if (!Match(ShaderSource, ')'))
				{
					return false;
				}

				UniformBlocks.Add(UniformBlock);

				if (Match(ShaderSource, '\n'))
				{
					break;
				}

				if (Match(ShaderSource, ','))
				{
					continue;
				}
			
				//#todo-rco: Need a log here
				//UE_LOG(ShaderCompilerCommon, Warning, TEXT("Invalid char '%c'"), *ShaderSource);
				return false;
			}
		}

		if (FCStringAnsi::Strncmp(ShaderSource, UniformsPrefix, UniformsPrefixLen) == 0)
		{
			// @todo-mobile: Will we ever need to support this code path?
			check(0);
			return false;
/*
			ShaderSource += UniformsPrefixLen;

			while (*ShaderSource && *ShaderSource != '\n')
			{
				uint16 ArrayIndex = 0;
				uint16 Offset = 0;
				uint16 NumComponents = 0;

				FString ParameterName = ParseIdentifier(ShaderSource);
				verify(ParameterName.Len() > 0);
				verify(Match(ShaderSource, '('));
				ArrayIndex = ParseNumber(ShaderSource);
				verify(Match(ShaderSource, ':'));
				Offset = ParseNumber(ShaderSource);
				verify(Match(ShaderSource, ':'));
				NumComponents = ParseNumber(ShaderSource);
				verify(Match(ShaderSource, ')'));

				ParameterMap.AddParameterAllocation(
					*ParameterName,
					ArrayIndex,
					Offset * BytesPerComponent,
					NumComponents * BytesPerComponent
					);

				if (ArrayIndex < OGL_NUM_PACKED_UNIFORM_ARRAYS)
				{
					PackedUniformSize[ArrayIndex] = FMath::Max<uint16>(
						PackedUniformSize[ArrayIndex],
						BytesPerComponent * (Offset + NumComponents)
						);
				}

				// Skip the comma.
				if (Match(ShaderSource, '\n'))
				{
					break;
				}

				verify(Match(ShaderSource, ','));
			}

			Match(ShaderSource, '\n');
*/
		}

		// @PackedGlobals: Global0(h:0,1),Global1(h:4,1),Global2(h:8,1)
		if (FCStringAnsi::Strncmp(ShaderSource, PackedGlobalsPrefix, PackedGlobalsPrefixLen) == 0)
		{
			ShaderSource += PackedGlobalsPrefixLen;
			while (*ShaderSource && *ShaderSource != '\n')
			{
				FPackedGlobal PackedGlobal;
				if (!ParseIdentifier(ShaderSource, PackedGlobal.Name))
				{
					return false;
				}

				if (!Match(ShaderSource, '('))
				{
					return false;
				}

				PackedGlobal.PackedType = *ShaderSource++;

				if (!Match(ShaderSource, ':'))
				{
					return false;
				}

				if (!ParseIntegerNumber(ShaderSource, PackedGlobal.Offset))
				{
					return false;
				}

				if (!Match(ShaderSource, ','))
				{
					return false;
				}

				if (!ParseIntegerNumber(ShaderSource, PackedGlobal.Count))
				{
					return false;
				}

				if (!Match(ShaderSource, ')'))
				{
					return false;
				}

				PackedGlobals.Add(PackedGlobal);

				// Break if EOL
				if (Match(ShaderSource, '\n'))
				{
					break;
				}

				// Has to be a comma!
				if (Match(ShaderSource, ','))
				{
					continue;
				}

				//#todo-rco: Need a log here
				//UE_LOG(ShaderCompilerCommon, Warning, TEXT("Invalid char '%c'"), *ShaderSource);
				return false;
			}
		}

		// Packed Uniform Buffers (Multiple lines)
		// @PackedUB: CBuffer(0): CBMember0(0,1),CBMember1(1,1)
		while (FCStringAnsi::Strncmp(ShaderSource, PackedUBPrefix, PackedUBPrefixLen) == 0)
		{
			ShaderSource += PackedUBPrefixLen;

			FPackedUB PackedUB;

			if (!ParseIdentifier(ShaderSource, PackedUB.Attribute.Name))
			{
				return false;
			}

			if (!Match(ShaderSource, '('))
			{
				return false;
			}
			
			if (!ParseIntegerNumber(ShaderSource, PackedUB.Attribute.Index))
			{
				return false;
			}

			if (!Match(ShaderSource, ')'))
			{
				return false;
			}

			if (!Match(ShaderSource, ':'))
			{
				return false;
			}

			if (!Match(ShaderSource, ' '))
			{
				return false;
			}

			while (*ShaderSource && *ShaderSource != '\n')
			{
				FPackedUB::FMember Member;
				ParseIdentifier(ShaderSource, Member.Name);
				if (!Match(ShaderSource, '('))
				{
					return false;
				}

				if (!ParseIntegerNumber(ShaderSource, Member.Offset))
				{
					return false;
				}
				
				if (!Match(ShaderSource, ','))
				{
					return false;
				}

				if (!ParseIntegerNumber(ShaderSource, Member.Count))
				{
					return false;
				}

				if (!Match(ShaderSource, ')'))
				{
					return false;
				}

				PackedUB.Members.Add(Member);

				// Break if EOL
				if (Match(ShaderSource, '\n'))
				{
					break;
				}

				// Has to be a comma!
				if (Match(ShaderSource, ','))
				{
					continue;
				}

				//#todo-rco: Need a log here
				//UE_LOG(ShaderCompilerCommon, Warning, TEXT("Invalid char '%c'"), *ShaderSource);
				return false;
			}

			PackedUBs.Add(PackedUB);
		}

		// @PackedUBCopies: 0:0-0:h:0:1,0:1-0:h:4:1,1:0-1:h:0:1
		if (FCStringAnsi::Strncmp(ShaderSource, PackedUBCopiesPrefix, PackedUBCopiesPrefixLen) == 0)
		{
			ShaderSource += PackedUBCopiesPrefixLen;
			if (!ReadCopies(ShaderSource, false, PackedUBCopies))
			{
				return false;
			}
		}

		// @PackedUBGlobalCopies: 0:0-h:12:1,0:1-h:16:1,1:0-h:20:1
		if (FCStringAnsi::Strncmp(ShaderSource, PackedUBGlobalCopiesPrefix, PackedUBGlobalCopiesPrefixLen) == 0)
		{
			ShaderSource += PackedUBGlobalCopiesPrefixLen;
			if (!ReadCopies(ShaderSource, true, PackedUBGlobalCopies))
			{
				return false;
			}
		}

		if (FCStringAnsi::Strncmp(ShaderSource, SamplersPrefix, SamplersPrefixLen) == 0)
		{
			ShaderSource += SamplersPrefixLen;

			while (*ShaderSource && *ShaderSource != '\n')
			{
				FSampler Sampler;

				if (!ParseIdentifier(ShaderSource, Sampler.Name))
				{
					return false;
				}

				if (!Match(ShaderSource, '('))
				{
					return false;
				}

				if (!ParseIntegerNumber(ShaderSource, Sampler.Offset))
				{
					return false;
				}

				if (!Match(ShaderSource, ':'))
				{
					return false;
				}

				if (!ParseIntegerNumber(ShaderSource, Sampler.Count))
				{
					return false;
				}

				if (Match(ShaderSource, '['))
				{
					// Sampler States
					do
					{
						FString SamplerState;
						
						if (!ParseIdentifier(ShaderSource, SamplerState))
						{
							return false;
						}

						Sampler.SamplerStates.Add(SamplerState);
					}
					while (Match(ShaderSource, ','));

					if (!Match(ShaderSource, ']'))
					{
						return false;
					}
				}

				if (!Match(ShaderSource, ')'))
				{
					return false;
				}

				Samplers.Add(Sampler);

				// Break if EOL
				if (Match(ShaderSource, '\n'))
				{
					break;
				}

				// Has to be a comma!
				if (Match(ShaderSource, ','))
				{
					continue;
				}

				//#todo-rco: Need a log here
				//UE_LOG(ShaderCompilerCommon, Warning, TEXT("Invalid char '%c'"), *ShaderSource);
				return false;
			}
		}

		if (FCStringAnsi::Strncmp(ShaderSource, UAVsPrefix, UAVsPrefixLen) == 0)
		{
			ShaderSource += UAVsPrefixLen;

			while (*ShaderSource && *ShaderSource != '\n')
			{
				FUAV UAV;

				if (!ParseIdentifier(ShaderSource, UAV.Name))
				{
					return false;
				}

				if (!Match(ShaderSource, '('))
				{
					return false;
				}

				if (!ParseIntegerNumber(ShaderSource, UAV.Offset))
				{
					return false;
				}

				if (!Match(ShaderSource, ':'))
				{
					return false;
				}

				if (!ParseIntegerNumber(ShaderSource, UAV.Count))
				{
					return false;
				}

				if (!Match(ShaderSource, ')'))
				{
					return false;
				}

				UAVs.Add(UAV);

				// Break if EOL
				if (Match(ShaderSource, '\n'))
				{
					break;
				}

				// Has to be a comma!
				if (Match(ShaderSource, ','))
				{
					continue;
				}

				//#todo-rco: Need a log here
				//UE_LOG(ShaderCompilerCommon, Warning, TEXT("Invalid char '%c'"), *ShaderSource);
				return false;
			}
		}

		if (FCStringAnsi::Strncmp(ShaderSource, SamplerStatesPrefix, SamplerStatesPrefixLen) == 0)
		{
			ShaderSource += SamplerStatesPrefixLen;
			while (*ShaderSource && *ShaderSource != '\n')
			{
				FAttribute SamplerState;
				if (!ParseIntegerNumber(ShaderSource, SamplerState.Index))
				{
					return false;
				}

				if (!Match(ShaderSource, ':'))
				{
					return false;
				}

				if (!ParseIdentifier(ShaderSource, SamplerState.Name))
				{
					return false;
				}

				SamplerStates.Add(SamplerState);

				// Break if EOL
				if (Match(ShaderSource, '\n'))
				{
					break;
				}

				// Has to be a comma!
				if (Match(ShaderSource, ','))
				{
					continue;
				}

				//#todo-rco: Need a log here
				//UE_LOG(ShaderCompilerCommon, Warning, TEXT("Invalid char '%c'"), *ShaderSource);
				return false;
			}
		}

		if (FCStringAnsi::Strncmp(ShaderSource, AccelerationStructuresPrefix, AccelerationStructuresPrefixLen) == 0)
		{
			ShaderSource += AccelerationStructuresPrefixLen;

			while (*ShaderSource && *ShaderSource != '\n')
			{
				FAccelerationStructure AccelerationStructure;

				if (!ParseIntegerNumber(ShaderSource, AccelerationStructure.Offset))
				{
					return false;
				}

				if (!Match(ShaderSource, ':'))
				{
					return false;
				}

				if (!ParseIdentifier(ShaderSource, AccelerationStructure.Name))
				{
					return false;
				}

				AccelerationStructures.Add(AccelerationStructure);

				if (Match(ShaderSource, '\n'))
				{
					break;
				}

				if (Match(ShaderSource, ','))
				{
					continue;
				}

				return false;
			}
		}

		if (FCStringAnsi::Strncmp(ShaderSource, NumThreadsPrefix, NumThreadsPrefixLen) == 0)
		{
			ShaderSource += NumThreadsPrefixLen;
			if (!ParseIntegerNumber(ShaderSource, NumThreads[0]))
			{
				return false;
			}
			if (!Match(ShaderSource, ','))
			{
				return false;
			}

			if (!Match(ShaderSource, ' '))
			{
				return false;
			}

			if (!ParseIntegerNumber(ShaderSource, NumThreads[1]))
			{
				return false;
			}

			if (!Match(ShaderSource, ','))
			{
				return false;
			}

			if (!Match(ShaderSource, ' '))
			{
				return false;
			}

			if (!ParseIntegerNumber(ShaderSource, NumThreads[2]))
			{
				return false;
			}

			if (!Match(ShaderSource, '\n'))
			{
				return false;
			}
		}
	
		return ParseCustomHeaderEntries(ShaderSource);
	}

	bool FHlslccHeader::ReadCopies(const ANSICHAR*& ShaderSource, bool bGlobals, TArray<FPackedUBCopy>& OutCopies)
	{
		while (*ShaderSource && *ShaderSource != '\n')
		{
			FPackedUBCopy PackedUBCopy;
			PackedUBCopy.DestUB = 0;

			if (!ParseIntegerNumber(ShaderSource, PackedUBCopy.SourceUB))
			{
				return false;
			}

			if (!Match(ShaderSource, ':'))
			{
				return false;
			}

			if (!ParseIntegerNumber(ShaderSource, PackedUBCopy.SourceOffset))
			{
				return false;
			}

			if (!Match(ShaderSource, '-'))
			{
				return false;
			}

			if (!bGlobals)
			{
				if (!ParseIntegerNumber(ShaderSource, PackedUBCopy.DestUB))
				{
					return false;
				}

				if (!Match(ShaderSource, ':'))
				{
					return false;
				}
			}

			PackedUBCopy.DestPackedType = *ShaderSource++;

			if (!Match(ShaderSource, ':'))
			{
				return false;
			}

			if (!ParseIntegerNumber(ShaderSource, PackedUBCopy.DestOffset))
			{
				return false;
			}

			if (!Match(ShaderSource, ':'))
			{
				return false;
			}

			if (!ParseIntegerNumber(ShaderSource, PackedUBCopy.Count))
			{
				return false;
			}

			OutCopies.Add(PackedUBCopy);

			// Break if EOL
			if (Match(ShaderSource, '\n'))
			{
				break;
			}

			// Has to be a comma!
			if (Match(ShaderSource, ','))
			{
				continue;
			}

			//#todo-rco: Need a log here
			//UE_LOG(ShaderCompilerCommon, Warning, TEXT("Invalid char '%c'"), *ShaderSource);
			return false;
		}

		return true;
	}

	bool FHlslccHeader::ReadInOut(const ANSICHAR*& ShaderSource, TArray<FInOut>& OutAttributes)
	{
		while (*ShaderSource && *ShaderSource != '\n')
		{
			FInOut Attribute;

			if (!ParseIdentifier(ShaderSource, Attribute.Type))
			{
				return false;
			}

			if (Match(ShaderSource, '['))
			{
				if (!ParseIntegerNumber(ShaderSource, Attribute.ArrayCount))
				{
					return false;
				}

				if (!Match(ShaderSource, ']'))
				{
					return false;
				}
			}
			else
			{
				Attribute.ArrayCount = 0;
			}

			if (Match(ShaderSource, ';'))
			{
				if (!ParseSignedNumber(ShaderSource, Attribute.Index))
				{
					return false;
				}
			}

			if (!Match(ShaderSource, ':'))
			{
				return false;
			}

			if (!ParseIdentifier(ShaderSource, Attribute.Name))
			{
				return false;
			}

			// Optional array suffix
			if (Match(ShaderSource, '['))
			{
				Attribute.Name += '[';
				while (*ShaderSource)
				{
					Attribute.Name += *ShaderSource;
					if (Match(ShaderSource, ']'))
					{
						break;
					}
					++ShaderSource;
				}
			}

			OutAttributes.Add(Attribute);

			// Break if EOL
			if (Match(ShaderSource, '\n'))
			{
				return true;
			}

			// Has to be a comma!
			if (Match(ShaderSource, ','))
			{
				continue;
			}

			//#todo-rco: Need a log here
			//UE_LOG(ShaderCompilerCommon, Warning, TEXT("Invalid char '%c'"), *ShaderSource);
			return false;
		}

		// Last character must be EOL
		return Match(ShaderSource, '\n');
	}

} // namespace CrossCompiler