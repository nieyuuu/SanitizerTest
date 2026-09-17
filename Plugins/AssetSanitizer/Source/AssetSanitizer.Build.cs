using UnrealBuildTool;

public class AssetSanitizer : ModuleRules
{
    public AssetSanitizer(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicIncludePaths.AddRange(new string[] { });
        PrivateIncludePaths.AddRange(new string[] { });

        PublicDependencyModuleNames.AddRange(new string[] { "Core" });
        PrivateDependencyModuleNames.AddRange(new string[] { "CoreUObject", "Engine", "Slate", "SlateCore", "AssetRegistry",
            "MeshDescription", "StaticMeshDescription","DeveloperSettings", "EditorStyle",
            "ContentBrowser", "InputCore", "DesktopPlatform", "UnrealEd", "ToolMenus", "AssetTools" });

        DynamicallyLoadedModuleNames.AddRange(new string[] { });
    }
}
