#pragma once

#include "Model/SanitizerOrphanTypes.h"

enum class ESanitizerMaterialFilter : uint8 { All, Bound, Empty, Unknown };

struct FContentSanitizerResultFilter
{
    FString Search;
    int64 MinimumBytes = 0;
    bool bCandidatesOnly = false;
    ESanitizerMaterialFilter Material = ESanitizerMaterialFilter::All;

    bool Matches(const FSanitizerDuplicateGroup& Group) const
    {
        return (!bCandidatesOnly || Group.Classification == ESanitizerClassification::SafeDuplicate)
            && Group.EstimatedReclaimableSize >= MinimumBytes
            && (Search.IsEmpty() || Group.Members.ContainsByPredicate([this](const auto& Member) { return Member.Record.GetObjectPath().Contains(Search); }));
    }

    bool Matches(const FSanitizerOrphanRecord& Record) const
    {
        if ((bCandidatesOnly && Record.Classification != ESanitizerOrphanClassification::Candidate) || Record.EstimatedDiskSize < MinimumBytes || (!Search.IsEmpty() && !Record.GetObjectPath().Contains(Search))) { return false; }
        if (Material == ESanitizerMaterialFilter::All) { return true; }
        if (Record.AssetType != ESanitizerOrphanAssetType::StaticMesh && Record.AssetType != ESanitizerOrphanAssetType::SkeletalMesh) { return false; }
        if (Material == ESanitizerMaterialFilter::Unknown) { return !Record.bMaterialMetadataKnown; }
        if (!Record.bMaterialMetadataKnown) { return false; }
        return Material == ESanitizerMaterialFilter::Bound ? Record.MaterialSlotsNum > Record.EmptyMaterialSlotsNum : Record.MaterialSlotsNum == Record.EmptyMaterialSlotsNum;
    }
};
