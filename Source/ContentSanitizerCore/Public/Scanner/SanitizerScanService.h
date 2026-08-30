#pragma once

#include "Model/SanitizerTypes.h"
#include "Providers/AssetFingerprintProvider.h"

struct FSanitizerScanRequest
{
    TArray<FName> PackagePaths { FName(TEXT("/Game")) };
    TArray<FName> ExcludedPackagePaths { FName(TEXT("/Game/Developers")) };
    bool bIncludePluginContent = false;
    bool bIncludeDeveloperContent = false;
};

struct FSanitizerScanResult
{
    ESanitizerSessionState State = ESanitizerSessionState::Idle;
    uint64 Revision = 0;
    FSanitizerScanSummary Summary;
    TArray<FSanitizerDuplicateGroup> Groups;
    TArray<FString> Errors;
};

class CONTENTSANITIZERCORE_API FSanitizerScanService
{
public:
    FSanitizerScanService();
    void RequestCancel();
    FSanitizerScanResult Scan(const FSanitizerScanRequest& Request);

private:
    TAtomic<bool> bCancelRequested { false };
    uint64 NextRevision = 1;
    TArray<TSharedRef<IAssetFingerprintProvider>> Providers;
    const IAssetFingerprintProvider* FindProvider(const FAssetData& AssetData) const;
};
