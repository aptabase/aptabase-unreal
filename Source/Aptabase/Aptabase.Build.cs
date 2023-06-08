using UnrealBuildTool;

public class Aptabase : ModuleRules
{
	public Aptabase(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(new string[]{
			"Core", 
			"Analytics",
		});

		PrivateDependencyModuleNames.AddRange(new string[]{
			"CoreUObject",
			"Engine",
			"EngineSettings",
			"Slate",
			"HTTP",
			"JsonUtilities",
			"Projects",
			"SlateCore",
			"DeveloperSettings",
		});
	}
}
