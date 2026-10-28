#include "ContentSanitizerRedirectorService.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "Engine/AssetManager.h"
#include "Operations/ContentSanitizerOperationSubsystem.h"
#include "ObjectTools.h"
#include "HAL/FileManager.h"
#include "ISourceControlModule.h"
#include "Model/SanitizerOrphanTypes.h"
#include "Misc/PackageName.h"
#include "Operations/SanitizerLoadedReferenceCheck.h"
#include "UObject/ObjectRedirector.h"

bool FContentSanitizerRedirectorService::CreatePlan(const FSanitizerActionPlan& Plan, const FSanitizerExecutionResult& Verified, FSanitizerRedirectorPlan& OutPlan) const
{
    OutPlan = {};
    if (!Verified.IsSucceeded() || Verified.VerifiedPlanIdentity != Plan.GetIdentity()) { return false; }
    OutPlan.OperationId = FGuid::NewGuid().ToString();
    OutPlan.CleanupRoots = Plan.CleanupPackagePaths;
    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    for (const auto& Source : Plan.SourceAssets)
    {
        if (Registry.GetAssetByObjectPath(Source).IsRedirector()) { OutPlan.Redirectors.Add(Source, Plan.CanonicalAsset); }
    }
    return !OutPlan.Redirectors.IsEmpty();
}

FSanitizerPreflightResult FContentSanitizerRedirectorService::Preflight(const FSanitizerRedirectorPlan& Plan) const
{
    FSanitizerPreflightResult Result;
    if (!IsInGameThread() || !GEditor || GEditor->PlayWorld || Plan.OperationId.IsEmpty() || Plan.CleanupRoots.IsEmpty() || Plan.Redirectors.IsEmpty()) { Result.Messages.Add(TEXT("重定向器计划为空或编辑器状态不允许执行。")); return Result; }
    if (GEditor->GetEditorSubsystem<UContentSanitizerOperationSubsystem>()->IsConsumed(Plan.OperationId)) { Result.Messages.Add(TEXT("该计划已执行过修改调用，必须重新审查。")); return Result; }
    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    if (Registry.IsLoadingAssets() || ISourceControlModule::Get().IsEnabled()) { Result.Messages.Add(TEXT("注册表未就绪；当前重定向器清理仅支持关闭源控制的可写包。")); return Result; }
    UAssetManager* Manager = UAssetManager::GetIfInitialized();
    if (!Manager || !Manager->HasInitialScanCompleted()) { Result.Messages.Add(TEXT("保护检查尚未就绪。")); return Result; }
    Manager->UpdateManagementDatabase();
    TArray<FSoftObjectPath> Paths;
    Plan.Redirectors.GetKeys(Paths);
    Paths.Sort([](const auto& A, const auto& B) { return A.ToString() < B.ToString(); });
    Result.PlanIdentity = Plan.OperationId;
    for (FName Root : Plan.CleanupRoots) { Result.PlanIdentity += TEXT("|") + Root.ToString(); }
    for (const auto& Path : Paths)
    {
        const auto& Destination = Plan.Redirectors[Path];
        Result.PlanIdentity += TEXT("|") + Path.ToString() + TEXT("->") + Destination.ToString();
        const FAssetData Data = Registry.GetAssetByObjectPath(Path);
        UObjectRedirector* Redirector = Data.IsRedirector() ? Cast<UObjectRedirector>(Data.GetAsset()) : nullptr;
        TArray<FName> Users;
        const bool bPackageInspected = Registry.GetReferencers(Path.GetLongPackageFName(), Users);
        TArray<FAssetIdentifier> Managers;
        const bool bManagementInspected = Registry.GetReferencers(FAssetIdentifier(Path.GetLongPackageFName()), Managers, UE::AssetRegistry::EDependencyCategory::Manage);
        FString DirtyUser, Filename;
        const bool bFileExists = FPackageName::DoesPackageExist(Path.GetLongPackageName(), &Filename);
        if (!SanitizerOrphanPolicy::IsInScope(Path.GetLongPackageName(), Plan.CleanupRoots) || !SanitizerOrphanPolicy::IsInScope(Path.GetLongPackageName(), { FName(TEXT("/Game")) })
            || SanitizerOrphanPolicy::IsInScope(Path.GetLongPackageName(), { FName(TEXT("/Game/Developers")) })
            || !Redirector || !Redirector->DestinationObject || Redirector->DestinationObject->GetPathName() != Destination.ToString()
            || !bPackageInspected || !bManagementInspected || !Managers.IsEmpty() || !Users.IsEmpty() || !bFileExists || IFileManager::Get().IsReadOnly(*Filename)
            || Redirector->GetOutermost()->IsDirty() || SanitizerHasDirtyReference({ Path }, DirtyUser) || !SanitizerGetLoadedUsers({ Path }, false).IsEmpty())
        {
            Result.Messages.Add(Path.ToString() + TEXT(": 范围、引用、目标、脏包或可写性检查失败。当前仅清理无剩余用户的已验证重定向器。"));
        }
        if (Redirector)
        {
            bool bReferenced = false, bUndo = false;
            ObjectTools::GatherObjectReferencersForDeletion(Redirector, bReferenced, bUndo);
            TArray<FAssetData> Companions; Registry.GetAssetsByPackageName(Path.GetLongPackageFName(), Companions);
            if (bReferenced || bUndo || Companions.Num() != 1) { Result.Messages.Add(TEXT("存在加载引用、撤销引用或同包对象，阻止清理。")); }
        }
        Result.AffectedPackages.AddUnique(Path.GetLongPackageFName());
        Result.EvidenceIdentity += FString::Printf(TEXT("%s|%lld|%lld\n"), *Path.ToString(), IFileManager::Get().FileSize(*Filename), IFileManager::Get().GetTimeStamp(*Filename).GetTicks());
    }
    Result.Status = Result.Messages.IsEmpty() ? ESanitizerPreflightStatus::ReadyWithWarnings : ESanitizerPreflightStatus::Blocked;
    if (Result.IsReady()) { Result.Messages.Add(SanitizerOrphanPolicy::CoverageLimitation()); }
    return Result;
}

