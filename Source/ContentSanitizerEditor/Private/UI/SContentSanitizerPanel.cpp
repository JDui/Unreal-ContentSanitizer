#include "UI/SContentSanitizerPanel.h"

#include "ContentSanitizerConsolidationService.h"
#include "Misc/MessageDialog.h"
#include "Operations/SanitizerActionPlan.h"
#include "Scanner/SanitizerScanService.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"

namespace ContentSanitizerPanel
{
    static bool IsPathWithin(const FString& PackagePath, const FString& RootPath)
    {
        return PackagePath == RootPath || PackagePath.StartsWith(RootPath + TEXT("/"));
    }
}

void SContentSanitizerPanel::Construct(const FArguments& InArgs)
{
    ScanService = MakeUnique<FSanitizerScanService>();
    ClassificationFilters = {
        MakeShared<FSanitizerClassificationFilter>(FSanitizerClassificationFilter { TEXT("全部类别"), TOptional<ESanitizerClassification>() }),
        MakeShared<FSanitizerClassificationFilter>(FSanitizerClassificationFilter { TEXT("安全重复项"), ESanitizerClassification::SafeDuplicate }),
        MakeShared<FSanitizerClassificationFilter>(FSanitizerClassificationFilter { TEXT("需要复核"), ESanitizerClassification::ReviewRequired }),
        MakeShared<FSanitizerClassificationFilter>(FSanitizerClassificationFilter { TEXT("相似项"), ESanitizerClassification::Similar }),
        MakeShared<FSanitizerClassificationFilter>(FSanitizerClassificationFilter { TEXT("冲突"), ESanitizerClassification::Conflict })
    };
    SelectedClassificationFilter = ClassificationFilters[0];
    ChildSlot
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(6)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(FText::FromString(TEXT("开始扫描"))).OnClicked(this, &SContentSanitizerPanel::Scan).IsEnabled(this, &SContentSanitizerPanel::CanStartScan)]
            + SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(FText::FromString(TEXT("取消扫描"))).OnClicked(this, &SContentSanitizerPanel::CancelScan).IsEnabled(this, &SContentSanitizerPanel::CanCancelScan)]
            + SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(FText::FromString(TEXT("加入安全整理队列"))).OnClicked(this, &SContentSanitizerPanel::AddSelectedToQueue).IsEnabled(this, &SContentSanitizerPanel::CanAddSelectedToQueue)]
            + SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(FText::FromString(TEXT("预检"))).OnClicked(this, &SContentSanitizerPanel::PreflightQueue).IsEnabled(this, &SContentSanitizerPanel::CanPreflightQueue)]
            + SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(FText::FromString(TEXT("执行整理并重定向"))).OnClicked(this, &SContentSanitizerPanel::ExecuteQueue).IsEnabled(this, &SContentSanitizerPanel::CanExecuteQueue)]
            + SHorizontalBox::Slot().AutoWidth().Padding(10, 2, 2, 2).VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(TEXT("重复类别：")))]
            + SHorizontalBox::Slot().AutoWidth().Padding(2)
            [
                SAssignNew(ClassificationFilterCombo, SComboBox<TSharedPtr<FSanitizerClassificationFilter>>)
                .OptionsSource(&ClassificationFilters)
                .InitiallySelectedItem(SelectedClassificationFilter)
                .OnGenerateWidget(this, &SContentSanitizerPanel::GenerateClassificationFilterWidget)
                .OnSelectionChanged(this, &SContentSanitizerPanel::OnClassificationFilterChanged)
                [SNew(STextBlock).Text(this, &SContentSanitizerPanel::GetClassificationFilterText)]
            ]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(6, 2)[SAssignNew(ScopeText, STextBlock)]
        + SVerticalBox::Slot().AutoHeight().Padding(6, 2)[SAssignNew(ScanProgressBar, SProgressBar).Percent(0.0f)]
        + SVerticalBox::Slot().AutoHeight().Padding(6, 2)[SAssignNew(ProgressText, STextBlock).Text(FText::FromString(TEXT("尚未开始扫描")))]
        + SVerticalBox::Slot().AutoHeight().Padding(6, 2)[SAssignNew(SummaryText, STextBlock).Text(FText::FromString(TEXT("就绪。选择清理目标后点击“开始扫描”；扫描为只读操作。")))]
        + SVerticalBox::Slot().FillHeight(1.f).Padding(6)
        [
            SNew(SSplitter)
            + SSplitter::Slot().Value(0.65f)[SAssignNew(GroupList, SListView<TSharedPtr<FSanitizerDuplicateGroup>>).ListItemsSource(&GroupItems).OnGenerateRow(this, &SContentSanitizerPanel::GenerateGroupRow).OnSelectionChanged(this, &SContentSanitizerPanel::OnSelectionChanged)]
            + SSplitter::Slot().Value(0.35f)[SAssignNew(InspectorText, STextBlock).AutoWrapText(true).Text(FText::FromString(TEXT("请选择一个重复组，查看判定依据和设置差异。")))]
        ]
    ];
    RefreshScopeText();
}

