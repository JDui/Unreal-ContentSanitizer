#include "Operations/SanitizerActionPlan.h"

namespace ContentSanitizerActionPlan
{
    static bool IsPathWithin(const FString& PackagePath, const FString& RootPath)
    {
        return PackagePath == RootPath || PackagePath.StartsWith(RootPath + TEXT("/"));
    }

    static bool IsInCleanupScope(const FSanitizerDuplicateMember& Member, const TArray<FName>& CleanupPackagePaths)
    {
        const FString PackagePath = Member.Record.PackagePath.ToString();
        return CleanupPackagePaths.ContainsByPredicate([&PackagePath](FName Root)
        {
            return IsPathWithin(PackagePath, Root.ToString());
        });
    }

    static bool ValidateVerifiedGroup(const FSanitizerDuplicateGroup& Group, FSanitizerActionPlan& OutPlan, FString& OutError)
    {
        if (Group.Classification != ESanitizerClassification::SafeDuplicate || !Group.Members.IsValidIndex(Group.CanonicalMemberIndex))
        {
            OutError = TEXT("只有已证实且主资产有效的“安全重复项”组才能生成整理计划。");
            return false;
        }
        const FSanitizerFingerprint& CanonicalFingerprint = Group.Members[Group.CanonicalMemberIndex].Fingerprint;
        if (!CanonicalFingerprint.bDeepVerified)
        {
            OutError = TEXT("主资产尚未完成深度等价验证。");
            return false;
        }
        for (const FSanitizerDuplicateMember& Member : Group.Members)
        {
            if (!Member.Fingerprint.bDeepVerified || Member.Fingerprint.SchemaVersion != CanonicalFingerprint.SchemaVersion || Member.Fingerprint.PayloadHash != CanonicalFingerprint.PayloadHash || Member.Fingerprint.SettingsHash != CanonicalFingerprint.SettingsHash)
            {
                OutError = TEXT("计划中的每个成员都必须与主资产具有相同且已验证的载荷、设置和指纹版本。");
                return false;
            }
        }
        OutPlan = {};
        OutPlan.PlanId = Group.GroupId;
        OutPlan.ProviderId = Group.ProviderId.ToString();
        OutPlan.SchemaVersion = CanonicalFingerprint.SchemaVersion;
        OutPlan.CanonicalAsset = FSoftObjectPath(Group.Members[Group.CanonicalMemberIndex].Record.GetObjectPath());
        OutPlan.PayloadHash = CanonicalFingerprint.PayloadHash;
        OutPlan.SettingsHash = CanonicalFingerprint.SettingsHash;
        return true;
    }
}

bool FSanitizerActionPlanner::CreatePlan(const FSanitizerDuplicateGroup& Group, uint64 ScanRevision, FSanitizerActionPlan& OutPlan, FString& OutError)
{
    if (!ContentSanitizerActionPlan::ValidateVerifiedGroup(Group, OutPlan, OutError)) { return false; }
    OutPlan.ScanRevision = ScanRevision;
    for (int32 Index = 0; Index < Group.Members.Num(); ++Index)
    {
        if (Index != Group.CanonicalMemberIndex) { OutPlan.SourceAssets.Add(FSoftObjectPath(Group.Members[Index].Record.GetObjectPath())); }
    }
    OutPlan.SourceAssets.Sort([](const FSoftObjectPath& Left, const FSoftObjectPath& Right) { return Left.ToString() < Right.ToString(); });
    OutError.Reset();
    return true;
}

bool FSanitizerActionPlanner::CreatePlanForScope(const FSanitizerDuplicateGroup& Group, uint64 ScanRevision, const TArray<FName>& CleanupPackagePaths, FSanitizerActionPlan& OutPlan, FString& OutError)
{
    if (CleanupPackagePaths.IsEmpty())
    {
        OutError = TEXT("整理范围为空，拒绝生成修改计划。");
        return false;
    }
    if (!ContentSanitizerActionPlan::ValidateVerifiedGroup(Group, OutPlan, OutError)) { return false; }
    OutPlan.ScanRevision = ScanRevision;
    for (int32 Index = 0; Index < Group.Members.Num(); ++Index)
    {
        if (Index == Group.CanonicalMemberIndex) { continue; }
        const FSanitizerDuplicateMember& Member = Group.Members[Index];
        if (ContentSanitizerActionPlan::IsInCleanupScope(Member, CleanupPackagePaths))
        {
            OutPlan.SourceAssets.Add(FSoftObjectPath(Member.Record.GetObjectPath()));
        }
    }
    if (OutPlan.SourceAssets.IsEmpty())
    {
        OutPlan = {};
        OutError = TEXT("该重复组在当前整理范围内没有可整理的来源资产。");
        return false;
    }
    OutPlan.SourceAssets.Sort([](const FSoftObjectPath& Left, const FSoftObjectPath& Right) { return Left.ToString() < Right.ToString(); });
    OutError.Reset();
    return true;
}

FSanitizerPreflightResult FSanitizerActionPlanner::ValidateShape(const FSanitizerActionPlan& Plan)
{
    FSanitizerPreflightResult Result;
    if (!Plan.CanonicalAsset.IsValid()) { Result.Messages.Add(TEXT("整理计划缺少有效的主资产。")); }
    if (Plan.SourceAssets.IsEmpty()) { Result.Messages.Add(TEXT("整理计划中没有待整理资产。")); }
    TSet<FSoftObjectPath> UniqueSources;
    for (const FSoftObjectPath& Source : Plan.SourceAssets)
    {
        if (!Source.IsValid()) { Result.Messages.Add(TEXT("整理计划包含无效的待整理资产。")); }
        if (Source == Plan.CanonicalAsset) { Result.Messages.Add(TEXT("主资产不能同时作为待整理资产。")); }
        if (UniqueSources.Contains(Source)) { Result.Messages.Add(TEXT("整理计划包含重复的待整理资产。")); }
        UniqueSources.Add(Source);
    }
    Result.Status = Result.Messages.IsEmpty() ? ESanitizerPreflightStatus::Ready : ESanitizerPreflightStatus::Blocked;
    return Result;
}
