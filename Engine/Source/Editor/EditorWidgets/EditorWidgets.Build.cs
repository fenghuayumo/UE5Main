// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class EditorWidgets : ModuleRules
{
	public EditorWidgets(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePathModuleNames.Add("AssetRegistry");

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",
                "InputCore",
				"Slate",
				"SlateCore",
                "EditorStyle",
				"EditorFramework",
				"UnrealEd",
				"ToolWidgets"
			}
		);

		DynamicallyLoadedModuleNames.Add("AssetRegistry");
	}
}
