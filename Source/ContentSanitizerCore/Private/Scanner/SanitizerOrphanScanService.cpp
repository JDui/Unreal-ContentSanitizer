#include "Scanner/SanitizerOrphanScanService.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Engine/AssetManager.h"
#include "HAL/FileManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"

enum class ESanitizerOrphanSessionState : uint8
{
    Idle,
    Inventory,
    Analyzing,
    Completed,
    CancelRequested,
    Canceled,
    Failed
};

struct FSanitizerOrphanScanSession
{
    FSanitizerOrphanScanRequest Request;
    FSanitizerOrphanScanResult Result;
    FSanitizerOrphanScanProgress Progress;
    ESanitizerOrphanSessionState State = ESanitizerOrphanSessionState::Idle;
    TArray<FAssetData> InventoryAssets;
    int32 CurrentIndex = 0;
};

namespace SanitizerOrphanScanPrivate
{
    static bool IsPathWithin(const FString& PackagePath, const FString& RootPath)
    {
        return PackagePath == RootPath || PackagePath.StartsWith(RootPath + TEXT("/"));
    }

    static bool IsChildOfClass(const FTopLevelAssetPath& AssetClassPath, UClass* BaseClass)
    {
        if (AssetClassPath == BaseClass->GetClassPathName())
        {
            return true;
        }
        if (UClass* Class = FindObject<UClass>(AssetClassPath))
        {
            return Class->IsChildOf(BaseClass);
        }
        return false;
    }

    static ESanitizerOrphanAssetType DetermineAssetType(const FAssetData& AssetData)
    {
        const FTopLevelAssetPath ClassPath = AssetData.AssetClassPath;
        if (IsChildOfClass(ClassPath, UTexture::StaticClass()))
        {
            return ESanitizerOrphanAssetType::Texture;
        }
        if (IsChildOfClass(ClassPath, UMaterialInterface::StaticClass()))
        {
            return ESanitizerOrphanAssetType::Material;
        }
        if (IsChildOfClass(ClassPath, UStaticMesh::StaticClass()))
        {
            return ESanitizerOrphanAssetType::StaticMesh;
        }
        if (IsChildOfClass(ClassPath, USkeletalMesh::StaticClass()))
        {
            return ESanitizerOrphanAssetType::SkeletalMesh;
        }
        return ESanitizerOrphanAssetType::All;
    }
}

FSanitizerOrphanScanService::FSanitizerOrphanScanService() = default;
FSanitizerOrphanScanService::~FSanitizerOrphanScanService() = default;

void FSanitizerOrphanScanService::BeginScan(const FSanitizerOrphanScanRequest& Request)
{
    Session = MakeUnique<FSanitizerOrphanScanSession>();
    Session->Request = Request;
    Session->Result.Revision = NextRevision++;
    check(IsInGameThread());
    Session->Result.Request = Request;
    Session->Result.State = ESanitizerSessionState::Inventory;
    Session->Result.bCanceled = false;
    Session->State = ESanitizerOrphanSessionState::Inventory;
    bCancelRequested.Store(false);

    FARFilter Filter;
    Filter.PackagePaths = Request.PackagePaths;
    Filter.bRecursivePaths = true;
    Filter.bRecursiveClasses = true;

    if (Request.bScanTextures)
    {
        Filter.ClassPaths.Add(UTexture::StaticClass()->GetClassPathName());
    }
    if (Request.bScanMaterials)
    {
        Filter.ClassPaths.Add(UMaterialInterface::StaticClass()->GetClassPathName());
    }
    if (Request.bScanStaticMeshes)
    {
        Filter.ClassPaths.Add(UStaticMesh::StaticClass()->GetClassPathName());
    }
    if (Request.bScanSkeletalMeshes)
    {
        Filter.ClassPaths.Add(USkeletalMesh::StaticClass()->GetClassPathName());
    }

    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    if (Registry.IsLoadingAssets() || Request.PackagePaths.IsEmpty())
    {
        Session->State = ESanitizerOrphanSessionState::Failed;
        Session->Result.State = ESanitizerSessionState::Failed;
        Session->Result.Errors.Add(TEXT("Registry is not ready or cleanup scope is empty."));
        return;
    }
    if (!Filter.ClassPaths.IsEmpty()) { Registry.GetAssets(Filter, Session->InventoryAssets); }

    // Sort deterministically
    Session->InventoryAssets.Sort([](const FAssetData& A, const FAssetData& B)
    {
        return A.GetObjectPathString() < B.GetObjectPathString();
    });

    Session->State = ESanitizerOrphanSessionState::Analyzing;
    Session->Result.State = ESanitizerSessionState::CandidateExtraction;
    Session->Progress.TotalAssets = Session->InventoryAssets.Num();
    Session->Progress.ProcessedAssets = 0;
    Session->CurrentIndex = 0;
}

