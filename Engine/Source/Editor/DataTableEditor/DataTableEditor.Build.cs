// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class DataTableEditor : ModuleRules
{
	public DataTableEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicIncludePathModuleNames.Add("LevelEditor");

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"ApplicationCore",
				"Engine",
                "InputCore",
				"Slate",
				"SlateCore",
                "EditorStyle",
                "PropertyEditor",
				"EditorFramework",
				"UnrealEd",
				"Json"
			}
			);

		DynamicallyLoadedModuleNames.Add("WorkspaceMenuStructure");
	}
}
