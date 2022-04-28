// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigVMDefines.h"
#include "RigVMCore/RigVMRegistry.h"
#include "RigVMCore/RigVMExternalVariable.h"
#include "RigVMCore/RigVMTraits.h"
#include "RigVMCore/RigVMUserWorkflow.h"
#include "RigVMStruct.generated.h"

// delegates used for variable introspection / creation
DECLARE_DELEGATE_RetVal(TArray<FRigVMExternalVariable>, FRigVMGetExternalVariablesDelegate)
DECLARE_DELEGATE_RetVal_TwoParams(FName, FRigVMCreateExternalVariableDelegate, FRigVMExternalVariable, FString)
DECLARE_DELEGATE_RetVal_TwoParams(bool, FRigVMBindPinToExternalVariableDelegate, FString, FString)

struct FRigVMStruct;

/** Context as of why the node was created */
enum class ERigVMNodeCreatedReason : uint8
{
	NodeSpawner,
	ScriptedEvent,
	Paste,
	BackwardsCompatibility,
	Unknown,
};

/**
 * A context struct passed to FRigVMStruct::OnUnitNodeCreated
 */
struct RIGVM_API FRigVMUnitNodeCreatedContext
{
public:

	struct FScope
	{
	public:
		FScope(FRigVMUnitNodeCreatedContext& InContext, ERigVMNodeCreatedReason InReason)
			: Context(InContext)
			, PreviousReason(InContext.GetReason())
		{
			Context.Reason = InReason;
		}

		~FScope()
		{
			Context.Reason = PreviousReason;
		}

	private:
		FRigVMUnitNodeCreatedContext& Context;
		ERigVMNodeCreatedReason PreviousReason;
	};

	/** Returns true if this context is valid to use */
	bool IsValid() const;

	/** Get the reason why this node was created */
	ERigVMNodeCreatedReason GetReason() const { return Reason; }

	/** Get the name of this node */
	FName GetNodeName() const { return NodeName; }

	/** Returns all currently existing external variables */
	TArray<FRigVMExternalVariable> GetExternalVariables() const;

	/** Creates a new variable within the host of this VM */
	FName AddExternalVariable(const FRigVMExternalVariable& InVariableToCreate, FString InDefaultValue = FString());

	/** Binds a pin to an external variable on the created node */
	bool BindPinToExternalVariable(FString InPinPath, FString InVariablePath);

	/** Returns a variable given a name (or a non-valid variable if not found) */
	FRigVMExternalVariable FindVariable(FName InVariableName) const;

	/** Returns the name of the first variable given a(or NAME_None if not found) */
	FName FindFirstVariableOfType(FName InCPPTypeName) const;

	/** Returns the name of the first variable given a type (or NAME_None if not found) */
	template <
		typename T,
		typename TEnableIf<TRigVMIsBaseStructure<T>::Value>::Type * = nullptr
	>
	FORCEINLINE FName FindFirstVariableOfType() const
	{
		return FindFirstVariableOfType(TBaseStructure<T>::Get());
	}

	/** Returns the name of the first variable given a type (or NAME_None if not found) */
	template <
		typename T,
		typename TEnableIf<TModels<CRigVMUStruct, T>::Value>::Type * = nullptr
	>
	FORCEINLINE FName FindFirstVariableOfType() const
	{
		return FindFirstVariableOfType(T::StaticStruct());
	}

	/** Returns the name of the first variable given a type (or NAME_None if not found) */
	template <
		typename T,
		typename TEnableIf<TModels<CRigVMUClass, T>::Value>::Type * = nullptr
	>
	FORCEINLINE FName FindFirstVariableOfType() const
	{
		return FindFirstVariableOfType(T::StaticClass());
	}

	/** Returns the name of the first variable given a type (or NAME_None if not found) */
	template <
		typename T,
		typename TEnableIf<TIsEnum<T>::Value>::Type * = nullptr
	>
		FORCEINLINE FName FindFirstVariableOfType() const
	{
		return FindFirstVariableOfType(StaticEnum<T>());
	}

