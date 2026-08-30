#include "Scanner/SanitizerScanService.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture2D.h"
#include "Providers/Texture2DFingerprintProvider.h"

FSanitizerScanService::FSanitizerScanService()
{
    Providers.Add(MakeShared<FTexture2DFingerprintProvider>());
}

void FSanitizerScanService::RequestCancel() { bCancelRequested = true; }

const IAssetFingerprintProvider* FSanitizerScanService::FindProvider(const FAssetData& AssetData) const
{
    for (const TSharedRef<IAssetFingerprintProvider>& Provider : Providers)
    {
        if (Provider->Supports(AssetData)) { return &Provider.Get(); }
    }
    return nullptr;
}

FSanitizerScanResult FSanitizerScanService::Scan(const FSanitizerScanRequest& Request)
{
    FSanitizerScanResult Result;
    Result.Revision = NextRevision++;
    bCancelRequested = false;
    Result.State = ESanitizerSessionState::Inventory;

    FARFilter Filter;
    Filter.PackagePaths = Request.PackagePaths;
    Filter.ClassPaths.Add(UTexture2D::StaticClass()->GetClassPathName());
    Filter.bRecursivePaths = true;
    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    TArray<FAssetData> Assets;
    Registry.GetAssets(Filter, Assets);
    Assets.Sort([](const FAssetData& A, const FAssetData& B) { return A.GetObjectPathString() < B.GetObjectPathString(); });

    TMap<FString, TArray<FSanitizerAssetRecord>> Buckets;
    Result.State = ESanitizerSessionState::Bucketing;
    for (const FAssetData& Asset : Assets)
    {
        if (bCancelRequested) { Result.State = ESanitizerSessionState::Canceled; return Result; }
        if (!Request.bIncludeDeveloperContent && Asset.PackagePath.ToString().StartsWith(TEXT("/Game/Developers"))) { continue; }
        bool bExcluded = false;
        for (const FName& Exclusion : Request.ExcludedPackagePaths)
        {
            if (Asset.PackagePath.ToString().StartsWith(Exclusion.ToString())) { bExcluded = true; break; }
        }
        if (bExcluded) { continue; }
        const IAssetFingerprintProvider* Provider = FindProvider(Asset);
        if (!Provider) { continue; }
        FSanitizerCheapFingerprint Cheap;
        FString Error;
        if (!Provider->BuildCheapFingerprint(Asset, Cheap, Error)) { Result.Errors.Add(Error); continue; }
        FSanitizerAssetRecord& Record = Buckets.FindOrAdd(Cheap.Key).AddDefaulted_GetRef();
        Record.AssetData = Asset;
        Record.PackageName = Asset.PackageName;
        Record.PackagePath = Asset.PackagePath;
        if (const TOptional<FAssetPackageData> PackageData = Registry.GetAssetPackageDataCopy(Asset.PackageName))
        {
            Record.EstimatedDiskSize = PackageData.GetValue().DiskSize;
        }
        ++Result.Summary.InventoriedAssets;
    }

    Result.State = ESanitizerSessionState::Fingerprinting;
    for (TPair<FString, TArray<FSanitizerAssetRecord>>& Bucket : Buckets)
    {
        if (bCancelRequested) { Result.State = ESanitizerSessionState::Canceled; return Result; }
        if (Bucket.Value.Num() < 2) { continue; }
        Result.Summary.CandidateAssets += Bucket.Value.Num();
        TMap<FString, FSanitizerDuplicateGroup> GroupsByPayload;
        for (const FSanitizerAssetRecord& Record : Bucket.Value)
        {
            UObject* Asset = Record.AssetData.GetAsset();
            FSanitizerFingerprint Fingerprint;
            FString Error;
            const IAssetFingerprintProvider* Provider = FindProvider(Record.AssetData);
            if (!Provider || !Provider->BuildDeepFingerprint(Asset, Fingerprint, Error))
            {
                Result.Errors.Add(FString::Printf(TEXT("%s: %s"), *Record.GetObjectPath(), *Error));
                continue;
            }
            FSanitizerDuplicateGroup& Group = GroupsByPayload.FindOrAdd(LexToString(Fingerprint.PayloadHash));
            Group.ProviderId = Provider->GetProviderId();
            Group.Members.Add({ Record, MoveTemp(Fingerprint) });
        }
        for (TPair<FString, FSanitizerDuplicateGroup>& Pair : GroupsByPayload)
        {
            FSanitizerDuplicateGroup& Group = Pair.Value;
            if (Group.Members.Num() < 2) { continue; }
            Group.Members.Sort([](const FSanitizerDuplicateMember& A, const FSanitizerDuplicateMember& B) { return A.Record.GetObjectPath() < B.Record.GetObjectPath(); });
            Group.GroupId = FString::Printf(TEXT("%s:%s"), *Group.ProviderId.ToString(), *Pair.Key);
            Group.CanonicalMemberIndex = 0;
            const FIoHash SettingsHash = Group.Members[0].Fingerprint.SettingsHash;
            Group.Classification = ESanitizerClassification::SafeDuplicate;
            for (int32 Index = 1; Index < Group.Members.Num(); ++Index)
            {
                if (Group.Members[Index].Fingerprint.SettingsHash != SettingsHash)
                {
                    Group.Classification = ESanitizerClassification::ReviewRequired;
                    Group.Reasons.Add(TEXT("Payload matches, but behavior-relevant texture settings differ."));
                    break;
                }
            }
            for (int32 Index = 1; Index < Group.Members.Num(); ++Index) { Group.EstimatedReclaimableSize += Group.Members[Index].Record.EstimatedDiskSize; }
            ++Result.Summary.DuplicateGroups;
            Result.Summary.EstimatedReclaimableSize += Group.EstimatedReclaimableSize;
            if (Group.Classification == ESanitizerClassification::SafeDuplicate) { ++Result.Summary.SafeGroups; }
            else { ++Result.Summary.ReviewGroups; }
            Result.Groups.Add(MoveTemp(Group));
        }
    }
    Result.Groups.Sort([](const FSanitizerDuplicateGroup& A, const FSanitizerDuplicateGroup& B) { return A.GroupId < B.GroupId; });
    Result.State = ESanitizerSessionState::Completed;
    return Result;
}
