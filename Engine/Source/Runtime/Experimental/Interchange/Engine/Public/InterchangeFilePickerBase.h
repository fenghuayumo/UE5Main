// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"
#include "InterchangeTranslatorBase.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "InterchangeFilePickerBase.generated.h"

UCLASS(Abstract, BlueprintType, Blueprintable, Experimental)
class INTERCHANGEENGINE_API UInterchangeFilePickerBase : public UObject
{
	GENERATED_BODY()

public:

	/**
	 * Non virtual helper to allow blueprint to implement event base function to implement the function FilePickerForTranslatorAssetType,
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interchange | Utilities")
	bool ScriptedFilePickerForTranslatorAssetType(const EInterchangeTranslatorAssetType TranslatorAssetType, FString& OutFilename);

	/** The default implementation (call if the blueprint do not have any implementation) will call the virtual FilePickerForTranslatorAssetType */
	bool ScriptedFilePickerForTranslatorAssetType_Implementation(const EInterchangeTranslatorAssetType TranslatorAssetType, FString& OutFilename)
	{
		//By default we call the virtual function FilePickerForTranslatorAssetType
		return FilePickerForTranslatorAssetType(TranslatorAssetType, OutFilename);
	}

	/**
	 * Non virtual helper to allow blueprint to implement event base function to implement the function FilePickerForTranslatorType,
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interchange | Utilities")
	bool ScriptedFilePickerForTranslatorType(const EInterchangeTranslatorType TranslatorType, FString& OutFilename);

	/** The default implementation (call if the blueprint do not have any implementation) will call the virtual FilePickerForTranslatorType */
	bool ScriptedFilePickerForTranslatorType_Implementation(const EInterchangeTranslatorType TranslatorAssetType, FString& OutFilename)
	{
		//By default we call the virtual function FilePickerForTranslatorType
		return FilePickerForTranslatorType(TranslatorAssetType, OutFilename);
	}


protected:

	/**
	 * This function must set OutFilename with a valid file that can be import to create the specified TranslatorAssetType.
	 * We expect the user to see a file picker dialog when the editor is available.
	 * 
	 * @Return - true if it set OutFilename with a valid file path, false otherwise.
	 */
	virtual bool FilePickerForTranslatorAssetType(const EInterchangeTranslatorAssetType TranslatorAssetType, FString& OutFilename) { return false; }

	/**
	 * This function must set OutFilename with a valid file that can be import to create the specified TranslatorType.
	 * We expect the user to see a file picker dialog when the editor is available.
	 *
	 * @Return - true if it set OutFilename with a valid file path, false otherwise.
	 */
	virtual bool FilePickerForTranslatorType(const EInterchangeTranslatorType TranslatorType, FString& OutFilename) { return false; }

};
