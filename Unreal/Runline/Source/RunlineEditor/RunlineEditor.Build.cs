using System.IO;
using UnrealBuildTool;

/// Editor-only tools for Runline: baking faith-runner's courses into maps, and the map tests.
public class RunlineEditor : ModuleRules
{
	public RunlineEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "UnrealEd", "Runline" });
		bEnableExceptions = true;
		FPSemantics = FPSemanticsMode.Precise;
		string RepoRoot = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "..", ".."));
		// The map tests replay the core's traces through the baked map and drive the test runner.
		PrivateIncludePaths.Add(ModuleDirectory);
		PrivateIncludePaths.Add(Path.Combine(RepoRoot, "RunlineCore", "tests"));
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "..", "Runline", "Tests"));
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "..", "Runline", "Runner"));
	}
}
