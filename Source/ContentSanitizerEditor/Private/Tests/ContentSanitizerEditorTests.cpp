#if WITH_DEV_AUTOMATION_TESTS
#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentSanitizerConsolidationService.h"
#include "ContentSanitizerRedirectorService.h"
#include "Model/SanitizerOrphanTypes.h"
#include "ContentSanitizerTestReferenceAsset.h"
#include "Editor.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "ObjectTools.h"
#include "UObject/Linker.h"
#include "Model/SanitizerTypes.h"
#include "Providers/Texture2DFingerprintProvider.h"
#include "Scanner/SanitizerScanService.h"
#include "Scanner/SanitizerOrphanScanService.h"
#include "Subsystems/EditorAssetSubsystem.h"

namespace ContentSanitizerEditorTests
{
    class FSimulatedConsolidation final : public FContentSanitizerConsolidationService
    {
    public:
        bool bReportedSuccess = false;
        mutable int32 Calls = 0;
    protected:
        virtual bool SubmitConsolidation(UObject*, const TArray<UObject*>&) const override { ++Calls; return bReportedSuccess; }
    };

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

    UTexture2D* Canonical = ContentSanitizerEditorTests::CreateTexture(FixtureRoot / TEXT("Shared/Canonical"), { 1, 2, 3, 4 });
    UTexture2D* Duplicate = ContentSanitizerEditorTests::CreateTexture(FixtureRoot / TEXT("Cleanup/Duplicate"), { 1, 2, 3, 4 });
    UTexture2D* Unrelated = ContentSanitizerEditorTests::CreateTexture(FixtureRoot / TEXT("Unrelated"), { 5, 6, 7, 8 });
    UContentSanitizerTestReferenceAsset* DuplicateReference = ContentSanitizerEditorTests::CreateReference(FixtureRoot / TEXT("DuplicateReference"), Duplicate);
    UContentSanitizerTestReferenceAsset* UnrelatedReference = ContentSanitizerEditorTests::CreateReference(FixtureRoot / TEXT("UnrelatedReference"), Unrelated);

