using UnrealBuildTool;

public class ContentSanitizerTestHostEditorTarget : TargetRules
{
    public ContentSanitizerTestHostEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        bBuildAllModules = false;
        ExtraModuleNames.Add("ContentSanitizerTestHost");
    }
}
