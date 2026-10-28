#pragma once

#include "CoreMinimal.h"
#include "UI/ContentSanitizerResultFilter.h"
#include "ContentSanitizerDeletionService.h"
#include "ContentSanitizerRedirectorService.h"
#include "Model/SanitizerTypes.h"
#include "Model/SanitizerOrphanTypes.h"
#include "Operations/SanitizerActionPlan.h"
#include "Scanner/SanitizerScanService.h"
#include "Scanner/SanitizerOrphanScanService.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Views/SListView.h"

enum class ESanitizerViewMode : uint8
{
    DuplicateConsolidation,
    OrphanSanitizer
};

struct FSanitizerClassificationFilter
{
    FString Label;
    TOptional<ESanitizerClassification> Classification;
};

struct FSanitizerOrphanTypeFilter
{
    FString Label;
    ESanitizerOrphanAssetType AssetType;
};

struct FSanitizerOrphanClassificationFilter
{
    FString Label;
    TOptional<ESanitizerOrphanClassification> Classification;
};

class SContentSanitizerPanel final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SContentSanitizerPanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    void SetCleanupPackagePaths(const TArray<FString>& PackagePaths);
    void SetOrphanCleanupPackagePaths(const TArray<FString>& PackagePaths);

private:
#if WITH_DEV_AUTOMATION_TESTS
    friend class FSanitizerPanelWorkflowTest;
