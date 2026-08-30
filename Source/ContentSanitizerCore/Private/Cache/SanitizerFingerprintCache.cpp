#include "Cache/SanitizerFingerprintCache.h"

#include "HAL/FileManager.h"
#include "IO/IoHash.h"
#include "Misc/Char.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/MemoryReader.h"

namespace ContentSanitizerCache
{
    static constexpr uint32 Magic = 0x41434958; // ACIX
    static constexpr int32 MaximumEntryCount = 10000000;
    static constexpr int32 MaximumSettingsPerEntry = 4096;

    static bool IsPathWithin(const FString& PackageName, const FString& RootPath)
    {
        return PackageName == RootPath || PackageName.StartsWith(RootPath + TEXT("/"));
    }

    static bool TryParseIoHash(const FString& Text, FIoHash& OutHash)
    {
        if (Text.Len() != sizeof(FIoHash::ByteArray) * 2)
        {
            return false;
        }
        for (const TCHAR Character : Text)
        {
            if (!FChar::IsHexDigit(Character))
            {
                return false;
            }
        }
        LexFromString(OutHash, *Text);
        return true;
    }

    static void SerializeEntry(FArchive& Archive, FSanitizerFingerprintCacheEntry& Entry)
    {
        Archive << Entry.ObjectPath;
        Archive << Entry.PackageName;
        Archive << Entry.ProviderId;
        Archive << Entry.ChangeIdentity;
        Archive << Entry.Fingerprint.SchemaVersion;

        FString PayloadHash = Archive.IsSaving() ? LexToString(Entry.Fingerprint.PayloadHash) : FString();
        FString SettingsHash = Archive.IsSaving() ? LexToString(Entry.Fingerprint.SettingsHash) : FString();
        Archive << PayloadHash;
        Archive << SettingsHash;
        int32 SettingsCount = Entry.Fingerprint.Settings.Num();
        Archive << SettingsCount;
        if (SettingsCount < 0 || SettingsCount > MaximumSettingsPerEntry)
        {
            Archive.SetError();
            return;
        }
        if (Archive.IsSaving())
        {
            TArray<FName> Keys;
            Entry.Fingerprint.Settings.GetKeys(Keys);
            Keys.Sort(FNameLexicalLess());
            for (FName Key : Keys)
            {
                FString Value = Entry.Fingerprint.Settings.FindChecked(Key);
                Archive << Key;
                Archive << Value;
            }
        }
        else
        {
            Entry.Fingerprint.Settings.Reset();
            for (int32 Index = 0; Index < SettingsCount && !Archive.IsError(); ++Index)
            {
                FName Key;
                FString Value;
                Archive << Key;
                Archive << Value;
                if (Key.IsNone()) { Archive.SetError(); break; }
                Entry.Fingerprint.Settings.Add(Key, MoveTemp(Value));
            }
        }
        if (Archive.IsLoading())
        {
            if (!TryParseIoHash(PayloadHash, Entry.Fingerprint.PayloadHash) || !TryParseIoHash(SettingsHash, Entry.Fingerprint.SettingsHash))
            {
                Archive.SetError();
                return;
            }
            Entry.Fingerprint.bDeepVerified = true;
        }
    }
}

FSanitizerFingerprintCache::FSanitizerFingerprintCache(FString InFilename)
    : Filename(MoveTemp(InFilename))
{
}

FString FSanitizerFingerprintCache::GetDefaultDirectory()
{
    return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AXICleanCache"));
}

FString FSanitizerFingerprintCache::GetDefaultFilename()
{
    return FPaths::Combine(GetDefaultDirectory(), TEXT("FingerprintIndex.bin"));
}

