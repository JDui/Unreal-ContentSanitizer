#include "UI/SContentSanitizerPanel.h"

#include "ContentSanitizerConsolidationService.h"
#include "Editor.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/UObjectGlobals.h"
#include "Misc/MessageDialog.h"
#include "Operations/SanitizerActionPlan.h"
#include "Scanner/SanitizerScanService.h"
#include "Scanner/SanitizerOrphanScanService.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
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
    OrphanScanService = MakeUnique<FSanitizerOrphanScanService>();

    // Duplicate classification filters
    ClassificationFilters = {
        MakeShared<FSanitizerClassificationFilter>(FSanitizerClassificationFilter { TEXT("全部类别"), TOptional<ESanitizerClassification>() }),
        MakeShared<FSanitizerClassificationFilter>(FSanitizerClassificationFilter { TEXT("安全重复项"), ESanitizerClassification::SafeDuplicate }),
        MakeShared<FSanitizerClassificationFilter>(FSanitizerClassificationFilter { TEXT("需要复核"), ESanitizerClassification::ReviewRequired }),
        MakeShared<FSanitizerClassificationFilter>(FSanitizerClassificationFilter { TEXT("相似项"), ESanitizerClassification::Similar }),
        MakeShared<FSanitizerClassificationFilter>(FSanitizerClassificationFilter { TEXT("冲突"), ESanitizerClassification::Conflict })
    };
    SelectedClassificationFilter = ClassificationFilters[0];

    // Orphan filters
    OrphanTypeFilters = {
        MakeShared<FSanitizerOrphanTypeFilter>(FSanitizerOrphanTypeFilter { TEXT("全部类型"), ESanitizerOrphanAssetType::All }),
        MakeShared<FSanitizerOrphanTypeFilter>(FSanitizerOrphanTypeFilter { TEXT("贴图"), ESanitizerOrphanAssetType::Texture }),
        MakeShared<FSanitizerOrphanTypeFilter>(FSanitizerOrphanTypeFilter { TEXT("材质"), ESanitizerOrphanAssetType::Material }),
        MakeShared<FSanitizerOrphanTypeFilter>(FSanitizerOrphanTypeFilter { TEXT("静态网格体"), ESanitizerOrphanAssetType::StaticMesh }),
        MakeShared<FSanitizerOrphanTypeFilter>(FSanitizerOrphanTypeFilter { TEXT("骨骼网格体"), ESanitizerOrphanAssetType::SkeletalMesh })
    };
    SelectedOrphanTypeFilter = OrphanTypeFilters[0];
    for (const TCHAR* Label : { TEXT("材质绑定：全部"), TEXT("有非空材质槽"), TEXT("全部为空/无槽"), TEXT("元数据未知") }) { MaterialFilterOptions.Add(MakeShared<FString>(Label)); }

    OrphanClassificationFilters = {
        MakeShared<FSanitizerOrphanClassificationFilter>(FSanitizerOrphanClassificationFilter { TEXT("全部类别"), TOptional<ESanitizerOrphanClassification>() }),
        MakeShared<FSanitizerOrphanClassificationFilter>(FSanitizerOrphanClassificationFilter { TEXT("未发现外部引用（候选）"), ESanitizerOrphanClassification::Candidate }),
        MakeShared<FSanitizerOrphanClassificationFilter>(FSanitizerOrphanClassificationFilter { TEXT("已阻止"), ESanitizerOrphanClassification::Blocked })
    };
    SelectedOrphanClassificationFilter = OrphanClassificationFilters[0];

    ChildSlot
    [
        SNew(SVerticalBox)
        // Top mode selection bar
        + SVerticalBox::Slot().AutoHeight().Padding(6, 4, 6, 2)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().Padding(2)
            [
                SNew(SButton)
                .Text(FText::FromString(TEXT("【模式】内容重复整理")))
                .OnClicked_Lambda([this]() { SetViewMode(ESanitizerViewMode::DuplicateConsolidation); return FReply::Handled(); })
            ]
            + SHorizontalBox::Slot().AutoWidth().Padding(2)
            [
                SNew(SButton)
                .Text(FText::FromString(TEXT("【模式】清理孤岛素材")))
                .OnClicked_Lambda([this]() { SetViewMode(ESanitizerViewMode::OrphanSanitizer); return FReply::Handled(); })
            ]
        ]
        // Duplicate action bar
        + SVerticalBox::Slot().AutoHeight().Padding(6, 2)
        [
            SAssignNew(DuplicateToolbarBox, SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(FText::FromString(TEXT("开始扫描"))).OnClicked(this, &SContentSanitizerPanel::Scan).IsEnabled(this, &SContentSanitizerPanel::CanStartScan)]
            + SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(FText::FromString(TEXT("取消扫描"))).OnClicked(this, &SContentSanitizerPanel::CancelScan).IsEnabled(this, &SContentSanitizerPanel::CanCancelScan)]
            + SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(FText::FromString(TEXT("加入安全整理队列"))).OnClicked(this, &SContentSanitizerPanel::AddSelectedToQueue).IsEnabled(this, &SContentSanitizerPanel::CanAddSelectedToQueue)]
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
        // Orphan action bar
        + SVerticalBox::Slot().AutoHeight().Padding(6, 2)
        [
            SAssignNew(OrphanToolbarBox, SHorizontalBox)
            .Visibility(EVisibility::Collapsed)
            + SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(FText::FromString(TEXT("开始扫描孤岛"))).OnClicked(this, &SContentSanitizerPanel::ScanOrphans).IsEnabled(this, &SContentSanitizerPanel::CanStartOrphanScan)]
            + SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(FText::FromString(TEXT("取消扫描"))).OnClicked(this, &SContentSanitizerPanel::CancelOrphanScan).IsEnabled(this, &SContentSanitizerPanel::CanCancelOrphanScan)]
            + SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(FText::FromString(TEXT("加入删除队列"))).OnClicked(this, &SContentSanitizerPanel::DeleteSelectedOrphan).IsEnabled(this, &SContentSanitizerPanel::CanDeleteSelectedOrphan)]
            + SHorizontalBox::Slot().AutoWidth().Padding(10, 2, 2, 2).VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(TEXT("资产类型：")))]
            + SHorizontalBox::Slot().AutoWidth().Padding(2)
            [
                SAssignNew(OrphanTypeFilterCombo, SComboBox<TSharedPtr<FSanitizerOrphanTypeFilter>>)
                .OptionsSource(&OrphanTypeFilters)
                .InitiallySelectedItem(SelectedOrphanTypeFilter)
                .OnGenerateWidget(this, &SContentSanitizerPanel::GenerateOrphanTypeFilterWidget)
                .OnSelectionChanged(this, &SContentSanitizerPanel::OnOrphanTypeFilterChanged)
                [SNew(STextBlock).Text(this, &SContentSanitizerPanel::GetOrphanTypeFilterText)]
            ]
            + SHorizontalBox::Slot().AutoWidth().Padding(10, 2, 2, 2).VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(TEXT("孤岛类别：")))]
            + SHorizontalBox::Slot().AutoWidth().Padding(2)
            [
                SAssignNew(OrphanClassificationFilterCombo, SComboBox<TSharedPtr<FSanitizerOrphanClassificationFilter>>)
                .OptionsSource(&OrphanClassificationFilters)
                .InitiallySelectedItem(SelectedOrphanClassificationFilter)
                .OnGenerateWidget(this, &SContentSanitizerPanel::GenerateOrphanClassificationFilterWidget)
                .OnSelectionChanged(this, &SContentSanitizerPanel::OnOrphanClassificationFilterChanged)
                [SNew(STextBlock).Text(this, &SContentSanitizerPanel::GetOrphanClassificationFilterText)]
            ]
        ]
        // Scope & progress
        + SVerticalBox::Slot().AutoHeight().Padding(6, 2)[SAssignNew(ScopeText, STextBlock)]
        + SVerticalBox::Slot().AutoHeight().Padding(6, 2)[SAssignNew(ScanProgressBar, SProgressBar).Percent(0.0f)]
        + SVerticalBox::Slot().AutoHeight().Padding(6, 2)[SAssignNew(ProgressText, STextBlock).Text(FText::FromString(TEXT("尚未开始扫描")))]
        + SVerticalBox::Slot().AutoHeight().Padding(6, 2)[SAssignNew(SummaryText, STextBlock).Text(FText::FromString(TEXT("就绪。选择清理目标后点击“开始扫描”；扫描为只读操作。")))]
        // Central splitter
        + SVerticalBox::Slot().FillHeight(1.f).Padding(6)
        [
            SNew(SSplitter)
            + SSplitter::Slot().Value(0.18f)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(4)[SNew(STextBlock).Text(FText::FromString(TEXT("清理目标（分号分隔）")))]
                + SVerticalBox::Slot().AutoHeight().Padding(4)[SAssignNew(ScopeInputBox, SEditableTextBox).Text(FText::FromString(ScopeInput)).OnTextChanged_Lambda([this](const FText& Text) { ScopeInput = Text.ToString(); }).IsEnabled(this, &SContentSanitizerPanel::CanStartScan)]
                + SVerticalBox::Slot().AutoHeight().Padding(4)[SNew(SButton).Text(FText::FromString(TEXT("应用目标（不扫描）"))).IsEnabled(this, &SContentSanitizerPanel::CanStartScan).OnClicked_Lambda([this]() { TArray<FString> Paths; ScopeInput.ParseIntoArray(Paths, TEXT(";"), true); for (auto& Path : Paths) { Path.TrimStartAndEndInline(); } if (CurrentViewMode == ESanitizerViewMode::DuplicateConsolidation) { SetCleanupPackagePaths(Paths); } else { SetOrphanCleanupPackagePaths(Paths); } return FReply::Handled(); })]
                + SVerticalBox::Slot().AutoHeight().Padding(4)[SNew(SSearchBox).HintText(FText::FromString(TEXT("搜索资产路径"))).OnTextChanged_Lambda([this](const FText& Text) { ResultFilter.Search = Text.ToString(); RefreshFilters(); })]
                + SVerticalBox::Slot().AutoHeight().Padding(4)[SNew(STextBlock).Text(FText::FromString(TEXT("最小估算字节（非回收保证）")))]
                + SVerticalBox::Slot().AutoHeight().Padding(4)[SNew(SNumericEntryBox<int64>).MinValue(0).Value_Lambda([this]() { return TOptional<int64>(ResultFilter.MinimumBytes); }).OnValueChanged_Lambda([this](int64 Value) { ResultFilter.MinimumBytes = FMath::Max<int64>(0, Value); RefreshFilters(); })]
                + SVerticalBox::Slot().AutoHeight().Padding(4)[SNew(SCheckBox).OnCheckStateChanged_Lambda([this](ECheckBoxState State) { ResultFilter.bCandidatesOnly = State == ECheckBoxState::Checked; RefreshFilters(); })[SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(TEXT("只显示可加入队列的证据分类")))]]
                + SVerticalBox::Slot().AutoHeight().Padding(4)
                [SNew(SComboBox<TSharedPtr<FString>>).OptionsSource(&MaterialFilterOptions).OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) { return SNew(STextBlock).Text(FText::FromString(*Item)); }).OnSelectionChanged_Lambda([this](TSharedPtr<FString> Item, ESelectInfo::Type) { const int32 Index = MaterialFilterOptions.IndexOfByKey(Item); if (Index != INDEX_NONE) { ResultFilter.Material = static_cast<ESanitizerMaterialFilter>(Index); RefreshFilters(); } })[SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(*MaterialFilterOptions[static_cast<int32>(ResultFilter.Material)]); })]]
                + SVerticalBox::Slot().AutoHeight().Padding(4)[SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(TEXT("材质绑定只筛选网格展示，不改变使用证据。硬/软/管理引用发现不限于清理目标。")))]
            ]
            + SSplitter::Slot().Value(0.47f)
            [
                SAssignNew(CentralViewSwitcher, SWidgetSwitcher)
                + SWidgetSwitcher::Slot()
                [
                    SAssignNew(GroupList, SListView<TSharedPtr<FSanitizerDuplicateGroup>>)
                    .ListItemsSource(&GroupItems)
                    .OnGenerateRow(this, &SContentSanitizerPanel::GenerateGroupRow)
                    .OnSelectionChanged(this, &SContentSanitizerPanel::OnSelectionChanged)
                ]
                + SWidgetSwitcher::Slot()
                [
                    SAssignNew(OrphanList, SListView<TSharedPtr<FSanitizerOrphanRecord>>)
                    .ListItemsSource(&OrphanItems)
                    .OnGenerateRow(this, &SContentSanitizerPanel::GenerateOrphanRow)
                    .OnSelectionChanged(this, &SContentSanitizerPanel::OnOrphanSelectionChanged)
                ]
            ]
            + SSplitter::Slot().Value(0.35f)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()
                [
                    SAssignNew(CanonicalCombo, SComboBox<TSharedPtr<FString>>)
                    .OptionsSource(&CanonicalOptions)
                    .OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) { return SNew(STextBlock).Text(FText::FromString(*Item)); })
                    .OnSelectionChanged(this, &SContentSanitizerPanel::OnCanonicalChanged)
                    .IsEnabled_Lambda([this]() { return CanAddSelectedToQueue(); })
                    [SNew(STextBlock).Text(FText::FromString(TEXT("选择保留主资产（仅已证明等价的组）")))]
                ]
                + SVerticalBox::Slot().FillHeight(1.f)
                [SNew(SScrollBox) + SScrollBox::Slot()[SAssignNew(InspectorText, STextBlock).AutoWrapText(true).Text(FText::FromString(TEXT("请选择一个条目查看详情。")))]]
            ]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(6)
        [SNew(STextBlock).Text(FText::FromString(TEXT("操作队列 / 影响审查（扫描不会修改资产；确认不可绕过阻止项）")))]
        + SVerticalBox::Slot().MaxHeight(150).FillHeight(0.25f).Padding(6)
        [SAssignNew(QueueList, SListView<TSharedPtr<FString>>).ListItemsSource(&QueueItems).OnGenerateRow(this, &SContentSanitizerPanel::GenerateQueueRow)]
        + SVerticalBox::Slot().MaxHeight(130).AutoHeight().Padding(6)
        [SNew(SScrollBox) + SScrollBox::Slot()[SAssignNew(QueueDetails, STextBlock).AutoWrapText(true)]]
        + SVerticalBox::Slot().AutoHeight().Padding(6)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(FText::FromString(TEXT("预检当前模式队列"))).OnClicked_Lambda([this]() { return CurrentViewMode == ESanitizerViewMode::DuplicateConsolidation ? PreflightQueue() : PreflightOrphanQueue(); }).IsEnabled_Lambda([this]() { return CanStartScan() && CanStartOrphanScan() && (!ActionQueue.IsEmpty() || !DeletionQueue.IsEmpty()); })]
            + SHorizontalBox::Slot().AutoWidth().Padding(20, 2)[SNew(SButton).Text(FText::FromString(TEXT("确认并执行当前模式队列"))).OnClicked_Lambda([this]() { return CurrentViewMode == ESanitizerViewMode::DuplicateConsolidation ? ExecuteQueue() : ExecuteOrphanQueue(); }).IsEnabled_Lambda([this]() { return CurrentViewMode == ESanitizerViewMode::DuplicateConsolidation ? CanExecuteQueue() : CanStartScan() && CanStartOrphanScan() && !DeletionQueue.IsEmpty() && ReviewedDeletions.Num() == DeletionQueue.Num() && !ReviewedDeletions.ContainsByPredicate([](const FSanitizerDeletionPreflight& Item) { return !Item.IsReady(); }); })]
            + SHorizontalBox::Slot().AutoWidth().Padding(20, 2)[SNew(SButton).Text(FText::FromString(TEXT("审查已验证整理的重定向器清理"))).OnClicked(this, &SContentSanitizerPanel::ReviewRedirectors).IsEnabled_Lambda([this]() { return CanStartScan() && CanStartOrphanScan() && !RedirectorPlans.IsEmpty(); })]
        ]
    ];

    RefreshScopeText();
    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    Registry.OnAssetAdded().AddSP(this, &SContentSanitizerPanel::OnRegistryChanged);
    Registry.OnAssetRemoved().AddSP(this, &SContentSanitizerPanel::OnRegistryChanged);
    Registry.OnAssetUpdated().AddSP(this, &SContentSanitizerPanel::OnRegistryChanged);
    FCoreUObjectDelegates::OnObjectPropertyChanged.AddSP(this, &SContentSanitizerPanel::OnAssetPropertyChanged);
}