FReply SContentSanitizerPanel::Scan()
{
    BeginIncrementalScan();
    return FReply::Handled();
}

void SContentSanitizerPanel::SetCleanupPackagePaths(const TArray<FString>& PackagePaths)
{
    if (!CanStartScan())
    {
        SummaryText->SetText(FText::FromString(TEXT("当前扫描尚未结束，无法变更清理目标。")));
        return;
    }
    CleanupPackagePaths.Reset();
    for (const FString& Path : PackagePaths)
    {
        FString Normalized = Path;
        Normalized.RemoveFromEnd(TEXT("/"));
        if (!Normalized.IsEmpty()) { CleanupPackagePaths.AddUnique(FName(*Normalized)); }
    }
    if (CleanupPackagePaths.IsEmpty()) { CleanupPackagePaths.Add(FName(TEXT("/Game"))); }
    SelectedGroup.Reset();
    ActionQueue.Reset();
    bQueuePreflightPassed = false;
    LastResult = {};
    GroupItems.Reset();
    if (GroupList.IsValid()) { GroupList->RequestListRefresh(); }
    RefreshScopeText();
    ProgressText->SetText(FText::FromString(TEXT("尚未开始扫描")));
    ScanProgressBar->SetPercent(0.0f);
    SummaryText->SetText(FText::FromString(TEXT("清理目标已设置。比较范围保持为整个 /Game；点击“开始扫描”后才会执行扫描。")));
}

void SContentSanitizerPanel::RefreshScopeText()
{
    if (!ScopeText.IsValid()) { return; }
    const FString CleanupText = FString::JoinBy(CleanupPackagePaths, TEXT("，"), [](FName Path) { return Path.ToString(); });
    const FString ComparisonText = FString::JoinBy(ComparisonPackagePaths, TEXT("，"), [](FName Path) { return Path.ToString(); });
    ScopeText->SetText(FText::FromString(FString::Printf(TEXT("清理目标：%s　|　重复比较范围：%s（只有清理目标内资产允许被整理）"), *CleanupText, *ComparisonText)));
}

bool SContentSanitizerPanel::IsMemberInCleanupScope(const FSanitizerDuplicateMember& Member) const
{
    const FString PackagePath = Member.Record.PackagePath.ToString();
    return CleanupPackagePaths.ContainsByPredicate([&PackagePath](FName Root)
    {
        return ContentSanitizerPanel::IsPathWithin(PackagePath, Root.ToString());
    });
}

void SContentSanitizerPanel::BeginIncrementalScan()
{
    SelectedGroup.Reset();
    ActionQueue.Reset();
    bQueuePreflightPassed = false;
    GroupItems.Reset();
    if (GroupList.IsValid()) { GroupList->RequestListRefresh(); }
    bScanStartPending = true;
    bScanCanceledBeforeStart = false;
    ScanProgressBar->SetPercent(0.0f);
    ProgressText->SetText(FText::FromString(TEXT("正在准备扫描…")));
    SummaryText->SetText(FText::FromString(TEXT("扫描进行中：从整个比较范围寻找与清理目标重复的资产。可随时取消；扫描不会修改项目内容。")));
    if (!bScanTimerRegistered)
    {
        bScanTimerRegistered = true;
        RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SContentSanitizerPanel::HandleScanTimer));
    }
}

