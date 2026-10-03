using System.IO;
using UnrealBuildTool;

public class UEFormat : ModuleRules
{
	public UEFormat(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"RawMesh",
				"MeshDescription",
				"AssetTools",
				"UnrealEd",
				"MeshUtilities",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Slate",
				"SlateCore",
				"EditorStyle",
				"EditorWidgets",
				"MainFrame",
				"PropertyEditor",
				"UEFormatZstd",
			}
		);
	}
}
