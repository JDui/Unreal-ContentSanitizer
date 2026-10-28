#include "Model/SanitizerOrphanTypes.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/AssetManager.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

bool SanitizerOrphanPolicy::IsInScope(const FString& PackageName, const TArray<FName>& Roots)
{
    return Roots.ContainsByPredicate([&](FName Root)
    {
        const FString Path = Root.ToString();
        return !Path.IsEmpty() && Path != TEXT("/") && (PackageName == Path || PackageName.StartsWith(Path + TEXT("/")));
    });
}

FString SanitizerOrphanPolicy::CoverageLimitation()
{
    return TEXT("覆盖已注册的硬/软/管理引用；无法排除配置、代码或字符串动态加载。未发现引用不等于已证明未使用。确认前请核对动态使用约定。");
}

bool SanitizerOrphanPolicy::IsProtectedPrimary(const FAssetData& Asset)
{
    if (Asset.GetPrimaryAssetId().IsValid()) { return true; }
    UAssetManager* Manager = UAssetManager::GetIfInitialized();
    if (!Manager) { return false; }
    const FSoftObjectPath Path(Asset.GetObjectPathString());
    if (Manager->GetPrimaryAssetIdForData(Asset).IsValid() || Manager->GetPrimaryAssetIdForPath(Path).IsValid()) { return true; }
    // Dynamic primary assets can lack registry tags/reverse path entries on newer engines.
    TArray<FPrimaryAssetTypeInfo> Types;
    Manager->GetPrimaryAssetTypeInfoList(Types);
    for (const FPrimaryAssetTypeInfo& Type : Types)
    {
        if (!Type.bIsDynamicAsset) { continue; }
        TArray<FSoftObjectPath> Paths;
        Manager->GetPrimaryAssetPathList(Type.PrimaryAssetType, Paths);
        if (Paths.Contains(Path)) { return true; }
    }
    return false;
}

