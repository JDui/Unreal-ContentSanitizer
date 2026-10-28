#include "ContentSanitizerConsolidationService.h"

#include "ContentSanitizerCore.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "ObjectTools.h"
#include "Operations/ContentSanitizerOperationSubsystem.h"
#include "Engine/Texture2D.h"
#include "Engine/AssetManager.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "Providers/Texture2DFingerprintProvider.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "ISourceControlState.h"
#include "Model/SanitizerOrphanTypes.h"
#include "Serialization/ArchiveUObject.h"
#include "UObject/SoftObjectPtr.h"
#include "Operations/SanitizerObjectCompatibility.h"
#include "UObject/UnrealType.h"
#include "Operations/SanitizerLoadedReferenceCheck.h"

namespace ContentSanitizerConsolidation
{
    static bool IsPackageWritable(FName PackageName, FString& OutFilename)
    {
        if (!FPackageName::DoesPackageExist(PackageName.ToString(), &OutFilename))
        {
            OutFilename.Reset();
            return false;
        }
        return !IFileManager::Get().IsReadOnly(*OutFilename);
    }

    static bool GetPackageReferencers(IAssetRegistry& Registry, FName PackageName, TSet<FName>& OutReferencers)
    {
        TArray<FName> Referencers;
        const bool bInspected = Registry.GetReferencers(PackageName, Referencers, UE::AssetRegistry::EDependencyCategory::Package);
        for (const FName Referencer : Referencers) { OutReferencers.Add(Referencer); }
        return bInspected;
    }
    // Count actual serialized destinations by owner/property, including hard and soft references.
    class FReferenceAudit final : public FArchiveUObject
    {
    public:
        using FArchiveUObject::operator<<;
        TMap<FString, int32> Counts;
        FString Owner;
        FReferenceAudit() { ArIsObjectReferenceCollector = true; }
        void Record(const FString& Path)
        {
            if (!Path.IsEmpty())
            {
                const FString Property = GetSerializedProperty() ? GetSerializedProperty()->GetPathName() : TEXT("Native");
                ++Counts.FindOrAdd(Owner + TEXT("|") + Property + TEXT("|") + Path);
            }
        }
        virtual FArchive& operator<<(UObject*& Object) override { if (Object) { Record(Object->GetPathName()); } return *this; }
        virtual FArchive& operator<<(FSoftObjectPath& Path) override { Record(Path.ToString()); return *this; }
        virtual FArchive& operator<<(FSoftObjectPtr& Pointer) override { Record(Pointer.ToSoftObjectPath().ToString()); return *this; }
    };

    static TMap<FString, int32> AuditReferences(const FSanitizerActionPlan& Plan, const TArray<FName>& Packages, TArray<FString>& Errors)
    {
        FReferenceAudit Audit;
        for (FName Name : Packages)
        {
            if (Plan.SourceAssets.ContainsByPredicate([&](const FSoftObjectPath& Source) { return Source.GetLongPackageFName() == Name; })) { continue; }
            UPackage* Package = LoadPackage(nullptr, *Name.ToString(), LOAD_None);
            if (!Package) { Errors.Add(Name.ToString() + TEXT(": 无法检查受影响引用包。")); continue; }
            TArray<UObject*> Objects;
            SanitizerGetPackageObjects(Package, Objects, true);
            for (UObject* Object : Objects)
            {
                if (!IsValid(Object)) { continue; }
                Audit.Owner = Object->GetPathName();
                Object->Serialize(Audit);
            }
        }
        return MoveTemp(Audit.Counts);
    }

    static FString ReferenceIdentity(const TMap<FString, int32>& Counts)
    {
        TArray<FString> Keys;
        Counts.GetKeys(Keys);
        Keys.Sort();
        FString Identity;
        for (const FString& Key : Keys) { Identity += Key + TEXT("=") + LexToString(Counts[Key]) + TEXT("\n"); }
        return Identity;
    }

