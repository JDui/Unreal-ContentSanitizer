#include "Providers/Texture2DFingerprintProvider.h"

#include "Engine/Texture2D.h"
#include "IO/IoHash.h"
#include "Runtime/Launch/Resources/Version.h"

namespace ContentSanitizerTexture
{
    static void UpdateString(FIoHashBuilder& Builder, const FString& Value)
    {
        FTCHARToUTF8 Utf8(*Value);
        const int32 Size = Utf8.Length();
        Builder.Update(&Size, sizeof(Size));
        if (Size > 0) { Builder.Update(Utf8.Get(), Size); }
    }

    static FString EnumText(const UEnum* Enum, int64 Value)
    {
        return Enum ? Enum->GetNameStringByValue(Value) : LexToString(Value);
    }

    static FString BoolText(bool bValue)
    {
        return bValue ? TEXT("true") : TEXT("false");
    }

    static FString Vector2Text(const FVector2D& Value)
    {
        return FString::Printf(TEXT("%s,%s"), *LexToString(Value.X), *LexToString(Value.Y));
    }

    static FString Vector4Text(const FVector4& Value)
    {
        return FString::Printf(TEXT("%s,%s,%s,%s"), *LexToString(Value.X), *LexToString(Value.Y), *LexToString(Value.Z), *LexToString(Value.W));
    }

    template <typename ValueType>
    static void UpdateValue(FIoHashBuilder& Builder, const ValueType& Value)
    {
        Builder.Update(&Value, sizeof(Value));
    }
}

FName FTexture2DFingerprintProvider::GetProviderId() const { return TEXT("Texture2D"); }
uint32 FTexture2DFingerprintProvider::GetSchemaVersion() const { return SchemaVersion; }

bool FTexture2DFingerprintProvider::Supports(const FAssetData& AssetData) const
{
    return AssetData.AssetClassPath == UTexture2D::StaticClass()->GetClassPathName();
}

bool FTexture2DFingerprintProvider::BuildCheapFingerprint(const FAssetData& AssetData, FSanitizerCheapFingerprint& OutFingerprint, FString& OutError) const
{
    FString Dimensions;
    AssetData.GetTagValue(TEXT("Dimensions"), Dimensions);
    // Missing metadata is represented as a wildcard. The scanner expands wildcard
    // records into every compatible bucket so it cannot introduce false negatives.
    OutFingerprint.Key = Dimensions.IsEmpty() ? TEXT("Texture2D|*") : FString::Printf(TEXT("Texture2D|%s"), *Dimensions);
    OutError.Reset();
    return true;
}