FReply SContentSanitizerPanel::CancelScan()
{
    if (bScanStartPending)
    {
        bScanStartPending = false;
        bScanCanceledBeforeStart = true;
        ProgressText->SetText(FText::FromString(TEXT("扫描已取消。")));
        SummaryText->SetText(FText::FromString(TEXT("扫描在开始前已取消，未修改任何项目内容。")));
    }
    else if (ScanService->IsScanActive())
    {
        ScanService->RequestCancel();
        ProgressText->SetText(FText::FromString(TEXT("正在取消扫描…")));
    }
    return FReply::Handled();
}

EActiveTimerReturnType SContentSanitizerPanel::HandleScanTimer(double CurrentTime, float DeltaTime)
{
    if (bScanCanceledBeforeStart)
    {
        bScanCanceledBeforeStart = false;
        bScanTimerRegistered = false;
        return EActiveTimerReturnType::Stop;
    }
    if (bScanStartPending)
    {
        bScanStartPending = false;
        FSanitizerScanRequest Request;
        Request.PackagePaths = ComparisonPackagePaths;
        Request.bIncludePluginContent = ComparisonPackagePaths.ContainsByPredicate([](FName Path)
        {
            const FString Value = Path.ToString();
            return Value != TEXT("/Game") && !Value.StartsWith(TEXT("/Game/"));
        });
        Request.bIncludeDeveloperContent = ComparisonPackagePaths.ContainsByPredicate([](FName Path)
        {
            const FString Value = Path.ToString();
            return Value == TEXT("/Game/Developers") || Value.StartsWith(TEXT("/Game/Developers/"));
        });
        if (Request.bIncludeDeveloperContent)
        {
            Request.ExcludedPackagePaths.Remove(FName(TEXT("/Game/Developers")));
        }
        ScanService->BeginScan(Request);
    }
    if (ScanService->IsScanActive())
    {
        ScanService->TickScan(0.006, 256);
        RefreshScanProgress();
    }
    if (!ScanService->IsScanActive())
    {
        bScanTimerRegistered = false;
        PublishScanResult();
        return EActiveTimerReturnType::Stop;
    }
    return EActiveTimerReturnType::Continue;
}

void SContentSanitizerPanel::RefreshScanProgress()
{
    const FSanitizerScanProgress& Progress = ScanService->GetProgress();
    ScanProgressBar->SetPercent(Progress.GetFraction());
    FString Stage;
    switch (Progress.State)
    {
    case ESanitizerSessionState::Bucketing: Stage = TEXT("阶段 1/2：清点比较范围并建立候选桶"); break;
    case ESanitizerSessionState::Fingerprinting: Stage = TEXT("阶段 2/2：读取缓存或计算增量指纹"); break;
    case ESanitizerSessionState::CancelRequested: Stage = TEXT("正在取消扫描"); break;
    case ESanitizerSessionState::Canceled: Stage = TEXT("扫描已取消"); break;
    case ESanitizerSessionState::Completed: Stage = TEXT("扫描完成"); break;
    default: Stage = TEXT("正在准备扫描"); break;
    }
    const FString Current = Progress.CurrentAsset.IsEmpty() ? TEXT("—") : Progress.CurrentAsset;
    ProgressText->SetText(FText::FromString(FString::Printf(TEXT("%s　%d/%d（%.1f%%）\n当前资产：%s"), *Stage, Progress.ProcessedAssets, Progress.TotalAssets, Progress.GetFraction() * 100.0f, *Current)));
}

void SContentSanitizerPanel::PublishScanResult()
{
    LastResult = ScanService->GetResult();
    RefreshScanProgress();
    if (LastResult.State == ESanitizerSessionState::Canceled)
    {
        SummaryText->SetText(FText::FromString(TEXT("扫描已取消。扫描为只读操作，未修改任何项目内容。")));
        return;
    }
    ApplyCleanupScopeToResult();
    RebuildFilteredGroupItems();
    RefreshPresentation();
}

