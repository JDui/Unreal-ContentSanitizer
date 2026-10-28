#include "Operations/SanitizerLoadedReferenceCheck.h"

#include "UObject/PropertyOptional.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

namespace
{
    // Inspect reflected values only; never invoke arbitrary UObject/custom native serializers.
    static bool HasReference(const FProperty* Property, const void* Value, const TArray<FSoftObjectPath>& Targets, int32 Depth)
    {
        if (Depth > 64) { return true; } // Unknown nested state conservatively blocks.
        if (const FSoftObjectProperty* Soft = CastField<FSoftObjectProperty>(Property)) { return Targets.Contains(Soft->GetPropertyValue(Value).ToSoftObjectPath()); }
        if (const FObjectPropertyBase* Object = CastField<FObjectPropertyBase>(Property))
        {
            UObject* Referenced = Object->GetObjectPropertyValue(Value);
            return IsValid(Referenced) && Targets.Contains(FSoftObjectPath(Referenced));
        }
        if (const FStructProperty* Struct = CastField<FStructProperty>(Property))
        {
            const FName Name = Struct->Struct->GetFName();
            if (Name == FName(TEXT("SoftObjectPath"))) { return Targets.Contains(*static_cast<const FSoftObjectPath*>(Value)); }
            if (Name == FName(TEXT("SoftClassPath"))) { return Targets.Contains(*static_cast<const FSoftClassPath*>(Value)); }
            for (TFieldIterator<FProperty> Field(Struct->Struct); Field; ++Field)
            {
                for (int32 Index = 0; Index < Field->ArrayDim; ++Index)
                {
                    if (HasReference(*Field, Field->ContainerPtrToValuePtr<void>(Value, Index), Targets, Depth + 1)) { return true; }
                }
            }
        }
        if (const FArrayProperty* Array = CastField<FArrayProperty>(Property))
        {
            FScriptArrayHelper Helper(Array, Value);
            for (int32 Index = 0; Index < Helper.Num(); ++Index) { if (HasReference(Array->Inner, Helper.GetRawPtr(Index), Targets, Depth + 1)) { return true; } }
        }
        if (const FSetProperty* Set = CastField<FSetProperty>(Property))
        {
            FScriptSetHelper Helper(Set, Value);
            for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index) { if (Helper.IsValidIndex(Index) && HasReference(Set->ElementProp, Helper.GetElementPtr(Index), Targets, Depth + 1)) { return true; } }
        }
        if (const FMapProperty* Map = CastField<FMapProperty>(Property))
        {
            FScriptMapHelper Helper(Map, Value);
            for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index)
            {
                if (Helper.IsValidIndex(Index) && (HasReference(Map->KeyProp, Helper.GetKeyPtr(Index), Targets, Depth + 1) || HasReference(Map->ValueProp, Helper.GetValuePtr(Index), Targets, Depth + 1))) { return true; }
            }
        }
        if (const FOptionalProperty* Optional = CastField<FOptionalProperty>(Property))
        {
            if (const void* Inner = Optional->GetValuePointerForReadIfSet(Value)) { return HasReference(Optional->GetValueProperty(), Inner, Targets, Depth + 1); }
        }
        return false;
    }
}

TArray<UObject*> SanitizerGetLoadedUsers(const TArray<FSoftObjectPath>& Targets, bool bDirtyOnly)
{
    check(IsInGameThread());
    TArray<UObject*> Users;
    for (TObjectIterator<UObject> It; It; ++It)
    {
        UObject* Object = *It;
        if (!IsValid(Object) || Object->IsA<UPackage>() || (bDirtyOnly && !Object->GetOutermost()->IsDirty())) { continue; }
        if (Targets.ContainsByPredicate([&](const FSoftObjectPath& Target) { return Object->GetOutermost()->GetFName() == Target.GetLongPackageFName(); })) { continue; }
        bool bFound = false;
        for (TFieldIterator<FProperty> Field(Object->GetClass()); Field && !bFound; ++Field)
        {
            for (int32 Index = 0; Index < Field->ArrayDim && !bFound; ++Index) { bFound = HasReference(*Field, Field->ContainerPtrToValuePtr<void>(Object, Index), Targets, 0); }
        }
        if (bFound) { Users.Add(Object); }
    }
    return Users;
}

bool SanitizerHasDirtyReference(const TArray<FSoftObjectPath>& Targets, FString& OutReferencer)
{
    const TArray<UObject*> Users = SanitizerGetLoadedUsers(Targets, true);
    if (!Users.IsEmpty()) { OutReferencer = Users[0]->GetPathName(); return true; }
    return false;
}
