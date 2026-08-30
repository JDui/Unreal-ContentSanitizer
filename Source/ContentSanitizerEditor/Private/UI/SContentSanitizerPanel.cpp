#include "UI/SContentSanitizerPanel.h"

#include "ContentSanitizerConsolidationService.h"
#include "Misc/MessageDialog.h"
#include "Operations/SanitizerActionPlan.h"
#include "Scanner/SanitizerScanService.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"

void SContentSanitizerPanel::Construct(const FArguments& InArgs)
{
    ScanService = MakeUnique<FSanitizerScanService>();
    ChildSlot
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(6)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(FText::FromString(TEXT("Scan /Game"))).OnClicked(this, &SContentSanitizerPanel::Scan)]
            + SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(FText::FromString(TEXT("Add Safe Group"))).OnClicked(this, &SContentSanitizerPanel::AddSelectedToQueue).IsEnabled(this, &SContentSanitizerPanel::CanAddSelectedToQueue)]
            + SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(FText::FromString(TEXT("Dry Run / Preflight"))).OnClicked(this, &SContentSanitizerPanel::PreflightQueue).IsEnabled(this, &SContentSanitizerPanel::CanPreflightQueue)]
            + SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(FText::FromString(TEXT("Consolidate Queue"))).OnClicked(this, &SContentSanitizerPanel::ExecuteQueue).IsEnabled(this, &SContentSanitizerPanel::CanExecuteQueue)]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(6, 2)[SAssignNew(SummaryText, STextBlock).Text(FText::FromString(TEXT("Ready. Scan is read-only; only Safe Duplicate groups can enter the queue.")))]
        + SVerticalBox::Slot().FillHeight(1.f).Padding(6)
        [
            SNew(SSplitter)
            + SSplitter::Slot().Value(0.65f)[SAssignNew(GroupList, SListView<TSharedPtr<FSanitizerDuplicateGroup>>).ListItemsSource(&GroupItems).OnGenerateRow(this, &SContentSanitizerPanel::GenerateGroupRow).OnSelectionChanged(this, &SContentSanitizerPanel::OnSelectionChanged)]
            + SSplitter::Slot().Value(0.35f)[SAssignNew(InspectorText, STextBlock).AutoWrapText(true).Text(FText::FromString(TEXT("Select a duplicate group to inspect its proof and settings.")))]
        ]
    ];
}

FReply SContentSanitizerPanel::Scan()
{
    LastResult = ScanService->Scan(FSanitizerScanRequest());
    SelectedGroup.Reset();
    ActionQueue.Reset();
    bQueuePreflightPassed = false;
    GroupItems.Reset();
    for (const FSanitizerDuplicateGroup& Group : LastResult.Groups) { GroupItems.Add(MakeShared<FSanitizerDuplicateGroup>(Group)); }
    RefreshPresentation();
    return FReply::Handled();
}

FReply SContentSanitizerPanel::AddSelectedToQueue()
{
    FString Error;
    FSanitizerActionPlan Plan;
    if (!SelectedGroup.IsValid() || !FSanitizerActionPlanner::CreatePlan(*SelectedGroup, LastResult.Revision, Plan, Error))
    {
        SummaryText->SetText(FText::FromString(Error.IsEmpty() ? TEXT("Select a Safe Duplicate group first.") : Error));
        return FReply::Handled();
    }
    if (ActionQueue.ContainsByPredicate([&Plan](const FSanitizerActionPlan& Existing) { return Existing.PlanId == Plan.PlanId; }))
    {
        SummaryText->SetText(FText::FromString(TEXT("This duplicate group is already in the action queue.")));
        return FReply::Handled();
    }
    ActionQueue.Add(MoveTemp(Plan));
    bQueuePreflightPassed = false;
    SummaryText->SetText(FText::FromString(FString::Printf(TEXT("%d safe action plan(s) queued. Run preflight before consolidation."), ActionQueue.Num())));
    return FReply::Handled();
}

FReply SContentSanitizerPanel::PreflightQueue()
{
    FContentSanitizerConsolidationService Service;
    int32 Ready = 0;
    TArray<FString> Messages;
    for (const FSanitizerActionPlan& Plan : ActionQueue)
    {
        const FSanitizerPreflightResult Result = Service.Preflight(Plan);
        Ready += Result.IsReady() ? 1 : 0;
        Messages.Append(Result.Messages);
    }
    bQueuePreflightPassed = !ActionQueue.IsEmpty() && Ready == ActionQueue.Num();
    SummaryText->SetText(FText::FromString(FString::Printf(TEXT("Preflight: %d/%d plan(s) ready. %s"), Ready, ActionQueue.Num(), Messages.IsEmpty() ? TEXT("No mutation was performed.") : *FString::Join(Messages, TEXT(" ")))));
    return FReply::Handled();
}

FReply SContentSanitizerPanel::ExecuteQueue()
{
    if (!CanExecuteQueue()) { return FReply::Handled(); }

    int32 SourceCount = 0;
    for (const FSanitizerActionPlan& Plan : ActionQueue) { SourceCount += Plan.SourceAssets.Num(); }
    const FText Confirmation = FText::FromString(FString::Printf(
        TEXT("Consolidate %d source asset(s) into %d canonical asset(s)?\n\nPreflight passed for every queued action. Unreal will rewrite references and save affected packages. This operation changes project content."),
        SourceCount, ActionQueue.Num()));
    if (FMessageDialog::Open(EAppMsgType::YesNo, Confirmation, FText::FromString(TEXT("Confirm Content Consolidation"))) != EAppReturnType::Yes)
    {
        return FReply::Handled();
    }

    FContentSanitizerConsolidationService Service;
    int32 Successes = 0;
    TArray<FString> Messages;
    TArray<FSanitizerActionPlan> FailedPlans;
    for (const FSanitizerActionPlan& Plan : ActionQueue)
    {
        const FSanitizerExecutionResult Result = Service.Execute(Plan);
        Successes += Result.bSucceeded ? 1 : 0;
        if (!Result.bSucceeded) { FailedPlans.Add(Plan); }
        Messages.Append(Result.Messages);
    }
    const int32 Attempted = ActionQueue.Num();
    ActionQueue = MoveTemp(FailedPlans);
    bQueuePreflightPassed = false;
    SummaryText->SetText(FText::FromString(FString::Printf(TEXT("Consolidation completed for %d/%d plan(s). %s"), Successes, Attempted, *FString::Join(Messages, TEXT(" ")))));
    return FReply::Handled();
}

