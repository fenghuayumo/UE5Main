// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class SlateCore : ModuleRules
{
	public SlateCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"InputCore",
				"ApplicationCore",
				"Json",
				"TraceLog",
			});

		PrivateIncludePaths.AddRange(
			new string[] {
				"Runtime/SlateCore/Private",
				"Runtime/SlateCore/Private/Animation",
				"Runtime/SlateCore/Private/Application",
				"Runtime/SlateCore/Private/Brushes",
				"Runtime/SlateCore/Private/Fonts",
				"Runtime/SlateCore/Private/Input",
				"Runtime/SlateCore/Private/Layout",
				"Runtime/SlateCore/Private/Rendering",
				"Runtime/SlateCore/Private/Sound",
				"Runtime/SlateCore/Private/Styling",
				"Runtime/SlateCore/Private/Textures",
				"Runtime/SlateCore/Private/Types",
				"Runtime/SlateCore/Private/Widgets",
			});

        if (Target.Type != TargetType.Server)
		{
			if (Target.bCompileFreeType)
			{
				AddEngineThirdPartyPrivateStaticDependencies(Target, "FreeType2");
				PublicDefinitions.Add("WITH_FREETYPE=1");
			}
			else
			{
				PublicDefinitions.Add("WITH_FREETYPE=0");
			}

			if (Target.bCompileICU)
			{
				AddEngineThirdPartyPrivateStaticDependencies(Target, "ICU");
			}

			AddEngineThirdPartyPrivateStaticDependencies(Target, "HarfBuzz");
			AddEngineThirdPartyPrivateStaticDependencies(Target, "Nanosvg");
		}
		else
		{
			PublicDefinitions.Add("WITH_FREETYPE=0");
			PublicDefinitions.Add("WITH_HARFBUZZ=0");
		}

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			AddEngineThirdPartyPrivateStaticDependencies(Target, "XInput");
		}
	}
}
