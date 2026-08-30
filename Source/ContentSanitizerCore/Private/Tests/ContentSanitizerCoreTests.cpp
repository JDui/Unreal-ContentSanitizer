#if WITH_DEV_AUTOMATION_TESTS
#include "Cache/SanitizerFingerprintCache.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Operations/SanitizerActionPlan.h"
#include "Providers/Texture2DFingerprintProvider.h"
#include "Scanner/SanitizerScanService.h"

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FContentSanitizerWildcardCandidateUniquenessTest, "ContentSanitizer.Unit.Scanner.WildcardCandidatesAreUnique", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FContentSanitizerWildcardCandidateUniquenessTest::RunTest(const FString& Parameters)
{
    UTexture2D* First = ContentSanitizerTests::MakeTexture({ 1, 2, 3, 4 });
    UTexture2D* Second = ContentSanitizerTests::MakeTexture({ 5, 6, 7, 8 });
    UTexture2D* Wildcard = ContentSanitizerTests::MakeTexture({ 9, 10, 11, 12 });
    FSanitizerAssetRecord FirstRecord; FirstRecord.AssetData = FAssetData(First);
    FSanitizerAssetRecord SecondRecord; SecondRecord.AssetData = FAssetData(Second);
    FSanitizerAssetRecord WildcardRecord; WildcardRecord.AssetData = FAssetData(Wildcard);

    TMap<FString, TArray<FSanitizerAssetRecord>> Buckets;
    Buckets.Add(TEXT("Texture2D|2x2"), { FirstRecord, SecondRecord, WildcardRecord });
    Buckets.Add(TEXT("Texture2D|*"), { WildcardRecord });
    TArray<FSanitizerAssetRecord> Candidates;
    ContentSanitizerScan::BuildUniqueCandidateRecords(Buckets, Candidates);

    TestEqual(TEXT("Wildcard candidate expansion keeps one record per object path"), Candidates.Num(), 3);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FContentSanitizerSafeReclaimableSizeTest, "ContentSanitizer.Unit.Scanner.ReclaimableSizeCountsSafeGroupsOnly", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FContentSanitizerSafeReclaimableSizeTest::RunTest(const FString& Parameters)
{
    FSanitizerDuplicateGroup Group;
    Group.CanonicalMemberIndex = 0;
    Group.Members.SetNum(3);
    Group.Members[0].Record.EstimatedDiskSize = 100;
    Group.Members[1].Record.EstimatedDiskSize = 20;
    Group.Members[2].Record.EstimatedDiskSize = 30;
    Group.Classification = ESanitizerClassification::ReviewRequired;
    TestEqual(TEXT("Review-required groups do not claim reclaimable size"), ContentSanitizerScan::CalculateSafeReclaimableSize(Group), int64(0));
    Group.Classification = ESanitizerClassification::SafeDuplicate;
    TestEqual(TEXT("Safe groups count only non-canonical members"), ContentSanitizerScan::CalculateSafeReclaimableSize(Group), int64(50));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FContentSanitizerScanCancellationTest, "ContentSanitizer.Unit.Scanner.CancelTransitionsToCanceled", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FContentSanitizerScanCancellationTest::RunTest(const FString& Parameters)
{
    FSanitizerScanService Service;
    FSanitizerScanRequest Request;
    Request.PackagePaths = { FName(TEXT("/Game/__ContentSanitizerTests/NoAssets")) };
    Service.BeginScan(Request);
    TestTrue(TEXT("Scan session is active before cancellation is observed"), Service.IsScanActive());
    Service.RequestCancel();
    TestFalse(TEXT("Canceled scan stops on the next incremental tick"), Service.TickScan());
    TestEqual(TEXT("Canceled scan publishes an explicit canceled state"), Service.GetResult().State, ESanitizerSessionState::Canceled);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FContentSanitizerFingerprintCacheRoundTripTest, "ContentSanitizer.Unit.Cache.RoundTripAndInvalidation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FContentSanitizerFingerprintCacheRoundTripTest::RunTest(const FString& Parameters)
{
    const FString Filename = FPaths::CreateTempFilename(*FPaths::ProjectIntermediateDir(), TEXT("AXICacheTest-"), TEXT(".bin"));
    ON_SCOPE_EXIT
    {
        IFileManager::Get().Delete(*Filename, false, true);
        IFileManager::Get().Delete(*(Filename + TEXT(".tmp")), false, true);
    };

    FSanitizerFingerprintCache Cache(Filename);
    FSanitizerFingerprintCacheEntry Entry;
    Entry.ObjectPath = TEXT("/Game/Test/Texture.Texture");
    Entry.PackageName = TEXT("/Game/Test/Texture");
    Entry.ProviderId = TEXT("Texture2D");
    Entry.ChangeIdentity = TEXT("PackageSavedHash:test");
    Entry.Fingerprint.SchemaVersion = FTexture2DFingerprintProvider::SchemaVersion;
    const uint8 PayloadBytes[] = { 1, 2, 3, 4 };
    const uint8 SettingsBytes[] = { 5, 6, 7, 8 };
    Entry.Fingerprint.PayloadHash = FIoHash::HashBuffer(PayloadBytes, sizeof(PayloadBytes));
    Entry.Fingerprint.SettingsHash = FIoHash::HashBuffer(SettingsBytes, sizeof(SettingsBytes));
    Entry.Fingerprint.Settings.Add(TEXT("SRGB"), TEXT("true"));
    Entry.Fingerprint.bDeepVerified = true;
    Cache.Upsert(Entry);

    FString Error;
    TestTrue(TEXT("Fingerprint cache saves"), Cache.Save(Error));
    FSanitizerFingerprintCache Reloaded(Filename);
    TestTrue(TEXT("Fingerprint cache loads"), Reloaded.Load(Error));
    const FSanitizerFingerprintCacheEntry* Match = Reloaded.FindValid(Entry.ObjectPath, Entry.ProviderId, Entry.Fingerprint.SchemaVersion, Entry.ChangeIdentity);
    TestNotNull(TEXT("Matching change identity reuses the cached fingerprint"), Match);
    if (Match)
    {
        TestEqual(TEXT("Cached payload hash survives round trip"), Match->Fingerprint.PayloadHash, Entry.Fingerprint.PayloadHash);
        TestEqual(TEXT("Cached settings hash survives round trip"), Match->Fingerprint.SettingsHash, Entry.Fingerprint.SettingsHash);
        TestTrue(TEXT("Loaded fingerprint retains completed proof state"), Match->Fingerprint.bDeepVerified);
    }
    TestNull(TEXT("Changed package identity invalidates the cache entry"), Reloaded.FindValid(Entry.ObjectPath, Entry.ProviderId, Entry.Fingerprint.SchemaVersion, TEXT("PackageSavedHash:changed")));
    TestNull(TEXT("Changed provider invalidates the cache entry"), Reloaded.FindValid(Entry.ObjectPath, TEXT("OtherProvider"), Entry.Fingerprint.SchemaVersion, Entry.ChangeIdentity));
    TestNull(TEXT("Changed schema invalidates the cache entry"), Reloaded.FindValid(Entry.ObjectPath, Entry.ProviderId, Entry.Fingerprint.SchemaVersion + 1, Entry.ChangeIdentity));
    return true;
}
#endif