void SContentSanitizerPanel::SetViewMode(ESanitizerViewMode NewMode)
{
    if ((ScanService && ScanService->IsScanActive()) || bScanStartPending || (OrphanScanService && OrphanScanService->IsScanActive()) || bOrphanScanStartPending)
    {
        SummaryText->SetText(FText::FromString(TEXT("当前有扫描正在进行中，无法切换模式。")));
        return;
    }

    CurrentViewMode = NewMode;
    if (CanonicalCombo) { CanonicalCombo->SetVisibility(NewMode == ESanitizerViewMode::DuplicateConsolidation ? EVisibility::Visible : EVisibility::Collapsed); }
    if (CurrentViewMode == ESanitizerViewMode::DuplicateConsolidation)
    {
        DuplicateToolbarBox->SetVisibility(EVisibility::Visible);
        OrphanToolbarBox->SetVisibility(EVisibility::Collapsed);
        CentralViewSwitcher->SetActiveWidgetIndex(0);
        RefreshScopeText();
        RefreshPresentation();
        InspectorText->SetText(FText::FromString(BuildInspectorText()));
    }
    else
    {
        DuplicateToolbarBox->SetVisibility(EVisibility::Collapsed);
        OrphanToolbarBox->SetVisibility(EVisibility::Visible);
        CentralViewSwitcher->SetActiveWidgetIndex(1);
        RefreshScopeText();
        SummaryText->SetText(FText::FromString(TEXT("已切换至【孤岛素材清理】模式。点击“开始扫描孤岛”以检索贴图、材质和网格体的外部引用候选。")));
        InspectorText->SetText(FText::FromString(BuildOrphanInspectorText()));
    }
}

