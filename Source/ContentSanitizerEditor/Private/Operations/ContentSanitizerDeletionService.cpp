#include "ContentSanitizerDeletionService.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "Operations/ContentSanitizerOperationSubsystem.h"
#include "Misc/ScopeExit.h"
#include "HAL/FileManager.h"
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "ISourceControlState.h"
#include "Misc/PackageName.h"
#include "ObjectTools.h"
#include "Operations/SanitizerLoadedReferenceCheck.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Operations/SanitizerObjectCompatibility.h"
#include "UObject/UObjectHash.h"

namespace ContentSanitizerDeletion
{
    static void InspectEditorState(const FSanitizerDeletionTarget& Target, TArray<FString>& Errors)
    {
        UObject* Asset = Target.Asset.TryLoad();
        if (!Asset || !GEditor || GEditor->PlayWorld || Asset->GetClass()->GetClassPathName() != Target.Class) { Errors.Add(TEXT("资产加载失败、类型变化或编辑器处于 PIE。")); return; }
        if (Asset->GetPackage() != Asset->GetOutermost()) { Errors.Add(TEXT("外部包状态不支持单资产删除。")); }
        if (Asset->GetOutermost()->IsDirty()) { Errors.Add(TEXT("包包含未保存更改。")); }
        FString DirtyUser;
        if (SanitizerHasDirtyReference({ Target.Asset }, DirtyUser)) { Errors.Add(TEXT("存在未保存引用：") + DirtyUser); }
        if (!SanitizerGetLoadedUsers({ Target.Asset }, false).IsEmpty()) { Errors.Add(TEXT("存在已加载的硬/软用户，禁止删除。")); }
        if (!Errors.IsEmpty()) { return; }
        if (Asset->IsRooted() || GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(Asset, false)) { Errors.Add(TEXT("资产被固定或正在资产编辑器中使用。")); }
        TArray<UObject*> TopLevelObjects;
        SanitizerGetPackageObjects(Asset->GetOutermost(), TopLevelObjects, false);
        for (UObject* Object : TopLevelObjects)
        {
            if (Object != Asset && !SanitizerIsPackageMetadata(Object) && IsValid(Object)) { Errors.Add(TEXT("存在同包对象，单资产删除被阻止。")); break; }
        }
        bool bReferenced = false;
        bool bUndo = false;
        ObjectTools::GatherObjectReferencersForDeletion(Asset, bReferenced, bUndo);
        if (bReferenced || bUndo) { Errors.Add(TEXT("存在内存/编辑器或撤销引用。请关闭使用者并重新扫描。")); }
        TArray<UObject*> Expanded { Asset };
        ObjectTools::AddExtraObjectsToDelete(Expanded);
        if (Expanded.Num() != 1) { Errors.Add(TEXT("引擎会扩展删除对象，超出单资产计划。")); }
        FString Filename;
        if (!FPackageName::DoesPackageExist(Target.Asset.GetLongPackageName(), &Filename)) { Errors.Add(TEXT("包文件不存在。")); return; }
        if (ISourceControlModule::Get().IsEnabled())
        {
            ISourceControlProvider& Provider = ISourceControlModule::Get().GetProvider();
            const FSourceControlStatePtr State = Provider.GetState(Filename, EStateCacheUsage::ForceUpdate);
            if (!Provider.IsAvailable() || !State.IsValid() || State->IsUnknown() || (State->IsSourceControlled() && !State->CanDelete())) { Errors.Add(TEXT("源控制状态不可确认或不允许删除。")); }
        }
    }
}

FSanitizerDeletionPreflight FContentSanitizerDeletionService::Preflight(const FSanitizerDeletionPlan& Plan) const
{
    FSanitizerDeletionPreflight Result;
    Result.Blockers = FSanitizerDeletionPlanner::ValidateShape(Plan);
    Result.Warnings = { SanitizerOrphanPolicy::CoverageLimitation(), TEXT("候选包大小是估算；删除不保证实际回收这些字节，也不保证撤销或自动回滚。") };
    if (!IsInGameThread() || !GEditor) { Result.Blockers.Add(TEXT("删除预检必须在编辑器游戏线程运行。")); }
    if (!Result.Blockers.IsEmpty()) { return Result; }
    if (GEditor->GetEditorSubsystem<UContentSanitizerOperationSubsystem>()->IsConsumed(Plan.OperationId)) { Result.Blockers.Add(TEXT("该计划已调用修改接口；必须重新扫描并创建新计划。")); return Result; }
    Result.PlanIdentity = Plan.GetIdentity();
    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    for (const FSanitizerDeletionTarget& Target : Plan.Targets)
    {
        const FAssetData Data = Registry.GetAssetByObjectPath(Target.Asset);
        if (!Data.IsValid()) { Result.Blockers.Add(Target.Asset.ToString() + TEXT(": 资产已不存在。")); continue; }
        const FSanitizerOrphanRecord Current = SanitizerOrphanPolicy::Inspect(Data);
        Result.Blockers.Append(Current.Blockers);
        if (Current.EvidenceIdentity != Target.EvidenceIdentity || Data.AssetClassPath != Target.Class) { Result.Blockers.Add(Target.Asset.ToString() + TEXT(": 扫描证据已变化，必须重新扫描/审查。")); }
        if (Result.Blockers.IsEmpty()) { ContentSanitizerDeletion::InspectEditorState(Target, Result.Blockers); }
        Result.AffectedPackages.AddUnique(Target.Asset.GetLongPackageFName());
        Result.EstimatedPackageBytes += Current.EstimatedDiskSize;
    }
    Result.AffectedPackages.Sort(FNameLexicalLess());
    if (!Result.IsReady()) { Result.EstimatedPackageBytes = 0; }
    return Result;
}

