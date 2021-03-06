// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AnimationBlueprintEditor : ModuleRules
{
	public AnimationBlueprintEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePaths.Add("Editor/AnimationBlueprintEditor/Private");	// For PCH includes (because they don't work with relative paths, yet)

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"AssetRegistry", 
				"MainFrame",
				"DesktopPlatform",
                "SkeletonEditor",
                "ContentBrowser",
                "AssetTools",
                "AnimationEditor",
            }
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
                "AppFramework",
				"Core", 
				"CoreUObject", 
				"Slate", 
				"SlateCore",
                "EditorStyle",
				"Engine", 
				"EditorFramework",
				"UnrealEd", 
				"GraphEditor", 
                "InputCore",
				"KismetWidgets",
				"AnimGraph",
				"AnimGraphRuntime",
				"PropertyEditor",
				"EditorWidgets",
                "BlueprintGraph",
                "RHI",
                "KismetCompiler",
				"ToolMenus",
				"AnimGraphRuntime",
            }
		);

		DynamicallyLoadedModuleNames.AddRange(
			new string[] {
				"ContentBrowser",
				"Documentation",
				"MainFrame",
				"DesktopPlatform",
                "SkeletonEditor",
                "AssetTools",
                "AnimationEditor",
            }
		);

        PublicDependencyModuleNames.AddRange(
            new string[] {
                "Kismet",
                "Persona",
            }
        );
    }
}