void SContentSanitizerPanel::ApplyCleanupScopeToResult()
{
    TArray<FSanitizerDuplicateGroup> ScopedGroups;
    FSanitizerScanSummary ScopedSummary = LastResult.Summary;
    ScopedSummary.DuplicateGroups = 0;
    ScopedSummary.SafeGroups = 0;
    ScopedSummary.ReviewGroups = 0;
    ScopedSummary.SimilarGroups = 0;
    ScopedSummary.ConflictGroups = 0;
    ScopedSummary.EstimatedReclaimableSize = 0;

    for (FSanitizerDuplicateGroup& Group : LastResult.Groups)
    {
        const bool bTouchesCleanupScope = Group.Members.ContainsByPredicate([this](const FSanitizerDuplicateMember& Member)
        {
            return IsMemberInCleanupScope(Member);
        });
        if (!bTouchesCleanupScope) { continue; }

        const int32 ExternalCanonicalIndex = Group.Members.IndexOfByPredicate([this](const FSanitizerDuplicateMember& Member)
        {
            return !IsMemberInCleanupScope(Member);
        });
        if (ExternalCanonicalIndex != INDEX_NONE)
        {
            Group.CanonicalMemberIndex = ExternalCanonicalIndex;
        }

        Group.EstimatedReclaimableSize = 0;
        if (Group.Classification == ESanitizerClassification::SafeDuplicate)
        {
            for (int32 Index = 0; Index < Group.Members.Num(); ++Index)
            {
                if (Index != Group.CanonicalMemberIndex && IsMemberInCleanupScope(Group.Members[Index]))
                {
                    Group.EstimatedReclaimableSize += FMath::Max<int64>(0, Group.Members[Index].Record.EstimatedDiskSize);
                }
            }
        }

        ++ScopedSummary.DuplicateGroups;
        switch (Group.Classification)
        {
        case ESanitizerClassification::SafeDuplicate: ++ScopedSummary.SafeGroups; break;
        case ESanitizerClassification::ReviewRequired: ++ScopedSummary.ReviewGroups; break;
        case ESanitizerClassification::Similar: ++ScopedSummary.SimilarGroups; break;
        case ESanitizerClassification::Conflict: ++ScopedSummary.ConflictGroups; break;
        }
        ScopedSummary.EstimatedReclaimableSize += Group.EstimatedReclaimableSize;
        ScopedGroups.Add(MoveTemp(Group));
    }

    LastResult.Groups = MoveTemp(ScopedGroups);
    LastResult.Summary = MoveTemp(ScopedSummary);
}

TSharedRef<SWidget> SContentSanitizerPanel::GenerateClassificationFilterWidget(TSharedPtr<FSanitizerClassificationFilter> Item) const
{
    return SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? Item->Label : TEXT("全部类别")));
}

void SContentSanitizerPanel::OnClassificationFilterChanged(TSharedPtr<FSanitizerClassificationFilter> Item, ESelectInfo::Type SelectInfo)
{
    if (!Item.IsValid()) { return; }
    SelectedClassificationFilter = Item;
    SelectedGroup.Reset();
    InspectorText->SetText(FText::FromString(BuildInspectorText()));
    RebuildFilteredGroupItems();
    RefreshPresentation();
}

FText SContentSanitizerPanel::GetClassificationFilterText() const
{
    return FText::FromString(SelectedClassificationFilter.IsValid() ? SelectedClassificationFilter->Label : TEXT("全部类别"));
}

void SContentSanitizerPanel::RebuildFilteredGroupItems()
{
    GroupItems.Reset();
    for (const FSanitizerDuplicateGroup& Group : LastResult.Groups)
    {
        if (!SelectedClassificationFilter.IsValid() || !SelectedClassificationFilter->Classification.IsSet() || Group.Classification == SelectedClassificationFilter->Classification.GetValue())
        {
            GroupItems.Add(MakeShared<FSanitizerDuplicateGroup>(Group));
        }
    }
    if (GroupList.IsValid()) { GroupList->RequestListRefresh(); }
}

FReply SContentSanitizerPanel::AddSelectedToQueue()
{
    FString Error;
    FSanitizerActionPlan Plan;
    if (!SelectedGroup.IsValid() || !FSanitizerActionPlanner::CreatePlanForScope(*SelectedGroup, LastResult.Revision, CleanupPackagePaths, Plan, Error))
    {
        SummaryText->SetText(FText::FromString(Error.IsEmpty() ? TEXT("请先选择一个“安全重复项”组。") : Error));
        return FReply::Handled();
    }
    if (ActionQueue.ContainsByPredicate([&Plan](const FSanitizerActionPlan& Existing) { return Existing.PlanId == Plan.PlanId; }))
    {
        SummaryText->SetText(FText::FromString(TEXT("该重复组已在整理队列中。")));
        return FReply::Handled();
    }
    ActionQueue.Add(MoveTemp(Plan));
    bQueuePreflightPassed = false;
    SummaryText->SetText(FText::FromString(FString::Printf(TEXT("已加入 %d 个安全整理计划。只有清理目标内资产会作为来源资产；执行前必须先通过预检。"), ActionQueue.Num())));
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
    SummaryText->SetText(FText::FromString(FString::Printf(TEXT("预检：%d/%d 个计划可执行。%s"), Ready, ActionQueue.Num(), Messages.IsEmpty() ? TEXT("尚未修改任何内容。") : *FString::Join(Messages, TEXT(" ")))));
    return FReply::Handled();
}

