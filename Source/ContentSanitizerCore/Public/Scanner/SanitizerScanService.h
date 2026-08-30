#pragma once

#include "Model/SanitizerTypes.h"
#include "Providers/AssetFingerprintProvider.h"

struct FSanitizerScanRequest
{
    TArray<FName> PackagePaths { FName(TEXT("/Game")) };
    TArray<FName> ExcludedPackagePaths { FName(TEXT("/Game/Developers")) };
    FString FingerprintCacheFilename;
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
    TArray<FString> CacheMessages;
};

struct FSanitizerScanProgress
{
    ESanitizerSessionState State = ESanitizerSessionState::Idle;
    int32 ProcessedAssets = 0;
    int32 TotalAssets = 0;
    FString CurrentAsset;

    float GetFraction() const { return TotalAssets > 0 ? FMath::Clamp(static_cast<float>(ProcessedAssets) / static_cast<float>(TotalAssets), 0.0f, 1.0f) : 0.0f; }
};

struct FSanitizerScanSession;

namespace ContentSanitizerScan
{
    CONTENTSANITIZERCORE_API void BuildUniqueCandidateRecords(const TMap<FString, TArray<FSanitizerAssetRecord>>& Buckets, TArray<FSanitizerAssetRecord>& OutCandidates);
    CONTENTSANITIZERCORE_API int64 CalculateSafeReclaimableSize(const FSanitizerDuplicateGroup& Group);
}

class CONTENTSANITIZERCORE_API FSanitizerScanService
{
public:
    FSanitizerScanService();
    ~FSanitizerScanService();
    void BeginScan(const FSanitizerScanRequest& Request);
    bool TickScan(double TimeBudgetSeconds = 0.008, int32 MaxAssetsPerTick = 1);
    void RequestCancel();
    bool IsScanActive() const;
    const FSanitizerScanProgress& GetProgress() const;
    const FSanitizerScanResult& GetResult() const;
    FSanitizerScanResult Scan(const FSanitizerScanRequest& Request);

private:
    TAtomic<bool> bCancelRequested { false };
    uint64 NextRevision = 1;
    TArray<TSharedRef<IAssetFingerprintProvider>> Providers;
    TUniquePtr<FSanitizerScanSession> Session;
    const IAssetFingerprintProvider* FindProvider(const FAssetData& AssetData) const;
};
