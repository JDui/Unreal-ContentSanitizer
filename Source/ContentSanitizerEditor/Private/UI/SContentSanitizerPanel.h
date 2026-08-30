#pragma once

#include "CoreMinimal.h"
#include "Model/SanitizerTypes.h"
#include "Operations/SanitizerActionPlan.h"
#include "Scanner/SanitizerScanService.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class SContentSanitizerPanel final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SContentSanitizerPanel) {}
    SLATE_END_ARGS()
    void Construct(const FArguments& InArgs);
private:
    FReply Scan();
    FReply AddSelectedToQueue();
    FReply PreflightQueue();
    FReply ExecuteQueue();
    TSharedRef<ITableRow> GenerateGroupRow(TSharedPtr<FSanitizerDuplicateGroup> Item, const TSharedRef<STableViewBase>& OwnerTable);
    void OnSelectionChanged(TSharedPtr<FSanitizerDuplicateGroup> Item, ESelectInfo::Type SelectInfo);
    void RefreshPresentation();
    FString BuildInspectorText() const;

    TSharedPtr<SListView<TSharedPtr<FSanitizerDuplicateGroup>>> GroupList;
    TSharedPtr<class STextBlock> SummaryText;
    TSharedPtr<class STextBlock> InspectorText;
    TArray<TSharedPtr<FSanitizerDuplicateGroup>> GroupItems;
    TSharedPtr<FSanitizerDuplicateGroup> SelectedGroup;
    TArray<FSanitizerActionPlan> ActionQueue;
    FSanitizerScanResult LastResult;
    TUniquePtr<FSanitizerScanService> ScanService;
};
