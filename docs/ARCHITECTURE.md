# Architecture

This document describes the intended architecture of Unreal Content Sanitizer. It is a design constraint for implementation agents, not merely a conceptual overview.

## 1. Architectural goals

The system must optimize for:

1. **Safety**: uncertain equivalence never becomes an automatic destructive action.
2. **Scalability**: project-wide discovery does not eagerly load all assets.
3. **Explainability**: the UI can explain why assets are grouped and why an action is safe or blocked.
4. **Extensibility**: new asset classes and future sanitizer rules do not require rewriting the scanner.
5. **Testability**: core decisions can be tested without constructing Slate widgets.
6. **Determinism**: a stable project state produces stable grouping, recommendations, and summaries.

## 2. Logical layers

```text
+--------------------------------------------------+
| Slate / Editor Integration                       |
+--------------------------------------------------+
| Session / View Model / Action Queue              |
+--------------------------------------------------+
| Classification / Canonical Selection / Preflight |
+--------------------------------------------------+
| Fingerprint Providers / Scan Pipeline            |
+--------------------------------------------------+
| Asset Registry / Engine Asset APIs               |
+--------------------------------------------------+
```

Dependency direction should flow downward. UI must not own core policy.

## 3. Primary domain objects

Names may evolve, but implementation should preserve equivalent responsibilities.

### `FSanitizerAssetRecord`

Lightweight inventory representation, preferably based on `FAssetData` and package metadata.

Conceptual fields:

```cpp
struct FSanitizerAssetRecord
{
    FAssetData AssetData;
    FTopLevelAssetPath AssetClass;
    FName PackageName;
    FName PackagePath;
    int64 EstimatedDiskSize = 0;
    int32 HardReferenceCount = 0;
    int32 SoftReferenceCount = 0;
};
```

Do not store a permanently loaded UObject solely because an asset was inventoried.

### `FSanitizerCheapFingerprint`

A cheap, metadata-oriented key used to eliminate impossible matches before asset loading/deep hashing.

It must be safe to produce false positives (extra candidates), but must avoid false negatives for the equivalence contract being implemented.

### `FAssetFingerprint`

Conceptually contains:

```cpp
struct FAssetFingerprint
{
    FIoHash PayloadHash;
    FIoHash SettingsHash;
    uint32 SchemaVersion = 0;
};
```

The concrete implementation should also be able to preserve structured comparison/debug data when needed by the inspector.

### `FSanitizerDuplicateGroup`

Represents a classified set of candidate-equivalent assets.

Conceptual contents:

- stable group id for the scan session;
- asset class/provider id;
- member asset records;
- classification;
- classification reasons/warnings;
- recommended canonical asset;
- estimated reclaimable size;
- optional per-member comparison data.

### `FSanitizerActionPlan`

Immutable or explicitly versioned description of intended mutation.

Must include:

- canonical asset identity;
- exact source assets to consolidate;
- scan/fingerprint identity used to justify the plan;
- warnings;
- preflight result/status;
- estimated impact.

The UI should execute an action plan, not reconstruct destructive intent from current row selection at the last moment.

## 4. Scan session state machine

A scan is a session with explicit state.

Recommended conceptual states:

```text
Idle
 -> Inventory
 -> Bucketing
 -> CandidateExtraction
 -> Fingerprinting
 -> Classification
 -> Completed

Any active state -> CancelRequested -> Canceled
Any active state -> Failed
Completed -> Stale (if tracked source state changes)
```

Rules:

- mutation never occurs inside a scan state;
- cancellation must produce a consistent terminal state;
- partial results must be marked partial or discarded explicitly;
- stale results must not silently execute destructive actions without revalidation.

## 5. Scan stage details

### Stage 1: Inventory

Input: scan scope and exclusions.

Use Asset Registry to enumerate relevant assets with minimal loading.

Output: lightweight `FSanitizerAssetRecord` collection.

### Stage 2: Cheap bucketing

Group records by provider-defined cheap fingerprints.

Example Texture2D cheap inputs may include:

- class;
- source width/height where available without expensive load;
- source format/tag metadata where reliable;
- approximate source/package size;
- layer/block counts where available.

