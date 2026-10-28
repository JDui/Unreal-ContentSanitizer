#pragma once

#include "EditorSubsystem.h"
#include "ContentSanitizerOperationSubsystem.generated.h"

// Editor-session journal prevents replay after any native mutation call, including partial failure.
UCLASS()
class UContentSanitizerOperationSubsystem final : public UEditorSubsystem
{
    GENERATED_BODY()
public:
    bool IsConsumed(const FString& OperationId) const { return ConsumedOperations.Contains(OperationId); }
    void Consume(const FString& OperationId) { check(IsInGameThread()); ConsumedOperations.Add(OperationId); }
private:
    TSet<FString> ConsumedOperations;
};
