# Architecture

This document describes the intended architecture of Unreal Content Sanitizer. It is a design constraint for implementation agents, not merely a conceptual overview.

The active stage is defined in `../AGENTS.md` section 11 (2026-10-28). Sections describing fingerprints and consolidation apply to duplicates; section 20 defines the independent single-asset orphan workflow. Requirements here are not claims that every service already exists.

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
- editor-facing scans advance through bounded per-frame ticks so Slate can repaint and process cancellation between assets;
- wildcard cheap metadata widens candidate extraction, but candidate object paths remain globally unique.

### Persistent fingerprint index

Completed candidate fingerprints are indexed at `Saved/AXICleanCache/FingerprintIndex.bin`. Cache identity includes object path, provider id, fingerprint schema version, and a package change identity. Prefer Asset Registry `PackageSavedHash`; use package file size and modification time only as a fallback. Loaded dirty packages never use cached fingerprints.

The index is an acceleration layer, not mutation authority. Preflight always rebuilds canonical and source fingerprints from current assets. A corrupt, stale, provider-mismatched, or schema-mismatched cache entry is ignored and rebuilt. Scoped scans may prune missing entries only inside their explicit package paths.

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

The active stage retains exact Texture2D duplicate detection only.

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

For the v0.1 `Texture2D` provider, fingerprint schema version 2 defines source payload as every source block descriptor, every layer format, and every block/layer/mip byte sequence. The settings domain conservatively includes color adjustments and source color management, compression and Oodle choices, mip generation/loading and LOD behavior, addressing/filtering, virtual-texture behavior, padding/resizing, chroma keying, compositing, normal/gamma processing, and per-layer format overrides. Adding or removing any of these inputs requires an explicit schema-version review.

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
 -> Mark NotExecuted / ConsolidationReportedFailure / VerificationFailed / Succeeded
 -> Optional redirector cleanup
