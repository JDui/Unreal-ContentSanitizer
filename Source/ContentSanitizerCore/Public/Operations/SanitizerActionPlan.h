#pragma once

#include "Model/SanitizerTypes.h"

struct FSanitizerActionPlan
{
    FString PlanId;
    FString ProviderId;
    uint32 SchemaVersion = 0;
    uint64 ScanRevision = 0;
    FSoftObjectPath CanonicalAsset;
    TArray<FSoftObjectPath> SourceAssets;
    FIoHash PayloadHash;
    FIoHash SettingsHash;
    TArray<FString> Warnings;
};

struct FSanitizerPreflightResult
{
    ESanitizerPreflightStatus Status = ESanitizerPreflightStatus::Blocked;
    TArray<FString> Messages;
    bool IsReady() const { return Status != ESanitizerPreflightStatus::Blocked; }
};

class CONTENTSANITIZERCORE_API FSanitizerActionPlanner
{
public:
    static bool CreatePlan(const FSanitizerDuplicateGroup& Group, uint64 ScanRevision, FSanitizerActionPlan& OutPlan, FString& OutError);
    static FSanitizerPreflightResult ValidateShape(const FSanitizerActionPlan& Plan);
};
