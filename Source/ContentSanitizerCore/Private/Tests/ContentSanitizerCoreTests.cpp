#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/Texture2D.h"
#include "Misc/AutomationTest.h"
#include "Operations/SanitizerActionPlan.h"
#include "Providers/Texture2DFingerprintProvider.h"

namespace ContentSanitizerTests
{
    static UTexture2D* MakeTexture(const TArray<uint8>& Pixels)
    {
        UTexture2D* Texture = NewObject<UTexture2D>(GetTransientPackage());
        Texture->Source.Init(2, 2, 1, 1, TSF_G8, Pixels.GetData());
        return Texture;
    }

    static UTexture2D* MakeLayeredTexture(const TArray<uint8>& Pixels)
    {
        UTexture2D* Texture = NewObject<UTexture2D>(GetTransientPackage());
        const ETextureSourceFormat Formats[] = { TSF_G8, TSF_G8 };
        Texture->Source.InitLayered(2, 2, 1, 2, 1, Formats, Pixels.GetData());
        return Texture;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FContentSanitizerTextureFingerprintDeterminismTest, "ContentSanitizer.Unit.TextureFingerprint.Deterministic", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FContentSanitizerTextureFingerprintDeterminismTest::RunTest(const FString& Parameters)
{
    UTexture2D* Texture = ContentSanitizerTests::MakeTexture({ 1, 2, 3, 4 });
    FTexture2DFingerprintProvider Provider;
    FSanitizerFingerprint First;
    FSanitizerFingerprint Second;
    FString Error;
    TestTrue(TEXT("First fingerprint succeeds"), Provider.BuildDeepFingerprint(Texture, First, Error));
    TestTrue(TEXT("Repeated fingerprint succeeds"), Provider.BuildDeepFingerprint(Texture, Second, Error));
    TestEqual(TEXT("Payload hash is deterministic"), First.PayloadHash, Second.PayloadHash);
    TestEqual(TEXT("Settings hash is deterministic"), First.SettingsHash, Second.SettingsHash);
    TestEqual(TEXT("Schema is current"), First.SchemaVersion, FTexture2DFingerprintProvider::SchemaVersion);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FContentSanitizerTexturePayloadDifferenceTest, "ContentSanitizer.Unit.TextureFingerprint.PixelDifferenceChangesPayload", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FContentSanitizerTexturePayloadDifferenceTest::RunTest(const FString& Parameters)
{
    UTexture2D* Left = ContentSanitizerTests::MakeTexture({ 1, 2, 3, 4 });
    UTexture2D* Right = ContentSanitizerTests::MakeTexture({ 1, 2, 3, 5 });
    FTexture2DFingerprintProvider Provider;
    FSanitizerFingerprint LeftFingerprint;
    FSanitizerFingerprint RightFingerprint;
    FString Error;
    TestTrue(TEXT("Left fingerprint succeeds"), Provider.BuildDeepFingerprint(Left, LeftFingerprint, Error));
    TestTrue(TEXT("Right fingerprint succeeds"), Provider.BuildDeepFingerprint(Right, RightFingerprint, Error));
    TestNotEqual(TEXT("A source pixel difference changes payload identity"), LeftFingerprint.PayloadHash, RightFingerprint.PayloadHash);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FContentSanitizerTextureLayerDifferenceTest, "ContentSanitizer.Unit.TextureFingerprint.SecondLayerChangesPayload", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FContentSanitizerTextureLayerDifferenceTest::RunTest(const FString& Parameters)
{
    UTexture2D* Left = ContentSanitizerTests::MakeLayeredTexture({ 1, 2, 3, 4, 10, 11, 12, 13 });
    UTexture2D* Right = ContentSanitizerTests::MakeLayeredTexture({ 1, 2, 3, 4, 10, 11, 12, 99 });
    FTexture2DFingerprintProvider Provider;
    FSanitizerFingerprint LeftFingerprint;
    FSanitizerFingerprint RightFingerprint;
    FString Error;
    TestTrue(TEXT("Left layered fingerprint succeeds"), Provider.BuildDeepFingerprint(Left, LeftFingerprint, Error));
    TestTrue(TEXT("Right layered fingerprint succeeds"), Provider.BuildDeepFingerprint(Right, RightFingerprint, Error));
    TestNotEqual(TEXT("A non-primary layer difference changes payload identity"), LeftFingerprint.PayloadHash, RightFingerprint.PayloadHash);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FContentSanitizerTextureSettingsDifferenceTest, "ContentSanitizer.Unit.TextureFingerprint.LODBiasChangesSettingsOnly", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FContentSanitizerTextureSettingsDifferenceTest::RunTest(const FString& Parameters)
{
    UTexture2D* Left = ContentSanitizerTests::MakeTexture({ 1, 2, 3, 4 });
    UTexture2D* Right = ContentSanitizerTests::MakeTexture({ 1, 2, 3, 4 });
    Right->LODBias = Left->LODBias + 1;
    FTexture2DFingerprintProvider Provider;
    FSanitizerFingerprint LeftFingerprint;
    FSanitizerFingerprint RightFingerprint;
    FString Error;
    TestTrue(TEXT("Left fingerprint succeeds"), Provider.BuildDeepFingerprint(Left, LeftFingerprint, Error));
    TestTrue(TEXT("Right fingerprint succeeds"), Provider.BuildDeepFingerprint(Right, RightFingerprint, Error));
    TestEqual(TEXT("Behavior settings do not alter source payload"), LeftFingerprint.PayloadHash, RightFingerprint.PayloadHash);
    TestNotEqual(TEXT("LOD bias changes behavior settings identity"), LeftFingerprint.SettingsHash, RightFingerprint.SettingsHash);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FContentSanitizerClassificationContractTest, "ContentSanitizer.Unit.Classification.SamePayloadDifferentSettingsIsReview", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FContentSanitizerClassificationContractTest::RunTest(const FString& Parameters)
{
    FSanitizerDuplicateGroup Group;
    Group.Classification = ESanitizerClassification::ReviewRequired;
    Group.CanonicalMemberIndex = 0;
    Group.Members.SetNum(2);
    FSanitizerActionPlan Plan;
    FString Error;
    TestFalse(TEXT("ReviewRequired groups are never action-plan eligible"), FSanitizerActionPlanner::CreatePlan(Group, 1, Plan, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FContentSanitizerUnverifiedSafeGroupTest, "ContentSanitizer.Unit.ActionPlan.UnverifiedSafeGroupIsBlocked", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FContentSanitizerUnverifiedSafeGroupTest::RunTest(const FString& Parameters)
{
    FSanitizerDuplicateGroup Group;
    Group.Classification = ESanitizerClassification::SafeDuplicate;
    Group.CanonicalMemberIndex = 0;
    Group.Members.SetNum(2);
    FSanitizerActionPlan Plan;
    FString Error;
    TestFalse(TEXT("A SafeDuplicate label alone is not mutation authority"), FSanitizerActionPlanner::CreatePlan(Group, 1, Plan, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FContentSanitizerPreflightShapeTest, "ContentSanitizer.Unit.Preflight.InvalidPlanIsBlocked", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FContentSanitizerPreflightShapeTest::RunTest(const FString& Parameters)
{
    FSanitizerActionPlan Plan;
    const FSanitizerPreflightResult Result = FSanitizerActionPlanner::ValidateShape(Plan);
    TestEqual(TEXT("Invalid empty plan is blocked"), Result.Status, ESanitizerPreflightStatus::Blocked);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FContentSanitizerDuplicateSourceShapeTest, "ContentSanitizer.Unit.Preflight.DuplicateSourceIsBlocked", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FContentSanitizerDuplicateSourceShapeTest::RunTest(const FString& Parameters)
{
    FSanitizerActionPlan Plan;
    Plan.CanonicalAsset = FSoftObjectPath(TEXT("/Game/Test/Canonical.Canonical"));
    Plan.SourceAssets.Add(FSoftObjectPath(TEXT("/Game/Test/Source.Source")));
    Plan.SourceAssets.Add(FSoftObjectPath(TEXT("/Game/Test/Source.Source")));
    const FSanitizerPreflightResult Result = FSanitizerActionPlanner::ValidateShape(Plan);
    TestEqual(TEXT("Duplicate source identities are blocked"), Result.Status, ESanitizerPreflightStatus::Blocked);
    return true;
}
#endif
