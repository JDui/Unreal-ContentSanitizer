#if WITH_DEV_AUTOMATION_TESTS
#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentSanitizerConsolidationService.h"
#include "ContentSanitizerTestReferenceAsset.h"
#include "Editor.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Model/SanitizerTypes.h"
#include "Providers/Texture2DFingerprintProvider.h"
#include "Scanner/SanitizerScanService.h"
#include "Subsystems/EditorAssetSubsystem.h"

namespace ContentSanitizerEditorTests
{
    static UTexture2D* CreateTexture(const FString& PackageName, const TArray<uint8>& Pixels)
    {
        UPackage* Package = CreatePackage(*PackageName);
        const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);
        UTexture2D* Texture = NewObject<UTexture2D>(Package, *AssetName, RF_Public | RF_Standalone);
        Texture->Source.Init(2, 2, 1, 1, TSF_G8, Pixels.GetData());
        FAssetRegistryModule::AssetCreated(Texture);
        Texture->MarkPackageDirty();
        return Texture;
    }

    static UContentSanitizerTestReferenceAsset* CreateReference(const FString& PackageName, UTexture2D* Texture)
    {
        UPackage* Package = CreatePackage(*PackageName);
        const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);
        UContentSanitizerTestReferenceAsset* Reference = NewObject<UContentSanitizerTestReferenceAsset>(Package, *AssetName, RF_Public | RF_Standalone);
        Reference->Texture = Texture;
        FAssetRegistryModule::AssetCreated(Reference);
        Reference->MarkPackageDirty();
        return Reference;
    }

    static FSanitizerDuplicateMember MakeMember(UTexture2D* Texture, const FSanitizerFingerprint& Fingerprint)
    {
        FSanitizerDuplicateMember Member;
        Member.Record.AssetData = FAssetData(Texture);
        Member.Record.PackageName = Texture->GetOutermost()->GetFName();
        Member.Record.PackagePath = FName(*FPackageName::GetLongPackagePath(Texture->GetOutermost()->GetName()));
        Member.Fingerprint = Fingerprint;
        Member.Fingerprint.bDeepVerified = true;
        return Member;
    }

    static bool CleanupFixture(UEditorAssetSubsystem* AssetSubsystem, const FString& FixtureRoot)
    {
        IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
        Registry.ScanPathsSynchronous({ FixtureRoot }, true, true);
        return !AssetSubsystem->DoesDirectoryExist(FixtureRoot) || AssetSubsystem->DeleteDirectory(FixtureRoot);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FContentSanitizerPresentationTest, "ContentSanitizer.Unit.Presentation.SafeStatusText", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FContentSanitizerPresentationTest::RunTest(const FString& Parameters)
{
    TestEqual(TEXT("Safe status has accessible Chinese text"), SanitizerClassificationToText(ESanitizerClassification::SafeDuplicate), FString(TEXT("安全重复项")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FContentSanitizerStaleSchemaPreflightTest, "ContentSanitizer.Unit.Preflight.StaleSchemaIsBlocked", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FContentSanitizerStaleSchemaPreflightTest::RunTest(const FString& Parameters)
{
    FSanitizerActionPlan Plan;
    Plan.ProviderId = TEXT("Texture2D");
    Plan.SchemaVersion = 1;
    Plan.CanonicalAsset = FSoftObjectPath(TEXT("/Game/__ContentSanitizerTests/MissingCanonical.MissingCanonical"));
    Plan.SourceAssets.Add(FSoftObjectPath(TEXT("/Game/__ContentSanitizerTests/MissingSource.MissingSource")));
    const FSanitizerPreflightResult Result = FContentSanitizerConsolidationService().Preflight(Plan);
    TestEqual(TEXT("Stale fingerprint schema blocks before asset loading"), Result.Status, ESanitizerPreflightStatus::Blocked);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FContentSanitizerExecutionStatusContractTest, "ContentSanitizer.Unit.Consolidation.ExecutionStatusIsUnambiguous", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FContentSanitizerExecutionStatusContractTest::RunTest(const FString& Parameters)
{
    FSanitizerExecutionResult Result;
    TestTrue(TEXT("Default result means the consolidation API was not invoked"), Result.IsSafeToRetryWithoutRescan());
    TestFalse(TEXT("Default result does not claim project mutation"), Result.MayHaveModifiedContent());
    Result.Status = ESanitizerExecutionStatus::ConsolidationReportedFailure;
    TestFalse(TEXT("A reported API failure is not safe to retry as an unexecuted action"), Result.IsSafeToRetryWithoutRescan());
    TestTrue(TEXT("A reported API failure explicitly admits possible project mutation"), Result.MayHaveModifiedContent());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FContentSanitizerConsolidationFixtureTest, "ContentSanitizer.Integration.Consolidation.RewritesOnlyPlannedReferences", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FContentSanitizerConsolidationFixtureTest::RunTest(const FString& Parameters)
{
    const FString FixtureRoot = TEXT("/Game/__ContentSanitizerTests/ConsolidationFixture");
    UEditorAssetSubsystem* AssetSubsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Editor asset subsystem is available"), AssetSubsystem)) { return false; }

    ContentSanitizerEditorTests::CleanupFixture(AssetSubsystem, FixtureRoot);
    ON_SCOPE_EXIT
    {
        if (!ContentSanitizerEditorTests::CleanupFixture(AssetSubsystem, FixtureRoot))
        {
            AddError(FString::Printf(TEXT("Failed to clean generated consolidation fixture: %s"), *FixtureRoot));
        }
    };

    UTexture2D* Canonical = ContentSanitizerEditorTests::CreateTexture(FixtureRoot / TEXT("Canonical"), { 1, 2, 3, 4 });
    UTexture2D* Duplicate = ContentSanitizerEditorTests::CreateTexture(FixtureRoot / TEXT("Duplicate"), { 1, 2, 3, 4 });
    UTexture2D* Unrelated = ContentSanitizerEditorTests::CreateTexture(FixtureRoot / TEXT("Unrelated"), { 5, 6, 7, 8 });
    UContentSanitizerTestReferenceAsset* DuplicateReference = ContentSanitizerEditorTests::CreateReference(FixtureRoot / TEXT("DuplicateReference"), Duplicate);
    UContentSanitizerTestReferenceAsset* UnrelatedReference = ContentSanitizerEditorTests::CreateReference(FixtureRoot / TEXT("UnrelatedReference"), Unrelated);

    TestTrue(TEXT("Canonical fixture saves"), AssetSubsystem->SaveLoadedAsset(Canonical, false));
    TestTrue(TEXT("Duplicate fixture saves"), AssetSubsystem->SaveLoadedAsset(Duplicate, false));
    TestTrue(TEXT("Unrelated fixture saves"), AssetSubsystem->SaveLoadedAsset(Unrelated, false));
    TestTrue(TEXT("Duplicate reference fixture saves"), AssetSubsystem->SaveLoadedAsset(DuplicateReference, false));
    TestTrue(TEXT("Unrelated reference fixture saves"), AssetSubsystem->SaveLoadedAsset(UnrelatedReference, false));

    const FString CacheFilename = FPaths::CreateTempFilename(*FPaths::ProjectIntermediateDir(), TEXT("AXIScanCacheTest-"), TEXT(".bin"));
    ON_SCOPE_EXIT
    {
        IFileManager::Get().Delete(*CacheFilename, false, true);
        IFileManager::Get().Delete(*(CacheFilename + TEXT(".tmp")), false, true);
    };
    FSanitizerScanRequest ScanRequest;
    ScanRequest.PackagePaths = { FName(*FixtureRoot) };
    ScanRequest.FingerprintCacheFilename = CacheFilename;
    FSanitizerScanService FirstScanService;
    const FSanitizerScanResult FirstScan = FirstScanService.Scan(ScanRequest);
    TestEqual(TEXT("Initial scan completes"), FirstScan.State, ESanitizerSessionState::Completed);
    TestTrue(TEXT("Initial scan calculates candidate fingerprints"), FirstScan.Summary.IncrementalFingerprints > 0);
    FSanitizerScanService SecondScanService;
    const FSanitizerScanResult SecondScan = SecondScanService.Scan(ScanRequest);
    TestEqual(TEXT("Cached scan completes"), SecondScan.State, ESanitizerSessionState::Completed);
    TestEqual(TEXT("Unchanged candidates avoid incremental fingerprint work"), SecondScan.Summary.IncrementalFingerprints, 0);
    TestEqual(TEXT("Every unchanged candidate uses the persistent index"), SecondScan.Summary.CachedFingerprints, SecondScan.Summary.CandidateAssets);

    FTexture2DFingerprintProvider Provider;
    FSanitizerFingerprint CanonicalFingerprint;
    FSanitizerFingerprint DuplicateFingerprint;
    FString Error;
    TestTrue(TEXT("Canonical fingerprint succeeds"), Provider.BuildDeepFingerprint(Canonical, CanonicalFingerprint, Error));
    TestTrue(TEXT("Duplicate fingerprint succeeds"), Provider.BuildDeepFingerprint(Duplicate, DuplicateFingerprint, Error));
    TestTrue(TEXT("Exact source verification succeeds"), Provider.DeepVerify(Canonical, Duplicate, Error));

    FSanitizerDuplicateGroup Group;
    Group.GroupId = TEXT("IntegrationFixture");
    Group.ProviderId = Provider.GetProviderId();
    Group.Classification = ESanitizerClassification::SafeDuplicate;
    Group.CanonicalMemberIndex = 0;
    Group.Members.Add(ContentSanitizerEditorTests::MakeMember(Canonical, CanonicalFingerprint));
    Group.Members.Add(ContentSanitizerEditorTests::MakeMember(Duplicate, DuplicateFingerprint));

    FSanitizerActionPlan Plan;
    TestTrue(TEXT("Verified group creates an action plan"), FSanitizerActionPlanner::CreatePlan(Group, 1, Plan, Error));
    const FSanitizerExecutionResult Execution = FContentSanitizerConsolidationService().Execute(Plan);
    TestTrue(TEXT("Consolidation and verification succeed"), Execution.IsSucceeded());
    TestEqual(TEXT("Successful execution has an unambiguous status"), Execution.Status, ESanitizerExecutionStatus::Succeeded);
    TestEqual(TEXT("Planned reference is rewritten to canonical"), DuplicateReference->Texture.Get(), Canonical);
    TestEqual(TEXT("Unrelated reference remains unchanged"), UnrelatedReference->Texture.Get(), Unrelated);
    TestTrue(TEXT("Canonical asset still exists"), AssetSubsystem->DoesAssetExist(Canonical->GetPathName()));
    TestTrue(TEXT("Unrelated asset still exists"), AssetSubsystem->DoesAssetExist(Unrelated->GetPathName()));

    return true;
}
#endif
