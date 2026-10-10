// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class RunlineEditorTarget : TargetRules
{
	public RunlineEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("Runline");
		// RunlineCore as a static library (scripts/build-core-ue.ps1).
		PreBuildSteps.Add("powershell -NoProfile -ExecutionPolicy Bypass -File \"$(ProjectDir)\\..\\..\\scripts\\build-core-ue.ps1\"");
	}
}
