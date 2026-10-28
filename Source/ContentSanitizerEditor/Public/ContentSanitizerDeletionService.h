#pragma once

#include "Operations/SanitizerDeletionPlan.h"

enum class ESanitizerDeletionStatus : uint8 { NotExecuted, ApiFailure, VerificationFailed, Succeeded };

struct FSanitizerDeletionPreflight
{
    FString PlanIdentity;
    TArray<FName> AffectedPackages;
    TArray<FString> Blockers;
    TArray<FString> Warnings;
    int64 EstimatedPackageBytes = 0;
    bool IsReady() const { return !PlanIdentity.IsEmpty() && Blockers.IsEmpty(); }
};

struct FSanitizerDeletionResult
{
    ESanitizerDeletionStatus Status = ESanitizerDeletionStatus::NotExecuted;
    TArray<FString> Messages;
    TArray<FSoftObjectPath> VerifiedDeleted;
    bool MayHaveModifiedContent() const { return Status != ESanitizerDeletionStatus::NotExecuted; }
};

class FContentSanitizerDeletionService
{
public:
    virtual ~FContentSanitizerDeletionService() = default;
    FSanitizerDeletionPreflight Preflight(const FSanitizerDeletionPlan& Plan) const;
    FSanitizerDeletionResult Execute(const FSanitizerDeletionPlan& Plan, const FSanitizerDeletionPreflight& Reviewed, bool bConfirmed) const;
protected:
    virtual int32 DeleteSingleAsset(UObject* Asset) const;
};