// -----------------------------------------------------------------------------------------
// Duplicate consolidation implementation
// -----------------------------------------------------------------------------------------

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
    InvalidateResults();
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
    SetViewMode(ESanitizerViewMode::DuplicateConsolidation);
    RefreshScopeText();
    ProgressText->SetText(FText::FromString(TEXT("尚未开始扫描")));
    ScanProgressBar->SetPercent(0.0f);
    SummaryText->SetText(FText::FromString(TEXT("清理目标已设置。比较范围保持为整个 /Game；点击“开始扫描”后才会执行扫描。")));
}

void SContentSanitizerPanel::RefreshScopeText()
{
    if (!ScopeText.IsValid()) { return; }
    ScopeInput = FString::JoinBy(CurrentViewMode == ESanitizerViewMode::DuplicateConsolidation ? CleanupPackagePaths : OrphanCleanupPackagePaths, TEXT(";"), [](FName Root) { return Root.ToString(); });
    if (ScopeInputBox) { ScopeInputBox->SetText(FText::FromString(ScopeInput)); }
    if (CurrentViewMode == ESanitizerViewMode::DuplicateConsolidation)
    {
        const FString CleanupText = FString::JoinBy(CleanupPackagePaths, TEXT("，"), [](FName Path) { return Path.ToString(); });
        const FString ComparisonText = FString::JoinBy(ComparisonPackagePaths, TEXT("，"), [](FName Path) { return Path.ToString(); });
        ScopeText->SetText(FText::FromString(FString::Printf(TEXT("【重复整理】清理目标：%s　|　比较范围：%s（只有清理目标内资产允许被整理）"), *CleanupText, *ComparisonText)));
    }
    else
    {
        const FString OrphanText = FString::JoinBy(OrphanCleanupPackagePaths, TEXT("，"), [](FName Path) { return Path.ToString(); });
        ScopeText->SetText(FText::FromString(FString::Printf(TEXT("【孤岛清理】扫描目录：%s（扫描该目录下贴图、材质、静态网格体及骨骼网格体的外部引用候选）"), *OrphanText)));
    }
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
    if (!CanStartScan() || !CanStartOrphanScan()) { return; }
    InvalidateResults();
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
        LastResult.State = ESanitizerSessionState::Canceled;
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

        const int32 ExternalCanonicalIndex = FSanitizerActionPlanner::RecommendCanonical(Group, CleanupPackagePaths);
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
        if (ResultFilter.Matches(Group) && (!SelectedClassificationFilter.IsValid() || !SelectedClassificationFilter->Classification.IsSet() || Group.Classification == SelectedClassificationFilter->Classification.GetValue()))
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
    ReviewedBatch = {};
    RefreshQueue();
    bQueuePreflightPassed = false;
    SummaryText->SetText(FText::FromString(FString::Printf(TEXT("已加入 %d 个安全整理计划。只有清理目标内资产会作为来源资产；执行前必须先通过预检。"), ActionQueue.Num())));
    return FReply::Handled();
}

