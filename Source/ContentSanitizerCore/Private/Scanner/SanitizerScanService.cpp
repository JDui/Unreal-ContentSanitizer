#include "Scanner/SanitizerScanService.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture2D.h"
#include "Providers/Texture2DFingerprintProvider.h"

FSanitizerScanService::FSanitizerScanService()
{
    Providers.Add(MakeShared<FTexture2DFingerprintProvider>());
}

void FSanitizerScanService::RequestCancel() { bCancelRequested.Store(true); }

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
}

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
    bCancelRequested.Store(false);
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
        if (bCancelRequested.Load()) { Result.State = ESanitizerSessionState::Canceled; return Result; }
        const FString AssetPackagePath = Asset.PackagePath.ToString();
        if (!Request.bIncludePluginContent && !ContentSanitizerScan::IsPathWithin(AssetPackagePath, TEXT("/Game"))) { continue; }
        if (!Request.bIncludeDeveloperContent && ContentSanitizerScan::IsPathWithin(AssetPackagePath, TEXT("/Game/Developers"))) { continue; }
        bool bExcluded = false;
        for (const FName& Exclusion : Request.ExcludedPackagePaths)
        {
            if (ContentSanitizerScan::IsPathWithin(AssetPackagePath, Exclusion.ToString())) { bExcluded = true; break; }
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
        TArray<FName> HardReferencers;
        TArray<FName> SoftReferencers;
        Registry.GetReferencers(Asset.PackageName, HardReferencers, UE::AssetRegistry::EDependencyCategory::Package, UE::AssetRegistry::EDependencyQuery::Hard);
        Registry.GetReferencers(Asset.PackageName, SoftReferencers, UE::AssetRegistry::EDependencyCategory::Package, UE::AssetRegistry::EDependencyQuery::Soft);
        Record.HardReferenceCount = HardReferencers.Num();
        Record.SoftReferenceCount = SoftReferencers.Num();
        ++Result.Summary.InventoriedAssets;
    }

    if (TArray<FSanitizerAssetRecord>* WildcardRecords = Buckets.Find(TEXT("Texture2D|*")))
    {
        const TArray<FSanitizerAssetRecord> Wildcards = *WildcardRecords;
        if (Buckets.Num() == 1)
        {
            // Multiple wildcard records remain comparable to each other.
        }
        else
        {
            Buckets.Remove(TEXT("Texture2D|*"));
            for (TPair<FString, TArray<FSanitizerAssetRecord>>& Bucket : Buckets)
            {
                Bucket.Value.Append(Wildcards);
            }
        }
    }

    Result.State = ESanitizerSessionState::Fingerprinting;
    for (TPair<FString, TArray<FSanitizerAssetRecord>>& Bucket : Buckets)
    {
        if (bCancelRequested.Load()) { Result.State = ESanitizerSessionState::Canceled; return Result; }
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
            Group.Members.Sort(&ContentSanitizerScan::PreferAsCanonical);
            Group.GroupId = FString::Printf(TEXT("%s:%s"), *Group.ProviderId.ToString(), *Pair.Key);
            Group.CanonicalMemberIndex = 0;
            const FIoHash SettingsHash = Group.Members[0].Fingerprint.SettingsHash;
            Group.Classification = ESanitizerClassification::SafeDuplicate;
            const IAssetFingerprintProvider* Provider = FindProvider(Group.Members[0].Record.AssetData);
            UObject* CanonicalAsset = Group.Members[0].Record.AssetData.GetAsset();
            for (int32 Index = 1; Index < Group.Members.Num(); ++Index)
            {
                FString VerifyError;
                UObject* CandidateAsset = Group.Members[Index].Record.AssetData.GetAsset();
                if (!Provider || !CanonicalAsset || !CandidateAsset || !Provider->DeepVerify(CanonicalAsset, CandidateAsset, VerifyError))
                {
                    Group.Classification = ESanitizerClassification::Conflict;
                    Group.Reasons.Add(FString::Printf(TEXT("Exact source equivalence could not be verified for %s: %s"), *Group.Members[Index].Record.GetObjectPath(), VerifyError.IsEmpty() ? TEXT("asset unavailable") : *VerifyError));
                    break;
                }
                if (Group.Members[Index].Fingerprint.SettingsHash != SettingsHash)
                {
                    Group.Classification = ESanitizerClassification::ReviewRequired;
                    Group.Reasons.Add(TEXT("Payload matches, but behavior-relevant texture settings differ."));
                }
            }
            if (Group.Classification != ESanitizerClassification::Conflict)
            {
                for (FSanitizerDuplicateMember& Member : Group.Members) { Member.Fingerprint.bDeepVerified = true; }
                if (Group.Classification == ESanitizerClassification::SafeDuplicate)
                {
                    Group.Reasons.Add(TEXT("All source blocks, layers, mips, and behavior settings were verified equivalent."));
                }
            }
            for (int32 Index = 1; Index < Group.Members.Num(); ++Index) { Group.EstimatedReclaimableSize += Group.Members[Index].Record.EstimatedDiskSize; }
            ++Result.Summary.DuplicateGroups;
            Result.Summary.EstimatedReclaimableSize += Group.EstimatedReclaimableSize;
            if (Group.Classification == ESanitizerClassification::SafeDuplicate) { ++Result.Summary.SafeGroups; }
            else if (Group.Classification == ESanitizerClassification::ReviewRequired) { ++Result.Summary.ReviewGroups; }
            else { ++Result.Summary.ConflictGroups; }
            Result.Groups.Add(MoveTemp(Group));
        }
    }
    Result.Groups.Sort([](const FSanitizerDuplicateGroup& A, const FSanitizerDuplicateGroup& B) { return A.GroupId < B.GroupId; });
    Result.State = ESanitizerSessionState::Completed;
    return Result;
}
