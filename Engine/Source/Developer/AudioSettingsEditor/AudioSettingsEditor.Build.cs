// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AudioSettingsEditor : ModuleRules
{
	public AudioSettingsEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] {
                "Core",
                "InputCore",
				"Engine",
				"Slate",
				"SlateCore",
				"EditorStyle",
				"PropertyEditor",
				"SharedSettingsWidgets",
				"EditorFramework",
				"UnrealEd",
                "CoreUObject"
            }
		);
	}
}
