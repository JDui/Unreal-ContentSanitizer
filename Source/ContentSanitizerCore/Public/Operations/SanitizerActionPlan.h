#pragma once

#include "Model/SanitizerTypes.h"

struct FSanitizerActionPlan
{
    FString PlanId;
    FString OperationId;
    FString ProviderId;
    uint32 SchemaVersion = 0;
    uint64 ScanRevision = 0;
    FSoftObjectPath CanonicalAsset;
    TArray<FSoftObjectPath> SourceAssets;
    FIoHash PayloadHash;
    FIoHash SettingsHash;
    TArray<FString> Warnings;
    TArray<FName> CleanupPackagePaths;
    CONTENTSANITIZERCORE_API FString GetIdentity() const;
};

struct FSanitizerPreflightResult
{
    ESanitizerPreflightStatus Status = ESanitizerPreflightStatus::Blocked;
    TArray<FString> Messages;
    FString PlanIdentity;
    FString EvidenceIdentity;
    // Structured evidence permits only verified batch substitutions to advance a review.
    TMap<FName, FString> PackageEvidence;
    TMap<FString, int32> ReferenceCounts;
    TArray<FName> AffectedPackages;
    TArray<FName> OutsideTargetPackages;
    bool IsReady() const { return Status != ESanitizerPreflightStatus::Blocked; }
};

class CONTENTSANITIZERCORE_API FSanitizerActionPlanner
{
public:
    static bool CreatePlan(const FSanitizerDuplicateGroup& Group, uint64 ScanRevision, FSanitizerActionPlan& OutPlan, FString& OutError);
    static bool CreatePlanForScope(const FSanitizerDuplicateGroup& Group, uint64 ScanRevision, const TArray<FName>& CleanupPackagePaths, FSanitizerActionPlan& OutPlan, FString& OutError);
    static FSanitizerPreflightResult ValidateShape(const FSanitizerActionPlan& Plan);
    static int64 EstimateSourceBytes(const FSanitizerDuplicateGroup& Group, const TArray<FName>& CleanupPackagePaths);
    static int32 RecommendCanonical(const FSanitizerDuplicateGroup& Group, const TArray<FName>& CleanupPackagePaths);
};
