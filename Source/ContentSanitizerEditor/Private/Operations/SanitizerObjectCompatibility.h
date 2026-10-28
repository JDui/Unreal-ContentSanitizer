#pragma once

#include "Runtime/Launch/Resources/Version.h"
#include "UObject/MetaData.h"
#include "UObject/UObjectHash.h"

inline void SanitizerGetPackageObjects(UPackage* Package, TArray<UObject*>& Objects, bool bNested)
{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8
    GetObjectsWithOuter(Package, Objects, bNested ? EGetObjectsFlags::IncludeNestedObjects : EGetObjectsFlags::None);
#else
    GetObjectsWithOuter(Package, Objects, bNested);
#endif
}

inline bool SanitizerIsPackageMetadata(UObject* Object)
{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8
    // Metadata is a package-owned value in 5.8. A legacy metadata object is not a deletion companion.
    return Object->GetClass()->GetFName() == FName(TEXT("MetaData"));
#else
    return Object->IsA<UMetaData>();
#endif
}
