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
        Result.Messages.Add(TEXT("Preflight must run on the game thread."));
        return Result;
    }
    if (Plan.ProviderId != TEXT("Texture2D") || Plan.SchemaVersion != FTexture2DFingerprintProvider::SchemaVersion)
    {
        Result.Status = ESanitizerPreflightStatus::Blocked;
        Result.Messages.Add(TEXT("The action plan provider or fingerprint schema is unsupported or stale."));
        return Result;
    }

    UEditorAssetSubsystem* AssetSubsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>() : nullptr;
    if (!AssetSubsystem)
    {
        Result.Status = ESanitizerPreflightStatus::Blocked;
        Result.Messages.Add(TEXT("Editor asset subsystem is unavailable; no mutation can be performed."));
        return Result;
    }
    if (!AssetSubsystem->DoesAssetExist(Plan.CanonicalAsset.ToString()))
    {
        Result.Status = ESanitizerPreflightStatus::Blocked;
        Result.Messages.Add(TEXT("Canonical asset no longer exists."));
        return Result;
    }

    UObject* Canonical = Plan.CanonicalAsset.TryLoad();
    if (!Canonical)
    {
        Result.Status = ESanitizerPreflightStatus::Blocked;
        Result.Messages.Add(TEXT("Canonical asset no longer exists."));
        return Result;
    }
    FTexture2DFingerprintProvider Provider;
    FSanitizerFingerprint CanonicalFingerprint;
    FString Error;
    if (!Provider.BuildDeepFingerprint(Canonical, CanonicalFingerprint, Error) || CanonicalFingerprint.SchemaVersion != Plan.SchemaVersion || CanonicalFingerprint.PayloadHash != Plan.PayloadHash || CanonicalFingerprint.SettingsHash != Plan.SettingsHash)
    {
        Result.Status = ESanitizerPreflightStatus::Blocked;
        Result.Messages.Add(TEXT("Canonical asset changed since scan; rescan before mutation."));
        return Result;
    }
    for (const FSoftObjectPath& SourcePath : Plan.SourceAssets)
    {
        if (!AssetSubsystem->DoesAssetExist(SourcePath.ToString()))
        {
            Result.Status = ESanitizerPreflightStatus::Blocked;
            Result.Messages.Add(FString::Printf(TEXT("Source asset no longer exists: %s"), *SourcePath.ToString()));
            return Result;
        }
        UObject* Source = SourcePath.TryLoad();
        FSanitizerFingerprint SourceFingerprint;
        if (!Source || !Provider.BuildDeepFingerprint(Source, SourceFingerprint, Error) || SourceFingerprint.SchemaVersion != Plan.SchemaVersion || SourceFingerprint.PayloadHash != Plan.PayloadHash || SourceFingerprint.SettingsHash != Plan.SettingsHash || !Provider.DeepVerify(Canonical, Source, Error))
        {
            Result.Status = ESanitizerPreflightStatus::Blocked;
            Result.Messages.Add(FString::Printf(TEXT("Source asset is missing, incompatible, or changed: %s"), *SourcePath.ToString()));
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
            Result.Messages.Add(FString::Printf(TEXT("Package is read-only and cannot be consolidated safely: %s (%s)"), *PackageName.ToString(), *Filename));
        }
    }
    if (!Result.IsReady()) { return Result; }
    Result.Status = ESanitizerPreflightStatus::Ready;
    Result.Messages.Add(TEXT("Preflight passed. No assets were modified."));
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
            Result.Messages.Add(FString::Printf(TEXT("A source disappeared after preflight; no mutation was performed: %s"), *SourcePath.ToString()));
            return Result;
        }
        Sources.Add(Source);
    }
    UEditorAssetSubsystem* AssetSubsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>() : nullptr;
    if (!AssetSubsystem)
    {
        Result.Messages.Add(TEXT("Editor asset subsystem is unavailable; no mutation was performed."));
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
        Result.Messages.Add(TEXT("Unreal consolidation failed; no force-delete fallback was attempted."));
        return Result;
    }
    const FAssetData CanonicalData = Registry.GetAssetByObjectPath(Plan.CanonicalAsset);
    if (!CanonicalData.IsValid() || CanonicalData.AssetClassPath != UTexture2D::StaticClass()->GetClassPathName())
    {
        Result.Messages.Add(TEXT("Consolidation returned success but the canonical asset could not be verified."));
        return Result;
    }
    for (const FSoftObjectPath& SourcePath : Plan.SourceAssets)
    {
        const FAssetData SourceData = Registry.GetAssetByObjectPath(SourcePath);
        if (SourceData.IsValid() && SourceData.AssetClassPath == UTexture2D::StaticClass()->GetClassPathName())
        {
            Result.Messages.Add(FString::Printf(TEXT("Consolidation verification failed: source still resolves as an original asset: %s"), *SourcePath.ToString()));
            return Result;
        }

        TSet<FName> RemainingReferencers;
        ContentSanitizerConsolidation::GetPackageReferencers(Registry, SourcePath.GetLongPackageFName(), RemainingReferencers);
        for (const FName PreviousReferencer : ReferencersBefore.FindChecked(SourcePath.GetLongPackageFName()))
        {
            if (RemainingReferencers.Contains(PreviousReferencer))
            {
                Result.Messages.Add(FString::Printf(TEXT("Consolidation verification failed: package %s still refers to source package %s."), *PreviousReferencer.ToString(), *SourcePath.GetLongPackageName()));
                return Result;
            }
        }
    }
    Result.bSucceeded = true;
    Result.Messages.Add(TEXT("Consolidation and post-operation asset identity verification succeeded. Redirector cleanup remains an explicit separate operation."));
    return Result;
}
