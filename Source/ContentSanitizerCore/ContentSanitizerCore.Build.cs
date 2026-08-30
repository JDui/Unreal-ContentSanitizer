using UnrealBuildTool;

public class ContentSanitizerCore : ModuleRules
{
    public ContentSanitizerCore(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "AssetRegistry" });
        PrivateDependencyModuleNames.AddRange(new[] { "Projects" });
    }
}
