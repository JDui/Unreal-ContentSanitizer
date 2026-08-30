#pragma once

#include "Providers/AssetFingerprintProvider.h"

class CONTENTSANITIZERCORE_API FTexture2DFingerprintProvider final : public IAssetFingerprintProvider
{
public:
    // Version 2 covers every TextureSource block/layer/mip and the documented
    // Texture2D behavior settings used by the v0.1 equivalence contract.
    static constexpr uint32 SchemaVersion = 2;
    virtual FName GetProviderId() const override;
    virtual uint32 GetSchemaVersion() const override;
    virtual bool Supports(const FAssetData& AssetData) const override;
    virtual bool BuildCheapFingerprint(const FAssetData& AssetData, FSanitizerCheapFingerprint& OutFingerprint, FString& OutError) const override;
    virtual bool BuildDeepFingerprint(UObject* Asset, FSanitizerFingerprint& OutFingerprint, FString& OutError) const override;
    virtual bool DeepVerify(UObject* Left, UObject* Right, FString& OutError) const override;
};