bool FSanitizerFingerprintCache::Load(FString& OutError)
{
    Entries.Reset();
    OutError.Reset();
    if (!IFileManager::Get().FileExists(*Filename)) { return true; }

    TArray<uint8> Data;
    if (!FFileHelper::LoadFileToArray(Data, *Filename))
    {
        OutError = FString::Printf(TEXT("无法读取指纹缓存索引：%s"), *Filename);
        return false;
    }
    FMemoryReader Reader(Data, true);
    uint32 Magic = 0;
    uint32 Version = 0;
    int32 EntryCount = 0;
    Reader << Magic;
    Reader << Version;
    Reader << EntryCount;
    if (Magic != ContentSanitizerCache::Magic || Version != FormatVersion || EntryCount < 0 || EntryCount > ContentSanitizerCache::MaximumEntryCount)
    {
        OutError = TEXT("指纹缓存索引格式无效或版本已过期，本次扫描将重新建立索引。");
        return false;
    }
    for (int32 Index = 0; Index < EntryCount && !Reader.IsError(); ++Index)
    {
        FSanitizerFingerprintCacheEntry Entry;
        ContentSanitizerCache::SerializeEntry(Reader, Entry);
        if (Entry.ObjectPath.IsEmpty() || Entry.PackageName.IsNone() || Entry.ProviderId.IsNone() || Entry.ChangeIdentity.IsEmpty())
        {
            Reader.SetError();
            break;
        }
        Entries.Add(Entry.ObjectPath, MoveTemp(Entry));
    }
    if (Reader.IsError())
    {
        Entries.Reset();
        OutError = TEXT("指纹缓存索引内容损坏，本次扫描将重新建立索引。");
        return false;
    }
    return true;
}

bool FSanitizerFingerprintCache::Save(FString& OutError) const
{
    OutError.Reset();
    if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true))
    {
        OutError = FString::Printf(TEXT("无法创建指纹缓存目录：%s"), *FPaths::GetPath(Filename));
        return false;
    }

    FBufferArchive Writer;
    uint32 Magic = ContentSanitizerCache::Magic;
    uint32 Version = FormatVersion;
    int32 EntryCount = Entries.Num();
    Writer << Magic;
    Writer << Version;
    Writer << EntryCount;
    TArray<FString> Keys;
    Entries.GetKeys(Keys);
    Keys.Sort();
    for (const FString& Key : Keys)
    {
        FSanitizerFingerprintCacheEntry Entry = Entries.FindChecked(Key);
        ContentSanitizerCache::SerializeEntry(Writer, Entry);
    }

    const FString TemporaryFilename = Filename + TEXT(".tmp");
    if (!FFileHelper::SaveArrayToFile(Writer, *TemporaryFilename))
    {
        OutError = FString::Printf(TEXT("无法写入临时指纹缓存索引：%s"), *TemporaryFilename);
        return false;
    }
    if (!IFileManager::Get().Move(*Filename, *TemporaryFilename, true, true, false, true))
    {
        IFileManager::Get().Delete(*TemporaryFilename, false, true);
        OutError = FString::Printf(TEXT("无法替换指纹缓存索引：%s"), *Filename);
        return false;
    }
    return true;
}

const FSanitizerFingerprintCacheEntry* FSanitizerFingerprintCache::FindValid(const FString& ObjectPath, FName ProviderId, uint32 SchemaVersion, const FString& ChangeIdentity) const
{
    const FSanitizerFingerprintCacheEntry* Entry = Entries.Find(ObjectPath);
    if (!Entry || ChangeIdentity.IsEmpty() || Entry->ProviderId != ProviderId || Entry->Fingerprint.SchemaVersion != SchemaVersion || Entry->ChangeIdentity != ChangeIdentity) { return nullptr; }
    return Entry;
}

void FSanitizerFingerprintCache::Upsert(FSanitizerFingerprintCacheEntry Entry)
{
    Entries.Add(Entry.ObjectPath, MoveTemp(Entry));
}

int32 FSanitizerFingerprintCache::RemoveMissingInScope(const TArray<FName>& PackagePaths, const TSet<FString>& SeenObjectPaths)
{
    int32 Removed = 0;
    for (auto Iterator = Entries.CreateIterator(); Iterator; ++Iterator)
    {
        bool bInScope = false;
        const FString PackageName = Iterator.Value().PackageName.ToString();
        for (const FName PackagePath : PackagePaths)
        {
            if (ContentSanitizerCache::IsPathWithin(PackageName, PackagePath.ToString())) { bInScope = true; break; }
        }
        if (bInScope && !SeenObjectPaths.Contains(Iterator.Key()))
        {
            Iterator.RemoveCurrent();
            ++Removed;
        }
    }
    return Removed;
}
