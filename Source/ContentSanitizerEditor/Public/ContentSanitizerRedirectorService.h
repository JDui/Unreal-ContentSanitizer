#pragma once

#include "ContentSanitizerConsolidationService.h"

struct FSanitizerRedirectorPlan
{
    FString OperationId;
    TArray<FName> CleanupRoots;
    TMap<FSoftObjectPath, FSoftObjectPath> Redirectors;
};

class FContentSanitizerRedirectorService
{
public:
    bool CreatePlan(const FSanitizerActionPlan& ConsolidatedPlan, const FSanitizerExecutionResult& VerifiedResult, FSanitizerRedirectorPlan& OutPlan) const;
    FSanitizerPreflightResult Preflight(const FSanitizerRedirectorPlan& Plan) const;
    FSanitizerExecutionResult Execute(const FSanitizerRedirectorPlan& Plan, const FSanitizerPreflightResult& Reviewed, bool bConfirmed) const;
};