FSanitizerDeletionResult FContentSanitizerDeletionService::Execute(const FSanitizerDeletionPlan& Plan, const FSanitizerDeletionPreflight& Reviewed, bool bConfirmed) const
{
    FSanitizerDeletionResult Result;
    bool bApiCalled = false;
    ON_SCOPE_EXIT { if (bApiCalled && GEditor) { GEditor->GetEditorSubsystem<UContentSanitizerOperationSubsystem>()->Consume(Plan.OperationId); } };
    if (!bConfirmed || !Reviewed.IsReady() || Reviewed.PlanIdentity != Plan.GetIdentity()) { Result.Messages.Add(TEXT("缺少当前计划的预检与明确确认，未执行。")); return Result; }
    const FSanitizerDeletionPreflight Current = Preflight(Plan);
    if (!Current.IsReady() || Current.AffectedPackages != Reviewed.AffectedPackages) { Result.Messages = Current.Blockers; Result.Messages.Add(TEXT("证据或影响变化，未执行；请重新扫描/预检。")); return Result; }
    for (const FSanitizerDeletionTarget& Target : Plan.Targets)
    {
        // Recheck each exact target immediately before its API call. No dependency closure.
        FSanitizerDeletionPlan Single = Plan;
        Single.Targets = { Target };
        const FSanitizerDeletionPreflight Item = Preflight(Single);
        if (!Item.IsReady()) { Result.Messages.Append(Item.Blockers); if (!Result.VerifiedDeleted.IsEmpty()) { Result.Status = ESanitizerDeletionStatus::ApiFailure; } return Result; }
        UObject* Asset = Target.Asset.TryLoad();
        IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
        TArray<FName> Dependencies;
        Registry.GetDependencies(Target.Asset.GetLongPackageFName(), Dependencies, UE::AssetRegistry::EDependencyCategory::Package);
        TSet<FName> ExistingDependencies;
        for (FName Dependency : Dependencies) { if (FPackageName::DoesPackageExist(Dependency.ToString())) { ExistingDependencies.Add(Dependency); } }
        // Ordinary engine deletion checks references and never chooses force-delete/replace.
        Result.Status = ESanitizerDeletionStatus::ApiFailure;
        bApiCalled = true;
        if (DeleteSingleAsset(Asset) != 1)
        {
            Result.Messages.Add(TEXT("普通删除接口失败，可能部分修改。停止队列；请重新扫描并检查项目。"));
            Result.Messages.Add(FString::Printf(TEXT("观察：%s 注册表资产=%s，磁盘包=%s"), *Target.Asset.ToString(), Registry.GetAssetByObjectPath(Target.Asset).IsValid() ? TEXT("存在") : TEXT("不存在"), FPackageName::DoesPackageExist(Target.Asset.GetLongPackageName()) ? TEXT("存在") : TEXT("不存在")));
            return Result;
        }
        Result.Status = ESanitizerDeletionStatus::VerificationFailed;
        if (Registry.GetAssetByObjectPath(Target.Asset).IsValid() || FPackageName::DoesPackageExist(Target.Asset.GetLongPackageName()))
        {
            Result.Messages.Add(TEXT("删除后资产或包仍存在，验证失败。停止并重新扫描。")); return Result;
        }
        for (FName Dependency : ExistingDependencies)
        {
            if (!FPackageName::DoesPackageExist(Dependency.ToString())) { Result.Messages.Add(TEXT("发现依赖包意外消失，停止并人工检查。")); return Result; }
        }
        Result.VerifiedDeleted.Add(Target.Asset);
        Result.Messages.Add(TEXT("已验证删除：") + Target.Asset.ToString());
    }
    Result.Status = ESanitizerDeletionStatus::Succeeded;
    Result.Messages.Add(TEXT("已验证计划内目标及包不存在，已观察依赖包保留。结果和计划已失效，后续清理需重新扫描。"));
    return Result;
}

int32 FContentSanitizerDeletionService::DeleteSingleAsset(UObject* Asset) const
{
    return ObjectTools::DeleteObjects({ Asset }, false, ObjectTools::EAllowCancelDuringDelete::CancelNotAllowed);
}