    static TMap<FString, int32> SubstituteReferences(const TMap<FString, int32>& Before, const FSanitizerActionPlan& Completed)
    {
        TMap<FString, int32> Expected;
        for (const auto& Pair : Before)
        {
            FString Key = Pair.Key;
            for (const auto& Source : Completed.SourceAssets)
            {
                const FString Suffix = TEXT("|") + Source.ToString();
                if (Key.EndsWith(Suffix)) { Key.LeftChopInline(Suffix.Len()); Key += TEXT("|") + Completed.CanonicalAsset.ToString(); break; }
            }
            Expected.FindOrAdd(Key) += Pair.Value;
        }
        return Expected;
    }

    static bool SameReview(const FSanitizerPreflightResult& Current, const FSanitizerPreflightResult& Reviewed)
    {
        return Current.IsReady() && Reviewed.IsReady() && Current.PlanIdentity == Reviewed.PlanIdentity
            && Current.AffectedPackages == Reviewed.AffectedPackages && Current.OutsideTargetPackages == Reviewed.OutsideTargetPackages
            && Current.EvidenceIdentity == Reviewed.EvidenceIdentity
            && Current.PackageEvidence.OrderIndependentCompareEqual(Reviewed.PackageEvidence)
            && Current.ReferenceCounts.OrderIndependentCompareEqual(Reviewed.ReferenceCounts);
    }

    static bool IsExpectedTransition(const FSanitizerPreflightResult& Before, const FSanitizerPreflightResult& After,
        const FSanitizerActionPlan& Completed, const TArray<FName>& WrittenPackages)
    {
        if (!After.IsReady() || Before.PlanIdentity != After.PlanIdentity || Before.AffectedPackages != After.AffectedPackages
            || Before.OutsideTargetPackages != After.OutsideTargetPackages || Before.PackageEvidence.Num() != After.PackageEvidence.Num()) { return false; }
        for (const auto& Pair : Before.PackageEvidence)
        {
            const FString* Current = After.PackageEvidence.Find(Pair.Key);
            if (!Current || (!WrittenPackages.Contains(Pair.Key) && Pair.Value != *Current)) { return false; }
        }
        return SubstituteReferences(Before.ReferenceCounts, Completed).OrderIndependentCompareEqual(After.ReferenceCounts);
    }

}

