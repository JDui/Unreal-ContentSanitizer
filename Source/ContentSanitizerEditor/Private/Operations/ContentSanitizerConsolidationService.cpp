#include "ContentSanitizerConsolidationService.h"

#include "ContentSanitizerCore.h"
#include "Editor.h"
#include "Engine/Texture2D.h"
#include "Providers/Texture2DFingerprintProvider.h"
#include "Subsystems/EditorAssetSubsystem.h"

FSanitizerPreflightResult FContentSanitizerConsolidationService::Preflight(const FSanitizerActionPlan& Plan) const
{
    FSanitizerPreflightResult Result = FSanitizerActionPlanner::ValidateShape(Plan);
    if (!Result.IsReady()) { return Result; }

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
        UObject* Source = SourcePath.TryLoad();
        FSanitizerFingerprint SourceFingerprint;
        if (!Source || !Provider.BuildDeepFingerprint(Source, SourceFingerprint, Error) || SourceFingerprint.SchemaVersion != Plan.SchemaVersion || SourceFingerprint.PayloadHash != Plan.PayloadHash || SourceFingerprint.SettingsHash != Plan.SettingsHash)
        {
            Result.Status = ESanitizerPreflightStatus::Blocked;
            Result.Messages.Add(FString::Printf(TEXT("Source asset is missing, incompatible, or changed: %s"), *SourcePath.ToString()));
            return Result;
        }
    }
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
        if (UObject* Source = SourcePath.TryLoad()) { Sources.Add(Source); }
    }
    UEditorAssetSubsystem* AssetSubsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>() : nullptr;
    if (!AssetSubsystem)
    {
        Result.Messages.Add(TEXT("Editor asset subsystem is unavailable; no mutation was performed."));
        return Result;
    }
    if (!AssetSubsystem->ConsolidateAssets(Canonical, Sources))
    {
        Result.Messages.Add(TEXT("Unreal consolidation failed; no force-delete fallback was attempted."));
        return Result;
    }
    if (!Plan.CanonicalAsset.ResolveObject())
    {
        Result.Messages.Add(TEXT("Consolidation returned success but the canonical asset could not be verified."));
        return Result;
    }
    for (const FSoftObjectPath& SourcePath : Plan.SourceAssets)
    {
        if (SourcePath.ResolveObject())
        {
            Result.Messages.Add(FString::Printf(TEXT("Consolidation verification failed: source still resolves as an original asset: %s"), *SourcePath.ToString()));
            return Result;
        }
    }
    Result.bSucceeded = true;
    Result.Messages.Add(TEXT("Consolidation and post-operation asset identity verification succeeded. Redirector cleanup remains an explicit separate operation."));
    return Result;
}
