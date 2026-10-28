using UnrealBuildTool;

public class ContentSanitizerEditor : ModuleRules
{
    public ContentSanitizerEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "ContentSanitizerCore", "Core", "CoreUObject", "Engine", "Slate", "SlateCore", "InputCore" });
        PrivateDependencyModuleNames.AddRange(new[] { "UnrealEd", "ToolMenus", "ContentBrowser", "AssetRegistry", "Projects", "MessageLog", "SourceControl", "EditorSubsystem" });
    }
}