```

Do not implement a fallback that force-deletes source assets when consolidation fails.

Once `ConsolidateAssets` has been invoked, a false return value must not be described as "nothing changed." Both a reported API failure and a post-operation verification failure mean project content may already have changed; stop the queue, require a rescan, and do not automatically retry that plan.

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

- cluster/root-reachability analysis beyond the active single-asset orphan workflow;
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

Do not build a generic rule framework merely for future expansion; preserve boundaries between the two active workflows.

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

## 20. Active-stage scope, orphan evidence, and deletion

### Scope and plans

Model cleanup target, evidence scope, and mutation impact separately. Duplicate comparison defaults to `/Game`; reference discovery includes all available registered users regardless of cleanup folder or candidate asset family. Out-of-target assets cannot silently become sources/deletion targets, but referencing packages outside the target may need modification during consolidation and must appear in the reviewed impact.

Canonical recommendation belongs in a non-widget policy: prefer equivalent assets outside the target, then apply deterministic scoring. Manual override is allowed among proven equivalent members and invalidates existing preflight/confirmation. Scope, membership, evidence, or impact changes likewise invalidate plans. Execution must enforce scope from the plan, not trust the widget that created it.

### Independent orphan evidence model

Orphan candidates do not need semantic fingerprints or canonical assets. Store asset/package identity, incoming reference evidence, protection flags, coverage/readiness, errors, outgoing metadata, and scan/policy revision. Keep per-item evidence classification separate from deletion-preflight readiness.

Zero observed incoming hard/soft references is a candidate signal only. Applicable management/protection references, loaded or dirty/unsaved state, and multi-object package companions must be accounted for before deletion. Missing registry readiness, failed queries/loads, unsupported asset states, or other failed required checks must not produce action-ready status. Self-package graph edges may be omitted from external counts, but that must not hide other objects sharing the package.

Known soft/management users block deletion. Configuration/code/dynamic-string use is a documented coverage limitation: show it explicitly and require informed confirmation without claiming exhaustive absence of use. Protected assets/roots remain blocked; user confirmation does not waive known blockers.

The direction of references matters. `Mesh -> Material` says that the mesh uses a material; it does not say that another asset uses the mesh. Project/default/engine/empty material slots are inspector/filter metadata only. Metadata extraction failure is an unknown field, never proof of orphan status.

Single-asset analysis does not discover orphan clusters, cycles, or assets unreachable from chosen maps. No deletion plan may expand to dependencies or newly exposed candidates. A new scan/review/plan is needed for those candidates.

### Deletion service and verification

Core owns non-destructive plan/rule models. Editor owns engine/editor checks, the deletion API call, and verification. Slate collects intent and displays evidence/results; it does not implement deletion policy or invoke deletion directly.

`Inventory -> Evidence -> Review -> Exact deletion plan -> Read-only preflight -> Confirm -> Immediate per-item revalidation -> Delete -> Verify`

A plan includes exact individual targets, cleanup scope, operation/evidence/policy identity, expected package effects, and warnings. Preflight accounts for existence/type, current incoming references and protection, package companions, loaded/dirty/unsaved state, and write/source-control constraints. If safe handling cannot be established, block with structured reasons. Revalidate immediately before each item. Changed impact or evidence requires renewed review, not reuse of an earlier confirmation.

Select an Unreal-supported deletion path only after checking its actual behavior on UE 5.5 and 5.8. Never bypass reference blockers, null references, or force-delete as a retry. Verify target absence and expected package state, report unexpected effects, and preserve unrelated content. API-reported failure and verification failure both imply possible partial mutation: stop, invalidate affected plans/results in both modes, and require rescan/review. Only pre-mutation rejection can be reported as not executed.

Keep candidate disk size separate from eligible-plan estimates and observed execution results. Recompute totals after changes, avoiding duplicate package accounting. Do not promise disk savings or undo/rollback that have not been verified.

### Implemented operation boundary (2026-10-28)

Reviewed preflight objects bind the exact plan identity, current evidence and expected package impact. Orphan plans also retain exclusions and policy revision. The Editor session subsystem consumes operation IDs after a native call, even on failure, preventing plan replay. Registry/property changes and mutations invalidate both UI result modes and their confirmations. Deletion rejects missing reference-graph nodes, unready manager/registry state, unknown subclasses, external package state and unreviewed external payload sidecars. No query failure is interpreted as absence of use.

Loaded reflected hard/soft properties are inspected without invoking arbitrary UObject serializers; the engine reference collector additionally checks hard/undo users. This remains bounded evidence alongside registered package/management relationships, with dynamic and non-reflected use disclosed as a limitation. Duplicate verification audits serialized destinations/counts in the affected packages.

Redirector cleanup is narrower than a general fix-up: after verified consolidation, only zero-user single-object writable redirector packages are eligible, with source control disabled. Ordinary supported deletion avoids the modal AssetTools fix-up report and does not rewrite references or collections. It has independent review/confirmation, immediate revalidation and outcome checks.

### Confirmed duplicate batches with shared reference packages

The Editor consolidation service preflights the complete ordered batch. It identifies shared impact packages before confirmation and rejects overlapping member packages or dependencies between groups. Independent groups may share reference packages. Each review retains structured per-package file evidence and serialized owner/property/destination counts as well as its complete identity.

Execution validates every pending review before each native submission. After a successful and verified group, a pending review may advance only if impact/outside-target membership is unchanged, file evidence outside the completed group's reviewed impact is unchanged, and the full reference audit equals the previous audit with exactly that group's source-to-canonical substitution (including merged counts). Only then may file save evidence for the already-reviewed written packages advance. A newly successful preflight alone never authorizes changed impact or references. The single-group evidence comparison remains in place.

The first native submission makes the batch and all its original individual plans non-replayable at batch exit, including untouched plans after a stop. Native/verification failure stops immediately. Per-plan results include untouched items; the panel keeps their exact targets in the read-only outcome report and requires new scan/review after mutation. No mutation retains the queue for fresh preflight. This is a synchronous game-thread boundary, not a filesystem transaction or rollback guarantee.
