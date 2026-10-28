#pragma once

#include "Operations/SanitizerActionPlan.h"

enum class ESanitizerExecutionStatus : uint8
{
    NotExecuted,
    ConsolidationReportedFailure,
    RedirectorReportedFailure,
    VerificationFailed,
    Succeeded
};

struct FSanitizerExecutionResult
{
    ESanitizerExecutionStatus Status = ESanitizerExecutionStatus::NotExecuted;
    TArray<FString> Messages;
    FString VerifiedPlanIdentity;

    bool IsSucceeded() const { return Status == ESanitizerExecutionStatus::Succeeded; }
    bool IsSafeToRetryWithoutRescan() const { return Status == ESanitizerExecutionStatus::NotExecuted; }
    bool MayHaveModifiedContent() const { return Status != ESanitizerExecutionStatus::NotExecuted; }
};

struct FSanitizerConsolidationBatchPreflight
{
    FString OperationId;
    TArray<FSanitizerPreflightResult> PlanReviews;
    TArray<FName> SharedPackages;
    TArray<FString> Messages;
    bool bReady = false;
    bool IsReady() const { return bReady && !PlanReviews.IsEmpty(); }
};

struct FSanitizerConsolidationBatchResult
{
    // One entry per submitted plan, including untouched plans after a stop.
    TArray<FSanitizerExecutionResult> PlanResults;
    TArray<FString> Messages;
    bool IsSucceeded() const { return !PlanResults.IsEmpty() && !PlanResults.ContainsByPredicate([](const auto& Result) { return !Result.IsSucceeded(); }); }
    bool MayHaveModifiedContent() const { return PlanResults.ContainsByPredicate([](const auto& Result) { return Result.MayHaveModifiedContent(); }); }
};

class FContentSanitizerConsolidationService
{
public:
    virtual ~FContentSanitizerConsolidationService() = default;
    FSanitizerPreflightResult Preflight(const FSanitizerActionPlan& Plan) const;
    FSanitizerExecutionResult Execute(const FSanitizerActionPlan& Plan, const FSanitizerPreflightResult& Reviewed, bool bConfirmed) const;
    FSanitizerConsolidationBatchPreflight PreflightBatch(const TArray<FSanitizerActionPlan>& Plans) const;
    FSanitizerConsolidationBatchResult ExecuteBatch(const TArray<FSanitizerActionPlan>& Plans, const FSanitizerConsolidationBatchPreflight& Reviewed, bool bConfirmed) const;
protected:
    virtual bool SubmitConsolidation(UObject* Canonical, const TArray<UObject*>& Sources) const;
};