FReply SContentSanitizerPanel::PreflightQueue()
{
    if (!CanPreflightQueue()) { return FReply::Handled(); }
    TGuardValue<bool> ServiceGuard(bExecutingMutation, true);
    FContentSanitizerConsolidationService Service;
    ReviewedBatch = Service.PreflightBatch(ActionQueue);
    const int32 Ready = ReviewedBatch.PlanReviews.FilterByPredicate([](const auto& Review) { return Review.IsReady(); }).Num();
    bQueuePreflightPassed = ReviewedBatch.IsReady();
    RefreshQueue();
    SummaryText->SetText(FText::FromString(FString::Printf(TEXT("批次预检：%d/%d 个单组就绪；批次%s。%s"), Ready, ActionQueue.Num(), bQueuePreflightPassed ? TEXT("可执行") : TEXT("已阻止"), *FString::Join(ReviewedBatch.Messages, TEXT(" ")))));
    return FReply::Handled();
}

FReply SContentSanitizerPanel::ExecuteQueue()
{
    if (!CanExecuteQueue()) { return FReply::Handled(); }

    int32 SourceCount = 0;
    for (const FSanitizerActionPlan& Plan : ActionQueue) { SourceCount += Plan.SourceAssets.Num(); }
    const FText Confirmation = FText::FromString(FString::Printf(
        TEXT("确定将清理目标内的 %d 个重复资产整理到 %d 个主资产吗？\n\n队列已通过批次预检，共享引用包只允许已确认的精确替换。Unreal 将重写引用并保存受影响的包，此操作会修改项目内容。"),
        SourceCount, ActionQueue.Num()));
    if (FMessageDialog::Open(EAppMsgType::YesNo, FText::FromString(Confirmation.ToString() + TEXT("\n\n") + QueueDetails->GetText().ToString()), FText::FromString(TEXT("确认整理重复内容与范围外影响"))) != EAppReturnType::Yes)
    {
        return FReply::Handled();
    }

    if (!CanExecuteQueue()) { SummaryText->SetText(FText::FromString(TEXT("确认期间计划已失效，未执行。"))); return FReply::Handled(); }
    TGuardValue<bool> MutationGuard(bExecutingMutation, true);
    FContentSanitizerConsolidationService Service;
    const auto Batch = Service.ExecuteBatch(ActionQueue, ReviewedBatch, true);
    PresentDuplicateBatchResult(Batch);
    return FReply::Handled();
}

void SContentSanitizerPanel::PresentDuplicateBatchResult(const FSanitizerConsolidationBatchResult& Batch)
{
    check(Batch.PlanResults.Num() == ActionQueue.Num());
    int32 Successes = 0, NotExecuted = 0, NeedsReview = 0;
    TArray<FSanitizerRedirectorPlan> PendingRedirectors;
    FString Report = FString::Join(Batch.Messages, TEXT("\n"));
    for (int32 Index = 0; Index < ActionQueue.Num(); ++Index)
    {
        const auto& Result = Batch.PlanResults[Index];
        Successes += Result.IsSucceeded() ? 1 : 0;
        NotExecuted += Result.IsSafeToRetryWithoutRescan() ? 1 : 0;
        NeedsReview += Result.MayHaveModifiedContent() && !Result.IsSucceeded() ? 1 : 0;
        Report += TEXT("\n") + ActionQueue[Index].CanonicalAsset.ToString() + TEXT(" <- ")
            + FString::JoinBy(ActionQueue[Index].SourceAssets, TEXT(", "), [](const auto& Path) { return Path.ToString(); })
            + (Result.IsSucceeded() ? TEXT(" [已验证成功]") : Result.MayHaveModifiedContent() ? TEXT(" [已调用接口，需人工检查]") : TEXT(" [未执行，需重新扫描/审查]"))
            + TEXT("\n") + FString::Join(Result.Messages, TEXT("\n"));
        FSanitizerRedirectorPlan Redirectors;
        if (FContentSanitizerRedirectorService().CreatePlan(ActionQueue[Index], Result, Redirectors)) { PendingRedirectors.Add(MoveTemp(Redirectors)); }
    }
    const int32 Attempted = ActionQueue.Num();
    if (Batch.MayHaveModifiedContent())
    {
        InvalidateResults();
        if (Batch.IsSucceeded()) { RedirectorPlans = MoveTemp(PendingRedirectors); }
        // Keep an exact read-only account of untouched items after invalidating executable plans.
        QueueDetails->SetText(FText::FromString(Report));
    }
    else
    {
        ReviewedBatch = {}; bQueuePreflightPassed = false;
        RefreshQueue();
        QueueDetails->SetText(FText::FromString(Report));
    }
    SummaryText->SetText(FText::FromString(FString::Printf(TEXT("整理结果：成功 %d/%d；未执行 %d；已调用接口但需检查 %d。%s"), Successes, Attempted, NotExecuted, NeedsReview, *Report)));
}

bool SContentSanitizerPanel::CanAddSelectedToQueue() const
{
    return LastResult.State == ESanitizerSessionState::Completed && CanStartScan() && CanStartOrphanScan() && SelectedGroup.IsValid() && SelectedGroup->Classification == ESanitizerClassification::SafeDuplicate;
}

bool SContentSanitizerPanel::CanPreflightQueue() const
{
    return CanStartScan() && CanStartOrphanScan() && !ActionQueue.IsEmpty();
}

bool SContentSanitizerPanel::CanExecuteQueue() const
{
    return CanStartScan() && CanStartOrphanScan() && !ActionQueue.IsEmpty() && bQueuePreflightPassed && ReviewedBatch.IsReady() && ReviewedBatch.PlanReviews.Num() == ActionQueue.Num();
}

bool SContentSanitizerPanel::CanStartScan() const
{
    return !bExecutingMutation && !bScanStartPending && !ScanService->IsScanActive() && !bOrphanScanStartPending && !OrphanScanService->IsScanActive();
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
    RebuildCanonicalOptions();
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
    const int32 Recommended = FSanitizerActionPlanner::RecommendCanonical(*SelectedGroup, CleanupPackagePaths);
    Text += Recommended == SelectedGroup->CanonicalMemberIndex ? TEXT("\n\n主资产推荐依据：范围外等价资产优先，其次引用数、较短路径、字典序。") : TEXT("\n\n已手动覆盖主资产：仍须证明等价，来源范围保持不变。");
    for (const FString& Reason : SelectedGroup->Reasons) { Text += FString::Printf(TEXT("\n\n%s"), *Reason); }
    return Text;
}

// -----------------------------------------------------------------------------------------
// Orphan sanitizer implementation
// -----------------------------------------------------------------------------------------

