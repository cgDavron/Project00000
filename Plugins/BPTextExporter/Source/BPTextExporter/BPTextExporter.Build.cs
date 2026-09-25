using UnrealBuildTool;

public class BPTextExporter : ModuleRules
{
	public BPTextExporter(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UnrealEd",
			"BlueprintGraph",
			"ToolMenus",
			"ContentBrowser",
			"ApplicationCore",
			"Slate",
			"SlateCore"
		});
	}
}
