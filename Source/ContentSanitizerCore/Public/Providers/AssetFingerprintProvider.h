#pragma once

#include "Model/SanitizerTypes.h"

class UObject;

class IAssetFingerprintProvider
{
public:
    virtual ~IAssetFingerprintProvider() = default;
    virtual FName GetProviderId() const = 0;
    virtual uint32 GetSchemaVersion() const = 0;
    virtual bool Supports(const FAssetData& AssetData) const = 0;
    virtual bool BuildCheapFingerprint(const FAssetData& AssetData, FSanitizerCheapFingerprint& OutFingerprint, FString& OutError) const = 0;
    virtual bool BuildDeepFingerprint(UObject* Asset, FSanitizerFingerprint& OutFingerprint, FString& OutError) const = 0;
    virtual bool DeepVerify(UObject* Left, UObject* Right, FString& OutError) const = 0;
};