	FRigVMGetExternalVariablesDelegate& GetAllExternalVariablesDelegate() { return AllExternalVariablesDelegate; }
	FRigVMCreateExternalVariableDelegate& GetCreateExternalVariableDelegate() { return CreateExternalVariableDelegate; }
	FRigVMBindPinToExternalVariableDelegate& GetBindPinToExternalVariableDelegate() { return BindPinToExternalVariableDelegate; }

private:
	
	FName NodeName = NAME_None;
	ERigVMNodeCreatedReason Reason = ERigVMNodeCreatedReason::Unknown;
	FRigVMGetExternalVariablesDelegate AllExternalVariablesDelegate;
	FRigVMCreateExternalVariableDelegate CreateExternalVariableDelegate;
	FRigVMBindPinToExternalVariableDelegate BindPinToExternalVariableDelegate;

	FName FindFirstVariableOfType(UObject* InCPPTypeObject) const;

	friend class URigVMController;
	friend struct FScope;
};

/** Structure used to upgrade to a new implementation of a node */
struct RIGVM_API FRigVMStructUpgradeInfo
{
public:
	
	FRigVMStructUpgradeInfo();

	template<typename Old, typename New>
	FRigVMStructUpgradeInfo(const Old& InOld, const New& InNew)
	{
		OldStruct = Old::StaticStruct();
		NewStruct = New::StaticStruct();
		SetDefaultValues(&InNew);
	}

	// returns true if this upgrade info can be applied
	bool IsValid() const;

	// returns the old struct trying to be upgraded
	UScriptStruct* GetOldStruct() const { return OldStruct; }

	// returns the new struct to upgrade to
	UScriptStruct* GetNewStruct() const { return NewStruct; }

	// returns the map for all default values
	const TMap<FName, FString>& GetDefaultValues() const { return DefaultValues; }

	// returns the default value for a given pin
	const FString& GetDefaultValueForPin(const FName& InPinName) const;

	// adds a pin to be remapped
	void AddRemappedPin(const FString& InOldPinPath, const FString& InNewPinPath, bool bAsInput = true, bool bAsOutput = true);

	// remaps a pin path based on our internals
	FString RemapPin(const FString& InPinPath, bool bIsInput, bool bContainsNodeName) const;

private:
	
	// The complete node path including models / collapse node.
	// The path may look like "RigGraph|CollapseNode1|Add"
	FString NodePath;
	
	// The old struct this upgrade info originates from
	UScriptStruct* OldStruct;

	// The new struct this upgrade info is targeting
	UScriptStruct* NewStruct;

	// Remapping info for re-linking inputs 
	// Entries can be root pins or sub pins
	TMap<FString, FString> InputLinkMap;

	// Remapping info for re-linking outputs 
	// Entries can be root pins or sub pins
	TMap<FString, FString> OutputLinkMap;

	// New sets of default values
	TMap<FName, FString> DefaultValues;

	// sets the default values from the new struct.
	void SetDefaultValues(const FRigVMStruct* InNewStructMemory);

	friend class URigVMController;
};

/**
 * The base class for all RigVM enabled structs.
 */
USTRUCT()
struct RIGVM_API FRigVMStruct
{
	GENERATED_BODY()

	virtual ~FRigVMStruct() {}
	virtual FString ProcessPinLabelForInjection(const FString& InLabel) const { return InLabel; }
	virtual FName GetEventName() const { return NAME_None; }

public:

	// loop related
	FORCEINLINE virtual bool IsForLoop() const { return false; }
	FORCEINLINE virtual int32 GetNumSlices() const { return 1; }

	// node creation
	FORCEINLINE virtual void OnUnitNodeCreated(FRigVMUnitNodeCreatedContext& InContext) const {}