FReply SContentSanitizerPanel::ExecuteQueue()
{
    if (!CanExecuteQueue()) { return FReply::Handled(); }

    int32 SourceCount = 0;
    for (const FSanitizerActionPlan& Plan : ActionQueue) { SourceCount += Plan.SourceAssets.Num(); }
    const FText Confirmation = FText::FromString(FString::Printf(
        TEXT("确定将清理目标内的 %d 个重复资产整理到 %d 个主资产吗？\n\n队列中的所有计划均已通过预检。Unreal 将重写引用并保存受影响的包，此操作会修改项目内容。"),
        SourceCount, ActionQueue.Num()));
    if (FMessageDialog::Open(EAppMsgType::YesNo, Confirmation, FText::FromString(TEXT("确认整理重复内容"))) != EAppReturnType::Yes)
    {
        return FReply::Handled();
    }

    FContentSanitizerConsolidationService Service;
    int32 Successes = 0;
    int32 NotExecuted = 0;
    int32 NeedsReview = 0;
    TArray<FString> Messages;
    TArray<FSanitizerActionPlan> RetryablePlans;
    bool bRequireRescan = false;
    for (int32 PlanIndex = 0; PlanIndex < ActionQueue.Num(); ++PlanIndex)
    {
        const FSanitizerActionPlan& Plan = ActionQueue[PlanIndex];
        const FSanitizerExecutionResult Result = Service.Execute(Plan);
        Successes += Result.IsSucceeded() ? 1 : 0;
        if (Result.IsSafeToRetryWithoutRescan())
        {
            for (int32 RemainingIndex = PlanIndex; RemainingIndex < ActionQueue.Num(); ++RemainingIndex)
            {
                RetryablePlans.Add(ActionQueue[RemainingIndex]);
                ++NotExecuted;
            }
            Messages.Add(TEXT("执行在首次“未执行”结果处停止，后续计划没有调用整理接口。"));
            Messages.Append(Result.Messages);
            break;
        }
        else if (!Result.IsSucceeded())
        {
            ++NeedsReview;
            bRequireRescan = true;
            NotExecuted += ActionQueue.Num() - PlanIndex - 1;
            Messages.Add(TEXT("执行已停止：整理接口已被调用但结果未通过验证。队列已清空，必须重新扫描后再决定后续操作。"));
            Messages.Append(Result.Messages);
            break;
        }
        Messages.Append(Result.Messages);
    }
    const int32 Attempted = ActionQueue.Num();
    if (bRequireRescan) { ActionQueue.Reset(); }
    else { ActionQueue = MoveTemp(RetryablePlans); }
    bQueuePreflightPassed = false;
    SummaryText->SetText(FText::FromString(FString::Printf(TEXT("整理结果：成功 %d/%d；未执行并保留在队列 %d；已调用整理接口但需要重新扫描/人工检查 %d。%s"), Successes, Attempted, NotExecuted, NeedsReview, *FString::Join(Messages, TEXT(" ")))));
    return FReply::Handled();
}

bool SContentSanitizerPanel::CanAddSelectedToQueue() const
{
    return !ScanService->IsScanActive() && !bScanStartPending && SelectedGroup.IsValid() && SelectedGroup->Classification == ESanitizerClassification::SafeDuplicate;
}

bool SContentSanitizerPanel::CanPreflightQueue() const
{
    return !ScanService->IsScanActive() && !bScanStartPending && !ActionQueue.IsEmpty();
}

bool SContentSanitizerPanel::CanExecuteQueue() const
{
    return !ScanService->IsScanActive() && !bScanStartPending && !ActionQueue.IsEmpty() && bQueuePreflightPassed;
}

bool SContentSanitizerPanel::CanStartScan() const
{
    return !bScanStartPending && !ScanService->IsScanActive();
}

