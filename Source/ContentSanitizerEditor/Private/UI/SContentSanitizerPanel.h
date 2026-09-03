#pragma once

#include "CoreMinimal.h"
#include "Model/SanitizerTypes.h"
#include "Operations/SanitizerActionPlan.h"
#include "Scanner/SanitizerScanService.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Views/SListView.h"

struct FSanitizerClassificationFilter
{
    FString Label;
    TOptional<ESanitizerClassification> Classification;
};

class SContentSanitizerPanel final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SContentSanitizerPanel) {}
    SLATE_END_ARGS()
    void Construct(const FArguments& InArgs);
    void SetCleanupPackagePaths(const TArray<FString>& PackagePaths);
private:
    FReply Scan();
    FReply CancelScan();
    FReply AddSelectedToQueue();
    FReply PreflightQueue();
    FReply ExecuteQueue();
    bool CanAddSelectedToQueue() const;
    bool CanPreflightQueue() const;
    bool CanExecuteQueue() const;
    bool CanStartScan() const;
    bool CanCancelScan() const;
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

    TSharedPtr<SListView<TSharedPtr<FSanitizerDuplicateGroup>>> GroupList;
    TSharedPtr<class STextBlock> SummaryText;
    TSharedPtr<class STextBlock> ScopeText;
    TSharedPtr<class STextBlock> ProgressText;
    TSharedPtr<class SProgressBar> ScanProgressBar;
    TSharedPtr<SComboBox<TSharedPtr<FSanitizerClassificationFilter>>> ClassificationFilterCombo;
    TSharedPtr<class STextBlock> InspectorText;
    TArray<TSharedPtr<FSanitizerDuplicateGroup>> GroupItems;
    TArray<TSharedPtr<FSanitizerClassificationFilter>> ClassificationFilters;
    TSharedPtr<FSanitizerClassificationFilter> SelectedClassificationFilter;
    TSharedPtr<FSanitizerDuplicateGroup> SelectedGroup;
    TArray<FSanitizerActionPlan> ActionQueue;
    bool bQueuePreflightPassed = false;
    FSanitizerScanResult LastResult;
    TUniquePtr<FSanitizerScanService> ScanService;
    TArray<FName> CleanupPackagePaths { FName(TEXT("/Game")) };
    TArray<FName> ComparisonPackagePaths { FName(TEXT("/Game")) };
    bool bScanStartPending = false;
    bool bScanTimerRegistered = false;
    bool bScanCanceledBeforeStart = false;
};
