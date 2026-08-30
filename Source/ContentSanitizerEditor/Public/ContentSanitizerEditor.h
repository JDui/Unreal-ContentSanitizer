#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FContentSanitizerEditorModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
private:
    TSharedRef<class SDockTab> SpawnTab(const class FSpawnTabArgs& Args);
    void RegisterMenus();
    void PopulateFolderContextMenu(class UToolMenu* Menu);
    void OpenForPackagePaths(TArray<FString> PackagePaths);
    TWeakPtr<class SContentSanitizerPanel> PanelWidget;
    static const FName TabName;
};