Only buckets containing at least two viable records proceed.

### Stage 3: Candidate extraction

Load only assets required by surviving candidate buckets.

On the game thread, extract a provider-defined immutable snapshot containing only data needed by worker-side hashing/comparison.

Avoid keeping all candidate UObjects loaded longer than necessary.

### Stage 4: Deep fingerprinting

Worker-safe immutable data is hashed/normalized.

Produce payload and settings fingerprints separately.

### Stage 5: Deep verification

A hash match is a strong grouping mechanism, but a provider may require an additional equality check for states where collision resistance alone is not the only concern or normalized semantics need verification.

The provider defines this requirement.

### Stage 6: Classification

Minimum classifications:

```text
SafeDuplicate
ReviewRequired
Similar
Conflict
```

Uncertainty is never promoted to `SafeDuplicate`.

### Stage 7: Recommendation

Recommend a canonical asset using a deterministic score. Recommendation is separate from duplicate proof.

Example scoring factors:

- preferred root/path;
- higher reference count;
- non-temporary naming;
- non-developer/non-test location;
- stable/shorter path;
- explicit project preference.

Tie-breaking must be deterministic, for example lexical package-path ordering after equal scores.

## 6. Provider architecture

A provider owns semantic equivalence rules for one or more compatible asset classes.

Preferred conceptual interface:

```cpp
class IAssetFingerprintProvider
{
public:
    virtual ~IAssetFingerprintProvider() = default;

    virtual FName GetProviderId() const = 0;
    virtual uint32 GetSchemaVersion() const = 0;
    virtual bool Supports(const FAssetData& AssetData) const = 0;

    virtual TSanitizerResult<FSanitizerCheapFingerprint>
        BuildCheapFingerprint(const FAssetData& AssetData) const = 0;

    virtual TSanitizerResult<FSanitizerAssetSnapshot>
        ExtractSnapshot(UObject* Asset) const = 0;

    virtual TSanitizerResult<FAssetFingerprint>
        BuildDeepFingerprint(const FSanitizerAssetSnapshot& Snapshot) const = 0;
};
```

The exact generic result/snapshot types are implementation choices. The key constraints are:

- extraction from UObject/editor state is separated from pure worker-safe hashing;
- schema version is explicit;
- provider-specific rules do not leak into a giant scanner switch statement.

## 7. Texture2D provider contract

v0.1 starts with exact Texture2D duplicate detection.

### Payload domain

The payload fingerprint should represent meaningful source content, including enough structure to distinguish source images reliably:

- dimensions;
- layer/block/mip topology as applicable;
- source format;
- source bytes for the relevant source representation.

Do not use thumbnails as payload truth.

### Settings domain

The settings fingerprint must include behavior-relevant texture properties. Candidate fields include:

- sRGB;
- compression settings;
- texture group/LOD group;
- mip generation settings;
- addressing modes;
- filtering;
- virtual texture streaming;
- maximum texture size or related behavior-changing limits;
- other settings established by tests as behavior-relevant.

The final field list must be documented in code/tests and covered by regression cases.

### Classification examples

```text
Same payload + same settings
    -> SafeDuplicate

Same payload + different settings
    -> ReviewRequired

Different payload
    -> not an exact duplicate group
```

## 8. Result explainability

Do not throw away all comparison context after hashing.

The inspector needs structured reasons such as:

```text
Payload: equal
Settings: differ
 - sRGB: true vs false
 - CompressionSettings: Default vs Normalmap
```

A raw hash mismatch is useful internally but is not a sufficient user-facing explanation when the tool can cheaply expose the relevant property difference.

## 9. Canonical selection service

Canonical recommendation should be an independent, testable policy component.

Conceptual API:

```cpp
FSanitizerCanonicalRecommendation RecommendCanonical(
    const FSanitizerDuplicateGroup& Group,
    const FSanitizerCanonicalPolicy& Policy);
```

It must return both:

- selected asset;
- reason/score breakdown suitable for UI explanation/debugging.

Do not embed canonical selection in the row widget.

## 10. Action planning

