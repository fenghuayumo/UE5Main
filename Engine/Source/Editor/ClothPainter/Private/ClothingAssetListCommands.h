// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Framework/Commands/Commands.h"
#include "Framework/Commands/UICommandInfo.h"
#include "EditorStyleSet.h"

class FClothingAssetListCommands : public TCommands<FClothingAssetListCommands>
{
public:
	FClothingAssetListCommands()
		: TCommands<FClothingAssetListCommands>(
			TEXT("ClothAssetList"), 
			NSLOCTEXT("Contexts", "ClothAssetList", "Clothing Asset List"), 
			NAME_None, 
			FEditorStyle::GetStyleSetName())
	{}

	virtual void RegisterCommands() override;

	TSharedPtr<FUICommandInfo> DeleteAsset;

#if WITH_APEX_CLOTHING
	TSharedPtr<FUICommandInfo> ReimportAsset;
#endif

	TSharedPtr<FUICommandInfo> RebuildAssetParams;
	TMap<FName, TSharedPtr<FUICommandInfo>> ExportAssets;
};
