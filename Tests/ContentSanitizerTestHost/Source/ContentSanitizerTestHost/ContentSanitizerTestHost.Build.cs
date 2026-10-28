using UnrealBuildTool;

public class ContentSanitizerTestHost : ModuleRules
{
    public ContentSanitizerTestHost(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
    }
}
