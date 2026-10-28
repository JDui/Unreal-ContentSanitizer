#if WITH_DEV_AUTOMATION_TESTS
#include "ContentSanitizerDeletionService.h"
#include "ContentSanitizerTestReferenceAsset.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "Engine/AssetManager.h"
#include "AssetRegistry/AssetBundleData.h"
#include "MaterialDomain.h"
#include "Misc/ScopeExit.h"
#include "ObjectTools.h"
#include "UI/ContentSanitizerResultFilter.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Runtime/Launch/Resources/Version.h"
#include "Scanner/SanitizerOrphanScanService.h"
#include "Subsystems/EditorAssetSubsystem.h"

namespace SanitizerDeletionTests
{
    static void RefreshManagement(UAssetManager& Manager)
    {
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8
        Manager.UpdateManagementDatabase(EUpdateManagementDatabaseFlags::BuildChunkMap | EUpdateManagementDatabaseFlags::ForceRefresh);
#else
        Manager.UpdateManagementDatabase(true);

#endif
    }

    struct FFixture
    {
        FAutomationTestBase& Test;
        FString Root = TEXT("/Game/__ContentSanitizerTests/Deletion_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
        UEditorAssetSubsystem* Assets = GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();
        explicit FFixture(FAutomationTestBase& InTest) : Test(InTest) {}
        ~FFixture()
        {
            if (!Assets->DeleteDirectory(Root)) { Test.AddError(TEXT("Fixture cleanup failed: ") + Root); }
            TArray<FAssetData> Remaining;
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().GetAssetsByPath(FName(*Root), Remaining, true);
            if (!Remaining.IsEmpty()) { Test.AddError(TEXT("Fixture registry entries remain: ") + Root); }
        }
        template<typename T> T* Create(const FString& Name)
        {
            UPackage* Package = CreatePackage(*(Root / Name));
            T* Object = NewObject<T>(Package, *FPackageName::GetLongPackageAssetName(Root / Name), RF_Public | RF_Standalone);
            FAssetRegistryModule::AssetCreated(Object);
            Object->MarkPackageDirty();
            return Object;
        }
        UTexture2D* Texture(const FString& Name)
        {
            UTexture2D* Object = Create<UTexture2D>(Name);
            const uint8 Pixels[] = { 1, 2, 3, 4 };
            Object->Source.Init(2, 2, 1, 1, TSF_G8, Pixels);
            Save(Object);
            return Object;
        }
        void Save(UObject* Object)
        {
            Test.TestTrue(TEXT("Generated fixture saves"), Assets->SaveLoadedAsset(Object, false));
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().ScanPathsSynchronous({ Root }, true);
        }
        FSanitizerDeletionPlan Plan(const TArray<FSoftObjectPath>& Targets)
        {
            FSanitizerOrphanScanRequest Request;
            Request.PackagePaths = { FName(*(Root / TEXT("Cleanup"))) };
            const FSanitizerOrphanScanResult Scan = FSanitizerOrphanScanService().Scan(Request);
            Test.TestEqual(TEXT("Orphan scan completed"), Scan.State, ESanitizerSessionState::Completed);
            FSanitizerDeletionPlan Result;
            FString Error;
            Test.TestTrue(TEXT("Completed candidate scan creates exact deletion plan: ") + Error, FSanitizerDeletionPlanner::CreatePlan(Scan, Targets, Result, Error));
            return Result;
        }
    };

