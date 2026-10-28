#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/Texture2D.h"
#include "Misc/AutomationTest.h"
#include "Providers/Texture2DFingerprintProvider.h"
#include "UObject/MetaData.h"
#include "Runtime/Launch/Resources/Version.h"

namespace
{
    static auto& GetTestMetadata(UPackage* Package)
    {
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8
        return Package->GetMetaData();
#else
        return *Package->GetMetaData();
#endif
    }
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSanitizerTextureBehaviorMatrixTest, "ContentSanitizer.Unit.TextureFingerprint.BehaviorSettingsMatrix", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSanitizerTextureBehaviorMatrixTest::RunTest(const FString&)
{
    UTexture2D* Texture = NewObject<UTexture2D>(GetTransientPackage());
    const uint8 Pixels[] = { 1, 2, 3, 4 };
    Texture->Source.Init(2, 2, 1, 1, TSF_G8, Pixels);
    FTexture2DFingerprintProvider Provider;
    FSanitizerFingerprint Baseline;
    FString Error;
    TestTrue(TEXT("Baseline fingerprint"), Provider.BuildDeepFingerprint(Texture, Baseline, Error));
    auto Check = [&](const TCHAR* Label)
    {
        FSanitizerFingerprint Changed;
        TestTrue(Label, Provider.BuildDeepFingerprint(Texture, Changed, Error));
        TestTrue(TEXT("Behavior edits preserve payload"), Changed.PayloadHash == Baseline.PayloadHash);
        TestTrue(Label, Changed.SettingsHash != Baseline.SettingsHash);
    };
    const bool SRGB = Texture->SRGB;
    Texture->SRGB = !SRGB; Check(TEXT("sRGB difference prevents safe equivalence")); Texture->SRGB = SRGB;
    const TextureCompressionSettings Compression = Texture->CompressionSettings;
    Texture->CompressionSettings = TC_Normalmap; Check(TEXT("Compression difference prevents safe equivalence")); Texture->CompressionSettings = Compression;
    const TextureAddress Address = Texture->AddressX;
    Texture->AddressX = TA_Clamp; Check(TEXT("Address mode is behavior-relevant")); Texture->AddressX = Address;
    const TextureFilter Filter = Texture->Filter;
    Texture->Filter = TF_Nearest; Check(TEXT("Filtering is behavior-relevant")); Texture->Filter = Filter;
    FSanitizerFingerprint Restored;
    TestTrue(TEXT("Restored fingerprint"), Provider.BuildDeepFingerprint(Texture, Restored, Error));
    TestTrue(TEXT("Restoring values restores equivalence"), Restored.SettingsHash == Baseline.SettingsHash);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSanitizerTextureIdentityContractTest, "ContentSanitizer.Unit.TextureFingerprint.NamesMetadataAndSourceDimensions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSanitizerTextureIdentityContractTest::RunTest(const FString&)
{
    UTexture2D* A = NewObject<UTexture2D>(GetTransientPackage());
    UTexture2D* B = NewObject<UTexture2D>(GetTransientPackage());
    const uint8 Pixels[] = { 1, 2, 3, 4 };
    A->Source.Init(2, 2, 1, 1, TSF_G8, Pixels); B->Source.Init(2, 2, 1, 1, TSF_G8, Pixels);
    GetTestMetadata(A->GetOutermost()).SetValue(A, TEXT("SanitizerTestNote"), TEXT("A"));
    GetTestMetadata(B->GetOutermost()).SetValue(B, TEXT("SanitizerTestNote"), TEXT("B"));
    FTexture2DFingerprintProvider Provider;
    FSanitizerFingerprint First, Second;
    FString Error;
    TestTrue(TEXT("First fingerprint"), Provider.BuildDeepFingerprint(A, First, Error));
    TestTrue(TEXT("Second fingerprint"), Provider.BuildDeepFingerprint(B, Second, Error));
    TestNotEqual(TEXT("Fixture identities differ"), A->GetPathName(), B->GetPathName());
    TestTrue(TEXT("Names and package metadata do not defeat equivalence"), First.PayloadHash == Second.PayloadHash && First.SettingsHash == Second.SettingsHash && Provider.DeepVerify(A, B, Error));
    B->Source.Init(4, 1, 1, 1, TSF_G8, Pixels);
    TestTrue(TEXT("Changed dimensions fingerprint"), Provider.BuildDeepFingerprint(B, Second, Error));
    TestTrue(TEXT("Identical bytes with different dimensions are not duplicate payloads"), First.PayloadHash != Second.PayloadHash);
    GetTestMetadata(A->GetOutermost()).RemoveValue(A, TEXT("SanitizerTestNote"));
    GetTestMetadata(B->GetOutermost()).RemoveValue(B, TEXT("SanitizerTestNote"));
    return true;
}
#endif
