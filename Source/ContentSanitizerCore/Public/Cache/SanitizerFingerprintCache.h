#pragma once

#include "Model/SanitizerTypes.h"

struct FSanitizerFingerprintCacheEntry
{
    FString ObjectPath;
    FName PackageName;
    FName ProviderId;
    FString ChangeIdentity;
    FSanitizerFingerprint Fingerprint;
};

class CONTENTSANITIZERCORE_API FSanitizerFingerprintCache
{
public:
    static constexpr uint32 FormatVersion = 1;

    explicit FSanitizerFingerprintCache(FString InFilename = GetDefaultFilename());

    static FString GetDefaultDirectory();
    static FString GetDefaultFilename();
    bool Load(FString& OutError);
    bool Save(FString& OutError) const;
    const FSanitizerFingerprintCacheEntry* FindValid(const FString& ObjectPath, FName ProviderId, uint32 SchemaVersion, const FString& ChangeIdentity) const;
    void Upsert(FSanitizerFingerprintCacheEntry Entry);
    int32 RemoveMissingInScope(const TArray<FName>& PackagePaths, const TSet<FString>& SeenObjectPaths);
    int32 Num() const { return Entries.Num(); }

private:
    FString Filename;
    TMap<FString, FSanitizerFingerprintCacheEntry> Entries;
};
