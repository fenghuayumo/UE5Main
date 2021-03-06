// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Crunch : ModuleRules
{
	public Crunch(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

        string BasePath = Target.UEThirdPartySourceDirectory + "Crunch/";
        PublicSystemIncludePaths.Add(BasePath + "include");

        if (Target.bCompileAgainstEditor)
        {
            // link with lib to allow encoding
            string LibPath = BasePath + "Lib/";

            if (Target.Platform == UnrealTargetPlatform.Win64)
            {
                LibPath += "Win64/";
                PublicAdditionalLibraries.Add(LibPath + "crnlib.lib");
            }
        }
    }
}