FSanitizerPreflightResult FContentSanitizerConsolidationService::Preflight(const FSanitizerActionPlan& Plan) const
{
    FSanitizerPreflightResult Result = FSanitizerActionPlanner::ValidateShape(Plan);
    if (!Result.IsReady()) { return Result; }

    if (!IsInGameThread())
    {
        Result.Status = ESanitizerPreflightStatus::Blocked;
        Result.Messages.Add(TEXT("预检必须在游戏线程运行。"));
        return Result;
    }
    if (Plan.ProviderId != TEXT("Texture2D") || Plan.SchemaVersion != FTexture2DFingerprintProvider::SchemaVersion)
    {
        Result.Status = ESanitizerPreflightStatus::Blocked;
        Result.Messages.Add(TEXT("操作计划使用了不受支持或已过期的指纹版本，请重新扫描。"));
        return Result;
    }
    FString DirtyUser;
    if (SanitizerHasDirtyReference(Plan.SourceAssets, DirtyUser))
    {
        Result.Status = ESanitizerPreflightStatus::Blocked;
        Result.Messages.Add(TEXT("存在未保存引用包：") + DirtyUser);
        return Result;
    }

    UEditorAssetSubsystem* AssetSubsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>() : nullptr;
    if (GEditor && GEditor->GetEditorSubsystem<UContentSanitizerOperationSubsystem>()->IsConsumed(Plan.OperationId))
    {
        Result.Status = ESanitizerPreflightStatus::Blocked; Result.Messages.Add(TEXT("该计划已调用过修改接口。必须重新扫描并创建新计划。")); return Result;
    }
    if (!AssetSubsystem || GEditor->PlayWorld)
    {
        Result.Status = ESanitizerPreflightStatus::Blocked;
        Result.Messages.Add(TEXT("编辑器资产子系统不可用，无法修改任何内容。"));
        return Result;
    }
    if (!AssetSubsystem->DoesAssetExist(Plan.CanonicalAsset.ToString()))
    {
        Result.Status = ESanitizerPreflightStatus::Blocked;
        Result.Messages.Add(TEXT("主资产已不存在。"));
        return Result;
    }

    UObject* Canonical = Plan.CanonicalAsset.TryLoad();
    if (!Canonical)
    {
        Result.Status = ESanitizerPreflightStatus::Blocked;
        Result.Messages.Add(TEXT("主资产已不存在。"));
        return Result;
    }
    FTexture2DFingerprintProvider Provider;
    FSanitizerFingerprint CanonicalFingerprint;
    FString Error;
    if (Canonical->GetClass() != UTexture2D::StaticClass() || !Provider.BuildDeepFingerprint(Canonical, CanonicalFingerprint, Error) || CanonicalFingerprint.SchemaVersion != Plan.SchemaVersion || CanonicalFingerprint.PayloadHash != Plan.PayloadHash || CanonicalFingerprint.SettingsHash != Plan.SettingsHash)
    {
        Result.Status = ESanitizerPreflightStatus::Blocked;
        Result.Messages.Add(TEXT("主资产在扫描后发生了变化，请重新扫描后再执行。"));
        return Result;
    }
    for (const FSoftObjectPath& SourcePath : Plan.SourceAssets)
    {
        if (!AssetSubsystem->DoesAssetExist(SourcePath.ToString()))
        {
            Result.Status = ESanitizerPreflightStatus::Blocked;
            Result.Messages.Add(FString::Printf(TEXT("待整理资产已不存在：%s"), *SourcePath.ToString()));
            return Result;
        }
        UObject* Source = SourcePath.TryLoad();
        FSanitizerFingerprint SourceFingerprint;
        if (!Source || Source->GetClass() != UTexture2D::StaticClass() || !Provider.DeepVerify(Canonical, Source, Error) || !Provider.BuildDeepFingerprint(Source, SourceFingerprint, Error) || SourceFingerprint.SchemaVersion != Plan.SchemaVersion || SourceFingerprint.PayloadHash != Plan.PayloadHash || SourceFingerprint.SettingsHash != Plan.SettingsHash)
        {
            Result.Status = ESanitizerPreflightStatus::Blocked;
            Result.Messages.Add(FString::Printf(TEXT("待整理资产缺失、不兼容或已发生变化：%s"), *SourcePath.ToString()));
            return Result;
        }
    }

    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    if (Registry.IsLoadingAssets()) { Result.Status = ESanitizerPreflightStatus::Blocked; Result.Messages.Add(TEXT("注册表未就绪。")); return Result; }
    TSet<FName> PackagesToModify;
    PackagesToModify.Add(Plan.CanonicalAsset.GetLongPackageFName());
    for (UObject* User : SanitizerGetLoadedUsers(Plan.SourceAssets, false))
    {
        const FName Name = User->GetOutermost()->GetFName();
        if (FPackageName::DoesPackageExist(Name.ToString())) { PackagesToModify.Add(Name); }
        else { Result.Status = ESanitizerPreflightStatus::Blocked; Result.Messages.Add(TEXT("存在无法覆盖的加载硬/软用户：") + User->GetPathName()); }
    }
    for (const FSoftObjectPath& SourcePath : Plan.SourceAssets)
    {
        const FName SourcePackage = SourcePath.GetLongPackageFName();
        TArray<FAssetData> AssetsInPackage;
        Registry.GetAssetsByPackageName(SourcePackage, AssetsInPackage);
        if (AssetsInPackage.Num() != 1) { Result.Status = ESanitizerPreflightStatus::Blocked; Result.Messages.Add(TEXT("来源包包含多个资产。")); }
        if (GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(SourcePath.ResolveObject(), false)) { Result.Status = ESanitizerPreflightStatus::Blocked; Result.Messages.Add(TEXT("来源正在资产编辑器中使用。")); }
        TArray<UObject*> TopLevel;
        UObject* SourceObject = SourcePath.ResolveObject();
        SanitizerGetPackageObjects(SourceObject->GetOutermost(), TopLevel, false);
        for (UObject* Object : TopLevel)
        {
            if (IsValid(Object) && Object != SourceObject && !SanitizerIsPackageMetadata(Object)) { Result.Status = ESanitizerPreflightStatus::Blocked; Result.Messages.Add(TEXT("来源包包含额外对象。")); break; }
        }
        bool bReferenced = false, bUndo = false;
        FReferencerInformationList MemoryUsers;
        ObjectTools::GatherObjectReferencersForDeletion(SourceObject, bReferenced, bUndo, &MemoryUsers);
        if (bUndo) { Result.Status = ESanitizerPreflightStatus::Blocked; Result.Messages.Add(TEXT("来源存在撤销引用。")); }
        for (const FReferencerInformation& User : MemoryUsers.ExternalReferences)
        {
            if (!User.Referencer) { continue; }
            const FName UserPackage = User.Referencer->GetOutermost()->GetFName();
            if (FPackageName::DoesPackageExist(UserPackage.ToString())) { PackagesToModify.Add(UserPackage); }
            else { Result.Status = ESanitizerPreflightStatus::Blocked; Result.Messages.Add(TEXT("存在无法覆盖的临时/未保存内存引用：") + User.Referencer->GetPathName()); }
        }
        PackagesToModify.Add(SourcePackage);
        if (!ContentSanitizerConsolidation::GetPackageReferencers(Registry, SourcePackage, PackagesToModify)) { Result.Status = ESanitizerPreflightStatus::Blocked; Result.Messages.Add(TEXT("来源引用图节点缺失/查询失败，请等待注册表更新并重新审查。")); }
        UAssetManager* Manager = UAssetManager::GetIfInitialized();
        if (!Manager || !Manager->HasInitialScanCompleted() || SanitizerOrphanPolicy::IsProtectedPrimary(Registry.GetAssetByObjectPath(SourcePath))) { Result.Status = ESanitizerPreflightStatus::Blocked; Result.Messages.Add(TEXT("来源是受保护的 Primary Asset，或保护检查尚未就绪。")); }
    }
    Result.PlanIdentity = Plan.GetIdentity();
    Result.AffectedPackages = PackagesToModify.Array();
    Result.AffectedPackages.Sort(FNameLexicalLess());
    for (const FName PackageName : Result.AffectedPackages)
    {
        if (!SanitizerOrphanPolicy::IsInScope(PackageName.ToString(), Plan.CleanupPackagePaths)) { Result.OutsideTargetPackages.Add(PackageName); }
        if (!SanitizerOrphanPolicy::IsInScope(PackageName.ToString(), { FName(TEXT("/Game")) }) || SanitizerOrphanPolicy::IsInScope(PackageName.ToString(), { FName(TEXT("/Game/Developers")), FName(TEXT("/Game/__ExternalActors__")), FName(TEXT("/Game/__ExternalObjects__")) })) { Result.Status = ESanitizerPreflightStatus::Blocked; Result.Messages.Add(PackageName.ToString() + TEXT(": 影响涉及默认保护目录，禁止修改。")); }
        if (UPackage* Package = FindPackage(nullptr, *PackageName.ToString()); Package && Package->IsDirty()) { Result.Status = ESanitizerPreflightStatus::Blocked; Result.Messages.Add(PackageName.ToString() + TEXT(": 存在未保存更改。")); }
        FString Filename;
        if (!ContentSanitizerConsolidation::IsPackageWritable(PackageName, Filename))
        {
            Result.Status = ESanitizerPreflightStatus::Blocked;
            Result.Messages.Add(FString::Printf(TEXT("包为只读，无法安全整理：%s（%s）"), *PackageName.ToString(), *Filename));
        }
    }
    for (FName PackageName : Result.AffectedPackages)
    {
        FString Filename;
        FPackageName::DoesPackageExist(PackageName.ToString(), &Filename);
        const FString PackageEvidence = FString::Printf(TEXT("%lld|%lld"), IFileManager::Get().FileSize(*Filename), IFileManager::Get().GetTimeStamp(*Filename).GetTicks());
        Result.PackageEvidence.Add(PackageName, PackageEvidence);
        Result.EvidenceIdentity += PackageName.ToString() + TEXT("|") + PackageEvidence + TEXT("\n");
        if (ISourceControlModule::Get().IsEnabled())
        {
            ISourceControlProvider& ProviderState = ISourceControlModule::Get().GetProvider();
            const FSourceControlStatePtr State = ProviderState.GetState(Filename, EStateCacheUsage::ForceUpdate);
            if (!ProviderState.IsAvailable() || !State.IsValid() || State->IsUnknown() || State->IsCheckedOutOther() || (State->IsSourceControlled() && !State->IsCheckedOut() && !State->IsAdded())) { Result.Status = ESanitizerPreflightStatus::Blocked; Result.Messages.Add(PackageName.ToString() + TEXT(": 源控制状态不允许修改。")); }
        }
    }
    TArray<FString> AuditErrors;
    Result.ReferenceCounts = ContentSanitizerConsolidation::AuditReferences(Plan, Result.AffectedPackages, AuditErrors);
    Result.EvidenceIdentity += ContentSanitizerConsolidation::ReferenceIdentity(Result.ReferenceCounts);
    if (!AuditErrors.IsEmpty()) { Result.Status = ESanitizerPreflightStatus::Blocked; Result.Messages.Append(AuditErrors); }
    for (FName Name : Result.AffectedPackages)
    {
        if (UPackage* Package = FindPackage(nullptr, *Name.ToString()); Package && Package->IsDirty()) { Result.Status = ESanitizerPreflightStatus::Blocked; Result.Messages.Add(Name.ToString() + TEXT(": 加载检查发现未保存更改。")); }
    }
    if (!Result.IsReady()) { return Result; }
    Result.Status = ESanitizerPreflightStatus::Ready;
    Result.Messages.Add(TEXT("预检通过，尚未修改任何资产。"));
    return Result;
}

