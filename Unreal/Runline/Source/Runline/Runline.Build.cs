// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class Runline : ModuleRules
{
	public Runline(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"Runline",
			"Runline/Variant_Horror",
			"Runline/Variant_Horror/UI",
			"Runline/Variant_Shooter",
			"Runline/Variant_Shooter/AI",
			"Runline/Variant_Shooter/UI",
			"Runline/Variant_Shooter/Weapons"
		});

		// RunlineCore: plain C++20, built by scripts/build-core-ue.ps1 (a pre-build step of both
		// targets) into a Release static library. Its NotPorted guard needs exceptions.
		string RepoRoot = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "..", ".."));
		PublicIncludePaths.Add(Path.Combine(RepoRoot, "RunlineCore", "include"));
		PublicAdditionalLibraries.Add(Path.Combine(RepoRoot, "build", "RunlineCore-ue", "Release", "RunlineCore.lib"));
		bEnableExceptions = true;
		// Game targets default to /fp:fast (Editor ones to /fp:precise); the core's inline maths and
		// RunlineUnits must round the same way in a packaged game as in the tests.
		FPSemantics = FPSemanticsMode.Precise;
		PublicIncludePaths.Add("Runline/Runner");
		// The adapter tests replay the core's traces and goldens.
		PrivateIncludePaths.Add(Path.Combine(RepoRoot, "RunlineCore", "tests"));
		PublicDefinitions.Add("RUNLINE_GOLDEN_DIR=\"" + Path.Combine(RepoRoot, "RunlineCore", "tests", "golden").Replace('\\', '/') + "\"");

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
