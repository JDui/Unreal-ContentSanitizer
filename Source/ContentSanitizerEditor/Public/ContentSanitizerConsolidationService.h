#pragma once

#include "Operations/SanitizerActionPlan.h"

enum class ESanitizerExecutionStatus : uint8
{
    NotExecuted,
    ConsolidationReportedFailure,
    VerificationFailed,
    Succeeded
};

struct FSanitizerExecutionResult
{
    ESanitizerExecutionStatus Status = ESanitizerExecutionStatus::NotExecuted;
    TArray<FString> Messages;

    bool IsSucceeded() const { return Status == ESanitizerExecutionStatus::Succeeded; }
    bool IsSafeToRetryWithoutRescan() const { return Status == ESanitizerExecutionStatus::NotExecuted; }
    bool MayHaveModifiedContent() const { return Status != ESanitizerExecutionStatus::NotExecuted; }
};

class FContentSanitizerConsolidationService
{
public:
    FSanitizerPreflightResult Preflight(const FSanitizerActionPlan& Plan) const;
    FSanitizerExecutionResult Execute(const FSanitizerActionPlan& Plan) const;
};
