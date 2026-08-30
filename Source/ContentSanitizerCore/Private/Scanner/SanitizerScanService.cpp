#include "Scanner/SanitizerScanService.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Cache/SanitizerFingerprintCache.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/PackageName.h"
#include "Providers/Texture2DFingerprintProvider.h"

struct FSanitizerScanSession
{
    FSanitizerScanRequest Request;
    FSanitizerScanResult Result;
    FSanitizerScanProgress Progress;
    TArray<FAssetData> InventoryAssets;
    int32 InventoryIndex = 0;
    TMap<FString, TArray<FSanitizerAssetRecord>> Buckets;
    TArray<FSanitizerAssetRecord> Candidates;
    int32 CandidateRecordIndex = 0;
    TMap<FString, FSanitizerDuplicateGroup> GroupsByPayload;
    TUniquePtr<FSanitizerFingerprintCache> Cache;
    TSet<FString> SeenObjectPaths;
    bool bCacheDirty = false;
};

FSanitizerScanService::FSanitizerScanService()
{
    Providers.Add(MakeShared<FTexture2DFingerprintProvider>());
}

FSanitizerScanService::~FSanitizerScanService() = default;

namespace ContentSanitizerScan
{
    static bool IsPathWithin(const FString& PackagePath, const FString& RootPath)
    {
        return PackagePath == RootPath || PackagePath.StartsWith(RootPath + TEXT("/"));
    }

    static bool PreferAsCanonical(const FSanitizerDuplicateMember& Left, const FSanitizerDuplicateMember& Right)
    {
        const int32 LeftReferences = Left.Record.HardReferenceCount + Left.Record.SoftReferenceCount;
        const int32 RightReferences = Right.Record.HardReferenceCount + Right.Record.SoftReferenceCount;
        if (LeftReferences != RightReferences) { return LeftReferences > RightReferences; }
        if (Left.Record.HardReferenceCount != Right.Record.HardReferenceCount) { return Left.Record.HardReferenceCount > Right.Record.HardReferenceCount; }
        const FString LeftPath = Left.Record.GetObjectPath();
        const FString RightPath = Right.Record.GetObjectPath();
        if (LeftPath.Len() != RightPath.Len()) { return LeftPath.Len() < RightPath.Len(); }
        return LeftPath < RightPath;
    }

    static bool IsActiveState(ESanitizerSessionState State)
    {
        return State == ESanitizerSessionState::Inventory || State == ESanitizerSessionState::Bucketing || State == ESanitizerSessionState::CandidateExtraction || State == ESanitizerSessionState::Fingerprinting || State == ESanitizerSessionState::Classification || State == ESanitizerSessionState::CancelRequested;
    }

    static FString BuildChangeIdentity(const FAssetData& AssetData, const TOptional<FAssetPackageData>& PackageData)
    {
        if (UObject* LoadedAsset = AssetData.FastGetAsset(false))
        {
            if (UPackage* Package = LoadedAsset->GetOutermost(); Package && Package->IsDirty()) { return FString(); }
        }
#if WITH_EDITORONLY_DATA
        if (PackageData.IsSet())
        {
            const FIoHash SavedHash = PackageData.GetValue().GetPackageSavedHash();
            if (!SavedHash.IsZero())
            {
                return FString::Printf(TEXT("PackageSavedHash:%s:%lld"), *LexToString(SavedHash), PackageData.GetValue().DiskSize);
            }
        }
#endif
        FString Filename;
        if (!FPackageName::DoesPackageExist(AssetData.PackageName.ToString(), &Filename)) { return FString(); }
        const int64 FileSize = IFileManager::Get().FileSize(*Filename);
        const FDateTime Timestamp = IFileManager::Get().GetTimeStamp(*Filename);
        return FString::Printf(TEXT("FileStat:%lld:%lld"), FileSize, Timestamp.GetTicks());
    }

    static void SaveCache(FSanitizerScanSession& Session)
    {
        if (!Session.Cache || !Session.bCacheDirty) { return; }
        FString Error;
        if (!Session.Cache->Save(Error)) { Session.Result.CacheMessages.Add(MoveTemp(Error)); }
        Session.bCacheDirty = false;
    }

