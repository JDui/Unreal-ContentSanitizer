#pragma once

#include "Operations/SanitizerActionPlan.h"

struct FSanitizerExecutionResult
{
    bool bSucceeded = false;
    TArray<FString> Messages;
};

class FContentSanitizerConsolidationService
{
public:
    FSanitizerPreflightResult Preflight(const FSanitizerActionPlan& Plan) const;
    FSanitizerExecutionResult Execute(const FSanitizerActionPlan& Plan) const;
};