bool FSanitizerOrphanScanService::TickScan(double TimeBudgetSeconds, int32 MaxAssetsPerTick)
{
    check(IsInGameThread());
    if (!IsScanActive())
    {
        return false;
    }

    if (bCancelRequested.Load())
    {
        Session->State = ESanitizerOrphanSessionState::Canceled;
        Session->Result.bCanceled = true;
        Session->Result.State = ESanitizerSessionState::Canceled;
        Session->Result.Orphans.Reset();
        Session->Result.Summary = {};
        Session->Progress.CurrentAsset.Reset();
        return false;
    }

    const double StartTime = FPlatformTime::Seconds();
    int32 ProcessedThisTick = 0;
    const int32 SafeMaxAssets = FMath::Max(1, MaxAssetsPerTick);
    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

    while (ProcessedThisTick < SafeMaxAssets && Session->CurrentIndex < Session->InventoryAssets.Num())
    {
        if (TimeBudgetSeconds > 0.0 && ProcessedThisTick > 0 && (FPlatformTime::Seconds() - StartTime) >= TimeBudgetSeconds)
        {
            break;
        }

        if (bCancelRequested.Load())
        {
            Session->State = ESanitizerOrphanSessionState::Canceled;
            Session->Result.bCanceled = true;
            Session->Result.State = ESanitizerSessionState::Canceled;
            Session->Result.Orphans.Reset();
            Session->Result.Summary = {};
            Session->Progress.CurrentAsset.Reset();
            return false;
        }

        const FAssetData& Asset = Session->InventoryAssets[Session->CurrentIndex++];
        Session->Progress.CurrentAsset = Asset.GetObjectPathString();
        Session->Progress.ProcessedAssets = Session->CurrentIndex;
        ++ProcessedThisTick;

        const FString AssetPackagePath = Asset.PackagePath.ToString();
        if (!Session->Request.bIncludePluginContent && !SanitizerOrphanScanPrivate::IsPathWithin(AssetPackagePath, TEXT("/Game")))
        {
            continue;
        }
        if (!Session->Request.bIncludeDeveloperContent && SanitizerOrphanScanPrivate::IsPathWithin(AssetPackagePath, TEXT("/Game/Developers")))
        {
            continue;
        }

        bool bExcluded = false;
        for (const FName& Exclusion : Session->Request.ExcludedPackagePaths)
        {
            if (SanitizerOrphanScanPrivate::IsPathWithin(AssetPackagePath, Exclusion.ToString()))
            {
                bExcluded = true;
                break;
            }
        }
        if (bExcluded)
        {
            continue;
        }

        const ESanitizerOrphanAssetType DeterminedType = SanitizerOrphanScanPrivate::DetermineAssetType(Asset);
        const ESanitizerOrphanAssetType AssetType = DeterminedType == ESanitizerOrphanAssetType::All ? ESanitizerOrphanAssetType::Unsupported : DeterminedType;

        ++Session->Result.Summary.InventoriedAssets;

        // Query referencers across the entire project
        TArray<FName> HardReferencers;
        Registry.GetReferencers(Asset.PackageName, HardReferencers, UE::AssetRegistry::EDependencyCategory::Package, UE::AssetRegistry::EDependencyQuery::Hard);

        // Filter out self-package references
        HardReferencers.Remove(Asset.PackageName);

        // If it has hard referencers, it is actively used by other assets
        if (HardReferencers.Num() > 0)
        {
            continue;
        }

        FSanitizerOrphanRecord Record = SanitizerOrphanPolicy::Inspect(Asset);
        Record.AssetType = AssetType;
        // Material bindings are outgoing metadata, never evidence that a mesh has users.
        if (AssetType == ESanitizerOrphanAssetType::StaticMesh || AssetType == ESanitizerOrphanAssetType::SkeletalMesh)
        {
            UObject* Object = Asset.GetAsset();
            if (UStaticMesh* Mesh = Cast<UStaticMesh>(Object))
            {
                Record.bMaterialMetadataKnown = true;
                Record.MaterialSlotsNum = Mesh->GetStaticMaterials().Num();
                for (const FStaticMaterial& Slot : Mesh->GetStaticMaterials()) { Record.EmptyMaterialSlotsNum += Slot.MaterialInterface == nullptr ? 1 : 0; }
            }
            if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(Object))
            {
                Record.bMaterialMetadataKnown = true;
                Record.MaterialSlotsNum = Mesh->GetMaterials().Num();
                for (const FSkeletalMaterial& Slot : Mesh->GetMaterials()) { Record.EmptyMaterialSlotsNum += Slot.MaterialInterface == nullptr ? 1 : 0; }
            }
            if (!Record.bMaterialMetadataKnown) { Record.Reasons.Add(TEXT("Material metadata unknown; this does not prove non-use.")); }
        }

        // Summary metrics
        ++Session->Result.Summary.OrphanAssets;
        if (Record.Classification == ESanitizerOrphanClassification::Candidate)
        {
            ++Session->Result.Summary.CandidateCount;
            Session->Result.Summary.EstimatedReclaimableSize += FMath::Max<int64>(0, Record.EstimatedDiskSize);
        }
        else
        {
            ++Session->Result.Summary.BlockedCount;
        }

        switch (AssetType)
        {
        case ESanitizerOrphanAssetType::Texture: ++Session->Result.Summary.TextureCount; break;
        case ESanitizerOrphanAssetType::Material: ++Session->Result.Summary.MaterialCount; break;
        case ESanitizerOrphanAssetType::StaticMesh: ++Session->Result.Summary.StaticMeshCount; break;
        case ESanitizerOrphanAssetType::SkeletalMesh: ++Session->Result.Summary.SkeletalMeshCount; break;
        default: break;
        }

        Session->Result.Orphans.Add(MoveTemp(Record));
    }

    if (Session->CurrentIndex >= Session->InventoryAssets.Num())
    {
        Session->State = ESanitizerOrphanSessionState::Completed;
        Session->Result.State = ESanitizerSessionState::Completed;
        Session->Progress.ProcessedAssets = Session->Progress.TotalAssets;
        Session->Progress.CurrentAsset.Reset();
        return false;
    }

    return true;
}