    void BuildUniqueCandidateRecords(const TMap<FString, TArray<FSanitizerAssetRecord>>& Buckets, TArray<FSanitizerAssetRecord>& OutCandidates)
    {
        OutCandidates.Reset();
        const bool bHasWildcard = Buckets.Contains(TEXT("Texture2D|*"));
        TSet<FString> CandidateObjectPaths;
        for (const TPair<FString, TArray<FSanitizerAssetRecord>>& Bucket : Buckets)
        {
            if (!bHasWildcard && Bucket.Value.Num() < 2) { continue; }
            for (const FSanitizerAssetRecord& Record : Bucket.Value)
            {
                const FString ObjectPath = Record.GetObjectPath();
                if (!CandidateObjectPaths.Contains(ObjectPath))
                {
                    CandidateObjectPaths.Add(ObjectPath);
                    OutCandidates.Add(Record);
                }
            }
        }
        OutCandidates.Sort([](const FSanitizerAssetRecord& A, const FSanitizerAssetRecord& B)
        {
            return A.GetObjectPath() < B.GetObjectPath();
        });
    }

    int64 CalculateSafeReclaimableSize(const FSanitizerDuplicateGroup& Group)
    {
        if (Group.Classification != ESanitizerClassification::SafeDuplicate || !Group.Members.IsValidIndex(Group.CanonicalMemberIndex)) { return 0; }
        int64 Total = 0;
        for (int32 Index = 0; Index < Group.Members.Num(); ++Index)
        {
            if (Index != Group.CanonicalMemberIndex) { Total += FMath::Max<int64>(0, Group.Members[Index].Record.EstimatedDiskSize); }
        }
        return Total;
    }
}

void FSanitizerScanService::RequestCancel()
{
    bCancelRequested.Store(true);
    if (Session && ContentSanitizerScan::IsActiveState(Session->Result.State))
    {
        Session->Result.State = ESanitizerSessionState::CancelRequested;
        Session->Progress.State = ESanitizerSessionState::CancelRequested;
    }
}

const IAssetFingerprintProvider* FSanitizerScanService::FindProvider(const FAssetData& AssetData) const
{
    for (const TSharedRef<IAssetFingerprintProvider>& Provider : Providers)
    {
        if (Provider->Supports(AssetData)) { return &Provider.Get(); }
    }
    return nullptr;
}

void FSanitizerScanService::BeginScan(const FSanitizerScanRequest& Request)
{
    Session = MakeUnique<FSanitizerScanSession>();
    Session->Request = Request;
    Session->Result.Revision = NextRevision++;
    bCancelRequested.Store(false);
    Session->Result.State = ESanitizerSessionState::Inventory;
    Session->Progress.State = ESanitizerSessionState::Inventory;
    Session->Cache = Request.FingerprintCacheFilename.IsEmpty()
        ? MakeUnique<FSanitizerFingerprintCache>()
        : MakeUnique<FSanitizerFingerprintCache>(Request.FingerprintCacheFilename);
    FString CacheError;
    if (!Session->Cache->Load(CacheError))
    {
        Session->Result.CacheMessages.Add(MoveTemp(CacheError));
        Session->bCacheDirty = true;
    }

    FARFilter Filter;
    Filter.PackagePaths = Request.PackagePaths;
    Filter.ClassPaths.Add(UTexture2D::StaticClass()->GetClassPathName());
    Filter.bRecursivePaths = true;
    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    Registry.GetAssets(Filter, Session->InventoryAssets);
    Session->InventoryAssets.Sort([](const FAssetData& A, const FAssetData& B) { return A.GetObjectPathString() < B.GetObjectPathString(); });
    Session->Result.State = ESanitizerSessionState::Bucketing;
    Session->Progress.State = ESanitizerSessionState::Bucketing;
    Session->Progress.TotalAssets = Session->InventoryAssets.Num();
}