FSanitizerOrphanRecord SanitizerOrphanPolicy::Inspect(const FAssetData& Asset)
{
    check(IsInGameThread());
    FSanitizerOrphanRecord Record;
    Record.AssetData = Asset;
    Record.PackageName = Asset.PackageName;
    Record.PackagePath = Asset.PackagePath;
    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    Record.bRegistryReady = !Registry.IsLoadingAssets();
    if (!Record.bRegistryReady) { Record.Blockers.Add(TEXT("资产注册表仍在发现资产。")); }
    if (UAssetManager* Manager = UAssetManager::GetIfInitialized())
    {
        if (!Manager->HasInitialScanCompleted()) { Record.Blockers.Add(TEXT("Asset Manager 初始扫描未完成，保护覆盖不可确认。")); }
        else if (Record.bRegistryReady) { Manager->UpdateManagementDatabase(); }
    }
    const FTopLevelAssetPath Class = Asset.AssetClassPath;
    Record.bSupportedState = Class == UTexture2D::StaticClass()->GetClassPathName()
        || Class == UMaterial::StaticClass()->GetClassPathName() || Class == UMaterialInstanceConstant::StaticClass()->GetClassPathName()
        || Class == UStaticMesh::StaticClass()->GetClassPathName() || Class == USkeletalMesh::StaticClass()->GetClassPathName();
    if (Class == UStaticMesh::StaticClass()->GetClassPathName()) { Record.AssetType = ESanitizerOrphanAssetType::StaticMesh; }
    else if (Class == USkeletalMesh::StaticClass()->GetClassPathName()) { Record.AssetType = ESanitizerOrphanAssetType::SkeletalMesh; }
    else if (Class == UMaterial::StaticClass()->GetClassPathName() || Class == UMaterialInstanceConstant::StaticClass()->GetClassPathName()) { Record.AssetType = ESanitizerOrphanAssetType::Material; }
    if (!Record.bSupportedState) { Record.AssetType = ESanitizerOrphanAssetType::Unsupported; }
    if (!Asset.IsValid() || !Record.bSupportedState || Asset.IsRedirector()) { Record.Blockers.Add(TEXT("不支持此资产子类或状态。")); }
    const FString PackageName = Asset.PackageName.ToString();
    if (!IsInScope(PackageName, { FName(TEXT("/Game")) }) || IsInScope(PackageName, { FName(TEXT("/Game/Developers")), FName(TEXT("/Game/__ExternalActors__")), FName(TEXT("/Game/__ExternalObjects__")) }))
    {
        Record.Blockers.Add(TEXT("引擎、插件、开发者或外部对象内容受保护。"));
    }
    const bool bHardInspected = Registry.GetReferencers(Asset.PackageName, Record.HardReferencers, UE::AssetRegistry::EDependencyCategory::Package, UE::AssetRegistry::EDependencyQuery::Hard);
    const bool bSoftInspected = Registry.GetReferencers(Asset.PackageName, Record.SoftReferencers, UE::AssetRegistry::EDependencyCategory::Package, UE::AssetRegistry::EDependencyQuery::Soft);
    TArray<FAssetIdentifier> Managers;
    const bool bManagersInspected = Registry.GetReferencers(FAssetIdentifier(Asset.PackageName), Managers, UE::AssetRegistry::EDependencyCategory::Manage);
    if (!bHardInspected || !bSoftInspected || !bManagersInspected) { Record.Blockers.Add(TEXT("引用图节点缺失或查询失败；必须等待注册表更新后重新扫描。")); }
    for (const FAssetIdentifier& Manager : Managers) { Record.ManagementReferencers.AddUnique(FName(*Manager.ToString())); }
    Record.HardReferencers.Remove(Asset.PackageName);
    Record.SoftReferencers.Remove(Asset.PackageName);
    Record.HardReferencers.Sort(FNameLexicalLess());
    Record.SoftReferencers.Sort(FNameLexicalLess());
    Record.ManagementReferencers.Sort(FNameLexicalLess());
    Record.HardReferenceCount = Record.HardReferencers.Num();
    Record.SoftReferenceCount = Record.SoftReferencers.Num();
    if (Record.HardReferenceCount || Record.SoftReferenceCount || !Record.ManagementReferencers.IsEmpty()) { Record.Blockers.Add(TEXT("存在硬、软或管理用户，禁止删除。")); }
    if (!UAssetManager::GetIfInitialized()) { Record.Blockers.Add(TEXT("Asset Manager 尚未初始化，保护检查不可确认。")); }
    if (IsProtectedPrimary(Asset)) { Record.Blockers.Add(TEXT("Primary Asset 受保护。")); }
    TArray<FAssetData> Companions;
    Registry.GetAssetsByPackageName(Asset.PackageName, Companions);
    if (Companions.Num() != 1) { Record.Blockers.Add(TEXT("包包含其他资产，不能按单资产删除。")); }
    FString Filename;
    if (!FPackageName::DoesPackageExist(PackageName, &Filename)) { Record.Blockers.Add(TEXT("资产未保存或包文件不存在。")); }
    else
    {
        Record.EstimatedDiskSize = FMath::Max<int64>(0, IFileManager::Get().FileSize(*Filename));
        if (IFileManager::Get().IsReadOnly(*Filename)) { Record.Blockers.Add(TEXT("包文件为只读。")); }
        const FString Base = FPaths::GetPath(Filename) / FPaths::GetBaseFilename(Filename);
        for (const TCHAR* Extension : { TEXT(".uexp"), TEXT(".ubulk"), TEXT(".uptnl"), TEXT(".m.ubulk"), TEXT(".upayload") })
        {
            if (IFileManager::Get().FileExists(*(Base + Extension))) { Record.Blockers.Add(TEXT("存在外置包/载荷文件，本阶段无法验证完整安全清理：") + Base + Extension); }
        }
    }
    if (UPackage* Package = FindPackage(nullptr, *PackageName); Package && Package->IsDirty()) { Record.Blockers.Add(TEXT("包包含未保存更改。")); }
    Record.Classification = Record.Blockers.IsEmpty() ? ESanitizerOrphanClassification::Candidate : ESanitizerOrphanClassification::Blocked;
    Record.Reasons.Add(CoverageLimitation());
    Record.Reasons.Append(Record.Blockers);
    Record.EvidenceIdentity = FString::Printf(TEXT("%u|%s|%s|%lld|%lld|%d|"), Revision, *Asset.GetObjectPathString(), *Class.ToString(), Record.EstimatedDiskSize, IFileManager::Get().GetTimeStamp(*Filename).GetTicks(), Companions.Num());
    for (const TArray<FName>* Users : { &Record.HardReferencers, &Record.SoftReferencers, &Record.ManagementReferencers })
    {
        for (FName User : *Users) { Record.EvidenceIdentity += User.ToString() + TEXT(";"); }
        Record.EvidenceIdentity += TEXT("|");
    }
    return Record;
}
