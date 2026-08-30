#include "Operations/SanitizerActionPlan.h"

bool FSanitizerActionPlanner::CreatePlan(const FSanitizerDuplicateGroup& Group, uint64 ScanRevision, FSanitizerActionPlan& OutPlan, FString& OutError)
{
    if (Group.Classification != ESanitizerClassification::SafeDuplicate || !Group.Members.IsValidIndex(Group.CanonicalMemberIndex))
    {
        OutError = TEXT("Only a proven Safe Duplicate group with a valid canonical asset can be planned.");
        return false;
    }
    OutPlan = {};
    OutPlan.PlanId = Group.GroupId;
    OutPlan.ProviderId = Group.ProviderId.ToString();
    OutPlan.SchemaVersion = Group.Members[Group.CanonicalMemberIndex].Fingerprint.SchemaVersion;
    OutPlan.ScanRevision = ScanRevision;
    OutPlan.CanonicalAsset = FSoftObjectPath(Group.Members[Group.CanonicalMemberIndex].Record.GetObjectPath());
    OutPlan.PayloadHash = Group.Members[Group.CanonicalMemberIndex].Fingerprint.PayloadHash;
    OutPlan.SettingsHash = Group.Members[Group.CanonicalMemberIndex].Fingerprint.SettingsHash;
    for (int32 Index = 0; Index < Group.Members.Num(); ++Index)
    {
        if (Index != Group.CanonicalMemberIndex) { OutPlan.SourceAssets.Add(FSoftObjectPath(Group.Members[Index].Record.GetObjectPath())); }
    }
    OutError.Reset();
    return true;
}

FSanitizerPreflightResult FSanitizerActionPlanner::ValidateShape(const FSanitizerActionPlan& Plan)
{
    FSanitizerPreflightResult Result;
    if (!Plan.CanonicalAsset.IsValid()) { Result.Messages.Add(TEXT("Canonical asset is missing.")); }
    if (Plan.SourceAssets.IsEmpty()) { Result.Messages.Add(TEXT("Action plan has no source assets.")); }
    TSet<FSoftObjectPath> UniqueSources;
    for (const FSoftObjectPath& Source : Plan.SourceAssets)
    {
        if (!Source.IsValid()) { Result.Messages.Add(TEXT("Action plan contains an invalid source asset.")); }
        if (Source == Plan.CanonicalAsset) { Result.Messages.Add(TEXT("Canonical asset cannot be a consolidation source.")); }
        if (UniqueSources.Contains(Source)) { Result.Messages.Add(TEXT("Action plan contains a duplicate source asset.")); }
        UniqueSources.Add(Source);
    }
    Result.Status = Result.Messages.IsEmpty() ? ESanitizerPreflightStatus::Ready : ESanitizerPreflightStatus::Blocked;
    return Result;
}