bool SContentSanitizerPanel::CanAddSelectedToQueue() const
{
    return SelectedGroup.IsValid() && SelectedGroup->Classification == ESanitizerClassification::SafeDuplicate;
}

bool SContentSanitizerPanel::CanPreflightQueue() const
{
    return !ActionQueue.IsEmpty();
}

bool SContentSanitizerPanel::CanExecuteQueue() const
{
    return !ActionQueue.IsEmpty() && bQueuePreflightPassed;
}

TSharedRef<ITableRow> SContentSanitizerPanel::GenerateGroupRow(TSharedPtr<FSanitizerDuplicateGroup> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
    return SNew(STableRow<TSharedPtr<FSanitizerDuplicateGroup>>, OwnerTable)[SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%s | %d assets | %s"), *SanitizerClassificationToText(Item->Classification), Item->Members.Num(), *Item->Members[Item->CanonicalMemberIndex].Record.GetObjectPath())))];
}

void SContentSanitizerPanel::OnSelectionChanged(TSharedPtr<FSanitizerDuplicateGroup> Item, ESelectInfo::Type SelectInfo)
{
    SelectedGroup = Item;
    InspectorText->SetText(FText::FromString(BuildInspectorText()));
}

void SContentSanitizerPanel::RefreshPresentation()
{
    if (GroupList.IsValid()) { GroupList->RequestListRefresh(); }
    FString Summary = FString::Printf(TEXT("Inventory: %d | Candidates: %d | Groups: %d | Safe: %d | Review: %d | Conflict: %d | Estimated reclaimable: %lld bytes"), LastResult.Summary.InventoriedAssets, LastResult.Summary.CandidateAssets, LastResult.Summary.DuplicateGroups, LastResult.Summary.SafeGroups, LastResult.Summary.ReviewGroups, LastResult.Summary.ConflictGroups, LastResult.Summary.EstimatedReclaimableSize);
    if (!LastResult.Errors.IsEmpty())
    {
        Summary += FString::Printf(TEXT(" | %d asset(s) could not be fingerprinted: %s"), LastResult.Errors.Num(), *FString::Join(LastResult.Errors, TEXT(" ")));
    }
    SummaryText->SetText(FText::FromString(Summary));
}

FString SContentSanitizerPanel::BuildInspectorText() const
{
    if (!SelectedGroup.IsValid()) { return TEXT("Select a duplicate group to inspect its proof and settings."); }
    FString Text = FString::Printf(TEXT("Classification: %s\nPayload: Same\nBehavior settings: %s\nCanonical: %s\n\nMembers:"), *SanitizerClassificationToText(SelectedGroup->Classification), SelectedGroup->Classification == ESanitizerClassification::SafeDuplicate ? TEXT("Same") : TEXT("Different"), *SelectedGroup->Members[SelectedGroup->CanonicalMemberIndex].Record.GetObjectPath());
    for (const FSanitizerDuplicateMember& Member : SelectedGroup->Members) { Text += FString::Printf(TEXT("\n- %s"), *Member.Record.GetObjectPath()); }
    if (SelectedGroup->Members.IsValidIndex(SelectedGroup->CanonicalMemberIndex))
    {
        const FSanitizerDuplicateMember& Canonical = SelectedGroup->Members[SelectedGroup->CanonicalMemberIndex];
        for (const FSanitizerDuplicateMember& Member : SelectedGroup->Members)
        {
            if (&Member == &Canonical) { continue; }
            TArray<FName> Keys;
            Canonical.Fingerprint.Settings.GetKeys(Keys);
            for (const TPair<FName, FString>& Pair : Member.Fingerprint.Settings)
            {
                Keys.AddUnique(Pair.Key);
            }
            Keys.Sort(FNameLexicalLess());
            bool bAddedHeader = false;
            for (const FName& Key : Keys)
            {
                const FString* CanonicalValue = Canonical.Fingerprint.Settings.Find(Key);
                const FString* MemberValue = Member.Fingerprint.Settings.Find(Key);
                if ((!CanonicalValue && MemberValue) || (CanonicalValue && !MemberValue) || (CanonicalValue && MemberValue && *CanonicalValue != *MemberValue))
                {
                    if (!bAddedHeader)
                    {
                        Text += FString::Printf(TEXT("\n\nSettings differences for %s:"), *Member.Record.GetObjectPath());
                        bAddedHeader = true;
                    }
                    Text += FString::Printf(TEXT("\n- %s: canonical=%s, candidate=%s"), *Key.ToString(), CanonicalValue ? **CanonicalValue : TEXT("<missing>"), MemberValue ? **MemberValue : TEXT("<missing>"));
                }
            }
        }
    }
    for (const FString& Reason : SelectedGroup->Reasons) { Text += FString::Printf(TEXT("\n\n%s"), *Reason); }
    return Text;
}
