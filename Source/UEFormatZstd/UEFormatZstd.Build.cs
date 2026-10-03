using System.IO;
using UnrealBuildTool;

// zstd lives in its own module so its C sources are compiled without the
// C++ precompiled header (UE 4.24+ force-includes the PCH into .c files too).
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

		// Export the zstd API from this module's DLL so UEFormat can link against it.
		PrivateDefinitions.Add("ZSTD_DLL_EXPORT=1");

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
			}
		);
	}
}
