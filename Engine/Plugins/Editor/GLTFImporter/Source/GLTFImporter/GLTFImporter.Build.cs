// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
    public class GLTFImporter : ModuleRules
    {
        public GLTFImporter(ReadOnlyTargetRules Target) : base(Target)
        {
            PublicIncludePaths.AddRange(
                new string[] {
                }
                );

            PrivateIncludePaths.AddRange(
                new string[] {
                }
                );

            PublicDependencyModuleNames.AddRange(
                new string[]
                {
                }
                );

            PrivateDependencyModuleNames.AddRange(
                new string[]
                {
                    "Core",
                    "CoreUObject",
                    "Engine",
					"EditorFramework",
                    "UnrealEd",
                    "MeshDescription",
					"StaticMeshDescription",
                    "MeshUtilities",
                    "MessageLog",
                    "Json",
                    "MaterialEditor",
                    "Slate",
                    "SlateCore",
                    "MainFrame",
                    "InputCore",
                    "EditorStyle",
					"GLTFCore",
                }
                );

            DynamicallyLoadedModuleNames.AddRange(
                new string[]
                {
                }
                );
        }
    }
}
