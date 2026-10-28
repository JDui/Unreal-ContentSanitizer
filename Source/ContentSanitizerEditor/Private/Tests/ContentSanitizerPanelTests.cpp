#if WITH_DEV_AUTOMATION_TESTS
#include "UI/SContentSanitizerPanel.h"
#include "Engine/Texture2D.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Widgets/Text/STextBlock.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSanitizerPanelWorkflowTest, "ContentSanitizer.Integration.Presentation.HeadlessScopeQueueAndInvalidation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSanitizerPanelWorkflowTest::RunTest(const FString&)
{
    // Construct an unattached widget only: no window, docking, focus, or mutation API.
    TSharedRef<SContentSanitizerPanel> Panel = SNew(SContentSanitizerPanel);
    Panel->SetCleanupPackagePaths({ TEXT("/Game/Cleanup") });
    TestFalse(TEXT("Context scope does not start scan"), Panel->bScanStartPending || Panel->ScanService->IsScanActive());
    TestTrue(TEXT("Context scope does not queue mutations"), Panel->ActionQueue.IsEmpty());
    Panel->bScanStartPending = true;
    Panel->CancelScan(); Panel->HandleScanTimer(0, 0);
    TestEqual(TEXT("Cancel before duplicate scan starts is explicit"), Panel->LastResult.State, ESanitizerSessionState::Canceled);
    FSanitizerDuplicateGroup Group;
    Group.GroupId = TEXT("WidgetFixture"); Group.ProviderId = TEXT("Texture2D"); Group.Classification = ESanitizerClassification::SafeDuplicate; Group.CanonicalMemberIndex = 0;
    for (const FString& Package : { FString(TEXT("/Game/Shared/A")), FString(TEXT("/Game/Cleanup/B")), FString(TEXT("/Game/Cleanup/C")) })
    {
        FSanitizerDuplicateMember Member;
        Member.Record.PackageName = FName(*Package); Member.Record.PackagePath = FName(*FPackageName::GetLongPackagePath(Package));
        Member.Record.AssetData = FAssetData(Member.Record.PackageName, Member.Record.PackagePath, FName(*FPackageName::GetLongPackageAssetName(Package)), UTexture2D::StaticClass()->GetClassPathName());
        Member.Fingerprint.bDeepVerified = true; Member.Fingerprint.SchemaVersion = 2;
        Group.Members.Add(Member);
    }
    Panel->LastResult.State = ESanitizerSessionState::Completed; Panel->LastResult.Revision = 1;
    Panel->LastResult.Groups = { Group };
    Panel->RebuildFilteredGroupItems();
    Panel->OnSelectionChanged(Panel->GroupItems[0], ESelectInfo::Direct);
    Panel->AddSelectedToQueue();
    TestEqual(TEXT("Reviewed group explicitly queues"), Panel->ActionQueue.Num(), 1);
    Panel->bQueuePreflightPassed = true;
    Panel->ReviewedBatch.PlanReviews.AddDefaulted();
    Panel->ReviewedBatch.OperationId = TEXT("StaleBatchReview");
    Panel->ReviewedBatch.bReady = true;
    Panel->OnCanonicalChanged(Panel->CanonicalOptions[1], ESelectInfo::Direct);
    TestTrue(TEXT("Canonical change removes stale queued group"), Panel->ActionQueue.IsEmpty());
    TestFalse(TEXT("Canonical change invalidates preflight"), Panel->bQueuePreflightPassed);
    TestTrue(TEXT("Canonical change invalidates batch identity"), Panel->ReviewedBatch.OperationId.IsEmpty());
    Panel->AddSelectedToQueue();
    TestEqual(TEXT("Manual canonical leaves only scoped remaining source"), Panel->ActionQueue[0].SourceAssets.Num(), 1);
    TestEqual(TEXT("External reference member never becomes a source"), Panel->ActionQueue[0].SourceAssets[0].GetLongPackageName(), FString(TEXT("/Game/Cleanup/C")));
    const auto ReportPlan = Panel->ActionQueue[0];
    Panel->SetOrphanCleanupPackagePaths({ TEXT("/Game/Cleanup") });
    TestEqual(TEXT("Canonical selector belongs only to duplicate mode"), Panel->CanonicalCombo->GetVisibility(), EVisibility::Collapsed);
    TestFalse(TEXT("Orphan context scope does not start scan"), Panel->bOrphanScanStartPending || Panel->OrphanScanService->IsScanActive());
    TestTrue(TEXT("Scope change invalidates duplicate queue"), Panel->ActionQueue.IsEmpty());
    Panel->bOrphanScanStartPending = true;
    Panel->CancelOrphanScan(); Panel->HandleOrphanScanTimer(0, 0);
    TestEqual(TEXT("Cancel before orphan scan starts is explicit"), Panel->LastOrphanResult.State, ESanitizerSessionState::Canceled);
    TestTrue(TEXT("Early orphan cancellation marked in result"), Panel->LastOrphanResult.bCanceled);
    FSanitizerOrphanRecord Candidate;
    Candidate.AssetData = Group.Members[1].Record.AssetData; Candidate.PackageName = Candidate.AssetData.PackageName;
    Candidate.Classification = ESanitizerOrphanClassification::Candidate; Candidate.bRegistryReady = true; Candidate.bSupportedState = true; Candidate.EvidenceIdentity = TEXT("FixtureEvidence");
    Panel->LastOrphanResult.bCanceled = false;
    Panel->LastOrphanResult.State = ESanitizerSessionState::Completed; Panel->LastOrphanResult.Revision = 1;
    Panel->LastOrphanResult.Request.PackagePaths = { FName(TEXT("/Game/Cleanup")) };
    Panel->LastOrphanResult.Orphans = { Candidate };
    Panel->SelectedOrphan = MakeShared<FSanitizerOrphanRecord>(Candidate);
    Panel->DeleteSelectedOrphan();
    TestEqual(TEXT("Orphan action only adds a deletion plan"), Panel->DeletionQueue.Num(), 1);
    TestEqual(TEXT("Queue inclusion leaves scan candidate intact"), Panel->LastOrphanResult.Orphans.Num(), 1);
    TestTrue(TEXT("Queue inclusion is not preflight approval"), Panel->ReviewedDeletions.IsEmpty());
    Panel->InvalidateResults();
    TestTrue(TEXT("Cross-mode invalidation clears all plans"), Panel->DeletionQueue.IsEmpty() && Panel->ActionQueue.IsEmpty());
    TestEqual(TEXT("Duplicate result stale"), Panel->LastResult.State, ESanitizerSessionState::Stale);
    TestEqual(TEXT("Orphan result stale"), Panel->LastOrphanResult.State, ESanitizerSessionState::Stale);
    TestEqual(TEXT("Summary recomputed to empty after invalidation"), Panel->LastOrphanResult.Summary.OrphanAssets, 0);
    Panel->ActionQueue = { ReportPlan, ReportPlan };
    Panel->ActionQueue[1].PlanId = TEXT("UnsubmittedGroup");
    Panel->ActionQueue[1].SourceAssets = { FSoftObjectPath(TEXT("/Game/Cleanup/Unsubmitted.Unsubmitted")) };
    FSanitizerConsolidationBatchResult Batch;
    Batch.PlanResults.SetNum(2);
    Batch.PlanResults[0].Status = ESanitizerExecutionStatus::ConsolidationReportedFailure;
    Panel->PresentDuplicateBatchResult(Batch);
    TestTrue(TEXT("Partial batch invalidates all executable plans"), Panel->ActionQueue.IsEmpty());
    const FString Report = Panel->QueueDetails->GetText().ToString();
    TestTrue(TEXT("Read-only report retains exact untouched target"), Report.Contains(TEXT("/Game/Cleanup/Unsubmitted.Unsubmitted")));
    TestTrue(TEXT("Read-only report labels untouched item as needing new review"), Report.Contains(TEXT("未执行，需重新扫描/审查")));
    Panel->ActionQueue = { ReportPlan };
    Batch.PlanResults.SetNum(1); Batch.PlanResults[0] = {};
    Panel->bQueuePreflightPassed = true;
    Panel->PresentDuplicateBatchResult(Batch);
    TestEqual(TEXT("No native call retains original queued targets"), Panel->ActionQueue.Num(), 1);
    TestFalse(TEXT("No native call clears old confirmation"), Panel->bQueuePreflightPassed);
    return true;
}
#endif
