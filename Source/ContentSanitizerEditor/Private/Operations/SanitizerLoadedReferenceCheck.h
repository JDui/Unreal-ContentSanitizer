#pragma once

#include "CoreMinimal.h"

// Registry queries only cover saved packages. Check unsaved hard/soft users separately.
bool SanitizerHasDirtyReference(const TArray<FSoftObjectPath>& Targets, FString& OutReferencer);
TArray<UObject*> SanitizerGetLoadedUsers(const TArray<FSoftObjectPath>& Targets, bool bDirtyOnly);