The action queue contains explicit `FSanitizerActionPlan` items.

Creating a plan is read-only.

Plan creation should snapshot enough identity to detect staleness before execution, for example:

- package/object paths;
- provider id;
- schema version;
- fingerprints or a scan result revision;
- canonical/source membership.

## 11. Preflight architecture

Preflight is a separate service and must run immediately before mutation.

Required validations:

1. Canonical asset exists.
2. All source assets exist.
3. Canonical asset is not also a source.
4. Source list contains no duplicate identities.
5. Provider/class compatibility still holds.
6. Equivalence proof is still current or is recomputed.
7. Action is not blocked by known editor/write/source-control constraints.
8. No action violates the current sanitizer safety policy.

Preflight result should be structured:

```text
Ready
ReadyWithWarnings
Blocked
```

A blocked plan is not executed.

## 12. Consolidation service

Mutation belongs in Editor integration code.

Conceptual flow:

```text
Action Plan
 -> Preflight
 -> Load canonical/source assets as required
 -> UEditorAssetSubsystem::ConsolidateAssets
 -> Collect operation result
 -> Verify references/state
 -> Mark success/warnings/failure
 -> Optional redirector cleanup
```

Do not implement a fallback that force-deletes source assets when consolidation fails.

## 13. Post-operation verification

Verification should confirm the observable contract, not merely trust a returned boolean.

For successful duplicate consolidation, verify where practical:

- canonical asset still exists;
- source asset identities no longer exist as original assets;
- known/affected references resolve to the canonical asset;
- unrelated fixture/reference state remains intact in tests;
- expected redirector policy is satisfied or reported.

Verification failures must be surfaced distinctly from initial consolidation API failures.

## 14. Redirector cleanup

Treat redirector cleanup as a separate operation after successful consolidation verification.

This keeps the state easier to reason about and makes failures diagnosable.

## 15. Future sanitizer rules

Do not assume every future sanitizer is duplicate-based.

Long-term architecture should allow scan rules such as:

- unused asset candidates;
- redirector inventory;
- broken/missing references;
- oversized assets;
- source-file missing checks;
- naming/path policy violations.

These rules may share inventory/session/UI infrastructure but should not be forced through a duplicate-fingerprint abstraction when it does not fit.

A future generic sanitizer interface may conceptually look like:

```cpp
class IContentSanitizerRule
{
public:
    virtual FName GetRuleId() const = 0;
    virtual void Evaluate(...) = 0;
};
```

Do not build this abstraction prematurely if v0.1 does not require it; preserve boundaries so it can be added cleanly later.

## 16. Persistence and cache

Caching is an optimization layer below correctness.

Potential cache key components:

```text
Package identity/change signal
Provider id
Provider schema version
Payload/settings fingerprint
```

A package saved hash may be used to determine whether cached fingerprints are stale. It is not the semantic duplicate fingerprint itself.

A cache miss or corruption must degrade to recomputation, not incorrect reuse.

## 17. Determinism requirements

Given the same asset state, provider schema, and settings, these must be deterministic:

- cheap fingerprints;
- deep fingerprints;
- duplicate group membership;
- group ordering;
- canonical recommendation;
- reclaimable-size calculation method;
- action-plan source ordering.

Do not rely on unordered container iteration for user-visible order without explicit sorting.

## 18. Observability

Each scan/operation should have a session/operation identifier usable in logs.

Useful metrics for diagnostics:

- assets inventoried;
- candidate buckets;
- candidates loaded;
- fingerprint count;
- elapsed time per stage;
- canceled/failed stage;
- duplicate groups by status;
- action plans executed;
- consolidation failures/warnings.

Metrics should support debugging without becoming a telemetry dependency.

## 19. Security/safety posture

This is local editor tooling, but asset destruction is still a high-impact operation.

Therefore:

- default to read-only behavior;
- require explicit user intent for mutation;
- revalidate immediately before mutation;
- do not broaden scan/execution scope implicitly;
- do not process Engine/plugin/developer content unless requested/policy-enabled;
- do not interpret similarity as deletion authority;
- use Unreal-supported mutation APIs;
- preserve detailed failure reporting.