    class FFailingDeletion final : public FContentSanitizerDeletionService
    {
    public:
        mutable int32 Calls = 0;
        int32 ReportedCount = 0;
    protected:
        virtual int32 DeleteSingleAsset(UObject*) const override { ++Calls; return ReportedCount; }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSanitizerDeletionWorkflowTest, "ContentSanitizer.Integration.Deletion.ExactTargetAndDependencySurvival", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSanitizerDeletionWorkflowTest::RunTest(const FString&)
{
    SanitizerDeletionTests::FFixture Fixture(*this);
    UTexture2D* Target = Fixture.Texture(TEXT("Cleanup/Target"));
    UTexture2D* Dependency = Fixture.Texture(TEXT("Dependency"));
    UTexture2D* Unrelated = Fixture.Texture(TEXT("Unrelated"));
    const FSoftObjectPath TargetPath(Target), DependencyPath(Dependency), UnrelatedPath(Unrelated);
    Target->SetCompositeTexture(Dependency);
    Target->MarkPackageDirty(); Fixture.Save(Target);
    const FSanitizerDeletionPlan Plan = Fixture.Plan({ TargetPath });
    FContentSanitizerDeletionService Service;
    const FSanitizerDeletionPreflight Reviewed = Service.Preflight(Plan);
    TestTrue(TEXT("Exact single asset is ready: ") + FString::Join(Reviewed.Blockers, TEXT(";")), Reviewed.IsReady());
    TestTrue(TEXT("Preflight leaves target present"), Fixture.Assets->DoesAssetExist(TargetPath.ToString()));
    TestEqual(TEXT("Canceled confirmation does not execute"), Service.Execute(Plan, Reviewed, false).Status, ESanitizerDeletionStatus::NotExecuted);
    const FSanitizerDeletionResult Result = Service.Execute(Plan, Reviewed, true);
    TestEqual(TEXT("Deletion and package verification succeed: ") + FString::Join(Result.Messages, TEXT(";")), Result.Status, ESanitizerDeletionStatus::Succeeded);
    TestFalse(TEXT("Target package absent"), FPackageName::DoesPackageExist(TargetPath.GetLongPackageName()));
    TestTrue(TEXT("Outgoing dependency survives, no recursive expansion"), Fixture.Assets->DoesAssetExist(DependencyPath.ToString()));
    TestTrue(TEXT("Unrelated asset survives"), Fixture.Assets->DoesAssetExist(UnrelatedPath.ToString()));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSanitizerDeletionStaleTest, "ContentSanitizer.Integration.Deletion.RejectsStaleReferencesAndState", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSanitizerDeletionStaleTest::RunTest(const FString&)
{
    SanitizerDeletionTests::FFixture Fixture(*this);
    UTexture2D* Target = Fixture.Texture(TEXT("Cleanup/Target"));
    const FSoftObjectPath Path(Target);
    FSanitizerDeletionPlan Plan = Fixture.Plan({ Path });
    FContentSanitizerDeletionService Service;
    const FSanitizerDeletionPreflight Reviewed = Service.Preflight(Plan);
    TestTrue(TEXT("Initial preflight is ready"), Reviewed.IsReady());
    auto* User = Fixture.Create<UContentSanitizerTestReferenceAsset>(TEXT("Outside/SoftUser"));
    User->SoftAsset = Target;
    TestFalse(TEXT("Unsaved soft user outside cleanup blocks"), Service.Preflight(Plan).IsReady());
    Fixture.Save(User);
    TestEqual(TEXT("New saved soft user rejects confirmed old preflight"), Service.Execute(Plan, Reviewed, true).Status, ESanitizerDeletionStatus::NotExecuted);
    Target = Cast<UTexture2D>(Path.TryLoad());
    if (!TestNotNull(TEXT("Blocked target remains loadable"), Target)) { return false; }
    User->SoftAsset.Reset(); User->Texture = Target; User->MarkPackageDirty(); Fixture.Save(User);
    TestFalse(TEXT("Hard user outside candidate classes blocks"), Service.Preflight(Plan).IsReady());
    User->Texture = nullptr; User->MarkPackageDirty(); Fixture.Save(User);
    Plan = Fixture.Plan({ Path });
    Target->MarkPackageDirty();
    TestFalse(TEXT("New dirty target blocks"), Service.Preflight(Plan).IsReady());
    Fixture.Save(Target);
    Plan = Fixture.Plan({ Path });
    FString Filename; FPackageName::DoesPackageExist(Path.GetLongPackageName(), &Filename);
    TestTrue(TEXT("Fixture can be made read-only"), FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*Filename, true));
    TestFalse(TEXT("New read-only package blocks"), Service.Preflight(Plan).IsReady());
    FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*Filename, false);
    Plan = Fixture.Plan({ Path });
    const FString Sidecar = (FPaths::GetPath(Filename) / FPaths::GetBaseFilename(Filename)) + TEXT(".ubulk");
    const TArray<uint8> SidecarBytes { 0 };
    TestTrue(TEXT("Disposable sidecar fixture writes"), FFileHelper::SaveArrayToFile(SidecarBytes, *Sidecar));
    TestFalse(TEXT("Unreviewed external package payload blocks deletion"), Service.Preflight(Plan).IsReady());
    TestTrue(TEXT("Disposable sidecar fixture cleans"), IFileManager::Get().Delete(*Sidecar));
    Plan = Fixture.Plan({ Path });
    FSanitizerDeletionPlan Changed = Plan;
    Changed.CleanupRoots = { FName(*(Fixture.Root / TEXT("Other"))) };
    TestFalse(TEXT("Service enforces target scope without Slate"), Service.Preflight(Changed).IsReady());
    Changed = Plan; ++Changed.PolicyRevision;
    TestFalse(TEXT("Changed policy blocks"), Service.Preflight(Changed).IsReady());
    Changed = Plan;
    const FSanitizerDeletionTarget DuplicateTarget = Changed.Targets[0];
    Changed.Targets.Add(DuplicateTarget);
    TestFalse(TEXT("Duplicate targets block"), Service.Preflight(Changed).IsReady());
    Changed = Plan; Changed.Targets[0].Class = UMaterial::StaticClass()->GetClassPathName();
    TestFalse(TEXT("Changed class blocks"), Service.Preflight(Changed).IsReady());
    TestTrue(TEXT("Every rejected operation leaves target present"), Fixture.Assets->DoesAssetExist(Path.ToString()));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSanitizerDeletionFailureTest, "ContentSanitizer.Integration.Deletion.ApiAndVerificationFailureStopQueue", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSanitizerDeletionFailureTest::RunTest(const FString&)
{
    SanitizerDeletionTests::FFixture Fixture(*this);
    const FSoftObjectPath A(Fixture.Texture(TEXT("Cleanup/A"))), B(Fixture.Texture(TEXT("Cleanup/B")));
    const FSanitizerDeletionPlan Plan = Fixture.Plan({ A, B });
    SanitizerDeletionTests::FFailingDeletion Service;
    const auto Reviewed = Service.Preflight(Plan);
    TestTrue(TEXT("Failure fixture ready"), Reviewed.IsReady());
    auto Result = Service.Execute(Plan, Reviewed, true);
    TestEqual(TEXT("API failure classified as possible modification"), Result.Status, ESanitizerDeletionStatus::ApiFailure);
    TestEqual(TEXT("Queue stops at first call"), Service.Calls, 1);
    TestTrue(TEXT("API failure must invalidate results"), Result.MayHaveModifiedContent());
    TestEqual(TEXT("Consumed failed operation cannot replay"), Service.Execute(Plan, Reviewed, true).Status, ESanitizerDeletionStatus::NotExecuted);
    TestEqual(TEXT("Replay made no additional call"), Service.Calls, 1);
    const auto NewPlan = Fixture.Plan({ A, B });
    const auto NewReviewed = Service.Preflight(NewPlan);
    Service.Calls = 0; Service.ReportedCount = 1;
    Result = Service.Execute(NewPlan, NewReviewed, true);
    TestEqual(TEXT("API success alone does not prove deletion"), Result.Status, ESanitizerDeletionStatus::VerificationFailed);
    TestEqual(TEXT("Verification failure stops queue"), Service.Calls, 1);
    TestTrue(TEXT("Second target survives"), Fixture.Assets->DoesAssetExist(B.ToString()));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSanitizerMeshMaterialIndependenceTest, "ContentSanitizer.Integration.Orphan.MeshMaterialBindingsDoNotChangeUseEvidence", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSanitizerMeshMaterialIndependenceTest::RunTest(const FString&)
{
    SanitizerDeletionTests::FFixture Fixture(*this);
    auto* Material = Fixture.Create<UMaterial>(TEXT("Material")); Fixture.Save(Material);
    auto* Instance = Fixture.Create<UMaterialInstanceConstant>(TEXT("Cleanup/Instance")); Fixture.Save(Instance);
    auto* Static = Fixture.Create<UStaticMesh>(TEXT("Cleanup/Static")); Fixture.Save(Static);
    auto* Skeletal = Fixture.Create<USkeletalMesh>(TEXT("Cleanup/Skeletal")); Fixture.Save(Skeletal);
    for (UObject* Asset : TArray<UObject*>{ Material, Instance, Static, Skeletal })
    {
        const auto Record = SanitizerOrphanPolicy::Inspect(FAssetData(Asset));
        TestTrue(TEXT("Advertised exact class supported"), Record.bSupportedState);
    }
    const auto Before = SanitizerOrphanPolicy::Inspect(FAssetData(Skeletal)).Classification;
    Skeletal->GetMaterials().Add(FSkeletalMaterial(Material)); Skeletal->MarkPackageDirty(); Fixture.Save(Skeletal);
    TestEqual(TEXT("Project material does not change mesh use evidence"), SanitizerOrphanPolicy::Inspect(FAssetData(Skeletal)).Classification, Before);
    Skeletal->GetMaterials()[0].MaterialInterface = UMaterial::GetDefaultMaterial(MD_Surface); Skeletal->MarkPackageDirty(); Fixture.Save(Skeletal);
    TestEqual(TEXT("Engine default material does not change mesh use evidence"), SanitizerOrphanPolicy::Inspect(FAssetData(Skeletal)).Classification, Before);
    Skeletal->GetMaterials()[0].MaterialInterface = nullptr; Skeletal->MarkPackageDirty(); Fixture.Save(Skeletal);
    TestEqual(TEXT("Empty slot does not change mesh use evidence"), SanitizerOrphanPolicy::Inspect(FAssetData(Skeletal)).Classification, Before);
    Static->GetStaticMaterials().Add(FStaticMaterial(Material)); Static->MarkPackageDirty(); Fixture.Save(Static);
    TestEqual(TEXT("Static mesh outgoing material does not create incoming mesh user"), SanitizerOrphanPolicy::Inspect(FAssetData(Static)).Classification, Before);
    auto* Companion = NewObject<UMaterial>(Static->GetOutermost(), TEXT("Companion"), RF_Public | RF_Standalone);
    FAssetRegistryModule::AssetCreated(Companion); Static->MarkPackageDirty(); Fixture.Save(Static);
    TestEqual(TEXT("Same-package companion blocks single-asset deletion"), SanitizerOrphanPolicy::Inspect(FAssetData(Static)).Classification, ESanitizerOrphanClassification::Blocked);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSanitizerNativeDeleteReferenceTest, "ContentSanitizer.Integration.Deletion.NativePathRejectsHardAndSoftUsers", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSanitizerNativeDeleteReferenceTest::RunTest(const FString&)
{
    SanitizerDeletionTests::FFixture Fixture(*this);
    UTexture2D* Hard = Fixture.Texture(TEXT("Cleanup/Hard"));
    UTexture2D* Soft = Fixture.Texture(TEXT("Cleanup/Soft"));
    auto* User = Fixture.Create<UContentSanitizerTestReferenceAsset>(TEXT("Outside/User"));
    User->Texture = Hard; User->SoftAsset = Soft; Fixture.Save(User);
    const FSoftObjectPath HardPath(Hard), SoftPath(Soft);
    TestEqual(TEXT("Native ordinary delete refuses hard user"), ObjectTools::DeleteObjects({ Hard }, false, ObjectTools::EAllowCancelDuringDelete::CancelNotAllowed), 0);
    TestEqual(TEXT("Native ordinary delete refuses saved soft user"), ObjectTools::DeleteObjects({ Soft }, false, ObjectTools::EAllowCancelDuringDelete::CancelNotAllowed), 0);
    TestEqual(TEXT("Hard reference never replaced with null"), User->Texture.Get(), Hard);
    TestEqual(TEXT("Soft reference preserved"), User->SoftAsset.ToSoftObjectPath(), SoftPath);
    TestTrue(TEXT("Hard target survives"), Fixture.Assets->DoesAssetExist(HardPath.ToString()));
    TestTrue(TEXT("Soft target survives"), Fixture.Assets->DoesAssetExist(SoftPath.ToString()));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSanitizerOrphanCoverageTest, "ContentSanitizer.Integration.Orphan.ManagementCoverageAndIncrementalCancel", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSanitizerOrphanCoverageTest::RunTest(const FString&)
{
    SanitizerDeletionTests::FFixture Fixture(*this);
    UTexture2D* Target = Fixture.Texture(TEXT("Cleanup/A"));
    Fixture.Texture(TEXT("Cleanup/B"));
    auto* Manager = Fixture.Create<UContentSanitizerTestReferenceAsset>(TEXT("Outside/Manager")); Fixture.Save(Manager);
    const FPrimaryAssetType ManagerType(FName(*(TEXT("SanitizerTest_") + FGuid::NewGuid().ToString(EGuidFormats::Digits))));
    FAssetBundleData Bundles;
    Bundles.AddBundleAsset(FName(TEXT("Use")), FSoftObjectPath(Target).GetAssetPath());
    UAssetManager& AssetManager = UAssetManager::Get();
    ON_SCOPE_EXIT { AssetManager.RemovePrimaryAssetType(ManagerType); SanitizerDeletionTests::RefreshManagement(AssetManager); };
    TestTrue(TEXT("Fixture dynamic primary manager registers"), AssetManager.AddDynamicAsset(FPrimaryAssetId(ManagerType, FName(TEXT("Manager"))), FSoftObjectPath(Manager), Bundles));
    SanitizerDeletionTests::RefreshManagement(AssetManager);
    const auto Record = SanitizerOrphanPolicy::Inspect(FAssetData(Target));
    TestFalse(TEXT("Management user is observable outside target and candidate types"), Record.ManagementReferencers.IsEmpty());
    TestEqual(TEXT("Management user blocks candidate"), Record.Classification, ESanitizerOrphanClassification::Blocked);
    // Dynamic assets have no disk representation; 5.8 may discard their optional object path.
    // Exercise the on-disk primary-asset metadata contract directly on this generated texture.
    FAssetData PrimaryTexture(Target);
    FAssetDataTagMap PrimaryTags;
    PrimaryTags.Add(FPrimaryAssetId::PrimaryAssetTypeTag, ManagerType.ToString());
    PrimaryTags.Add(FPrimaryAssetId::PrimaryAssetNameTag, TEXT("PrimaryTexture"));
    PrimaryTexture.SetTagsAndAssetBundles(MoveTemp(PrimaryTags));
    TestTrue(TEXT("Fixture carries a valid primary asset ID"), PrimaryTexture.GetPrimaryAssetId().IsValid());
    const auto Primary = SanitizerOrphanPolicy::Inspect(PrimaryTexture);
    TestTrue(TEXT("Primary texture protection is explicit"), Primary.Blockers.ContainsByPredicate([](const FString& Reason) { return Reason.Contains(TEXT("Primary Asset")); }));
    FSanitizerOrphanScanRequest Request;
    Request.PackagePaths = { FName(*(Fixture.Root / TEXT("Cleanup"))) };
    FSanitizerOrphanScanService Service;
    Service.BeginScan(Request); Service.TickScan(0, 1);
    TestTrue(TEXT("Incremental scan remains active between assets"), Service.IsScanActive());
    Service.RequestCancel(); Service.TickScan(0, 1);
    TestEqual(TEXT("Cancellation reaches explicit terminal state"), Service.GetResult().State, ESanitizerSessionState::Canceled);
    TestFalse(TEXT("Cancellation is terminal"), Service.IsScanActive());
    TestTrue(TEXT("Partial results discarded"), Service.GetResult().Orphans.IsEmpty());
    Request.ExcludedPackagePaths = Request.PackagePaths;
    const auto Excluded = Service.Scan(Request);
    TestEqual(TEXT("Excluded scope inventory contributes zero candidates"), Excluded.Summary.OrphanAssets, 0);
    Request.PackagePaths.Reset();
    TestEqual(TEXT("Empty target produces explicit failure"), Service.Scan(Request).State, ESanitizerSessionState::Failed);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSanitizerFilterModelTest, "ContentSanitizer.Unit.Presentation.FiltersDoNotChangeEvidence", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSanitizerFilterModelTest::RunTest(const FString&)
{
    FSanitizerOrphanRecord Record;
    Record.AssetType = ESanitizerOrphanAssetType::SkeletalMesh;
    Record.Classification = ESanitizerOrphanClassification::Candidate;
    Record.EstimatedDiskSize = 100;
    FContentSanitizerResultFilter Filter;
    Filter.Material = ESanitizerMaterialFilter::Unknown;
    TestTrue(TEXT("Unknown metadata is separately filterable"), Filter.Matches(Record));
    Record.bMaterialMetadataKnown = true; Record.MaterialSlotsNum = 1;
    Filter.Material = ESanitizerMaterialFilter::Bound;
    TestTrue(TEXT("Bound material filter matches mesh"), Filter.Matches(Record));
    Record.EmptyMaterialSlotsNum = 1;
    TestFalse(TEXT("Empty slot hidden from bound filter"), Filter.Matches(Record));
    TestEqual(TEXT("Filtering never changes classification"), Record.Classification, ESanitizerOrphanClassification::Candidate);
    Filter.Material = ESanitizerMaterialFilter::Empty; Filter.MinimumBytes = 101;
    TestFalse(TEXT("Minimum estimate filter enforced"), Filter.Matches(Record));
    Filter.MinimumBytes = 0; Filter.bCandidatesOnly = true; Record.Classification = ESanitizerOrphanClassification::Blocked;
    TestFalse(TEXT("Blocked records excluded from candidate-only view"), Filter.Matches(Record));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSanitizerDeletionFamiliesTest, "ContentSanitizer.Integration.Deletion.AllSupportedFamiliesPreserveDependencies", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSanitizerDeletionFamiliesTest::RunTest(const FString&)
{
    SanitizerDeletionTests::FFixture Fixture(*this);
    auto* DependencyMaterial = Fixture.Create<UMaterial>(TEXT("Dependencies/Material")); Fixture.Save(DependencyMaterial);
    auto* DependencyTexture = Fixture.Texture(TEXT("Dependencies/Texture"));
    const FSoftObjectPath MaterialDependencyPath(DependencyMaterial), TextureDependencyPath(DependencyTexture);
    auto* Texture = Fixture.Texture(TEXT("Cleanup/Texture"));
    Texture->SetCompositeTexture(DependencyTexture); Texture->MarkPackageDirty(); Fixture.Save(Texture);
    auto* Material = Fixture.Create<UMaterial>(TEXT("Cleanup/Material")); Fixture.Save(Material);
    auto* Instance = Fixture.Create<UMaterialInstanceConstant>(TEXT("Cleanup/Instance"));
    Instance->SetParentEditorOnly(DependencyMaterial, false); Instance->MarkPackageDirty(); Fixture.Save(Instance);
    auto* Static = Fixture.Create<UStaticMesh>(TEXT("Cleanup/Static"));
    Static->GetStaticMaterials().Add(FStaticMaterial(DependencyMaterial)); Static->MarkPackageDirty(); Fixture.Save(Static);
    auto* Skeletal = Fixture.Create<USkeletalMesh>(TEXT("Cleanup/Skeletal"));
    Skeletal->GetMaterials().Add(FSkeletalMaterial(DependencyMaterial)); Skeletal->MarkPackageDirty(); Fixture.Save(Skeletal);
    const TArray<FSoftObjectPath> Targets { FSoftObjectPath(Texture), FSoftObjectPath(Material), FSoftObjectPath(Instance), FSoftObjectPath(Static), FSoftObjectPath(Skeletal) };
    const auto Plan = Fixture.Plan(Targets);
    FContentSanitizerDeletionService Service;
    const auto Reviewed = Service.Preflight(Plan);
    TestTrue(TEXT("Every advertised exact class has an executable fixture: ") + FString::Join(Reviewed.Blockers, TEXT(";")), Reviewed.IsReady());
    if (!Reviewed.IsReady()) { return false; }
    const auto Result = Service.Execute(Plan, Reviewed, true);
    TestEqual(TEXT("Individual deletion of all advertised classes verifies: ") + FString::Join(Result.Messages, TEXT(";")), Result.Status, ESanitizerDeletionStatus::Succeeded);
    TestEqual(TEXT("Only all five exact targets were deleted"), Result.VerifiedDeleted.Num(), Targets.Num());
    for (const auto& Target : Targets)
    {
        TestFalse(TEXT("Target absent from registry"), Fixture.Assets->DoesAssetExist(Target.ToString()));
        TestFalse(TEXT("Target package absent"), FPackageName::DoesPackageExist(Target.GetLongPackageName()));
    }
    TestTrue(TEXT("Shared material dependency survives all mesh/instance deletions"), Fixture.Assets->DoesAssetExist(MaterialDependencyPath.ToString()));
    TestTrue(TEXT("Texture dependency survives"), Fixture.Assets->DoesAssetExist(TextureDependencyPath.ToString()));
    return true;
}
#endif
