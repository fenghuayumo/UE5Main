// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class BlueprintGraph : ModuleRules
{
	public BlueprintGraph(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePaths.AddRange(
            new string[] {
                "Editor/BlueprintGraph/Private",
                "Editor/KismetCompiler/Public",
            }
		);

		OverridePackageType = PackageOverrideType.EngineDeveloper;
		 
		PublicDependencyModuleNames.AddRange(
			new string[] { 
				"Core", 
				"CoreUObject", 
				"Engine",
                "InputCore",
				"Slate",
                "EditorStyle",
				"EditorSubsystem",
				"DeveloperSettings"
			}
		);

		PrivateDependencyModuleNames.AddRange( 
			new string[] {
				"EditorStyle",
                "KismetCompiler",
				"EditorFramework",
				"UnrealEd",
                "GraphEditor",
				"SlateCore",
                "Kismet",
                "KismetWidgets",
                "PropertyEditor",
				"ToolMenus",
			}
		);

		CircularlyReferencedDependentModules.AddRange(
            new string[] {
                "KismetCompiler",
                "UnrealEd",
                "GraphEditor",
                "Kismet",
            }
		); 
	}
}
