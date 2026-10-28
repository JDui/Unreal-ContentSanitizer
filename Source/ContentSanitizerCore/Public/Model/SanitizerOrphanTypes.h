#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"
#include "Model/SanitizerTypes.h"

enum class ESanitizerOrphanAssetType : uint8
{
    All,
    Texture,
    Material,
    StaticMesh,
    SkeletalMesh,
    Unsupported
};

struct FSanitizerOrphanRecord;
namespace SanitizerOrphanPolicy
{
    constexpr uint32 Revision = 1;
    CONTENTSANITIZERCORE_API bool IsInScope(const FString& PackageName, const TArray<FName>& Roots);
    CONTENTSANITIZERCORE_API FSanitizerOrphanRecord Inspect(const FAssetData& Asset);
    CONTENTSANITIZERCORE_API FString CoverageLimitation();
    CONTENTSANITIZERCORE_API bool IsProtectedPrimary(const FAssetData& Asset);
}

enum class ESanitizerOrphanClassification : uint8
{
    Candidate,
    ReviewRequired,
    Blocked
};

struct FSanitizerOrphanRecord
{
    FAssetData AssetData;
    FName PackageName;
    FName PackagePath;
    ESanitizerOrphanAssetType AssetType = ESanitizerOrphanAssetType::Texture;
    ESanitizerOrphanClassification Classification = ESanitizerOrphanClassification::Blocked;
    bool bRegistryReady = false;
    bool bSupportedState = false;
    bool bMaterialMetadataKnown = false;
    TArray<FName> HardReferencers;
    TArray<FName> SoftReferencers;
    TArray<FName> ManagementReferencers;
    TArray<FString> Blockers;
    FString EvidenceIdentity;
    int64 EstimatedDiskSize = 0;
    int32 HardReferenceCount = 0;
    int32 SoftReferenceCount = 0;
    int32 MaterialSlotsNum = 0;
    int32 EmptyMaterialSlotsNum = 0;
    TArray<FString> Reasons;

    FString GetObjectPath() const { return AssetData.GetObjectPathString(); }
};

struct FSanitizerOrphanScanRequest
{
    TArray<FName> PackagePaths { FName(TEXT("/Game")) };
    TArray<FName> ExcludedPackagePaths { FName(TEXT("/Game/Developers")) };
    bool bScanTextures = true;
    bool bScanMaterials = true;
    bool bScanStaticMeshes = true;
    bool bScanSkeletalMeshes = true;
    bool bIncludePluginContent = false;
    bool bIncludeDeveloperContent = false;
};

struct FSanitizerOrphanScanSummary
{
    int32 InventoriedAssets = 0;
    int32 OrphanAssets = 0;
    int32 CandidateCount = 0;
    int32 BlockedCount = 0;
    int32 TextureCount = 0;
    int32 MaterialCount = 0;
    int32 StaticMeshCount = 0;
    int32 SkeletalMeshCount = 0;
    int64 EstimatedReclaimableSize = 0;
};

struct FSanitizerOrphanScanProgress
{
    int32 ProcessedAssets = 0;
    int32 TotalAssets = 0;
    FString CurrentAsset;

    float GetFraction() const
    {
        return TotalAssets > 0 ? FMath::Clamp(static_cast<float>(ProcessedAssets) / static_cast<float>(TotalAssets), 0.0f, 1.0f) : 0.0f;
    }
};

struct FSanitizerOrphanScanResult
{
    ESanitizerSessionState State = ESanitizerSessionState::Idle;
    FSanitizerOrphanScanRequest Request;
    bool bCanceled = false;
    uint64 Revision = 0;
    FSanitizerOrphanScanSummary Summary;
    TArray<FSanitizerOrphanRecord> Orphans;
    TArray<FString> Errors;
};

inline FString SanitizerOrphanAssetTypeToText(ESanitizerOrphanAssetType Type)
{
    switch (Type)
    {
    case ESanitizerOrphanAssetType::Texture: return TEXT("贴图");
    case ESanitizerOrphanAssetType::Material: return TEXT("材质");
    case ESanitizerOrphanAssetType::StaticMesh: return TEXT("静态网格体");
    case ESanitizerOrphanAssetType::SkeletalMesh: return TEXT("骨骼网格体");
    case ESanitizerOrphanAssetType::Unsupported: return TEXT("不支持/未知子类");
    default: return TEXT("全部");
    }
}

inline FString SanitizerOrphanClassificationToText(ESanitizerOrphanClassification Classification)
{
    switch (Classification)
    {
    case ESanitizerOrphanClassification::Candidate: return TEXT("未发现外部引用（候选）");
    case ESanitizerOrphanClassification::ReviewRequired: return TEXT("需要复核");
    case ESanitizerOrphanClassification::Blocked: return TEXT("已阻止");
    default: return TEXT("未知");
    }
}
