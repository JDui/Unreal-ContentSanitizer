#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FContentSanitizerEditorModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
    void OpenForPackagePaths(TArray<FString> PackagePaths);
    void OpenOrphanCleanerForPackagePaths(TArray<FString> PackagePaths);

private:
    TSharedRef<class SDockTab> SpawnTab(const class FSpawnTabArgs& Args);
    void RegisterMenus();
    void PopulateFolderContextMenu(class UToolMenu* Menu);
    TWeakPtr<class SContentSanitizerPanel> PanelWidget;
    static const FName TabName;
};