#endif
    void SetViewMode(ESanitizerViewMode NewMode);

    // Duplicate consolidation actions
    FReply Scan();
    FReply CancelScan();
    FReply AddSelectedToQueue();
    FReply PreflightQueue();
    FReply ExecuteQueue();
    void PresentDuplicateBatchResult(const FSanitizerConsolidationBatchResult& Batch);
    FReply ReviewRedirectors();
    bool CanAddSelectedToQueue() const;
    bool CanPreflightQueue() const;
    bool CanExecuteQueue() const;
    bool CanStartScan() const;
    bool CanCancelScan() const;

    // Orphan scanner actions
    FReply ScanOrphans();
    FReply CancelOrphanScan();
    FReply DeleteSelectedOrphan();
    FReply PreflightOrphanQueue();
    FReply ExecuteOrphanQueue();
    void InvalidateResults();
    void OnRegistryChanged(const FAssetData& Asset);
    void OnAssetPropertyChanged(UObject* Object, FPropertyChangedEvent& Event);
    void RefreshQueue();
    void RefreshFilters();
    TSharedRef<ITableRow> GenerateQueueRow(TSharedPtr<FString> Item, const TSharedRef<STableViewBase>& OwnerTable);
    void RebuildCanonicalOptions();
    void OnCanonicalChanged(TSharedPtr<FString> Item, ESelectInfo::Type SelectInfo);
    bool CanStartOrphanScan() const;
    bool CanCancelOrphanScan() const;
    bool CanDeleteSelectedOrphan() const;
    EActiveTimerReturnType HandleOrphanScanTimer(double CurrentTime, float DeltaTime);
    void BeginIncrementalOrphanScan();
    void PublishOrphanScanResult();
    void RefreshOrphanScanProgress();
    void RebuildFilteredOrphanItems();
    TSharedRef<ITableRow> GenerateOrphanRow(TSharedPtr<FSanitizerOrphanRecord> Item, const TSharedRef<STableViewBase>& OwnerTable);
    void OnOrphanSelectionChanged(TSharedPtr<FSanitizerOrphanRecord> Item, ESelectInfo::Type SelectInfo);
    FString BuildOrphanInspectorText() const;

    // Filter helpers for orphans
    TSharedRef<SWidget> GenerateOrphanTypeFilterWidget(TSharedPtr<FSanitizerOrphanTypeFilter> Item) const;
    void OnOrphanTypeFilterChanged(TSharedPtr<FSanitizerOrphanTypeFilter> Item, ESelectInfo::Type SelectInfo);
    FText GetOrphanTypeFilterText() const;

    TSharedRef<SWidget> GenerateOrphanClassificationFilterWidget(TSharedPtr<FSanitizerOrphanClassificationFilter> Item) const;
    void OnOrphanClassificationFilterChanged(TSharedPtr<FSanitizerOrphanClassificationFilter> Item, ESelectInfo::Type SelectInfo);
    FText GetOrphanClassificationFilterText() const;

    // Duplicate helpers
    bool IsMemberInCleanupScope(const FSanitizerDuplicateMember& Member) const;
    EActiveTimerReturnType HandleScanTimer(double CurrentTime, float DeltaTime);
    void BeginIncrementalScan();
    void PublishScanResult();
    void ApplyCleanupScopeToResult();
    void RefreshScanProgress();
    void RefreshScopeText();
    TSharedRef<SWidget> GenerateClassificationFilterWidget(TSharedPtr<FSanitizerClassificationFilter> Item) const;
    void OnClassificationFilterChanged(TSharedPtr<FSanitizerClassificationFilter> Item, ESelectInfo::Type SelectInfo);
    FText GetClassificationFilterText() const;
    void RebuildFilteredGroupItems();
    TSharedRef<ITableRow> GenerateGroupRow(TSharedPtr<FSanitizerDuplicateGroup> Item, const TSharedRef<STableViewBase>& OwnerTable);
    void OnSelectionChanged(TSharedPtr<FSanitizerDuplicateGroup> Item, ESelectInfo::Type SelectInfo);
    void RefreshPresentation();
    FString BuildInspectorText() const;

    FContentSanitizerResultFilter ResultFilter;
    TArray<TSharedPtr<FString>> MaterialFilterOptions;
    FString ScopeInput = TEXT("/Game");
    TSharedPtr<class SEditableTextBox> ScopeInputBox;
    bool bExecutingMutation = false;
    ESanitizerViewMode CurrentViewMode = ESanitizerViewMode::DuplicateConsolidation;

    // Mode toolbars & switcher widgets
    TSharedPtr<class SWidget> DuplicateToolbarBox;
    TSharedPtr<class SWidget> OrphanToolbarBox;
    TSharedPtr<class SWidgetSwitcher> CentralViewSwitcher;

    // Duplicate widgets & state
    TSharedPtr<SListView<TSharedPtr<FSanitizerDuplicateGroup>>> GroupList;
    TSharedPtr<SComboBox<TSharedPtr<FSanitizerClassificationFilter>>> ClassificationFilterCombo;
    TArray<TSharedPtr<FSanitizerDuplicateGroup>> GroupItems;
    TArray<TSharedPtr<FSanitizerClassificationFilter>> ClassificationFilters;
    TSharedPtr<FSanitizerClassificationFilter> SelectedClassificationFilter;
    TSharedPtr<FSanitizerDuplicateGroup> SelectedGroup;
    TArray<FSanitizerActionPlan> ActionQueue;
    bool bQueuePreflightPassed = false;
    FSanitizerConsolidationBatchPreflight ReviewedBatch;
    TArray<FSanitizerRedirectorPlan> RedirectorPlans;
    TArray<TSharedPtr<FString>> CanonicalOptions;
    TSharedPtr<SComboBox<TSharedPtr<FString>>> CanonicalCombo;
    TArray<FSanitizerDeletionPlan> DeletionQueue;
    TArray<FSanitizerDeletionPreflight> ReviewedDeletions;
    TArray<TSharedPtr<FString>> QueueItems;
    TSharedPtr<SListView<TSharedPtr<FString>>> QueueList;
    TSharedPtr<class STextBlock> QueueDetails;
    FSanitizerScanResult LastResult;
    TUniquePtr<FSanitizerScanService> ScanService;
    TArray<FName> CleanupPackagePaths { FName(TEXT("/Game")) };
    TArray<FName> ComparisonPackagePaths { FName(TEXT("/Game")) };
    bool bScanStartPending = false;
    bool bScanTimerRegistered = false;
    bool bScanCanceledBeforeStart = false;

    // Orphan widgets & state
    TSharedPtr<SListView<TSharedPtr<FSanitizerOrphanRecord>>> OrphanList;
    TSharedPtr<SComboBox<TSharedPtr<FSanitizerOrphanTypeFilter>>> OrphanTypeFilterCombo;
    TSharedPtr<SComboBox<TSharedPtr<FSanitizerOrphanClassificationFilter>>> OrphanClassificationFilterCombo;
    TArray<TSharedPtr<FSanitizerOrphanRecord>> OrphanItems;
    TArray<TSharedPtr<FSanitizerOrphanTypeFilter>> OrphanTypeFilters;
    TSharedPtr<FSanitizerOrphanTypeFilter> SelectedOrphanTypeFilter;
    TArray<TSharedPtr<FSanitizerOrphanClassificationFilter>> OrphanClassificationFilters;
    TSharedPtr<FSanitizerOrphanClassificationFilter> SelectedOrphanClassificationFilter;
    TSharedPtr<FSanitizerOrphanRecord> SelectedOrphan;
    FSanitizerOrphanScanResult LastOrphanResult;
    TUniquePtr<FSanitizerOrphanScanService> OrphanScanService;
    TArray<FName> OrphanCleanupPackagePaths { FName(TEXT("/Game")) };
    bool bOrphanScanStartPending = false;
    bool bOrphanScanTimerRegistered = false;
    bool bOrphanScanCanceledBeforeStart = false;

    // Shared UI elements
    TSharedPtr<class STextBlock> SummaryText;
    TSharedPtr<class STextBlock> ScopeText;
    TSharedPtr<class STextBlock> ProgressText;
    TSharedPtr<class SProgressBar> ScanProgressBar;
    TSharedPtr<class STextBlock> InspectorText;
};
