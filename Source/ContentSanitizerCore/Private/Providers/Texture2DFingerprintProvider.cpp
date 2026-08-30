#include "Providers/Texture2DFingerprintProvider.h"

#include "Engine/Texture2D.h"
#include "IO/IoHash.h"

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
}

FName FTexture2DFingerprintProvider::GetProviderId() const { return TEXT("Texture2D"); }
uint32 FTexture2DFingerprintProvider::GetSchemaVersion() const { return SchemaVersion; }

bool FTexture2DFingerprintProvider::Supports(const FAssetData& AssetData) const
{
    return AssetData.AssetClassPath == UTexture2D::StaticClass()->GetClassPathName();
}

bool FTexture2DFingerprintProvider::BuildCheapFingerprint(const FAssetData& AssetData, FSanitizerCheapFingerprint& OutFingerprint, FString& OutError) const
{
    FString Width;
    FString Height;
    AssetData.GetTagValue(TEXT("Dimensions"), Width);
    AssetData.GetTagValue(TEXT("ImportedSize"), Height);
    // Tags are only a broad candidate filter. Missing metadata deliberately produces a broad bucket.
    OutFingerprint.Key = FString::Printf(TEXT("Texture2D|%s|%s"), *Width, *Height);
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
    const int32 SizeX = Source.GetSizeX();
    const int32 SizeY = Source.GetSizeY();
    const int32 NumSlices = Source.GetNumSlices();
    const int32 NumMips = Source.GetNumMips();
    const int32 Format = static_cast<int32>(Source.GetFormat());
    PayloadBuilder.Update(&SchemaVersion, sizeof(SchemaVersion));
    PayloadBuilder.Update(&SizeX, sizeof(SizeX));
    PayloadBuilder.Update(&SizeY, sizeof(SizeY));
    PayloadBuilder.Update(&NumSlices, sizeof(NumSlices));
    PayloadBuilder.Update(&NumMips, sizeof(NumMips));
    PayloadBuilder.Update(&Format, sizeof(Format));
    for (int32 MipIndex = 0; MipIndex < NumMips; ++MipIndex)
    {
        TArray64<uint8> MipData;
        if (!Source.GetMipData(MipData, MipIndex))
        {
            OutError = FString::Printf(TEXT("Unable to extract source mip %d."), MipIndex);
            return false;
        }
        const int64 ByteCount = MipData.Num();
        PayloadBuilder.Update(&ByteCount, sizeof(ByteCount));
        if (ByteCount > 0) { PayloadBuilder.Update(MipData.GetData(), ByteCount); }
    }

    FIoHashBuilder SettingsBuilder;
    TMap<FName, FString>& Settings = OutFingerprint.Settings;
    Settings.Reset();
    Settings.Add(TEXT("SRGB"), Texture->SRGB ? TEXT("true") : TEXT("false"));
    Settings.Add(TEXT("CompressionSettings"), ContentSanitizerTexture::EnumText(StaticEnum<TextureCompressionSettings>(), static_cast<int64>(Texture->CompressionSettings)));
    Settings.Add(TEXT("LODGroup"), ContentSanitizerTexture::EnumText(StaticEnum<TextureGroup>(), static_cast<int64>(Texture->LODGroup)));
    Settings.Add(TEXT("MipGenSettings"), ContentSanitizerTexture::EnumText(StaticEnum<TextureMipGenSettings>(), static_cast<int64>(Texture->MipGenSettings)));
    Settings.Add(TEXT("AddressX"), ContentSanitizerTexture::EnumText(StaticEnum<TextureAddress>(), static_cast<int64>(Texture->AddressX)));
    Settings.Add(TEXT("AddressY"), ContentSanitizerTexture::EnumText(StaticEnum<TextureAddress>(), static_cast<int64>(Texture->AddressY)));
    Settings.Add(TEXT("Filter"), ContentSanitizerTexture::EnumText(StaticEnum<TextureFilter>(), static_cast<int64>(Texture->Filter)));
    Settings.Add(TEXT("VirtualTextureStreaming"), Texture->VirtualTextureStreaming ? TEXT("true") : TEXT("false"));
    Settings.Add(TEXT("MaxTextureSize"), LexToString(Texture->MaxTextureSize));

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
    OutFingerprint.bDeepVerified = true;
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
    OutError.Reset();
    return true;
}
