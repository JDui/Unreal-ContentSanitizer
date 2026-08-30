#include "ContentSanitizerConsolidationService.h"

#include "ContentSanitizerCore.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Providers/Texture2DFingerprintProvider.h"
#include "Subsystems/EditorAssetSubsystem.h"

namespace ContentSanitizerConsolidation
{
    static bool IsPackageWritable(FName PackageName, FString& OutFilename)
    {
        if (!FPackageName::DoesPackageExist(PackageName.ToString(), &OutFilename))
        {
            OutFilename.Reset();
            return true;
        }
        return !IFileManager::Get().IsReadOnly(*OutFilename);
    }

    static void GetPackageReferencers(IAssetRegistry& Registry, FName PackageName, TSet<FName>& OutReferencers)
    {
        TArray<FName> Referencers;
        Registry.GetReferencers(PackageName, Referencers, UE::AssetRegistry::EDependencyCategory::Package);
        for (const FName Referencer : Referencers) { OutReferencers.Add(Referencer); }
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

    UEditorAssetSubsystem* AssetSubsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>() : nullptr;
    if (!AssetSubsystem)
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
    if (!Provider.BuildDeepFingerprint(Canonical, CanonicalFingerprint, Error) || CanonicalFingerprint.SchemaVersion != Plan.SchemaVersion || CanonicalFingerprint.PayloadHash != Plan.PayloadHash || CanonicalFingerprint.SettingsHash != Plan.SettingsHash)
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
        if (!Source || !Provider.BuildDeepFingerprint(Source, SourceFingerprint, Error) || SourceFingerprint.SchemaVersion != Plan.SchemaVersion || SourceFingerprint.PayloadHash != Plan.PayloadHash || SourceFingerprint.SettingsHash != Plan.SettingsHash)
        {
            Result.Status = ESanitizerPreflightStatus::Blocked;
            Result.Messages.Add(FString::Printf(TEXT("待整理资产缺失、不兼容或已发生变化：%s"), *SourcePath.ToString()));
            return Result;
        }
    }

    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    TSet<FName> PackagesToModify;
    PackagesToModify.Add(Plan.CanonicalAsset.GetLongPackageFName());
    for (const FSoftObjectPath& SourcePath : Plan.SourceAssets)
    {
        const FName SourcePackage = SourcePath.GetLongPackageFName();
        PackagesToModify.Add(SourcePackage);
        ContentSanitizerConsolidation::GetPackageReferencers(Registry, SourcePackage, PackagesToModify);
    }
    for (const FName PackageName : PackagesToModify)
    {
        FString Filename;
        if (!ContentSanitizerConsolidation::IsPackageWritable(PackageName, Filename))
        {
            Result.Status = ESanitizerPreflightStatus::Blocked;
            Result.Messages.Add(FString::Printf(TEXT("包为只读，无法安全整理：%s（%s）"), *PackageName.ToString(), *Filename));
        }
    }
    if (!Result.IsReady()) { return Result; }
    Result.Status = ESanitizerPreflightStatus::Ready;
    Result.Messages.Add(TEXT("预检通过，尚未修改任何资产。"));
    return Result;
}

FSanitizerExecutionResult FContentSanitizerConsolidationService::Execute(const FSanitizerActionPlan& Plan) const
{
    FSanitizerExecutionResult Result;
    const FSanitizerPreflightResult Preflight = this->Preflight(Plan);
    if (!Preflight.IsReady())
    {
        Result.Messages = Preflight.Messages;
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

    if (!AssetSubsystem->ConsolidateAssets(Canonical, Sources))
    {
        Result.Status = ESanitizerExecutionStatus::ConsolidationReportedFailure;
        Result.Messages.Add(TEXT("Unreal 整理接口报告失败。接口已经被调用，项目内容可能已部分变化；不会强制删除，也不会自动重试。请重新扫描并检查引用。"));
        return Result;
    }
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
    Result.Status = ESanitizerExecutionStatus::Succeeded;
    Result.Messages.Add(TEXT("整理与操作后验证均成功。重定向器清理由独立的显式操作完成。"));
    return Result;
}
