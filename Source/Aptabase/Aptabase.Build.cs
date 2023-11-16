using UnrealBuildTool;

public class Aptabase : ModuleRules
{
	public Aptabase(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(new string[]{
			"Analytics",
			"Core", 
			"CoreUObject",
			"DeveloperSettings",
			"Engine",
			"EngineSettings",
			"HTTP",
			"Json",
			"JsonUtilities",
			"Projects",
			"Slate",
			"SlateCore",
		});

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
		}
	}
}
