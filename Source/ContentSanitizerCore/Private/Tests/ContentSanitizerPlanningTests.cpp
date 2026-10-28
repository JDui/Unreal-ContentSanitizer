#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Operations/SanitizerActionPlan.h"
#include "Operations/SanitizerDeletionPlan.h"
#include "Engine/Texture2D.h"
#include "Misc/PackageName.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSanitizerScopePlanningTest, "ContentSanitizer.Unit.Plans.CanonicalOverrideNeverBroadensSourceScope", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSanitizerScopePlanningTest::RunTest(const FString&)
{
    FSanitizerDuplicateGroup Group;
    Group.GroupId = TEXT("ScopeFixture"); Group.ProviderId = TEXT("Texture2D");
    Group.Classification = ESanitizerClassification::SafeDuplicate;
    for (const FString& Package : { FString(TEXT("/Game/Cleanup/A")), FString(TEXT("/Game/Shared/B")), FString(TEXT("/Game/Cleanup/C")) })
    {
        FSanitizerDuplicateMember Member;
        Member.Record.PackageName = FName(*Package);
        Member.Record.PackagePath = FName(*FPackageName::GetLongPackagePath(Package));
        Member.Record.AssetData = FAssetData(Member.Record.PackageName, Member.Record.PackagePath, FName(*FPackageName::GetLongPackageAssetName(Package)), UTexture2D::StaticClass()->GetClassPathName());
        Member.Record.EstimatedDiskSize = 100;
        Member.Fingerprint.bDeepVerified = true; Member.Fingerprint.SchemaVersion = 2;
        Group.Members.Add(Member);
    }
    const TArray<FName> Roots { FName(TEXT("/Game/Cleanup")) };
    Group.Members[0].Record.HardReferenceCount = 100;
    TestEqual(TEXT("Outside equivalent preferred even with fewer users"), FSanitizerActionPlanner::RecommendCanonical(Group, Roots), 1);
    Group.CanonicalMemberIndex = 1;
    FSanitizerActionPlan Plan;
    FString Error;
    TestTrue(TEXT("Outside canonical is eligible"), FSanitizerActionPlanner::CreatePlanForScope(Group, 1, Roots, Plan, Error));
    TestEqual(TEXT("Only cleanup members become sources"), Plan.SourceAssets.Num(), 2);
    TestEqual(TEXT("External canonical source-byte estimate"), FSanitizerActionPlanner::EstimateSourceBytes(Group, Roots), int64(200));
    const FString PreviousIdentity = Plan.GetIdentity();
    Group.CanonicalMemberIndex = 0;
    TestTrue(TEXT("Manual equivalent override accepted"), FSanitizerActionPlanner::CreatePlanForScope(Group, 1, Roots, Plan, Error));
    TestEqual(TEXT("Override updates source-byte estimate"), FSanitizerActionPlanner::EstimateSourceBytes(Group, Roots), int64(100));
    TestEqual(TEXT("Override never promotes external member into sources"), Plan.SourceAssets.Num(), 1);
    TestEqual(TEXT("Remaining source is inside cleanup"), Plan.SourceAssets[0].GetLongPackageName(), FString(TEXT("/Game/Cleanup/C")));
    TestNotEqual(TEXT("Canonical edit invalidates confirmation identity"), Plan.GetIdentity(), PreviousIdentity);
    Plan.SourceAssets.Add(FSoftObjectPath(TEXT("/Game/Shared/B.B")));
    TestFalse(TEXT("Service shape rejects forged out-of-scope source"), FSanitizerActionPlanner::ValidateShape(Plan).IsReady());
    Group.Classification = ESanitizerClassification::ReviewRequired;
    TestFalse(TEXT("Manual selection cannot waive settings differences"), FSanitizerActionPlanner::CreatePlanForScope(Group, 1, Roots, Plan, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSanitizerDeletionPlanShapeTest, "ContentSanitizer.Unit.Plans.PartialOrphanEvidenceCannotAuthorizeDeletion", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSanitizerDeletionPlanShapeTest::RunTest(const FString&)
{
    FSanitizerOrphanScanResult Scan;
    Scan.Revision = 1;
    Scan.State = ESanitizerSessionState::Canceled; Scan.bCanceled = true;
    FSanitizerDeletionPlan Plan;
    FString Error;
    TestFalse(TEXT("Canceled evidence rejected"), FSanitizerDeletionPlanner::CreatePlan(Scan, { FSoftObjectPath(TEXT("/Game/Cleanup/A.A")) }, Plan, Error));
    Scan.State = ESanitizerSessionState::Failed; Scan.bCanceled = false;
    TestFalse(TEXT("Failed evidence rejected"), FSanitizerDeletionPlanner::CreatePlan(Scan, {}, Plan, Error));
    Scan.State = ESanitizerSessionState::Completed;
    TestFalse(TEXT("Empty targets rejected"), FSanitizerDeletionPlanner::CreatePlan(Scan, {}, Plan, Error));
    TestFalse(TEXT("Unknown target rejected even with completed scan"), FSanitizerDeletionPlanner::CreatePlan(Scan, { FSoftObjectPath(TEXT("/Game/Cleanup/A.A")) }, Plan, Error));
    TestFalse(TEXT("Empty shape blocked"), FSanitizerDeletionPlanner::ValidateShape({}).IsEmpty());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSanitizerUnknownEvidenceTest, "ContentSanitizer.Unit.Orphan.UnknownClassAndMissingGraphAreBlocked", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSanitizerUnknownEvidenceTest::RunTest(const FString&)
{
    const FName Package(TEXT("/Game/__ContentSanitizerTests/UnregisteredEvidence"));
    const FName Root(TEXT("/Game/__ContentSanitizerTests"));
    const FAssetData MissingGraph(Package, Root, FName(TEXT("UnregisteredEvidence")), UTexture2D::StaticClass()->GetClassPathName());
    const auto Missing = SanitizerOrphanPolicy::Inspect(MissingGraph);
    TestEqual(TEXT("Missing graph never becomes candidate"), Missing.Classification, ESanitizerOrphanClassification::Blocked);
    TestTrue(TEXT("Failed graph inspection has a reason"), Missing.Blockers.ContainsByPredicate([](const FString& Reason) { return Reason.Contains(TEXT("引用图节点")); }));
    const FAssetData UnknownClass(Package, Root, FName(TEXT("UnregisteredEvidence")), FTopLevelAssetPath(FName(TEXT("/Script/MissingSanitizerFixture")), FName(TEXT("UnknownTexture"))));
    const auto Unknown = SanitizerOrphanPolicy::Inspect(UnknownClass);
    TestFalse(TEXT("Unknown class is unsupported"), Unknown.bSupportedState);
    TestEqual(TEXT("Unknown class identified for review"), Unknown.AssetType, ESanitizerOrphanAssetType::Unsupported);
    return true;
}
#endif
