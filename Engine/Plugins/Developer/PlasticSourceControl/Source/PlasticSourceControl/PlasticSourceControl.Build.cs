// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class PlasticSourceControl : ModuleRules
{
	public PlasticSourceControl(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Slate",
				"SlateCore",
				"InputCore",
				"EditorStyle",
				"EditorFramework",
				"UnrealEd",
				"LevelEditor",
				"SourceControl",
				"XmlParser",
				"Projects",
				"AssetRegistry",
			}
		);
	}
}
