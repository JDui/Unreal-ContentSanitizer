#pragma once

#include "Model/SanitizerOrphanTypes.h"

struct FSanitizerDeletionTarget
{
    FSoftObjectPath Asset;
    FTopLevelAssetPath Class;
    FString EvidenceIdentity;
};

struct FSanitizerDeletionPlan
{
    FString OperationId;
    uint64 ScanRevision = 0;
    uint32 PolicyRevision = SanitizerOrphanPolicy::Revision;
    TArray<FName> CleanupRoots;
    TArray<FName> ExcludedRoots;
    TArray<FSanitizerDeletionTarget> Targets;
    TArray<FString> Warnings { SanitizerOrphanPolicy::CoverageLimitation() };
    CONTENTSANITIZERCORE_API FString GetIdentity() const;
};

class CONTENTSANITIZERCORE_API FSanitizerDeletionPlanner
{
public:
    static bool CreatePlan(const FSanitizerOrphanScanResult& Scan, const TArray<FSoftObjectPath>& Targets, FSanitizerDeletionPlan& OutPlan, FString& OutError);
    static TArray<FString> ValidateShape(const FSanitizerDeletionPlan& Plan);
};
