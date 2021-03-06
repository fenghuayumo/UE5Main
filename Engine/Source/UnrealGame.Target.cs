// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

[SupportedPlatforms(UnrealPlatformClass.All)]
public class UnrealGameTarget : TargetRules
{
	public UnrealGameTarget( TargetInfo Target ) : base(Target)
	{
		Type = TargetType.Game;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		BuildEnvironment = TargetBuildEnvironment.Shared;

		ExtraModuleNames.Add("UnrealGame");

		if (Target.Platform == UnrealTargetPlatform.IOS)
		{
			// to make iOS projects as small as possible we excluded some items from the engine.
			// uncomment below to make a smaller iOS build
			/*bCompileRecast = false;
			bCompileSpeedTree = false;
			bCompileAPEX = false;
			bCompileLeanAndMeanUE = true;
			bCompilePhysXVehicle = false;
			bCompileFreeType = false;
			bCompileForSize = true;*/
		}
	}
}