void SContentSanitizerPanel::SetOrphanCleanupPackagePaths(const TArray<FString>& PackagePaths)
{
    if (!CanStartOrphanScan())
    {
        SummaryText->SetText(FText::FromString(TEXT("当前孤岛扫描尚未结束，无法变更清理目标。")));
        return;
    }

    InvalidateResults();
    OrphanCleanupPackagePaths.Reset();
    for (const FString& Path : PackagePaths)
    {
        FString Normalized = Path;
        Normalized.RemoveFromEnd(TEXT("/"));
        if (!Normalized.IsEmpty())
        {
            OrphanCleanupPackagePaths.AddUnique(FName(*Normalized));
        }
    }
    if (OrphanCleanupPackagePaths.IsEmpty())
    {
        OrphanCleanupPackagePaths.Add(FName(TEXT("/Game")));
    }

    SelectedOrphan.Reset();
    LastOrphanResult = {};
    OrphanItems.Reset();
    if (OrphanList.IsValid())
    {
        OrphanList->RequestListRefresh();
    }

    SetViewMode(ESanitizerViewMode::OrphanSanitizer);
    ProgressText->SetText(FText::FromString(TEXT("尚未开始扫描")));
    ScanProgressBar->SetPercent(0.0f);
    SummaryText->SetText(FText::FromString(TEXT("孤岛清理目标已设置。点击“开始扫描孤岛”后执行只读扫描。")));
}

FReply SContentSanitizerPanel::ScanOrphans()
{
    BeginIncrementalOrphanScan();
    return FReply::Handled();
}

void SContentSanitizerPanel::BeginIncrementalOrphanScan()
{
    if (!CanStartScan() || !CanStartOrphanScan()) { return; }
    InvalidateResults();
    SelectedOrphan.Reset();
    OrphanItems.Reset();
    if (OrphanList.IsValid())
    {
        OrphanList->RequestListRefresh();
    }
    bOrphanScanStartPending = true;
    bOrphanScanCanceledBeforeStart = false;
    ScanProgressBar->SetPercent(0.0f);
    ProgressText->SetText(FText::FromString(TEXT("正在准备扫描孤岛素材…")));
    SummaryText->SetText(FText::FromString(TEXT("孤岛扫描进行中：分析目标目录下贴图、材质、静态网格体及骨骼网格体的引用状态。只读操作，可随时取消。")));

    if (!bOrphanScanTimerRegistered)
    {
        bOrphanScanTimerRegistered = true;
        RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SContentSanitizerPanel::HandleOrphanScanTimer));
    }
}

FReply SContentSanitizerPanel::CancelOrphanScan()
{
    if (bOrphanScanStartPending)
    {
        bOrphanScanStartPending = false;
        bOrphanScanCanceledBeforeStart = true;
        LastOrphanResult.State = ESanitizerSessionState::Canceled;
        LastOrphanResult.bCanceled = true;
        ProgressText->SetText(FText::FromString(TEXT("扫描已取消。")));
        SummaryText->SetText(FText::FromString(TEXT("扫描在开始前已取消，未修改任何项目内容。")));
    }
    else if (OrphanScanService->IsScanActive())
    {
        OrphanScanService->RequestCancel();
        ProgressText->SetText(FText::FromString(TEXT("正在取消扫描…")));
    }
    return FReply::Handled();
}

EActiveTimerReturnType SContentSanitizerPanel::HandleOrphanScanTimer(double CurrentTime, float DeltaTime)
{
    if (bOrphanScanCanceledBeforeStart)
    {
        bOrphanScanCanceledBeforeStart = false;
        bOrphanScanTimerRegistered = false;
        return EActiveTimerReturnType::Stop;
    }

    if (bOrphanScanStartPending)
    {
        bOrphanScanStartPending = false;
        FSanitizerOrphanScanRequest Request;
        Request.PackagePaths = OrphanCleanupPackagePaths;
        Request.bIncludePluginContent = OrphanCleanupPackagePaths.ContainsByPredicate([](FName Path)
        {
            const FString Value = Path.ToString();
            return Value != TEXT("/Game") && !Value.StartsWith(TEXT("/Game/"));
        });
        Request.bIncludeDeveloperContent = OrphanCleanupPackagePaths.ContainsByPredicate([](FName Path)
        {
            const FString Value = Path.ToString();
            return Value == TEXT("/Game/Developers") || Value.StartsWith(TEXT("/Game/Developers/"));
        });
        if (Request.bIncludeDeveloperContent)
        {
            Request.ExcludedPackagePaths.Remove(FName(TEXT("/Game/Developers")));
        }
        OrphanScanService->BeginScan(Request);
    }

    if (OrphanScanService->IsScanActive())
    {
        OrphanScanService->TickScan(0.006, 64);
        RefreshOrphanScanProgress();
    }

    if (!OrphanScanService->IsScanActive())
    {
        bOrphanScanTimerRegistered = false;
        PublishOrphanScanResult();
        return EActiveTimerReturnType::Stop;
    }

    return EActiveTimerReturnType::Continue;
}

void SContentSanitizerPanel::RefreshOrphanScanProgress()
{
    const FSanitizerOrphanScanProgress& Progress = OrphanScanService->GetProgress();
    ScanProgressBar->SetPercent(Progress.GetFraction());
    const FString Current = Progress.CurrentAsset.IsEmpty() ? TEXT("—") : Progress.CurrentAsset;
    ProgressText->SetText(FText::FromString(FString::Printf(TEXT("正在分析引用与关联　%d/%d（%.1f%%）\n当前资产：%s"), Progress.ProcessedAssets, Progress.TotalAssets, Progress.GetFraction() * 100.0f, *Current)));
}

void SContentSanitizerPanel::PublishOrphanScanResult()
{
    LastOrphanResult = OrphanScanService->GetResult();
    RefreshOrphanScanProgress();
    if (LastOrphanResult.State == ESanitizerSessionState::Failed) { SummaryText->SetText(FText::FromString(FString::Join(LastOrphanResult.Errors, TEXT("\n")))); return; }
    if (LastOrphanResult.bCanceled)
    {
        SummaryText->SetText(FText::FromString(TEXT("孤岛扫描已取消。扫描为只读操作，未修改任何项目内容。")));
        return;
    }

    RebuildFilteredOrphanItems();
    FString Summary = FString::Printf(
        TEXT("孤岛扫描完成：清点资产 %d | 候选/阻止项 %d（贴图 %d，材质 %d，静态网格体 %d，骨骼网格体 %d）| 无引用候选 %d | 阻止/复核 %d | 当前显示：%d | 候选包大小估算（非回收保证）：%lld 字节"),
        LastOrphanResult.Summary.InventoriedAssets,
        LastOrphanResult.Summary.OrphanAssets,
        LastOrphanResult.Summary.TextureCount,
        LastOrphanResult.Summary.MaterialCount,
        LastOrphanResult.Summary.StaticMeshCount,
        LastOrphanResult.Summary.SkeletalMeshCount,
        LastOrphanResult.Summary.CandidateCount,
        LastOrphanResult.Summary.BlockedCount,
        OrphanItems.Num(),
        LastOrphanResult.Summary.EstimatedReclaimableSize);

    SummaryText->SetText(FText::FromString(Summary));
}

