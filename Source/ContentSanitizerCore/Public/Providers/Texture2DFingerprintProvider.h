#pragma once

#include "Providers/AssetFingerprintProvider.h"

class CONTENTSANITIZERCORE_API FTexture2DFingerprintProvider final : public IAssetFingerprintProvider
{
public:
    static constexpr uint32 SchemaVersion = 1;
    virtual FName GetProviderId() const override;
    virtual uint32 GetSchemaVersion() const override;
    virtual bool Supports(const FAssetData& AssetData) const override;
    virtual bool BuildCheapFingerprint(const FAssetData& AssetData, FSanitizerCheapFingerprint& OutFingerprint, FString& OutError) const override;
    virtual bool BuildDeepFingerprint(UObject* Asset, FSanitizerFingerprint& OutFingerprint, FString& OutError) const override;
    virtual bool DeepVerify(UObject* Left, UObject* Right, FString& OutError) const override;
};
