using UnrealBuildTool;

public class AssetSanitizer : ModuleRules
{
    public AssetSanitizer(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicIncludePaths.AddRange(new string[] { });
        PrivateIncludePaths.AddRange(new string[] { });

        PublicDependencyModuleNames.AddRange(new string[] { "Core", "DeveloperSettings", "Json", "JsonUtilities" });
        PrivateDependencyModuleNames.AddRange(new string[] { "CoreUObject", "Engine", "Slate", "SlateCore", "AssetRegistry",
            "MeshDescription", "StaticMeshDescription" });

        DynamicallyLoadedModuleNames.AddRange(new string[] { });
    }
}
