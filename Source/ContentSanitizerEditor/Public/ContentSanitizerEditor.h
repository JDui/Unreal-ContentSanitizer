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
    static const FName TabName;
};