FSanitizerExecutionResult FContentSanitizerConsolidationService::Execute(const FSanitizerActionPlan& Plan, const FSanitizerPreflightResult& Reviewed, bool bConfirmed) const
{
    FSanitizerExecutionResult Result;
    if (!bConfirmed || !Reviewed.IsReady() || Reviewed.PlanIdentity != Plan.GetIdentity()) { Result.Messages.Add(TEXT("缺少当前计划的预检与明确确认，未执行。")); return Result; }
    const FSanitizerPreflightResult Preflight = this->Preflight(Plan);
    if (!Preflight.IsReady() || Preflight.AffectedPackages != Reviewed.AffectedPackages || Preflight.EvidenceIdentity != Reviewed.EvidenceIdentity)
    {
        Result.Messages = Preflight.Messages;
        Result.Messages.Add(TEXT("预检证据或影响已变化；必须重新审查并确认。"));
        return Result;
    }
    UObject* Canonical = Plan.CanonicalAsset.TryLoad();
    TArray<UObject*> Sources;
    for (const FSoftObjectPath& SourcePath : Plan.SourceAssets)
    {
        UObject* Source = SourcePath.TryLoad();
        if (!Source)
        {
            Result.Messages.Add(FString::Printf(TEXT("预检后有待整理资产消失，操作尚未执行：%s"), *SourcePath.ToString()));
            return Result;
        }
        Sources.Add(Source);
    }
    UEditorAssetSubsystem* AssetSubsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>() : nullptr;
    if (!AssetSubsystem)
    {
        Result.Messages.Add(TEXT("编辑器资产子系统不可用，操作尚未执行。"));
        return Result;
    }
    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    TMap<FName, TSet<FName>> ReferencersBefore;
    for (const FSoftObjectPath& SourcePath : Plan.SourceAssets)
    {
        ContentSanitizerConsolidation::GetPackageReferencers(Registry, SourcePath.GetLongPackageFName(), ReferencersBefore.FindOrAdd(SourcePath.GetLongPackageFName()));
    }

    TArray<FString> AuditErrors;
    const TMap<FString, int32> BeforeCounts = ContentSanitizerConsolidation::AuditReferences(Plan, Preflight.AffectedPackages, AuditErrors);
    if (!AuditErrors.IsEmpty()) { Result.Messages = MoveTemp(AuditErrors); return Result; }
    GEditor->GetEditorSubsystem<UContentSanitizerOperationSubsystem>()->Consume(Plan.OperationId);
    if (!SubmitConsolidation(Canonical, Sources))
    {
        Result.Status = ESanitizerExecutionStatus::ConsolidationReportedFailure;
        for (const auto& Path : Plan.SourceAssets)
        {
            const FAssetData Observed = Registry.GetAssetByObjectPath(Path);
            Result.Messages.Add(Path.ToString() + TEXT(": 当前注册表状态=") + (Observed.IsValid() ? Observed.AssetClassPath.ToString() : TEXT("不存在")));
        }
        Result.Messages.Add(TEXT("Unreal 整理接口报告失败。接口已经被调用，项目内容可能已部分变化；不会强制删除，也不会自动重试。请重新扫描并检查引用。"));
        return Result;
    }
    // Saving packages does not synchronously publish their dependency graph on every engine.
    TArray<FString> WrittenFiles;
    for (FName Name : Preflight.AffectedPackages)
    {
        FString Filename;
        if (FPackageName::DoesPackageExist(Name.ToString(), &Filename)) { WrittenFiles.Add(Filename); }
    }
    Registry.ScanFilesSynchronous(WrittenFiles, true);
    const FAssetData CanonicalData = Registry.GetAssetByObjectPath(Plan.CanonicalAsset);
    if (!CanonicalData.IsValid() || CanonicalData.AssetClassPath != UTexture2D::StaticClass()->GetClassPathName())
    {
        Result.Status = ESanitizerExecutionStatus::VerificationFailed;
        Result.Messages.Add(TEXT("Unreal 整理接口报告成功，但主资产验证失败。项目内容已经发生变化，请重新扫描并人工检查。"));
        return Result;
    }
    for (const FSoftObjectPath& SourcePath : Plan.SourceAssets)
    {
        const FAssetData SourceData = Registry.GetAssetByObjectPath(SourcePath);
        if (SourceData.IsValid() && SourceData.AssetClassPath == UTexture2D::StaticClass()->GetClassPathName())
        {
            Result.Status = ESanitizerExecutionStatus::VerificationFailed;
            Result.Messages.Add(FString::Printf(TEXT("整理后的验证失败：待整理路径仍解析为原始纹理资产：%s。项目内容已经发生变化，请重新扫描并人工检查。"), *SourcePath.ToString()));
            return Result;
        }

        TSet<FName> RemainingReferencers;
        ContentSanitizerConsolidation::GetPackageReferencers(Registry, SourcePath.GetLongPackageFName(), RemainingReferencers);
        for (const FName PreviousReferencer : ReferencersBefore.FindChecked(SourcePath.GetLongPackageFName()))
        {
            if (RemainingReferencers.Contains(PreviousReferencer))
            {
                Result.Status = ESanitizerExecutionStatus::VerificationFailed;
                Result.Messages.Add(FString::Printf(TEXT("整理后的验证失败：包 %s 仍引用原包 %s。项目内容已经发生变化，请重新扫描并人工检查。"), *PreviousReferencer.ToString(), *SourcePath.GetLongPackageName()));
                return Result;
            }
        }
    }
    const TMap<FString, int32> ExpectedCounts = ContentSanitizerConsolidation::SubstituteReferences(BeforeCounts, Plan);
    const TMap<FString, int32> AfterCounts = ContentSanitizerConsolidation::AuditReferences(Plan, Preflight.AffectedPackages, AuditErrors);
    if (!AuditErrors.IsEmpty() || ContentSanitizerConsolidation::ReferenceIdentity(ExpectedCounts) != ContentSanitizerConsolidation::ReferenceIdentity(AfterCounts))
    {
        Result.Status = ESanitizerExecutionStatus::VerificationFailed;
        Result.Messages.Add(TEXT("引用目标/数量验证失败，内容可能已经变化。停止并重新扫描/人工检查。"));
        Result.Messages.Append(AuditErrors);
        return Result;
    }
    Result.Status = ESanitizerExecutionStatus::Succeeded;
    Result.VerifiedPlanIdentity = Plan.GetIdentity();
    Result.Messages.Add(TEXT("整理与操作后验证均成功。重定向器清理由独立的显式操作完成。"));
    return Result;
}

