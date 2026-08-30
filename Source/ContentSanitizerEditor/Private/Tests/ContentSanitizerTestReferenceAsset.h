#pragma once

#include "Engine/DataAsset.h"
#include "ContentSanitizerTestReferenceAsset.generated.h"

class UTexture2D;

UCLASS()
class UContentSanitizerTestReferenceAsset final : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY()
    TObjectPtr<UTexture2D> Texture;
};
