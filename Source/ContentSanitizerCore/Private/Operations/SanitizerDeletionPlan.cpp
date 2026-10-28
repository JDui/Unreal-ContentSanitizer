#include "Operations/SanitizerDeletionPlan.h"

FString FSanitizerDeletionPlan::GetIdentity() const
{
    FString Identity = FString::Printf(TEXT("%s|%llu|%u|"), *OperationId, ScanRevision, PolicyRevision);
    for (FName Root : CleanupRoots) { Identity += Root.ToString() + TEXT("|"); }
    for (FName Root : ExcludedRoots) { Identity += TEXT("exclude:") + Root.ToString() + TEXT("|"); }
    for (const FSanitizerDeletionTarget& Target : Targets) { Identity += Target.Asset.ToString() + TEXT("|") + Target.Class.ToString() + TEXT("|") + Target.EvidenceIdentity + TEXT("\n"); }
    return Identity;
}

TArray<FString> FSanitizerDeletionPlanner::ValidateShape(const FSanitizerDeletionPlan& Plan)
{
    TArray<FString> Errors;
    if (Plan.OperationId.IsEmpty() || !Plan.ScanRevision || Plan.PolicyRevision != SanitizerOrphanPolicy::Revision || Plan.CleanupRoots.IsEmpty() || Plan.Targets.IsEmpty()) { Errors.Add(TEXT("删除计划为空或版本/操作身份无效。")); }
    TSet<FName> Packages;
    for (const FSanitizerDeletionTarget& Target : Plan.Targets)
    {
        if (SanitizerOrphanPolicy::IsInScope(Target.Asset.GetLongPackageName(), Plan.ExcludedRoots) || !Target.Asset.IsValid() || !Target.Asset.GetSubPathString().IsEmpty() || Target.EvidenceIdentity.IsEmpty() || !SanitizerOrphanPolicy::IsInScope(Target.Asset.GetLongPackageName(), Plan.CleanupRoots)) { Errors.Add(TEXT("删除目标无效、缺少证据或超出清理范围。")); }
        if (Packages.Contains(Target.Asset.GetLongPackageFName())) { Errors.Add(TEXT("不允许重复目标或同包多个目标。")); }
        Packages.Add(Target.Asset.GetLongPackageFName());
    }
    return Errors;
}

bool FSanitizerDeletionPlanner::CreatePlan(const FSanitizerOrphanScanResult& Scan, const TArray<FSoftObjectPath>& Targets, FSanitizerDeletionPlan& OutPlan, FString& OutError)
{
    OutPlan = {};
    if (Scan.State != ESanitizerSessionState::Completed || Scan.bCanceled || !Scan.Errors.IsEmpty()) { OutError = TEXT("仅完整完成的扫描可生成删除计划。请重新扫描。"); return false; }
    OutPlan.OperationId = FGuid::NewGuid().ToString();
    OutPlan.ScanRevision = Scan.Revision;
    OutPlan.CleanupRoots = Scan.Request.PackagePaths;
    OutPlan.ExcludedRoots = Scan.Request.ExcludedPackagePaths;
    for (const FSoftObjectPath& Path : Targets)
    {
        const FSanitizerOrphanRecord* Record = Scan.Orphans.FindByPredicate([&](const FSanitizerOrphanRecord& Item) { return Item.GetObjectPath() == Path.ToString(); });
        if (!Record || !Record->bRegistryReady || !Record->bSupportedState || Record->Classification != ESanitizerOrphanClassification::Candidate || !Record->Blockers.IsEmpty()) { OutError = TEXT("目标不是已审查的无引用候选，禁止生成计划。"); OutPlan = {}; return false; }
        OutPlan.Targets.Add({ Path, Record->AssetData.AssetClassPath, Record->EvidenceIdentity });
    }
    OutPlan.Targets.Sort([](const FSanitizerDeletionTarget& A, const FSanitizerDeletionTarget& B) { return A.Asset.ToString() < B.Asset.ToString(); });
    const TArray<FString> Errors = ValidateShape(OutPlan);
    OutError = FString::Join(Errors, TEXT("\n"));
    return Errors.IsEmpty();
}
