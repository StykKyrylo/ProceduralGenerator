using UnrealBuildTool;

public class ProceduralGenerator : ModuleRules
{
	public ProceduralGenerator(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "Landscape" });
		PrivateDependencyModuleNames.AddRange(new string[] { });

		PrivateIncludePaths.Add(System.IO.Path.Combine(ModuleDirectory, "WorldGen", "Core", "include"));

		bUseUnity = false;
		bEnableExceptions = true;
		ShadowVariableWarningLevel = WarningLevel.Off;
		UnsafeTypeCastWarningLevel = WarningLevel.Off;
		UndefinedIdentifierWarningLevel = WarningLevel.Off;
	}
}