FSanitizerExecutionResult FContentSanitizerRedirectorService::Execute(const FSanitizerRedirectorPlan& Plan, const FSanitizerPreflightResult& Reviewed, bool bConfirmed) const
{
    FSanitizerExecutionResult Result;
    if (!bConfirmed || !Reviewed.IsReady()) { Result.Messages.Add(TEXT("重定向器清理未确认。")); return Result; }
    const auto Current = Preflight(Plan);
    if (!Current.IsReady() || Current.PlanIdentity != Reviewed.PlanIdentity || Current.EvidenceIdentity != Reviewed.EvidenceIdentity || Current.AffectedPackages != Reviewed.AffectedPackages) { Result.Messages = Current.Messages; Result.Messages.Add(TEXT("重定向器证据已变化，未执行。")); return Result; }
    TArray<UObjectRedirector*> Objects;
    IAssetRegistry& CurrentRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    for (const auto& Pair : Plan.Redirectors)
    {
        const FAssetData Data = CurrentRegistry.GetAssetByObjectPath(Pair.Key);
        UObjectRedirector* Object = Data.IsRedirector() ? Cast<UObjectRedirector>(Data.GetAsset()) : nullptr;
        if (!Object || Object->GetPathName() != Pair.Key.ToString()) { Result.Messages.Add(TEXT("重定向器对象身份已变化，未执行。")); return Result; }
        Objects.Add(Object);
    }
    Result.Status = ESanitizerExecutionStatus::VerificationFailed;
    GEditor->GetEditorSubsystem<UContentSanitizerOperationSubsystem>()->Consume(Plan.OperationId);
    TArray<UObject*> UnreferencedRedirectors;
    for (UObjectRedirector* Object : Objects) { UnreferencedRedirectors.Add(Object); }
    // No users remain, so no reference fix-up is needed. Avoid AssetTools' modal report path.
    if (ObjectTools::DeleteObjects(UnreferencedRedirectors, false, ObjectTools::EAllowCancelDuringDelete::CancelNotAllowed) != UnreferencedRedirectors.Num())
    {
        Result.Status = ESanitizerExecutionStatus::RedirectorReportedFailure;
        Result.Messages.Add(TEXT("普通重定向器删除接口未报告全部成功，可能部分修改；停止并重新扫描。"));
        return Result;
    }
    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    for (const auto& Pair : Plan.Redirectors)
    {
        if (Registry.GetAssetByObjectPath(Pair.Key).IsValid() || FPackageName::DoesPackageExist(Pair.Key.GetLongPackageName()) || !Registry.GetAssetByObjectPath(Pair.Value).IsValid()) { Result.Messages.Add(TEXT("清理后验证失败，可能已部分修改；请重新扫描并检查。")); return Result; }
    }
    Result.Status = ESanitizerExecutionStatus::Succeeded;
    Result.Messages.Add(TEXT("独立重定向器清理已验证。"));
    return Result;
}