FSanitizerConsolidationBatchPreflight FContentSanitizerConsolidationService::PreflightBatch(const TArray<FSanitizerActionPlan>& Plans) const
{
    FSanitizerConsolidationBatchPreflight Batch;
    Batch.OperationId = FGuid::NewGuid().ToString();
    Batch.bReady = !Plans.IsEmpty();
    TMap<FName, int32> MemberPackages;
    TMap<FName, int32> ImpactCounts;
    TSet<FString> Operations;
    for (int32 Index = 0; Index < Plans.Num(); ++Index)
    {
        const auto& Plan = Plans[Index];
        if (Operations.Contains(Plan.OperationId)) { Batch.bReady = false; Batch.Messages.Add(TEXT("批次包含重复操作标识。")); }
        Operations.Add(Plan.OperationId);
        TArray<FSoftObjectPath> Members = Plan.SourceAssets;
        Members.Add(Plan.CanonicalAsset);
        for (const auto& Member : Members)
        {
            if (MemberPackages.Contains(Member.GetLongPackageFName())) { Batch.bReady = false; Batch.Messages.Add(TEXT("重复组的资产包重叠，不能作为独立批次执行。")); }
            MemberPackages.Add(Member.GetLongPackageFName(), Index);
        }
        auto Check = Preflight(Plan);
        Batch.bReady &= Check.IsReady();
        Batch.Messages.Append(Check.Messages);
        for (FName Package : Check.AffectedPackages) { ++ImpactCounts.FindOrAdd(Package); }
        Batch.PlanReviews.Add(MoveTemp(Check));
    }
    for (int32 Index = 0; Index < Batch.PlanReviews.Num(); ++Index)
    {
        for (FName Package : Batch.PlanReviews[Index].AffectedPackages)
        {
            const int32* Owner = MemberPackages.Find(Package);
            if (Owner && *Owner != Index) { Batch.bReady = false; Batch.Messages.Add(TEXT("组间存在资产依赖，需分开扫描审查，不能作为独立批次执行。")); }
        }
    }
    for (const auto& Pair : ImpactCounts) { if (Pair.Value > 1) { Batch.SharedPackages.Add(Pair.Key); } }
    Batch.SharedPackages.Sort(FNameLexicalLess());
    if (!Batch.SharedPackages.IsEmpty()) { Batch.Messages.Add(TEXT("批次共享引用包将按已确认替换逐组验证；新增影响或非预期引用变化会停止整个批次。")); }
    return Batch;
}

