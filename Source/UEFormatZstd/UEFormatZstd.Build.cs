using System.IO;
using UnrealBuildTool;

public class UEFormatZstd : ModuleRules
{
	public UEFormatZstd(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.NoPCHs;
		bEnableUndefinedIdentifierWarnings = false;

		PublicIncludePaths.AddRange(
			new string[]
			{
				Path.Combine(ModuleDirectory, "ThirdParty/zstd"),
				Path.Combine(ModuleDirectory, "ThirdParty/zstd/common"),
				Path.Combine(ModuleDirectory, "ThirdParty/zstd/compress"),
				Path.Combine(ModuleDirectory, "ThirdParty/zstd/decompress"),
				Path.Combine(ModuleDirectory, "ThirdParty/zstd/deprecated"),
				Path.Combine(ModuleDirectory, "ThirdParty/zstd/dictBuilder"),
				Path.Combine(ModuleDirectory, "ThirdParty/zstd/legacy")
			}
		);

		PrivateDefinitions.Add("ZSTD_DLL_EXPORT=1");

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
			}
		);
	}
}