void FSanitizerOrphanScanService::RequestCancel()
{
    bCancelRequested.Store(true);
    if (Session && Session->State == ESanitizerOrphanSessionState::Analyzing)
    {
        Session->State = ESanitizerOrphanSessionState::CancelRequested;
        Session->Result.State = ESanitizerSessionState::CancelRequested;
    }
}

bool FSanitizerOrphanScanService::IsScanActive() const
{
    return Session && (Session->State == ESanitizerOrphanSessionState::Analyzing || Session->State == ESanitizerOrphanSessionState::CancelRequested);
}

const FSanitizerOrphanScanProgress& FSanitizerOrphanScanService::GetProgress() const
{
    static const FSanitizerOrphanScanProgress EmptyProgress;
    return Session ? Session->Progress : EmptyProgress;
}

const FSanitizerOrphanScanResult& FSanitizerOrphanScanService::GetResult() const
{
    static const FSanitizerOrphanScanResult EmptyResult;
    return Session ? Session->Result : EmptyResult;
}

FSanitizerOrphanScanResult FSanitizerOrphanScanService::Scan(const FSanitizerOrphanScanRequest& Request)
{
    BeginScan(Request);
    while (IsScanActive())
    {
        TickScan(0.0, MAX_int32);
    }
    return GetResult();
}
