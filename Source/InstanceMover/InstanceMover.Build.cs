using UnrealBuildTool;

public class InstanceMover : ModuleRules
{
	public InstanceMover(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"EditorFramework",
			"Engine",
			"Foliage",
			"InputCore",
			"InteractiveToolsFramework",
			"EditorInteractiveToolsFramework",
			"PropertyEditor",
			"Slate",
			"SlateCore",
			"UnrealEd",
		});
	}
}
