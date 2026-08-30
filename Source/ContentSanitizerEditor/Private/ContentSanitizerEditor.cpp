#include "ContentSanitizerEditor.h"

#include "ContentSanitizerCore.h"
#include "UI/SContentSanitizerPanel.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Framework/Docking/TabManager.h"

const FName FContentSanitizerEditorModule::TabName(TEXT("ContentSanitizer"));
IMPLEMENT_MODULE(FContentSanitizerEditorModule, ContentSanitizerEditor)

void FContentSanitizerEditorModule::StartupModule()
{
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TabName, FOnSpawnTab::CreateRaw(this, &FContentSanitizerEditorModule::SpawnTab))
        .SetDisplayName(NSLOCTEXT("ContentSanitizer", "TabTitle", "Content Sanitizer"))
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
    return SNew(SDockTab).TabRole(ETabRole::NomadTab)[SNew(SContentSanitizerPanel)];
}

void FContentSanitizerEditorModule::RegisterMenus()
{
    FToolMenuOwnerScoped Owner(this);
    UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Window"));
    FToolMenuSection& Section = Menu->FindOrAddSection(TEXT("WindowLayout"));
    Section.AddMenuEntry(TEXT("ContentSanitizer.Open"), NSLOCTEXT("ContentSanitizer", "OpenLabel", "Content Sanitizer"), NSLOCTEXT("ContentSanitizer", "OpenTip", "Open the Content Sanitizer tab."), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([] { FGlobalTabmanager::Get()->TryInvokeTab(TabName); })));
}