bool FSanitizerScanService::TickScan(double TimeBudgetSeconds, int32 MaxAssetsPerTick)
{
    if (!Session || !ContentSanitizerScan::IsActiveState(Session->Result.State)) { return false; }
    if (bCancelRequested.Load())
    {
        Session->Result.State = ESanitizerSessionState::Canceled;
        Session->Progress.State = ESanitizerSessionState::Canceled;
        Session->Progress.CurrentAsset.Reset();
        return false;
    }

    const double StartTime = FPlatformTime::Seconds();
    int32 ProcessedThisTick = 0;
    const int32 SafeMaxAssets = FMath::Max(1, MaxAssetsPerTick);
    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

    while (ProcessedThisTick < SafeMaxAssets)
    {
        if (TimeBudgetSeconds > 0.0 && ProcessedThisTick > 0 && FPlatformTime::Seconds() - StartTime >= TimeBudgetSeconds) { break; }
        if (bCancelRequested.Load())
        {
            Session->Result.State = ESanitizerSessionState::Canceled;
            Session->Progress.State = ESanitizerSessionState::Canceled;
            Session->Progress.CurrentAsset.Reset();
            return false;
        }

        if (Session->Result.State == ESanitizerSessionState::Bucketing)
        {
            if (Session->InventoryIndex < Session->InventoryAssets.Num())
            {
                const FAssetData& Asset = Session->InventoryAssets[Session->InventoryIndex++];
                Session->Progress.CurrentAsset = Asset.GetObjectPathString();
                Session->Progress.ProcessedAssets = Session->InventoryIndex;
                ++ProcessedThisTick;

                const FString AssetPackagePath = Asset.PackagePath.ToString();
                if (!Session->Request.bIncludePluginContent && !ContentSanitizerScan::IsPathWithin(AssetPackagePath, TEXT("/Game"))) { continue; }
                if (!Session->Request.bIncludeDeveloperContent && ContentSanitizerScan::IsPathWithin(AssetPackagePath, TEXT("/Game/Developers"))) { continue; }
                bool bExcluded = false;
                for (const FName& Exclusion : Session->Request.ExcludedPackagePaths)
                {
                    if (ContentSanitizerScan::IsPathWithin(AssetPackagePath, Exclusion.ToString())) { bExcluded = true; break; }
                }
                if (bExcluded) { continue; }
                const IAssetFingerprintProvider* Provider = FindProvider(Asset);
                if (!Provider) { continue; }
                Session->SeenObjectPaths.Add(Asset.GetObjectPathString());
                FSanitizerCheapFingerprint Cheap;
                FString Error;
                if (!Provider->BuildCheapFingerprint(Asset, Cheap, Error))
                {
                    Session->Result.Errors.Add(FString::Printf(TEXT("%s：%s"), *Asset.GetObjectPathString(), *Error));
                    continue;
                }
                FSanitizerAssetRecord& Record = Session->Buckets.FindOrAdd(Cheap.Key).AddDefaulted_GetRef();
                Record.AssetData = Asset;
                Record.PackageName = Asset.PackageName;
                Record.PackagePath = Asset.PackagePath;
                const TOptional<FAssetPackageData> PackageData = Registry.GetAssetPackageDataCopy(Asset.PackageName);
                if (PackageData.IsSet())
                {
                    Record.EstimatedDiskSize = PackageData.GetValue().DiskSize;
                }
                Record.ChangeIdentity = ContentSanitizerScan::BuildChangeIdentity(Asset, PackageData);
                ++Session->Result.Summary.InventoriedAssets;
                continue;
            }

            ContentSanitizerScan::BuildUniqueCandidateRecords(Session->Buckets, Session->Candidates);
            Session->Result.Summary.CandidateAssets = Session->Candidates.Num();
            Session->Result.State = ESanitizerSessionState::Fingerprinting;
            Session->Progress.State = ESanitizerSessionState::Fingerprinting;
            Session->Progress.ProcessedAssets = 0;
            Session->Progress.TotalAssets = Session->Result.Summary.CandidateAssets;
            Session->Progress.CurrentAsset.Reset();
            continue;
        }

        if (Session->Result.State == ESanitizerSessionState::Fingerprinting)
        {
            if (Session->CandidateRecordIndex >= Session->Candidates.Num())
            {
                for (TPair<FString, FSanitizerDuplicateGroup>& Pair : Session->GroupsByPayload)
                {
                    FSanitizerDuplicateGroup& Group = Pair.Value;
                    if (Group.Members.Num() < 2) { continue; }
                    Group.Members.Sort(&ContentSanitizerScan::PreferAsCanonical);
                    Group.GroupId = Pair.Key;
                    Group.CanonicalMemberIndex = 0;
                    const FIoHash SettingsHash = Group.Members[0].Fingerprint.SettingsHash;
                    Group.Classification = ESanitizerClassification::SafeDuplicate;
                    for (int32 Index = 1; Index < Group.Members.Num(); ++Index)
                    {
                        if (Group.Members[Index].Fingerprint.SettingsHash != SettingsHash)
                        {
                            Group.Classification = ESanitizerClassification::ReviewRequired;
                            Group.Reasons.Add(TEXT("源数据相同，但会影响行为的纹理设置不同。"));
                        }
                    }
                    if (Group.Classification == ESanitizerClassification::SafeDuplicate)
                    {
                        Group.Reasons.Add(TEXT("所有源数据块、图层、Mip 和行为设置均已验证一致。"));
                        Group.EstimatedReclaimableSize = ContentSanitizerScan::CalculateSafeReclaimableSize(Group);
                        Session->Result.Summary.EstimatedReclaimableSize += Group.EstimatedReclaimableSize;
                        ++Session->Result.Summary.SafeGroups;
                    }
                    else
                    {
                        ++Session->Result.Summary.ReviewGroups;
                    }
                    ++Session->Result.Summary.DuplicateGroups;
                    Session->Result.Groups.Add(MoveTemp(Group));
                }
                if (Session->Cache->RemoveMissingInScope(Session->Request.PackagePaths, Session->SeenObjectPaths) > 0) { Session->bCacheDirty = true; }
                ContentSanitizerScan::SaveCache(*Session);
                Session->Result.Groups.Sort([](const FSanitizerDuplicateGroup& A, const FSanitizerDuplicateGroup& B) { return A.GroupId < B.GroupId; });
                Session->Result.State = ESanitizerSessionState::Completed;
                Session->Progress.State = ESanitizerSessionState::Completed;
                Session->Progress.ProcessedAssets = Session->Progress.TotalAssets;
                Session->Progress.CurrentAsset.Reset();
                return false;
            }

            {
                FSanitizerAssetRecord& Record = Session->Candidates[Session->CandidateRecordIndex++];
                Session->Progress.CurrentAsset = Record.GetObjectPath();
                ++Session->Progress.ProcessedAssets;
                ++ProcessedThisTick;

                TArray<FName> HardReferencers;
                TArray<FName> SoftReferencers;
                Registry.GetReferencers(Record.PackageName, HardReferencers, UE::AssetRegistry::EDependencyCategory::Package, UE::AssetRegistry::EDependencyQuery::Hard);
                Registry.GetReferencers(Record.PackageName, SoftReferencers, UE::AssetRegistry::EDependencyCategory::Package, UE::AssetRegistry::EDependencyQuery::Soft);
                Record.HardReferenceCount = HardReferencers.Num();
                Record.SoftReferenceCount = SoftReferencers.Num();

                FSanitizerFingerprint Fingerprint;
                FString Error;
                const IAssetFingerprintProvider* Provider = FindProvider(Record.AssetData);
                if (!Provider)
                {
                    Session->Result.Errors.Add(FString::Printf(TEXT("%s：没有可用的指纹提供器。"), *Record.GetObjectPath()));
                    continue;
                }
                const FSanitizerFingerprintCacheEntry* CachedEntry = Session->Cache->FindValid(Record.GetObjectPath(), Provider->GetProviderId(), Provider->GetSchemaVersion(), Record.ChangeIdentity);
                if (CachedEntry)
                {
                    Fingerprint = CachedEntry->Fingerprint;
                    ++Session->Result.Summary.CachedFingerprints;
                }
                else
                {
                    UObject* Asset = Record.AssetData.GetAsset();
                    if (!Provider->BuildDeepFingerprint(Asset, Fingerprint, Error))
                    {
                        Session->Result.Errors.Add(FString::Printf(TEXT("%s：%s"), *Record.GetObjectPath(), *Error));
                        continue;
                    }
                    Fingerprint.bDeepVerified = true;
                    ++Session->Result.Summary.IncrementalFingerprints;
                    if (!Record.ChangeIdentity.IsEmpty())
                    {
                        FSanitizerFingerprintCacheEntry Entry;
                        Entry.ObjectPath = Record.GetObjectPath();
                        Entry.PackageName = Record.PackageName;
                        Entry.ProviderId = Provider->GetProviderId();
                        Entry.ChangeIdentity = Record.ChangeIdentity;
                        Entry.Fingerprint = Fingerprint;
                        Session->Cache->Upsert(MoveTemp(Entry));
                        Session->bCacheDirty = true;
                    }
                }
                Fingerprint.bDeepVerified = true;
                const FString PayloadGroupKey = FString::Printf(TEXT("%s:%s"), *Provider->GetProviderId().ToString(), *LexToString(Fingerprint.PayloadHash));
                FSanitizerDuplicateGroup& Group = Session->GroupsByPayload.FindOrAdd(PayloadGroupKey);
                Group.ProviderId = Provider->GetProviderId();
                Group.Members.Add({ Record, MoveTemp(Fingerprint) });
                continue;
            }
        }
    }
    return IsScanActive();
}

bool FSanitizerScanService::IsScanActive() const
{
    return Session && ContentSanitizerScan::IsActiveState(Session->Result.State);
}

const FSanitizerScanProgress& FSanitizerScanService::GetProgress() const
{
    static const FSanitizerScanProgress EmptyProgress;
    return Session ? Session->Progress : EmptyProgress;
}

const FSanitizerScanResult& FSanitizerScanService::GetResult() const
{
    static const FSanitizerScanResult EmptyResult;
    return Session ? Session->Result : EmptyResult;
}

FSanitizerScanResult FSanitizerScanService::Scan(const FSanitizerScanRequest& Request)
{
    BeginScan(Request);
    while (IsScanActive()) { TickScan(0.0, MAX_int32); }
    return GetResult();
}