void SContentSanitizerPanel::RebuildFilteredOrphanItems()
{
    OrphanItems.Reset();
    for (const FSanitizerOrphanRecord& Record : LastOrphanResult.Orphans)
    {
        if (!ResultFilter.Matches(Record)) { continue; }
        if (SelectedOrphanTypeFilter.IsValid() && SelectedOrphanTypeFilter->AssetType != ESanitizerOrphanAssetType::All)
        {
            if (Record.AssetType != SelectedOrphanTypeFilter->AssetType)
            {
                continue;
            }
        }

        if (SelectedOrphanClassificationFilter.IsValid() && SelectedOrphanClassificationFilter->Classification.IsSet())
        {
            if (Record.Classification != SelectedOrphanClassificationFilter->Classification.GetValue())
            {
                continue;
            }
        }

        OrphanItems.Add(MakeShared<FSanitizerOrphanRecord>(Record));
    }

    if (OrphanList.IsValid())
    {
        OrphanList->RequestListRefresh();
    }
}

bool SContentSanitizerPanel::CanStartOrphanScan() const
{
    return !bExecutingMutation && !bOrphanScanStartPending && !OrphanScanService->IsScanActive() && !bScanStartPending && !ScanService->IsScanActive();
}

bool SContentSanitizerPanel::CanCancelOrphanScan() const
{
    return bOrphanScanStartPending || OrphanScanService->IsScanActive();
}

bool SContentSanitizerPanel::CanDeleteSelectedOrphan() const
{
    return LastOrphanResult.State == ESanitizerSessionState::Completed && CanStartScan() && CanStartOrphanScan() && SelectedOrphan.IsValid() && SelectedOrphan->Classification == ESanitizerOrphanClassification::Candidate;
}

FReply SContentSanitizerPanel::DeleteSelectedOrphan()
{
    if (!CanDeleteSelectedOrphan()) { return FReply::Handled(); }
    const FSoftObjectPath Path(SelectedOrphan->GetObjectPath());
    if (DeletionQueue.ContainsByPredicate([&](const FSanitizerDeletionPlan& Plan) { return Plan.Targets[0].Asset == Path; })) { return FReply::Handled(); }
    FSanitizerDeletionPlan Plan;
    FString Error;
    if (!FSanitizerDeletionPlanner::CreatePlan(LastOrphanResult, { Path }, Plan, Error)) { SummaryText->SetText(FText::FromString(Error)); return FReply::Handled(); }
    DeletionQueue.Add(MoveTemp(Plan));
    ReviewedDeletions.Reset();
    RefreshQueue();
    return FReply::Handled();
}

FReply SContentSanitizerPanel::PreflightOrphanQueue()
{
    if (!CanStartScan() || !CanStartOrphanScan()) { return FReply::Handled(); }
    TGuardValue<bool> ServiceGuard(bExecutingMutation, true);
    ReviewedDeletions.Reset();
    for (const FSanitizerDeletionPlan& Plan : DeletionQueue) { ReviewedDeletions.Add(FContentSanitizerDeletionService().Preflight(Plan)); }
    RefreshQueue();
    return FReply::Handled();
}

FReply SContentSanitizerPanel::ExecuteOrphanQueue()
{
    if (!CanStartScan() || !CanStartOrphanScan() || DeletionQueue.IsEmpty() || ReviewedDeletions.Num() != DeletionQueue.Num()
        || ReviewedDeletions.ContainsByPredicate([](const FSanitizerDeletionPreflight& Item) { return !Item.IsReady(); })) { return FReply::Handled(); }
    const FText Confirm = FText::FromString(TEXT("确认仅删除以下明确目标？不会递归清理依赖。\n") + QueueDetails->GetText().ToString() + TEXT("\n") + SanitizerOrphanPolicy::CoverageLimitation());
    if (FMessageDialog::Open(EAppMsgType::YesNo, Confirm, FText::FromString(TEXT("确认删除计划与覆盖限制"))) != EAppReturnType::Yes) { return FReply::Handled(); }
    if (DeletionQueue.IsEmpty() || ReviewedDeletions.Num() != DeletionQueue.Num()) { SummaryText->SetText(FText::FromString(TEXT("确认期间计划已失效，未执行。"))); return FReply::Handled(); }
    TGuardValue<bool> MutationGuard(bExecutingMutation, true);
    TArray<FString> Messages;
    for (int32 Index = 0; Index < DeletionQueue.Num(); ++Index)
    {
        const FSanitizerDeletionResult Result = FContentSanitizerDeletionService().Execute(DeletionQueue[Index], ReviewedDeletions[Index], true);
        Messages.Append(Result.Messages);
        if (Result.Status != ESanitizerDeletionStatus::Succeeded) { break; }
    }
    InvalidateResults();
    SummaryText->SetText(FText::FromString(FString::Join(Messages, TEXT("\n"))));
    return FReply::Handled();
}

void SContentSanitizerPanel::InvalidateResults()
{
    ActionQueue.Reset(); ReviewedBatch = {}; RedirectorPlans.Reset(); bQueuePreflightPassed = false;
    DeletionQueue.Reset(); ReviewedDeletions.Reset();
    LastResult = {}; LastResult.State = ESanitizerSessionState::Stale;
    LastOrphanResult = {}; LastOrphanResult.State = ESanitizerSessionState::Stale;
    SelectedGroup.Reset(); SelectedOrphan.Reset();
    GroupItems.Reset(); OrphanItems.Reset();
    if (GroupList) { GroupList->RequestListRefresh(); }
    if (OrphanList) { OrphanList->RequestListRefresh(); }
    if (InspectorText) { InspectorText->SetText(FText::FromString(TEXT("结果已失效，汇总已清空。请重新扫描后审查。"))); }
    RefreshQueue();
}

void SContentSanitizerPanel::RebuildCanonicalOptions()
{
    CanonicalOptions.Reset();
    if (SelectedGroup && SelectedGroup->Classification == ESanitizerClassification::SafeDuplicate)
    {
        for (const auto& Member : SelectedGroup->Members) { CanonicalOptions.Add(MakeShared<FString>(Member.Record.GetObjectPath())); }
    }
    CanonicalCombo->RefreshOptions();
}

void SContentSanitizerPanel::OnCanonicalChanged(TSharedPtr<FString> Item, ESelectInfo::Type SelectInfo)
{
    if (!Item || !CanAddSelectedToQueue()) { return; }
    const int32 Index = SelectedGroup->Members.IndexOfByPredicate([&](const auto& Member) { return Member.Record.GetObjectPath() == *Item; });
    if (Index == INDEX_NONE) { return; }
    SelectedGroup->CanonicalMemberIndex = Index;
    for (auto& Group : LastResult.Groups) { if (Group.GroupId == SelectedGroup->GroupId) { Group.CanonicalMemberIndex = Index; } }
    ActionQueue.RemoveAll([this](const auto& Plan) { return Plan.PlanId == SelectedGroup->GroupId; });
    ReviewedBatch = {}; bQueuePreflightPassed = false;
    LastResult.Summary.EstimatedReclaimableSize = 0;
    for (auto& Group : LastResult.Groups)
    {
        Group.EstimatedReclaimableSize = FSanitizerActionPlanner::EstimateSourceBytes(Group, CleanupPackagePaths);
        LastResult.Summary.EstimatedReclaimableSize += Group.EstimatedReclaimableSize;
        if (Group.GroupId == SelectedGroup->GroupId) { SelectedGroup->EstimatedReclaimableSize = Group.EstimatedReclaimableSize; }
    }
    RefreshPresentation();
    RefreshQueue();
    InspectorText->SetText(FText::FromString(BuildInspectorText()));
}