bool SContentSanitizerPanel::CanCancelScan() const
{
    return bScanStartPending || ScanService->IsScanActive();
}

TSharedRef<ITableRow> SContentSanitizerPanel::GenerateGroupRow(TSharedPtr<FSanitizerDuplicateGroup> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
    return SNew(STableRow<TSharedPtr<FSanitizerDuplicateGroup>>, OwnerTable)[SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%s | %d 个资产 | 主资产 %s"), *SanitizerClassificationToText(Item->Classification), Item->Members.Num(), *Item->Members[Item->CanonicalMemberIndex].Record.GetObjectPath())))];
}

void SContentSanitizerPanel::OnSelectionChanged(TSharedPtr<FSanitizerDuplicateGroup> Item, ESelectInfo::Type SelectInfo)
{
    SelectedGroup = Item;
    InspectorText->SetText(FText::FromString(BuildInspectorText()));
}

void SContentSanitizerPanel::RefreshPresentation()
{
    if (GroupList.IsValid()) { GroupList->RequestListRefresh(); }
    FString Summary = FString::Printf(TEXT("比较范围已清点：%d | 候选：%d | 缓存命中：%d | 增量计算：%d | 涉及清理目标的重复组：%d | 当前显示：%d | 安全：%d | 需复核：%d | 相似：%d | 冲突：%d | 目标范围安全可回收估算：%lld 字节"), LastResult.Summary.InventoriedAssets, LastResult.Summary.CandidateAssets, LastResult.Summary.CachedFingerprints, LastResult.Summary.IncrementalFingerprints, LastResult.Summary.DuplicateGroups, GroupItems.Num(), LastResult.Summary.SafeGroups, LastResult.Summary.ReviewGroups, LastResult.Summary.SimilarGroups, LastResult.Summary.ConflictGroups, LastResult.Summary.EstimatedReclaimableSize);
    if (!LastResult.Errors.IsEmpty())
    {
        Summary += FString::Printf(TEXT(" | %d 个资产无法生成指纹：%s"), LastResult.Errors.Num(), *FString::Join(LastResult.Errors, TEXT(" ")));
    }
    if (!LastResult.CacheMessages.IsEmpty())
    {
        Summary += FString::Printf(TEXT(" | 缓存提示：%s"), *FString::Join(LastResult.CacheMessages, TEXT(" ")));
    }
    SummaryText->SetText(FText::FromString(Summary));
}

FString SContentSanitizerPanel::BuildInspectorText() const
{
    if (!SelectedGroup.IsValid()) { return TEXT("请选择一个重复组，查看判定依据和设置差异。"); }
    FString Text = FString::Printf(TEXT("分类：%s\n载荷：相同\n行为设置：%s\n主资产：%s\n\n成员："), *SanitizerClassificationToText(SelectedGroup->Classification), SelectedGroup->Classification == ESanitizerClassification::SafeDuplicate ? TEXT("相同") : TEXT("不同"), *SelectedGroup->Members[SelectedGroup->CanonicalMemberIndex].Record.GetObjectPath());
    for (int32 Index = 0; Index < SelectedGroup->Members.Num(); ++Index)
    {
        const FSanitizerDuplicateMember& Member = SelectedGroup->Members[Index];
        const TCHAR* ScopeLabel = IsMemberInCleanupScope(Member) ? TEXT("清理目标") : TEXT("比较参照");
        const TCHAR* CanonicalLabel = Index == SelectedGroup->CanonicalMemberIndex ? TEXT("，主资产") : TEXT("");
        Text += FString::Printf(TEXT("\n- [%s%s] %s"), ScopeLabel, CanonicalLabel, *Member.Record.GetObjectPath());
    }
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
                        Text += FString::Printf(TEXT("\n\n%s 的设置差异："), *Member.Record.GetObjectPath());
                        bAddedHeader = true;
                    }
                    Text += FString::Printf(TEXT("\n- %s：主资产=%s，候选资产=%s"), *Key.ToString(), CanonicalValue ? **CanonicalValue : TEXT("<缺失>") , MemberValue ? **MemberValue : TEXT("<缺失>"));
                }
            }
        }
    }
    for (const FString& Reason : SelectedGroup->Reasons) { Text += FString::Printf(TEXT("\n\n%s"), *Reason); }
    return Text;
}
