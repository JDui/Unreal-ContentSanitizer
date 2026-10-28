#include "ContentSanitizerEditor.h"

#include "ContentSanitizerCore.h"
#include "ContentBrowserMenuContexts.h"
#include "UI/SContentSanitizerPanel.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Framework/Docking/TabManager.h"

const FName FContentSanitizerEditorModule::TabName(TEXT("ContentSanitizer"));
IMPLEMENT_MODULE(FContentSanitizerEditorModule, ContentSanitizerEditor)

void FContentSanitizerEditorModule::StartupModule()
{
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TabName, FOnSpawnTab::CreateRaw(this, &FContentSanitizerEditorModule::SpawnTab))
        .SetDisplayName(NSLOCTEXT("ContentSanitizer", "TabTitle", "内容整理器"))
        .SetMenuType(ETabSpawnerMenuType::Hidden);
    UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FContentSanitizerEditorModule::RegisterMenus));
}

void FContentSanitizerEditorModule::ShutdownModule()
{
    UToolMenus::UnRegisterStartupCallback(this);
    UToolMenus::UnregisterOwner(this);
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabName);
}

TSharedRef<SDockTab> FContentSanitizerEditorModule::SpawnTab(const FSpawnTabArgs& Args)
{
    TSharedRef<SContentSanitizerPanel> Panel = SNew(SContentSanitizerPanel);
    PanelWidget = Panel;
    return SNew(SDockTab).TabRole(ETabRole::NomadTab)[Panel];
}

void FContentSanitizerEditorModule::RegisterMenus()
{
    FToolMenuOwnerScoped Owner(this);
    UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Window"));
    FToolMenuSection& Section = Menu->FindOrAddSection(TEXT("WindowLayout"));
    Section.AddMenuEntry(
        TEXT("ContentSanitizer.Open"),
        NSLOCTEXT("ContentSanitizer", "OpenLabel", "内容整理器"),
        NSLOCTEXT("ContentSanitizer", "OpenTip", "打开内容整理器工具。"),
        FSlateIcon(),
        FUIAction(FExecuteAction::CreateLambda([] { FGlobalTabmanager::Get()->TryInvokeTab(TabName); })));

    UToolMenu* FolderMenu = UToolMenus::Get()->ExtendMenu(TEXT("ContentBrowser.FolderContextMenu"));
    FolderMenu->AddDynamicSection(TEXT("ContentSanitizer.FolderActions"), FNewToolMenuDelegate::CreateRaw(this, &FContentSanitizerEditorModule::PopulateFolderContextMenu));
}

void FContentSanitizerEditorModule::PopulateFolderContextMenu(UToolMenu* Menu)
{
    const UContentBrowserFolderContext* Context = Menu->FindContext<UContentBrowserFolderContext>();
    if (!Context || Context->GetSelectedPackagePaths().IsEmpty()) { return; }

    const TArray<FString> PackagePaths = Context->GetSelectedPackagePaths();
    FToolMenuSection& Section = Menu->FindOrAddSection(TEXT("ContentSanitizer"), NSLOCTEXT("ContentSanitizer", "FolderSection", "内容整理"));
    Section.AddMenuEntry(
        TEXT("ContentSanitizer.CleanDuplicateContent"),
        NSLOCTEXT("ContentSanitizer", "FolderCleanLabel", "整理此目录的重复内容"),
        NSLOCTEXT("ContentSanitizer", "FolderCleanTip", "将所选目录设为清理目标并打开工具。不会自动扫描；开始扫描后会与整个 /Game 比较，只有目标目录内资产允许被整理。"),
        FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Find"),
        FUIAction(FExecuteAction::CreateLambda([this, PackagePaths]() { OpenForPackagePaths(PackagePaths); })));

    Section.AddMenuEntry(
        TEXT("ContentSanitizer.CleanOrphanContent"),
        NSLOCTEXT("ContentSanitizer", "FolderCleanOrphanLabel", "清理孤岛素材"),
        NSLOCTEXT("ContentSanitizer", "FolderCleanOrphanTip", "将所选目录设为清理目标并打开工具，不会自动扫描或删除。显式扫描后可审查贴图、材质和网格体的外部引用候选；材质绑定仅作元数据。"),
        FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Delete"),
        FUIAction(FExecuteAction::CreateLambda([this, PackagePaths]() { OpenOrphanCleanerForPackagePaths(PackagePaths); })));
}

void FContentSanitizerEditorModule::OpenForPackagePaths(TArray<FString> PackagePaths)
{
    FGlobalTabmanager::Get()->TryInvokeTab(TabName);
    if (const TSharedPtr<SContentSanitizerPanel> Panel = PanelWidget.Pin())
    {
        Panel->SetCleanupPackagePaths(PackagePaths);
    }
}

void FContentSanitizerEditorModule::OpenOrphanCleanerForPackagePaths(TArray<FString> PackagePaths)
{
    FGlobalTabmanager::Get()->TryInvokeTab(TabName);
    if (const TSharedPtr<SContentSanitizerPanel> Panel = PanelWidget.Pin())
    {
        Panel->SetOrphanCleanupPackagePaths(PackagePaths);
    }
}