void SContentSanitizerPanel::RefreshQueue()
{
    QueueItems.Reset();
    FString Details;
    if (!ReviewedBatch.PlanReviews.IsEmpty())
    {
        Details += TEXT("批次状态：") + FString(ReviewedBatch.IsReady() ? TEXT("可执行") : TEXT("已阻止"));
        Details += TEXT("\n共享引用包：") + FString::JoinBy(ReviewedBatch.SharedPackages, TEXT(", "), [](FName Name) { return Name.ToString(); });
        Details += TEXT("\n") + FString::Join(ReviewedBatch.Messages, TEXT("\n"));
    }
    for (int32 Index = 0; Index < ActionQueue.Num(); ++Index)
    {
        const auto& Plan = ActionQueue[Index];
        QueueItems.Add(MakeShared<FString>(TEXT("D:") + Plan.PlanId));
        Details += TEXT("\n保留：") + Plan.CanonicalAsset.ToString() + TEXT("\n来源：") + FString::JoinBy(Plan.SourceAssets, TEXT(", "), [](const auto& Path) { return Path.ToString(); });
        if (ReviewedBatch.PlanReviews.IsValidIndex(Index))
        {
            const auto& Check = ReviewedBatch.PlanReviews[Index];
            Details += TEXT("\n") + FString::Join(Check.Messages, TEXT("\n"));
            Details += TEXT("\n影响包：") + FString::JoinBy(Check.AffectedPackages, TEXT(", "), [](FName Name) { return Name.ToString(); });
            Details += TEXT("\n范围外影响（需确认）：") + FString::JoinBy(Check.OutsideTargetPackages, TEXT(", "), [](FName Name) { return Name.ToString(); });
        }
    }
    for (int32 Index = 0; Index < DeletionQueue.Num(); ++Index)
    {
        const auto& Plan = DeletionQueue[Index];
        QueueItems.Add(MakeShared<FString>(TEXT("O:") + Plan.OperationId));
        Details += TEXT("\n删除目标/包：") + Plan.Targets[0].Asset.ToString();
        if (ReviewedDeletions.IsValidIndex(Index))
        {
            const auto& Check = ReviewedDeletions[Index];
            Details += Check.IsReady() ? TEXT(" [预检可执行]") : TEXT(" [已阻止]");
            Details += TEXT("\n") + FString::Join(Check.Blockers, TEXT("\n")) + TEXT("\n") + FString::Join(Check.Warnings, TEXT("\n"));
            Details += FString::Printf(TEXT("\n预检就绪包大小估算：%lld 字节"), Check.EstimatedPackageBytes);
        }
    }
    if (QueueDetails) { QueueDetails->SetText(FText::FromString(Details.IsEmpty() ? TEXT("队列为空。") : Details)); }
    if (QueueList) { QueueList->RequestListRefresh(); }
}

TSharedRef<ITableRow> SContentSanitizerPanel::GenerateQueueRow(TSharedPtr<FString> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
    FString Label = *Item;
    for (const auto& Plan : ActionQueue) { if (*Item == TEXT("D:") + Plan.PlanId) { Label = TEXT("整理 -> ") + Plan.CanonicalAsset.ToString(); } }
    for (const auto& Plan : DeletionQueue) { if (*Item == TEXT("O:") + Plan.OperationId) { Label = TEXT("删除 -> ") + Plan.Targets[0].Asset.ToString(); } }
    return SNew(STableRow<TSharedPtr<FString>>, OwnerTable)
    [SNew(SHorizontalBox)
        + SHorizontalBox::Slot().FillWidth(1.f)[SNew(STextBlock).Text(FText::FromString(Label))]
        + SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(FText::FromString(TEXT("移出队列"))).OnClicked_Lambda([this, Item]()
        {
            ActionQueue.RemoveAll([&](const auto& Plan) { return *Item == TEXT("D:") + Plan.PlanId; });
            DeletionQueue.RemoveAll([&](const auto& Plan) { return *Item == TEXT("O:") + Plan.OperationId; });
            ReviewedBatch = {}; ReviewedDeletions.Reset(); bQueuePreflightPassed = false;
            RefreshQueue(); return FReply::Handled();
        })]];
}

TSharedRef<ITableRow> SContentSanitizerPanel::GenerateOrphanRow(TSharedPtr<FSanitizerOrphanRecord> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
    FString SizeStr = TEXT("—");
    if (Item->EstimatedDiskSize > 0)
    {
        if (Item->EstimatedDiskSize >= 1024 * 1024)
        {
            SizeStr = FString::Printf(TEXT("%.2f MB"), Item->EstimatedDiskSize / (1024.0f * 1024.0f));
        }
        else
        {
            SizeStr = FString::Printf(TEXT("%.1f KB"), Item->EstimatedDiskSize / 1024.0f);
        }
    }

    const FString RowLabel = FString::Printf(
        TEXT("[%s] [%s] %s (%s)"),
        *SanitizerOrphanAssetTypeToText(Item->AssetType),
        *SanitizerOrphanClassificationToText(Item->Classification),
        *Item->GetObjectPath(),
        *SizeStr);

    return SNew(STableRow<TSharedPtr<FSanitizerOrphanRecord>>, OwnerTable)
    [
        SNew(STextBlock).Text(FText::FromString(RowLabel))
    ];
}

void SContentSanitizerPanel::OnOrphanSelectionChanged(TSharedPtr<FSanitizerOrphanRecord> Item, ESelectInfo::Type SelectInfo)
{
    SelectedOrphan = Item;
    InspectorText->SetText(FText::FromString(BuildOrphanInspectorText()));
}