	// user workflow
	TArray<FRigVMUserWorkflow> GetWorkflows(ERigVMUserWorkflowType InType, const UObject* InSubject) const; 

#if WITH_EDITOR
	static bool ValidateStruct(UScriptStruct* InStruct, FString* OutErrorMessage);
	static bool CheckPinType(UScriptStruct* InStruct, const FName& PinName, const FString& ExpectedType, FString* OutErrorMessage = nullptr);
	static bool CheckPinDirection(UScriptStruct* InStruct, const FName& PinName, const FName& InDirectionMetaName);
	static ERigVMPinDirection GetPinDirectionFromProperty(FProperty* InProperty);
	static bool CheckPinExists(UScriptStruct* InStruct, const FName& PinName, const FString& ExpectedType = FString(), FString* OutErrorMessage = nullptr);
	static bool CheckMetadata(UScriptStruct* InStruct, const FName& PinName, const FName& InMetadataKey, FString* OutErrorMessage = nullptr);
	static bool CheckFunctionExists(UScriptStruct* InStruct, const FName& FunctionName, FString* OutErrorMessage = nullptr);
#endif
	static FString ExportToFullyQualifiedText(const FProperty* InMemberProperty, const uint8* InMemberMemoryPtr, bool bUseQuotes = true);
	static FString ExportToFullyQualifiedText(const UScriptStruct* InStruct, const uint8* InStructMemoryPtr);

	template <
		typename T,
		typename TEnableIf<TRigVMIsBaseStructure<T>::Value>::Type * = nullptr
	>
	FORCEINLINE static FString ExportToFullyQualifiedText(const T& InStructValue)
	{
		return ExportToFullyQualifiedText(TBaseStructure<T>::Get(), (const uint8*)&InStructValue);
	}
	
	template <
		typename T,
		typename TEnableIf<TModels<CRigVMUStruct, T>::Value>::Type * = nullptr
	>
	FORCEINLINE static FString ExportToFullyQualifiedText(const T& InStructValue)
	{
		return ExportToFullyQualifiedText(T::StaticStruct(), (const uint8*)&InStructValue);
	}

	FString ExportToFullyQualifiedText(const UScriptStruct* InScriptStruct, const FName& InPropertyName, const uint8* InStructMemoryPointer = nullptr) const;
	
	virtual FName GetNextAggregateName(const FName& InLastAggregatePinName) const { return FName(); }
	virtual FRigVMStructUpgradeInfo GetUpgradeInfo() const { return FRigVMStructUpgradeInfo(); }

	static const FName DeprecatedMetaName;
	static const FName InputMetaName;
	static const FName OutputMetaName;
	static const FName IOMetaName;
	static const FName HiddenMetaName;
	static const FName VisibleMetaName;
	static const FName DetailsOnlyMetaName;
	static const FName AbstractMetaName;
	static const FName CategoryMetaName;
	static const FName DisplayNameMetaName;
	static const FName MenuDescSuffixMetaName;
	static const FName ShowVariableNameInTitleMetaName;
	static const FName CustomWidgetMetaName;
	static const FName ConstantMetaName;
	static const FName TitleColorMetaName;
	static const FName NodeColorMetaName;
	// icon meta name format: StyleSetName|StyleName|SmallStyleName|StatusOverlayStyleName
	// the last two names are optional, see FSlateIcon() for reference
	// Example: Icon="EditorStyle|GraphEditor.Sequence_16x"
	static const FName IconMetaName;
	static const FName KeywordsMetaName;
	static const FName TemplateNameMetaName;
	static const FName AggregateMetaName;
	static const FName ExpandPinByDefaultMetaName;
	static const FName DefaultArraySizeMetaName;
	static const FName VaryingMetaName;
	static const FName SingletonMetaName;
	static const FName SliceContextMetaName;
	static const FName ExecuteName;
	static const FName ExecuteContextName;
	static const FName ForLoopCountPinName;
	static const FName ForLoopContinuePinName;
	static const FName ForLoopCompletedPinName;
	static const FName ForLoopIndexPinName;

protected:

	static float GetRatioFromIndex(int32 InIndex, int32 InCount);
	TMap<FName, FString> GetDefaultValues(UScriptStruct* InScriptStruct) const;
	bool ApplyUpgradeInfo(const FRigVMStructUpgradeInfo& InUpgradeInfo);
	FORCEINLINE virtual TArray<FRigVMUserWorkflow> GetSupportedWorkflows(const UObject* InSubject) const { return TArray<FRigVMUserWorkflow>(); } 

	friend struct FRigVMStructUpgradeInfo;
	friend class FRigVMGraphStructUpgradeInfoTest;
	friend class URigVMController;
};