bool FTexture2DFingerprintProvider::BuildDeepFingerprint(UObject* Asset, FSanitizerFingerprint& OutFingerprint, FString& OutError) const
{
    UTexture2D* Texture = Cast<UTexture2D>(Asset);
    if (!Texture)
    {
        OutError = TEXT("Asset is not a Texture2D.");
        return false;
    }

    FTextureSource& Source = Texture->Source;
    if (!Source.IsValid())
    {
        OutError = TEXT("Texture has no valid source data; exact duplicate proof is unavailable.");
        return false;
    }

    FIoHashBuilder PayloadBuilder;
    const int32 NumBlocks = Source.GetNumBlocks();
    const int32 NumLayers = Source.GetNumLayers();
    ContentSanitizerTexture::UpdateValue(PayloadBuilder, SchemaVersion);
    ContentSanitizerTexture::UpdateValue(PayloadBuilder, NumBlocks);
    ContentSanitizerTexture::UpdateValue(PayloadBuilder, NumLayers);

    for (int32 LayerIndex = 0; LayerIndex < NumLayers; ++LayerIndex)
    {
        const int32 LayerFormat = static_cast<int32>(Source.GetFormat(LayerIndex));
        ContentSanitizerTexture::UpdateValue(PayloadBuilder, LayerFormat);
    }

    for (int32 BlockIndex = 0; BlockIndex < NumBlocks; ++BlockIndex)
    {
        FTextureSourceBlock Block;
        Source.GetBlock(BlockIndex, Block);
        ContentSanitizerTexture::UpdateValue(PayloadBuilder, Block.BlockX);
        ContentSanitizerTexture::UpdateValue(PayloadBuilder, Block.BlockY);
        ContentSanitizerTexture::UpdateValue(PayloadBuilder, Block.SizeX);
        ContentSanitizerTexture::UpdateValue(PayloadBuilder, Block.SizeY);
        ContentSanitizerTexture::UpdateValue(PayloadBuilder, Block.NumSlices);
        ContentSanitizerTexture::UpdateValue(PayloadBuilder, Block.NumMips);

        for (int32 LayerIndex = 0; LayerIndex < NumLayers; ++LayerIndex)
        {
            for (int32 MipIndex = 0; MipIndex < Block.NumMips; ++MipIndex)
            {
                TArray64<uint8> MipData;
                if (!Source.GetMipData(MipData, BlockIndex, LayerIndex, MipIndex))
                {
                    OutError = FString::Printf(TEXT("Unable to extract source block %d, layer %d, mip %d."), BlockIndex, LayerIndex, MipIndex);
                    return false;
                }
                const int64 ByteCount = MipData.Num();
                ContentSanitizerTexture::UpdateValue(PayloadBuilder, ByteCount);
                if (ByteCount > 0)
                {
                    PayloadBuilder.Update(MipData.GetData(), ByteCount);
                }
            }
        }
    }

    FIoHashBuilder SettingsBuilder;
    TMap<FName, FString>& Settings = OutFingerprint.Settings;
    Settings.Reset();
    Settings.Add(TEXT("SRGB"), ContentSanitizerTexture::BoolText(Texture->SRGB));
    Settings.Add(TEXT("CompressionSettings"), ContentSanitizerTexture::EnumText(StaticEnum<TextureCompressionSettings>(), static_cast<int64>(Texture->CompressionSettings)));
    Settings.Add(TEXT("CompressionNoAlpha"), ContentSanitizerTexture::BoolText(Texture->CompressionNoAlpha));
    Settings.Add(TEXT("CompressionForceAlpha"), ContentSanitizerTexture::BoolText(Texture->CompressionForceAlpha));
    Settings.Add(TEXT("CompressionNone"), ContentSanitizerTexture::BoolText(Texture->CompressionNone));
    Settings.Add(TEXT("CompressionYCoCg"), ContentSanitizerTexture::BoolText(Texture->CompressionYCoCg));
    Settings.Add(TEXT("CompressionQuality"), LexToString(static_cast<int32>(Texture->CompressionQuality)));
    Settings.Add(TEXT("LossyCompressionAmount"), LexToString(static_cast<int32>(Texture->LossyCompressionAmount)));
    Settings.Add(TEXT("OodleTextureSdkVersion"), Texture->OodleTextureSdkVersion.ToString());
    Settings.Add(TEXT("OodlePreserveExtremes"), ContentSanitizerTexture::BoolText(Texture->bOodlePreserveExtremes));
    Settings.Add(TEXT("LODGroup"), ContentSanitizerTexture::EnumText(StaticEnum<TextureGroup>(), static_cast<int64>(Texture->LODGroup)));
    Settings.Add(TEXT("LODBias"), LexToString(Texture->LODBias));
    Settings.Add(TEXT("Downscale"), LexToString(Texture->Downscale.Default));
    Settings.Add(TEXT("DownscaleOptions"), LexToString(static_cast<int32>(Texture->DownscaleOptions)));
#if WITH_EDITORONLY_DATA
    {
        TArray<FName> DownscalePlatforms;
        Texture->Downscale.PerPlatform.GetKeys(DownscalePlatforms);
        DownscalePlatforms.Sort(FNameLexicalLess());
        for (const FName Platform : DownscalePlatforms)
        {
            Settings.Add(FName(*FString::Printf(TEXT("Downscale.%s"), *Platform.ToString())), LexToString(Texture->Downscale.PerPlatform[Platform]));
        }
    }
#endif
    Settings.Add(TEXT("NumCinematicMipLevels"), LexToString(Texture->NumCinematicMipLevels));
    Settings.Add(TEXT("NeverStream"), ContentSanitizerTexture::BoolText(Texture->NeverStream));
    Settings.Add(TEXT("GlobalForceMipLevelsToBeResident"), ContentSanitizerTexture::BoolText(Texture->bGlobalForceMipLevelsToBeResident));
    Settings.Add(TEXT("MipGenSettings"), ContentSanitizerTexture::EnumText(StaticEnum<TextureMipGenSettings>(), static_cast<int64>(Texture->MipGenSettings)));
    Settings.Add(TEXT("MipLoadOptions"), LexToString(static_cast<int32>(Texture->MipLoadOptions)));
    Settings.Add(TEXT("AddressX"), ContentSanitizerTexture::EnumText(StaticEnum<TextureAddress>(), static_cast<int64>(Texture->AddressX)));
    Settings.Add(TEXT("AddressY"), ContentSanitizerTexture::EnumText(StaticEnum<TextureAddress>(), static_cast<int64>(Texture->AddressY)));
    Settings.Add(TEXT("Filter"), ContentSanitizerTexture::EnumText(StaticEnum<TextureFilter>(), static_cast<int64>(Texture->Filter)));
    Settings.Add(TEXT("VirtualTextureStreaming"), ContentSanitizerTexture::BoolText(Texture->VirtualTextureStreaming));
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
    Settings.Add(TEXT("VirtualTextureStreamingPriorityOverride"), ContentSanitizerTexture::BoolText(Texture->bUseVirtualTextureStreamingPriority));
    Settings.Add(TEXT("VirtualTextureStreamingPriority"), LexToString(static_cast<int32>(Texture->VirtualTextureStreamingPriority)));
#endif
    Settings.Add(TEXT("MaxTextureSize"), LexToString(Texture->MaxTextureSize));
    Settings.Add(TEXT("CookPlatformTilingSettings"), LexToString(static_cast<int32>(Texture->CookPlatformTilingSettings)));
    Settings.Add(TEXT("Availability"), LexToString(static_cast<int32>(Texture->Availability)));
    Settings.Add(TEXT("NoTiling"), ContentSanitizerTexture::BoolText(Texture->bNoTiling));
    Settings.Add(TEXT("AdjustBrightness"), LexToString(Texture->AdjustBrightness));
    Settings.Add(TEXT("AdjustBrightnessCurve"), LexToString(Texture->AdjustBrightnessCurve));
    Settings.Add(TEXT("AdjustVibrance"), LexToString(Texture->AdjustVibrance));
    Settings.Add(TEXT("AdjustSaturation"), LexToString(Texture->AdjustSaturation));
    Settings.Add(TEXT("AdjustRGBCurve"), LexToString(Texture->AdjustRGBCurve));
    Settings.Add(TEXT("AdjustHue"), LexToString(Texture->AdjustHue));
    Settings.Add(TEXT("AdjustMinAlpha"), LexToString(Texture->AdjustMinAlpha));
    Settings.Add(TEXT("AdjustMaxAlpha"), LexToString(Texture->AdjustMaxAlpha));
    Settings.Add(TEXT("ScaleMipsForAlphaCoverage"), ContentSanitizerTexture::BoolText(Texture->bDoScaleMipsForAlphaCoverage));
    Settings.Add(TEXT("AlphaCoverageThresholds"), ContentSanitizerTexture::Vector4Text(Texture->AlphaCoverageThresholds));
    Settings.Add(TEXT("UseNewMipFilter"), ContentSanitizerTexture::BoolText(Texture->bUseNewMipFilter));
    Settings.Add(TEXT("PreserveBorder"), ContentSanitizerTexture::BoolText(Texture->bPreserveBorder));
    Settings.Add(TEXT("FlipGreenChannel"), ContentSanitizerTexture::BoolText(Texture->bFlipGreenChannel));
    Settings.Add(TEXT("PowerOfTwoMode"), LexToString(static_cast<int32>(Texture->PowerOfTwoMode)));
    Settings.Add(TEXT("PaddingColor"), Texture->PaddingColor.ToString());
    Settings.Add(TEXT("PadWithBorderColor"), ContentSanitizerTexture::BoolText(Texture->bPadWithBorderColor));
    Settings.Add(TEXT("ResizeDuringBuildX"), LexToString(Texture->ResizeDuringBuildX));
    Settings.Add(TEXT("ResizeDuringBuildY"), LexToString(Texture->ResizeDuringBuildY));
    Settings.Add(TEXT("ChromaKeyTexture"), ContentSanitizerTexture::BoolText(Texture->bChromaKeyTexture));
    Settings.Add(TEXT("ChromaKeyThreshold"), LexToString(Texture->ChromaKeyThreshold));
    Settings.Add(TEXT("ChromaKeyColor"), Texture->ChromaKeyColor.ToString());
    Settings.Add(TEXT("CompositeTexture"), Texture->GetCompositeTexture() ? Texture->GetCompositeTexture()->GetPathName() : FString());
    Settings.Add(TEXT("CompositeTextureMode"), LexToString(static_cast<int32>(Texture->CompositeTextureMode)));
    Settings.Add(TEXT("CompositePower"), LexToString(Texture->CompositePower));
    Settings.Add(TEXT("NormalizeNormals"), ContentSanitizerTexture::BoolText(Texture->bNormalizeNormals));
    Settings.Add(TEXT("UseLegacyGamma"), ContentSanitizerTexture::BoolText(Texture->bUseLegacyGamma));
    Settings.Add(TEXT("SourceEncodingOverride"), LexToString(static_cast<int32>(Texture->SourceColorSettings.EncodingOverride)));
    Settings.Add(TEXT("SourceColorSpace"), LexToString(static_cast<int32>(Texture->SourceColorSettings.ColorSpace)));
    Settings.Add(TEXT("SourceRedChromaticity"), ContentSanitizerTexture::Vector2Text(Texture->SourceColorSettings.RedChromaticityCoordinate));
    Settings.Add(TEXT("SourceGreenChromaticity"), ContentSanitizerTexture::Vector2Text(Texture->SourceColorSettings.GreenChromaticityCoordinate));
    Settings.Add(TEXT("SourceBlueChromaticity"), ContentSanitizerTexture::Vector2Text(Texture->SourceColorSettings.BlueChromaticityCoordinate));
    Settings.Add(TEXT("SourceWhiteChromaticity"), ContentSanitizerTexture::Vector2Text(Texture->SourceColorSettings.WhiteChromaticityCoordinate));
    Settings.Add(TEXT("SourceChromaticAdaptation"), LexToString(static_cast<int32>(Texture->SourceColorSettings.ChromaticAdaptationMethod)));

    for (int32 LayerIndex = 0; LayerIndex < Texture->LayerFormatSettings.Num(); ++LayerIndex)
    {
        const FTextureFormatSettings& Layer = Texture->LayerFormatSettings[LayerIndex];
        const FString Prefix = FString::Printf(TEXT("Layer%d."), LayerIndex);
        Settings.Add(FName(*(Prefix + TEXT("CompressionSettings"))), LexToString(static_cast<int32>(Layer.CompressionSettings)));
        Settings.Add(FName(*(Prefix + TEXT("CompressionNoAlpha"))), ContentSanitizerTexture::BoolText(Layer.CompressionNoAlpha));
        Settings.Add(FName(*(Prefix + TEXT("CompressionForceAlpha"))), ContentSanitizerTexture::BoolText(Layer.CompressionForceAlpha));
        Settings.Add(FName(*(Prefix + TEXT("CompressionNone"))), ContentSanitizerTexture::BoolText(Layer.CompressionNone));
        Settings.Add(FName(*(Prefix + TEXT("CompressionYCoCg"))), ContentSanitizerTexture::BoolText(Layer.CompressionYCoCg));
        Settings.Add(FName(*(Prefix + TEXT("SRGB"))), ContentSanitizerTexture::BoolText(Layer.SRGB));
    }

    TArray<FName> Keys;
    Settings.GetKeys(Keys);
    Keys.Sort(FNameLexicalLess());
    SettingsBuilder.Update(&SchemaVersion, sizeof(SchemaVersion));
    for (const FName& Key : Keys)
    {
        ContentSanitizerTexture::UpdateString(SettingsBuilder, Key.ToString());
        ContentSanitizerTexture::UpdateString(SettingsBuilder, Settings[Key]);
    }

    OutFingerprint.PayloadHash = PayloadBuilder.Finalize();
    OutFingerprint.SettingsHash = SettingsBuilder.Finalize();
    OutFingerprint.SchemaVersion = SchemaVersion;
    OutFingerprint.bDeepVerified = false;
    OutError.Reset();
    return true;
}

bool FTexture2DFingerprintProvider::DeepVerify(UObject* Left, UObject* Right, FString& OutError) const
{
    FSanitizerFingerprint LeftFingerprint;
    FSanitizerFingerprint RightFingerprint;
    if (!BuildDeepFingerprint(Left, LeftFingerprint, OutError) || !BuildDeepFingerprint(Right, RightFingerprint, OutError)) { return false; }
    if (LeftFingerprint.PayloadHash != RightFingerprint.PayloadHash)
    {
        OutError = TEXT("Texture source payloads differ.");
        return false;
    }
    if (LeftFingerprint.SchemaVersion != RightFingerprint.SchemaVersion)
    {
        OutError = TEXT("Texture fingerprint schema versions differ.");
        return false;
    }
    OutError.Reset();
    return true;
}
