#pragma once

#include "AssetRegistry/AssetData.h"
#include "IO/IoHash.h"

enum class ESanitizerClassification : uint8
{
    SafeDuplicate,
    ReviewRequired,
    Similar,
    Conflict
};

enum class ESanitizerSessionState : uint8
{
    Idle, Inventory, Bucketing, CandidateExtraction, Fingerprinting, Classification, Completed, CancelRequested, Canceled, Failed, Stale
};

enum class ESanitizerPreflightStatus : uint8 { Ready, ReadyWithWarnings, Blocked };

struct FSanitizerAssetRecord
{
    FAssetData AssetData;
    FName PackageName;
    FName PackagePath;
    int64 EstimatedDiskSize = 0;
    int32 HardReferenceCount = 0;
    int32 SoftReferenceCount = 0;
    FString ChangeIdentity;

    FString GetObjectPath() const { return AssetData.GetObjectPathString(); }
};

struct FSanitizerCheapFingerprint
{
    FString Key;
    bool IsValid() const { return !Key.IsEmpty(); }
    friend bool operator==(const FSanitizerCheapFingerprint& A, const FSanitizerCheapFingerprint& B) { return A.Key == B.Key; }
};

struct FSanitizerFingerprint
{
    FIoHash PayloadHash;
    FIoHash SettingsHash;
    uint32 SchemaVersion = 0;
    TMap<FName, FString> Settings;
    bool bDeepVerified = false;
};

struct FSanitizerDuplicateMember
{
    FSanitizerAssetRecord Record;
    FSanitizerFingerprint Fingerprint;
};

struct FSanitizerDuplicateGroup
{
    FString GroupId;
    FName ProviderId;
    ESanitizerClassification Classification = ESanitizerClassification::Conflict;
    TArray<FSanitizerDuplicateMember> Members;
    int32 CanonicalMemberIndex = INDEX_NONE;
    int64 EstimatedReclaimableSize = 0;
    TArray<FString> Reasons;
};

struct FSanitizerScanSummary
{
    int32 InventoriedAssets = 0;
    int32 CandidateAssets = 0;
    int32 DuplicateGroups = 0;
    int32 SafeGroups = 0;
    int32 ReviewGroups = 0;
    int32 SimilarGroups = 0;
    int32 ConflictGroups = 0;
    int32 CachedFingerprints = 0;
    int32 IncrementalFingerprints = 0;
    int64 EstimatedReclaimableSize = 0;
};

inline FString SanitizerClassificationToText(ESanitizerClassification Classification)
{
    switch (Classification)
    {
    case ESanitizerClassification::SafeDuplicate: return TEXT("安全重复项");
    case ESanitizerClassification::ReviewRequired: return TEXT("需要复核");
    case ESanitizerClassification::Similar: return TEXT("相似项");
    default: return TEXT("冲突");
    }
}