    DuplicateReference->SoftAsset = Duplicate;
    DuplicateReference->OtherAsset = Canonical;
    TestTrue(TEXT("Canonical fixture saves"), AssetSubsystem->SaveLoadedAsset(Canonical, false));
    TestTrue(TEXT("Duplicate fixture saves"), AssetSubsystem->SaveLoadedAsset(Duplicate, false));
    TestTrue(TEXT("Unrelated fixture saves"), AssetSubsystem->SaveLoadedAsset(Unrelated, false));
    TestTrue(TEXT("Duplicate reference fixture saves"), AssetSubsystem->SaveLoadedAsset(DuplicateReference, false));
    TestTrue(TEXT("Unrelated reference fixture saves"), AssetSubsystem->SaveLoadedAsset(UnrelatedReference, false));
    FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().ScanPathsSynchronous({ FixtureRoot }, true);

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
    TestTrue(TEXT("Verified group creates a scoped plan"), FSanitizerActionPlanner::CreatePlanForScope(Group, 1, { FName(*(FixtureRoot / TEXT("Cleanup"))) }, Plan, Error));
    FContentSanitizerConsolidationService Service;
    auto Reviewed = Service.Preflight(Plan);
    TestTrue(TEXT("Preflight ready: ") + FString::Join(Reviewed.Messages, TEXT(";")), Reviewed.IsReady());
    TestTrue(TEXT("Outside canonical listed in impact"), Reviewed.OutsideTargetPackages.Contains(Canonical->GetOutermost()->GetFName()));
    TestTrue(TEXT("Outside reference package listed in impact"), Reviewed.OutsideTargetPackages.Contains(DuplicateReference->GetOutermost()->GetFName()));
    TestEqual(TEXT("Canceled confirmation does not execute"), Service.Execute(Plan, Reviewed, false).Status, ESanitizerExecutionStatus::NotExecuted);
    auto* NewReference = ContentSanitizerEditorTests::CreateReference(FixtureRoot / TEXT("NewOutsideReference"), Duplicate);
    TestTrue(TEXT("New referencer saves"), AssetSubsystem->SaveLoadedAsset(NewReference, false));
    FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().ScanPathsSynchronous({ FixtureRoot }, true);
    TestEqual(TEXT("Expanded impact rejects old confirmation"), Service.Execute(Plan, Reviewed, true).Status, ESanitizerExecutionStatus::NotExecuted);
    const bool OriginalSRGB = Duplicate->SRGB;
    Duplicate->SRGB = !OriginalSRGB;
    TestFalse(TEXT("Changed behavior cannot be consolidated"), Service.Preflight(Plan).IsReady());
    Duplicate->SRGB = OriginalSRGB;
    Reviewed = Service.Preflight(Plan);
    ContentSanitizerEditorTests::FSimulatedConsolidation Simulated;
    const auto NativeFailure = Simulated.Execute(Plan, Reviewed, true);
    TestEqual(TEXT("Native reported failure is distinct"), NativeFailure.Status, ESanitizerExecutionStatus::ConsolidationReportedFailure);
    TestTrue(TEXT("Native reported failure admits partial modification"), NativeFailure.MayHaveModifiedContent());
    TestEqual(TEXT("Native submission occurs once without fallback"), Simulated.Calls, 1);
    TestEqual(TEXT("Consumed operation cannot replay through another service"), Service.Execute(Plan, Reviewed, true).Status, ESanitizerExecutionStatus::NotExecuted);
    auto Rescan = SecondScanService.Scan(ScanRequest);
    TestEqual(TEXT("Rescan after failed operation completes"), Rescan.State, ESanitizerSessionState::Completed);
    TestTrue(TEXT("Fresh plan after rescan"), FSanitizerActionPlanner::CreatePlanForScope(Group, Rescan.Revision, { FName(*(FixtureRoot / TEXT("Cleanup"))) }, Plan, Error));
    Reviewed = Service.Preflight(Plan);
    Simulated.bReportedSuccess = true;
    const auto FalseSuccess = Simulated.Execute(Plan, Reviewed, true);
    TestEqual(TEXT("Reported success without removal fails verification"), FalseSuccess.Status, ESanitizerExecutionStatus::VerificationFailed);
    TestTrue(TEXT("Source survives simulated incomplete operation"), AssetSubsystem->DoesAssetExist(Plan.SourceAssets[0].ToString()));
    Rescan = SecondScanService.Scan(ScanRequest);
    TestEqual(TEXT("Rescan after verification failure completes"), Rescan.State, ESanitizerSessionState::Completed);
    TestTrue(TEXT("Fresh reviewed plan for actual consolidation"), FSanitizerActionPlanner::CreatePlanForScope(Group, Rescan.Revision, { FName(*(FixtureRoot / TEXT("Cleanup"))) }, Plan, Error));
    Reviewed = Service.Preflight(Plan);
    const FSanitizerExecutionResult Execution = Service.Execute(Plan, Reviewed, true);
    for (const auto& Message : Execution.Messages) { AddInfo(Message); }
    TestTrue(TEXT("Consolidation and verification succeed"), Execution.IsSucceeded());
    TestEqual(TEXT("Successful execution has an unambiguous status"), Execution.Status, ESanitizerExecutionStatus::Succeeded);
    TestEqual(TEXT("Planned reference is rewritten to canonical"), DuplicateReference->Texture.Get(), Canonical);
    TestEqual(TEXT("New outside user redirected after renewed review"), NewReference->Texture.Get(), Canonical);
    TestEqual(TEXT("Soft destination is canonical"), DuplicateReference->SoftAsset.ToSoftObjectPath(), FSoftObjectPath(Canonical));
    TestEqual(TEXT("Existing canonical destination preserved"), DuplicateReference->OtherAsset.Get(), static_cast<UObject*>(Canonical));
    TestEqual(TEXT("Unrelated reference remains unchanged"), UnrelatedReference->Texture.Get(), Unrelated);
    TestTrue(TEXT("Canonical asset still exists"), AssetSubsystem->DoesAssetExist(Canonical->GetPathName()));
    TestTrue(TEXT("Unrelated asset still exists"), AssetSubsystem->DoesAssetExist(Unrelated->GetPathName()));
    FContentSanitizerRedirectorService RedirectorService;
    FSanitizerRedirectorPlan Redirectors;
    if (RedirectorService.CreatePlan(Plan, Execution, Redirectors))
    {
        const auto RedirectorCheck = RedirectorService.Preflight(Redirectors);
        TestTrue(TEXT("Independent redirector preflight ready: ") + FString::Join(RedirectorCheck.Messages, TEXT(";")), RedirectorCheck.IsReady());
        TestTrue(TEXT("Redirector review retains dynamic-use limitation"), RedirectorCheck.Messages.Contains(SanitizerOrphanPolicy::CoverageLimitation()));
        TestEqual(TEXT("Redirector confirmation cancellation is read-only"), RedirectorService.Execute(Redirectors, RedirectorCheck, false).Status, ESanitizerExecutionStatus::NotExecuted);
        const auto Cleanup = RedirectorService.Execute(Redirectors, RedirectorCheck, true);
        TestTrue(TEXT("Independent redirector cleanup verifies: ") + FString::Join(Cleanup.Messages, TEXT(";")), Cleanup.IsSucceeded());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FContentSanitizerOrphanScanIntegrationTest, "ContentSanitizer.Integration.Orphan.DiscoversUnreferencedAssets", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FContentSanitizerOrphanScanIntegrationTest::RunTest(const FString& Parameters)
{
    const FString FixtureRoot = TEXT("/Game/__ContentSanitizerTests/OrphanFixture");
    UEditorAssetSubsystem* AssetSubsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Editor asset subsystem is available"), AssetSubsystem)) { return false; }

    ContentSanitizerEditorTests::CleanupFixture(AssetSubsystem, FixtureRoot);
    ON_SCOPE_EXIT
    {
        if (!ContentSanitizerEditorTests::CleanupFixture(AssetSubsystem, FixtureRoot)) { AddError(TEXT("Orphan fixture cleanup failed.")); }
    };

    UTexture2D* OrphanTexture = ContentSanitizerEditorTests::CreateTexture(FixtureRoot / TEXT("OrphanTexture"), { 1, 2, 3, 4 });
    UTexture2D* UsedTexture = ContentSanitizerEditorTests::CreateTexture(FixtureRoot / TEXT("UsedTexture"), { 5, 6, 7, 8 });
    UContentSanitizerTestReferenceAsset* Referencer = ContentSanitizerEditorTests::CreateReference(FixtureRoot / TEXT("Referencer"), UsedTexture);

    TestTrue(TEXT("Orphan texture saves"), AssetSubsystem->SaveLoadedAsset(OrphanTexture, false));
    TestTrue(TEXT("Used texture saves"), AssetSubsystem->SaveLoadedAsset(UsedTexture, false));
    TestTrue(TEXT("Referencer saves"), AssetSubsystem->SaveLoadedAsset(Referencer, false));

    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    Registry.ScanPathsSynchronous({ FixtureRoot }, true, true);

    FSanitizerOrphanScanService OrphanService;
    FSanitizerOrphanScanRequest Request;
    Request.PackagePaths = { FName(*FixtureRoot) };
    const FSanitizerOrphanScanResult Result = OrphanService.Scan(Request);

    TestFalse(TEXT("Scan completed without cancel"), Result.bCanceled);
    TestTrue(TEXT("Found orphan assets"), Result.Summary.OrphanAssets >= 1);

    const bool bFoundOrphan = Result.Orphans.ContainsByPredicate([&OrphanTexture](const FSanitizerOrphanRecord& Record)
    {
        return Record.GetObjectPath() == OrphanTexture->GetPathName();
    });
    TestTrue(TEXT("Unreferenced texture is classified as orphan"), bFoundOrphan);

    const bool bContainsUsed = Result.Orphans.ContainsByPredicate([&UsedTexture](const FSanitizerOrphanRecord& Record)
    {
        return Record.GetObjectPath() == UsedTexture->GetPathName();
    });
    TestFalse(TEXT("Referenced texture is NOT classified as orphan"), bContainsUsed);

    return true;
}

namespace ContentSanitizerEditorTests
{
    static bool CleanupBatchFixture(UEditorAssetSubsystem* Assets, const FString& Root)
    {
        // Fixture-only teardown, independent of the product mutation services.
        const FString Prefix = TEXT("/Game/__ContentSanitizerTests/Batch_");
        FGuid OwnedId;
        if (!Root.StartsWith(Prefix) || !FGuid::ParseExact(Root.Mid(Prefix.Len()), EGuidFormats::Digits, OwnedId)) { return false; }
        IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
        TArray<FAssetData> Data;
        Registry.GetAssetsByPath(FName(*Root), Data, true);
        const FString DiskRoot = FPaths::ConvertRelativePathToFull(FPackageName::LongPackageNameToFilename(Root));
        if (!FPaths::IsUnderDirectory(DiskRoot, FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()))) { return false; }
        TArray<UObject*> Objects;
        TArray<FString> Filenames;
        TArray<FName> Packages;
        bool bSaved = true;
        for (const auto& Asset : Data)
        {
            const FString Filename = FPaths::ConvertRelativePathToFull(FPackageName::LongPackageNameToFilename(Asset.PackageName.ToString(), FPackageName::GetAssetPackageExtension()));
            if (!FPaths::IsUnderDirectory(Filename, DiskRoot)) { return false; }
            Filenames.AddUnique(Filename); Packages.AddUnique(Asset.PackageName);
            UObject* Object = Asset.GetAsset();
            if (!Object) { bSaved = false; continue; }
            Objects.Add(Object);
            if (auto* User = Cast<UContentSanitizerTestReferenceAsset>(Object))
            {
                User->Texture = nullptr; User->OtherAsset = nullptr; User->SoftAsset.Reset();
                User->MarkPackageDirty();
                bSaved &= Assets->SaveLoadedAsset(User, false);
            }
        }
        UPackage::WaitForAsyncFileWrites();
        const bool bObjectsDeleted = ObjectTools::ForceDeleteObjects(Objects, false) == Objects.Num();
        // The native bulk helper can report object removal while retaining package files on 5.8.
        // Only filenames inventoried inside this exact generated root may be removed here.
        bool bFilesDeleted = true;
        for (int32 Index = 0; Index < Packages.Num(); ++Index)
        {
            if (UPackage* Package = FindPackage(nullptr, *Packages[Index].ToString())) { ResetLoaders(Package); }
            if (IFileManager::Get().FileExists(*Filenames[Index])) { bFilesDeleted &= IFileManager::Get().Delete(*Filenames[Index], true, false); }
        }
        Registry.ScanPathsSynchronous({ Root }, true);
        TArray<FAssetData> Remaining;
        Registry.GetAssetsByPath(FName(*Root), Remaining, true);
        TArray<FString> RemainingFiles;
        IFileManager::Get().FindFilesRecursive(RemainingFiles, *DiskRoot, TEXT("*"), true, false);
        const bool bEmpty = RemainingFiles.IsEmpty();
        if (bEmpty) { IFileManager::Get().DeleteDirectory(*DiskRoot, false, true); Registry.RemovePath(Root); }
        return bSaved && bObjectsDeleted && bFilesDeleted && bEmpty && Remaining.IsEmpty();
    }

    class FObservedBatchConsolidation final : public FContentSanitizerConsolidationService
    {
    public:
        mutable int32 Calls = 0;
        int32 FailOnCall = INDEX_NONE;
        int32 FalseSuccessOnCall = INDEX_NONE;
        TFunction<void(int32)> AfterSubmit;
    protected:
        virtual bool SubmitConsolidation(UObject* Canonical, const TArray<UObject*>& Sources) const override
        {
            ++Calls;
            if (Calls == FailOnCall) { return false; }
            if (Calls == FalseSuccessOnCall) { return true; }
            const bool bSucceeded = FContentSanitizerConsolidationService::SubmitConsolidation(Canonical, Sources);
            if (bSucceeded && AfterSubmit) { AfterSubmit(Calls); }
            return bSucceeded;
        }
    };
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FSanitizerSharedPackageBatchTest, "ContentSanitizer.Integration.Consolidation.Batch", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
void FSanitizerSharedPackageBatchTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
    for (const TCHAR* Case : { TEXT("SharedPackageCompletes"), TEXT("NewImpactBeforeExecution"), TEXT("ChangedReferencesBeforeExecution"),
        TEXT("NewImpactAfterFirst"), TEXT("ChangedReferencesAfterFirst"), TEXT("UnrelatedPackageSaveAfterFirst"), TEXT("UnexpectedSharedChange"),
        TEXT("PartialNativeFailureStops"), TEXT("FalseSuccessStops"), TEXT("OverlappingMembersBlocked") })
    {
        Names.Add(Case); Commands.Add(Case);
    }
}

bool FSanitizerSharedPackageBatchTest::RunTest(const FString& Case)
{
    using namespace ContentSanitizerEditorTests;
    const FString Root = TEXT("/Game/__ContentSanitizerTests/Batch_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    auto* Assets = GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();
    auto& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    ON_SCOPE_EXIT { TestTrue(TEXT("Exact generated batch root cleaned"), CleanupBatchFixture(Assets, Root)); };
    TArray<UTexture2D*> Keep, Sources;
    TArray<FSanitizerActionPlan> Plans;
    FTexture2DFingerprintProvider Provider;
    const int32 GroupCount = Case == TEXT("PartialNativeFailureStops") ? 3 : 2;
    for (int32 Index = 0; Index < GroupCount; ++Index)
    {
        const TArray<uint8> Pixels { static_cast<uint8>(Index + 1), 2, 3, 4 };
        Keep.Add(CreateTexture(Root / FString::Printf(TEXT("Keep/A%d"), Index), Pixels));
        Sources.Add(CreateTexture(Root / FString::Printf(TEXT("Cleanup/B%d"), Index), Pixels));
        TestTrue(TEXT("Canonical saves"), Assets->SaveLoadedAsset(Keep.Last(), false));
        TestTrue(TEXT("Source saves"), Assets->SaveLoadedAsset(Sources.Last(), false));
        FSanitizerFingerprint KF, SF;
        FString Error;
        TestTrue(TEXT("Canonical fingerprint"), Provider.BuildDeepFingerprint(Keep.Last(), KF, Error));
        TestTrue(TEXT("Source fingerprint"), Provider.BuildDeepFingerprint(Sources.Last(), SF, Error));
        FSanitizerDuplicateGroup Group;
        Group.GroupId = FString::Printf(TEXT("BatchGroup%d"), Index); Group.ProviderId = TEXT("Texture2D");
        Group.Classification = ESanitizerClassification::SafeDuplicate; Group.CanonicalMemberIndex = 0;
        Group.Members = { MakeMember(Keep.Last(), KF), MakeMember(Sources.Last(), SF) };
        FSanitizerActionPlan Plan;
        TestTrue(TEXT("Scoped independent plan"), FSanitizerActionPlanner::CreatePlanForScope(Group, 1, { FName(*(Root / TEXT("Cleanup"))) }, Plan, Error));
        Plans.Add(Plan);
    }
    UTexture2D* Unrelated = CreateTexture(Root / TEXT("Unrelated"), {9,8,7,6});
    auto* Shared = CreateReference(Root / TEXT("Shared"), Sources[0]);
    Shared->OtherAsset = Sources[1];
    Shared->SoftAsset = GroupCount == 3 ? Sources[2] : Sources[0];
    auto* SecondOnly = CreateReference(Root / TEXT("SecondOnly"), Sources[1]);
    SecondOnly->OtherAsset = Keep[1];
    for (UObject* Object : TArray<UObject*>{ Unrelated, Shared, SecondOnly }) { TestTrue(TEXT("User/dependency saves"), Assets->SaveLoadedAsset(Object, false)); }
    Registry.ScanPathsSynchronous({Root}, true);
    FObservedBatchConsolidation Service;
    if (Case == TEXT("OverlappingMembersBlocked"))
    {
        auto Repeated = Plans[0]; Repeated.OperationId = FGuid::NewGuid().ToString(); Plans.Add(Repeated);
        const auto Check = Service.PreflightBatch(Plans);
        TestFalse(TEXT("Overlapping member packages blocked before confirmation"), Check.IsReady());
        TestFalse(TEXT("Blocked batch cannot mutate"), Service.ExecuteBatch(Plans, Check, true).MayHaveModifiedContent());
        TestEqual(TEXT("No native call"), Service.Calls, 0);
        return true;
    }
    const auto Review = Service.PreflightBatch(Plans);
    TestTrue(TEXT("Independent batch preflight ready: ") + FString::Join(Review.Messages, TEXT(" | ")), Review.IsReady());
    TestTrue(TEXT("Shared package explicitly presented"), Review.SharedPackages.Contains(Shared->GetOutermost()->GetFName()));
    TestTrue(TEXT("Shared outside-target impact explicitly confirmed"), Review.PlanReviews[1].OutsideTargetPackages.Contains(Shared->GetOutermost()->GetFName()));
    TestFalse(TEXT("No confirmation is read-only"), Service.ExecuteBatch(Plans, Review, false).MayHaveModifiedContent());
    TestEqual(TEXT("No native call before confirmation"), Service.Calls, 0);
    auto Reordered = Plans;
    Reordered.Swap(0, 1);
    TestFalse(TEXT("Old review cannot authorize a reordered batch"), Service.ExecuteBatch(Reordered, Review, true).MayHaveModifiedContent());
    TestEqual(TEXT("Reordered batch makes no native call"), Service.Calls, 0);
    auto AddOutsideUser = [&]()
    {
        auto* Added = CreateReference(Root / TEXT("NewUser"), Sources[1]);
        TestTrue(TEXT("New external user saves"), Assets->SaveLoadedAsset(Added, false));
        Registry.ScanPathsSynchronous({Root}, true);
    };
    auto ChangeSecondUser = [&]()
    {
        SecondOnly->SoftAsset = Unrelated;
        SecondOnly->MarkPackageDirty();
        TestTrue(TEXT("Unplanned reference change saves"), Assets->SaveLoadedAsset(SecondOnly, false));
        Registry.ScanPathsSynchronous({Root}, true);
    };
    if (Case == TEXT("NewImpactBeforeExecution")) { AddOutsideUser(); }
    if (Case == TEXT("ChangedReferencesBeforeExecution")) { ChangeSecondUser(); }
    if (Case == TEXT("NewImpactAfterFirst") || Case == TEXT("ChangedReferencesAfterFirst") || Case == TEXT("UnrelatedPackageSaveAfterFirst") || Case == TEXT("UnexpectedSharedChange"))
    {
        Service.AfterSubmit = [&](int32 Call)
        {
            if (Call != 1) { return; }
            if (Case == TEXT("NewImpactAfterFirst")) { AddOutsideUser(); }
            else if (Case == TEXT("ChangedReferencesAfterFirst")) { ChangeSecondUser(); }
            else if (Case == TEXT("UnrelatedPackageSaveAfterFirst"))
            {
                FString Filename;
                TestTrue(TEXT("Second-only file exists"), FPackageName::DoesPackageExist(SecondOnly->GetOutermost()->GetName(), &Filename));
                // Same references, different disk evidence outside the completed group's impact.
                IFileManager::Get().SetTimeStamp(*Filename, IFileManager::Get().GetTimeStamp(*Filename) + FTimespan::FromSeconds(10));
            }
            else
            {
                Shared->OtherAsset = Unrelated;
                Shared->MarkPackageDirty();
                TestTrue(TEXT("Unexpected shared reference change saves"), Assets->SaveLoadedAsset(Shared, false));
                Registry.ScanPathsSynchronous({Root}, true);
            }
        };
    }
    if (Case == TEXT("PartialNativeFailureStops")) { Service.FailOnCall = 2; }
    if (Case == TEXT("FalseSuccessStops")) { Service.FalseSuccessOnCall = 1; }
    const auto Result = Service.ExecuteBatch(Plans, Review, true);
    for (const auto& Message : Result.Messages) { AddInfo(Message); }
    TestEqual(TEXT("Every requested plan retains an individual result"), Result.PlanResults.Num(), Plans.Num());
    if (Case == TEXT("SharedPackageCompletes"))
    {
        TestTrue(TEXT("One confirmation completes independent shared-user groups"), Result.IsSucceeded());
        TestEqual(TEXT("Exactly two native calls"), Service.Calls, 2);
        TestEqual(TEXT("First hard destination"), Shared->Texture.Get(), Keep[0]);
        TestEqual(TEXT("First soft destination"), Shared->SoftAsset.ToSoftObjectPath(), FSoftObjectPath(Keep[0]));
        TestEqual(TEXT("Second hard destination"), Shared->OtherAsset.Get(), static_cast<UObject*>(Keep[1]));
        TestEqual(TEXT("Second separate user destination"), SecondOnly->Texture.Get(), Keep[1]);
        TestEqual(TEXT("Existing canonical reference preserved"), SecondOnly->OtherAsset.Get(), static_cast<UObject*>(Keep[1]));
        for (const auto& Plan : Plans) { TestTrue(TEXT("Only redirector remains at source"), Registry.GetAssetByObjectPath(Plan.SourceAssets[0]).IsRedirector()); }
    }
    else
    {
        TestFalse(TEXT("Unexpected change or native failure cannot complete batch"), Result.IsSucceeded());
        const bool bBefore = Case.EndsWith(TEXT("BeforeExecution"));
        TestEqual(TEXT("Stop without submitting later groups"), Service.Calls, bBefore ? 0 : Case == TEXT("PartialNativeFailureStops") ? 2 : 1);
        const int32 UnsubmittedIndex = Case == TEXT("PartialNativeFailureStops") ? 2 : 1;
        TestEqual(TEXT("Untouched group result is NotExecuted"), Result.PlanResults[UnsubmittedIndex].Status, ESanitizerExecutionStatus::NotExecuted);
        TestEqual(TEXT("Unsubmitted source survives as a texture"), Registry.GetAssetByObjectPath(Plans[UnsubmittedIndex].SourceAssets[0]).AssetClassPath, UTexture2D::StaticClass()->GetClassPathName());
        if (Case == TEXT("NewImpactAfterFirst") || Case == TEXT("ChangedReferencesAfterFirst") || Case == TEXT("UnrelatedPackageSaveAfterFirst") || Case == TEXT("PartialNativeFailureStops"))
        {
            TestTrue(TEXT("First native group verified before batch stop"), Result.PlanResults[0].IsSucceeded());
        }
        if (Case == TEXT("UnexpectedSharedChange") || Case == TEXT("FalseSuccessStops"))
        {
            TestEqual(TEXT("Unexpected transformation fails native outcome verification"), Result.PlanResults[0].Status, ESanitizerExecutionStatus::VerificationFailed);
        }
    }
    TestTrue(TEXT("Unrelated package survives"), Assets->DoesAssetExist(Unrelated->GetPathName()));
    if (Result.MayHaveModifiedContent())
    {
        const int32 Calls = Service.Calls;
        FContentSanitizerConsolidationService OtherService;
        TestFalse(TEXT("Old batch cannot replay in a new service"), OtherService.ExecuteBatch(Plans, Review, true).MayHaveModifiedContent());
        for (int32 Index = 0; Index < Plans.Num(); ++Index)
        {
            TestFalse(TEXT("All old individual plans including untouched ones consumed"), OtherService.Preflight(Plans[Index]).IsReady());
        }
        TestEqual(TEXT("No retry native call"), Service.Calls, Calls);
    }
    return true;
}
#endif
