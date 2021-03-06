// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"
#include "InterchangeFilePickerBase.h"
#include "InterchangeTranslatorBase.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "InterchangeOpenFileDialog.generated.h"

UCLASS(BlueprintType, Blueprintable)
class INTERCHANGEEDITORUTILITIES_API UInterchangeFilePickerGeneric : public UInterchangeFilePickerBase
{
	GENERATED_BODY()

public:

protected:

	virtual bool FilePickerForTranslatorAssetType(const EInterchangeTranslatorAssetType TranslatorAssetType, FString& OutFilename) override;
	virtual bool FilePickerForTranslatorType(const EInterchangeTranslatorType TranslatorType, FString& OutFilename) override;
};