FString SContentSanitizerPanel::BuildOrphanInspectorText() const
{
    if (!SelectedOrphan.IsValid())
    {
        return TEXT("请选择一个孤岛资产，查看引用观察与覆盖限制。");
    }

    FString Text = FString::Printf(
        TEXT("资产路径：%s\n资产类型：%s\n判定分类：%s\n估算大小：%lld 字节\n\n引用与关联诊断：\n- 已注册硬引用数（不限目标）：%d\n- 已注册软引用数（不限目标）：%d"),
        *SelectedOrphan->GetObjectPath(),
        *SanitizerOrphanAssetTypeToText(SelectedOrphan->AssetType),
        *SanitizerOrphanClassificationToText(SelectedOrphan->Classification),
        SelectedOrphan->EstimatedDiskSize,
        SelectedOrphan->HardReferenceCount,
        SelectedOrphan->SoftReferenceCount);

    if (SelectedOrphan->AssetType == ESanitizerOrphanAssetType::SkeletalMesh || SelectedOrphan->AssetType == ESanitizerOrphanAssetType::StaticMesh)
    {
        Text += FString::Printf(
            TEXT("\n- 材质槽总数：%d\n- 无效/空材质槽数：%d"),
            SelectedOrphan->MaterialSlotsNum,
            SelectedOrphan->EmptyMaterialSlotsNum);
    }

    Text += TEXT("\n- 资产类：") + SelectedOrphan->AssetData.AssetClassPath.ToString();
    Text += TEXT("\n- 硬引用包：") + FString::JoinBy(SelectedOrphan->HardReferencers, TEXT(", "), [](FName Name) { return Name.ToString(); });
    Text += TEXT("\n- 软引用包：") + FString::JoinBy(SelectedOrphan->SoftReferencers, TEXT(", "), [](FName Name) { return Name.ToString(); });
    Text += TEXT("\n- 管理用户：") + FString::JoinBy(SelectedOrphan->ManagementReferencers, TEXT(", "), [](FName Name) { return Name.ToString(); });
    Text += TEXT("\n- 管理引用数：") + LexToString(SelectedOrphan->ManagementReferencers.Num());
    Text += TEXT("\n- 注册表就绪：") + FString(SelectedOrphan->bRegistryReady ? TEXT("是") : TEXT("否"));
    Text += TEXT("\n- 材质元数据：") + FString(SelectedOrphan->bMaterialMetadataKnown ? TEXT("已检查（仅展示）") : TEXT("未检查/未知"));
    if (!SelectedOrphan->Reasons.IsEmpty())
    {
        Text += TEXT("\n\n判定依据：");
        for (const FString& Reason : SelectedOrphan->Reasons)
        {
            Text += FString::Printf(TEXT("\n• %s"), *Reason);
        }
    }

    return Text;
}

TSharedRef<SWidget> SContentSanitizerPanel::GenerateOrphanTypeFilterWidget(TSharedPtr<FSanitizerOrphanTypeFilter> Item) const
{
    return SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? Item->Label : TEXT("全部类型")));
}

void SContentSanitizerPanel::OnOrphanTypeFilterChanged(TSharedPtr<FSanitizerOrphanTypeFilter> Item, ESelectInfo::Type SelectInfo)
{
    if (!Item.IsValid()) { return; }
    SelectedOrphanTypeFilter = Item;
    SelectedOrphan.Reset();
    InspectorText->SetText(FText::FromString(BuildOrphanInspectorText()));
    RebuildFilteredOrphanItems();
}

FText SContentSanitizerPanel::GetOrphanTypeFilterText() const
{
    return FText::FromString(SelectedOrphanTypeFilter.IsValid() ? SelectedOrphanTypeFilter->Label : TEXT("全部类型"));
}

TSharedRef<SWidget> SContentSanitizerPanel::GenerateOrphanClassificationFilterWidget(TSharedPtr<FSanitizerOrphanClassificationFilter> Item) const
{
    return SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? Item->Label : TEXT("全部类别")));
}

void SContentSanitizerPanel::OnOrphanClassificationFilterChanged(TSharedPtr<FSanitizerOrphanClassificationFilter> Item, ESelectInfo::Type SelectInfo)
{
    if (!Item.IsValid()) { return; }
    SelectedOrphanClassificationFilter = Item;
    SelectedOrphan.Reset();
    InspectorText->SetText(FText::FromString(BuildOrphanInspectorText()));
    RebuildFilteredOrphanItems();
}

FText SContentSanitizerPanel::GetOrphanClassificationFilterText() const
{
    return FText::FromString(SelectedOrphanClassificationFilter.IsValid() ? SelectedOrphanClassificationFilter->Label : TEXT("全部类别"));
}

FReply SContentSanitizerPanel::ReviewRedirectors()
{
    if (!CanStartScan() || !CanStartOrphanScan() || RedirectorPlans.IsEmpty()) { return FReply::Handled(); }
    TGuardValue<bool> ServiceGuard(bExecutingMutation, true);
    FContentSanitizerRedirectorService Service;
    TArray<FSanitizerPreflightResult> Reviewed;
    FString Impact;
    for (const auto& Plan : RedirectorPlans)
    {
        const auto Check = Service.Preflight(Plan);
        if (!Check.IsReady()) { SummaryText->SetText(FText::FromString(FString::Join(Check.Messages, TEXT("\n")))); return FReply::Handled(); }
        Impact += FString::JoinBy(Check.AffectedPackages, TEXT("\n"), [](FName Name) { return Name.ToString(); }) + TEXT("\n");
        Reviewed.Add(Check);
    }
    if (FMessageDialog::Open(EAppMsgType::YesNo, FText::FromString(TEXT("独立可选操作：删除以下未发现已注册/已加载用户的重定向器包？\n") + Impact + TEXT("\n") + SanitizerOrphanPolicy::CoverageLimitation()), FText::FromString(TEXT("确认重定向器清理影响"))) != EAppReturnType::Yes) { return FReply::Handled(); }
    TGuardValue<bool> MutationGuard(bExecutingMutation, true);
    TArray<FString> Messages;
    for (int32 Index = 0; Index < RedirectorPlans.Num(); ++Index)
    {
        const auto Result = Service.Execute(RedirectorPlans[Index], Reviewed[Index], true);
        Messages.Append(Result.Messages);
        if (!Result.IsSucceeded()) { break; }
    }
    InvalidateResults();
    SummaryText->SetText(FText::FromString(FString::Join(Messages, TEXT("\n"))));
    return FReply::Handled();
}

void SContentSanitizerPanel::OnRegistryChanged(const FAssetData& Asset)
{
    if (bExecutingMutation || !SanitizerOrphanPolicy::IsInScope(Asset.PackageName.ToString(), { FName(TEXT("/Game")) })) { return; }
    if (ScanService->IsScanActive()) { ScanService->RequestCancel(); }
    if (OrphanScanService->IsScanActive()) { OrphanScanService->RequestCancel(); }
    InvalidateResults();
    SummaryText->SetText(FText::FromString(TEXT("资产状态已变化，两种模式的结果和确认已失效。请重新扫描。")));
}

void SContentSanitizerPanel::OnAssetPropertyChanged(UObject* Object, FPropertyChangedEvent& Event)
{
    if (Object && Object->IsAsset()) { OnRegistryChanged(FAssetData(Object)); }
}

void SContentSanitizerPanel::RefreshFilters()
{
    SelectedGroup.Reset(); SelectedOrphan.Reset();
    RebuildCanonicalOptions();
    RebuildFilteredGroupItems(); RebuildFilteredOrphanItems();
    InspectorText->SetText(FText::FromString(TEXT("筛选只改变展示；请选择结果查看证据。")));
    if (CurrentViewMode == ESanitizerViewMode::DuplicateConsolidation) { RefreshPresentation(); }
    else { SummaryText->SetText(FText::FromString(FString::Printf(TEXT("候选/阻止项总计：%d；当前显示：%d；候选包大小估算：%lld 字节。"), LastOrphanResult.Summary.OrphanAssets, OrphanItems.Num(), LastOrphanResult.Summary.EstimatedReclaimableSize))); }
}