FSanitizerConsolidationBatchResult FContentSanitizerConsolidationService::ExecuteBatch(const TArray<FSanitizerActionPlan>& Plans,
    const FSanitizerConsolidationBatchPreflight& Reviewed, bool bConfirmed) const
{
    FSanitizerConsolidationBatchResult Batch;
    Batch.PlanResults.SetNum(Plans.Num());
    if (!bConfirmed || !Reviewed.IsReady() || Reviewed.PlanReviews.Num() != Plans.Num() || Reviewed.OperationId.IsEmpty()
        || !IsInGameThread() || !GEditor) { Batch.Messages.Add(TEXT("缺少完整批次预检及明确确认，未执行。")); return Batch; }
    auto* Journal = GEditor->GetEditorSubsystem<UContentSanitizerOperationSubsystem>();
    if (Journal->IsConsumed(Reviewed.OperationId)) { Batch.Messages.Add(TEXT("批次已经调用修改接口，必须重新扫描和审查。")); return Batch; }
    bool bAnySubmitted = false;
    // A partial batch invalidates all original plans, including untouched items. Never retry them as standalone old plans.
    ON_SCOPE_EXIT
    {
        if (bAnySubmitted)
        {
            Journal->Consume(Reviewed.OperationId);
            for (const auto& Plan : Plans) { Journal->Consume(Plan.OperationId); }
        }
    };
    const auto Initial = PreflightBatch(Plans);
    if (!Initial.IsReady() || Initial.SharedPackages != Reviewed.SharedPackages) { Batch.Messages = Initial.Messages; Batch.Messages.Add(TEXT("批次结构或影响已变化，未执行。")); return Batch; }
    for (int32 Index = 0; Index < Plans.Num(); ++Index)
    {
        if (!ContentSanitizerConsolidation::SameReview(Initial.PlanReviews[Index], Reviewed.PlanReviews[Index]))
        {
            Batch.Messages.Add(TEXT("批次预检证据已变化，未调用任何整理接口；请重新审查确认。")); return Batch;
        }
    }
    TArray<FSanitizerPreflightResult> Expected = Reviewed.PlanReviews;
    for (int32 Index = 0; Index < Plans.Num(); ++Index)
    {
        // Validate every pending item before each native call, not only the next group.
        for (int32 Pending = Index; Pending < Plans.Num(); ++Pending)
        {
            const auto Current = Preflight(Plans[Pending]);
            if (!ContentSanitizerConsolidation::SameReview(Current, Expected[Pending]))
            {
                Batch.Messages.Append(Current.Messages);
                Batch.Messages.Add(TEXT("剩余批次证据发生非预期变化，停止；未执行项须重新扫描和审查。")); return Batch;
            }
        }
        Batch.PlanResults[Index] = Execute(Plans[Index], Expected[Index], true);
        bAnySubmitted |= Batch.PlanResults[Index].MayHaveModifiedContent();
        if (!Batch.PlanResults[Index].IsSucceeded()) { Batch.Messages.Add(TEXT("批次在首个未执行、接口失败或验证失败处停止。")); return Batch; }
        for (int32 Pending = Index + 1; Pending < Plans.Num(); ++Pending)
        {
            auto Current = Preflight(Plans[Pending]);
            if (!ContentSanitizerConsolidation::IsExpectedTransition(Expected[Pending], Current, Plans[Index], Expected[Index].AffectedPackages))
            {
                Batch.Messages.Append(Current.Messages);
                Batch.Messages.Add(TEXT("已完成组之外的证据或影响发生变化，停止；不会把重新预检通过当成新的用户确认。")); return Batch;
            }
            // Only an exact verified substitution permits the already-reviewed package save stamps to advance.
            Expected[Pending] = MoveTemp(Current);
        }
    }
    Batch.Messages.Add(TEXT("全部已确认的独立重复组及共享引用包验证完成。"));
    return Batch;
}

bool FContentSanitizerConsolidationService::SubmitConsolidation(UObject* Canonical, const TArray<UObject*>& Sources) const
{
    UEditorAssetSubsystem* Assets = GEditor ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>() : nullptr;
    return Assets && Assets->ConsolidateAssets(Canonical, Sources);
}
