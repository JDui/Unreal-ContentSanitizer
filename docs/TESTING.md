# Automated Testing Strategy

This document defines the automated-test contract for Unreal Content Sanitizer.

The project handles destructive editor operations. Tests are therefore a product requirement, not optional cleanup work.

The active stage requires duplicate consolidation and single-asset orphan cleanup on **UE 5.5 and UE 5.8**. Sections below retain duplicate coverage; section 16 adds required orphan/scope regression coverage. This specification lists required tests, not a claim that they all exist or pass.

## 1. Test framework

Use Unreal Engine's native Automation Framework.

Preferred forms:

- **Automation Spec** for behavior-oriented tests with setup/teardown, latent work, or multi-step editor flows.
- `IMPLEMENT_SIMPLE_AUTOMATION_TEST` / related native test macros for small deterministic unit-style cases where a Spec would add ceremony without value.

All project tests must live under a common hierarchy beginning with:

```text
ContentSanitizer.
```

Examples:

```text
ContentSanitizer.Unit.Hash.Determinism
ContentSanitizer.Unit.Classification.PayloadSameSettingsDifferent
ContentSanitizer.Unit.CanonicalSelection.ReferenceCount
ContentSanitizer.Provider.Texture2D.ExactDuplicate
ContentSanitizer.Integration.ScanScope.ExcludesDevelopers
ContentSanitizer.Integration.Consolidation.ReplacesReferences
```

## 2. Test classes

### 2.1 Unit / pure-domain tests

Purpose: validate deterministic logic without editor content mutation.

Required areas:

- hash/fingerprint input serialization determinism;
- fingerprint schema version handling;
- cheap fingerprint equality/inequality;
- payload/settings classification matrix;
- duplicate group construction;
- canonical asset scoring;
- path preference scoring;
- deterministic sort order;
- error/status propagation;
- cancellation state transitions where the logic is pure.

These tests should be fast and isolated.

### 2.2 Provider tests

Purpose: prove that each supported asset provider implements the equivalence contract correctly.

For `Texture2D`, cover at minimum:

1. Same source data + same behavior settings -> `SafeDuplicate`.
2. Same source data + different sRGB -> not `SafeDuplicate`.
3. Same source data + different compression setting -> not `SafeDuplicate`.
4. Same source dimensions + different pixel/source data -> not duplicate.
5. Different names/paths with equivalent content -> can still be duplicate.
6. Package metadata differences alone must not defeat semantic equivalence.
7. Provider result is deterministic across repeated runs in the same engine version/schema.

When a new behavior-relevant texture property is added to the fingerprint contract, add a regression test proving that changing it changes classification appropriately.

### 2.3 Scan integration tests

Purpose: verify interaction with Asset Registry and scan scope.

Cover:

- `/Game` default inclusion;
- configured exclusions;
- developer content excluded by default;
- plugin content excluded by default unless explicitly requested;
- selected-folder scope;
- unsupported asset classes ignored or reported according to policy;
- cancellation returns a consistent final session state;
- repeated scans of unchanged fixtures produce deterministic grouping/order.

### 2.4 Mutation/consolidation integration tests

Purpose: prove destructive operations are safe.

These tests must operate only on generated/disposable assets created specifically for the test.

Required assertions:

1. Preflight does not mutate assets.
2. A valid action plan identifies exactly one canonical asset.
3. Only explicitly planned duplicate assets are consolidation sources.
4. Consolidation rewrites references to the canonical asset.
5. Unrelated references/assets remain unchanged.
6. Consolidated source assets are removed according to Unreal's consolidation behavior.
7. Redirectors are handled only according to the requested policy.
8. Post-operation verification detects an incomplete/failed operation.
9. Failure does not trigger fallback force-delete behavior.
10. Cleanup removes all test-generated assets/packages.

### 2.5 GUI/model tests

Do not make screenshot tests the primary GUI test strategy.

Test UI behavior through separable view models/domain models where possible:

- filter model;
- selection model;
- action queue state;
- enabled/disabled state for destructive actions;
- summary totals;
- sorting;
- status-to-presentation mapping.

Use Slate integration tests only where widget behavior itself is material and cannot be proven below the widget layer.

## 3. Test isolation rules

Epic's automation guidance requires tests not to assume editor state and to leave files as they were found. This project adopts an even stricter rule because it performs content mutation.

Tests must:

- be order-independent;
- not depend on production project content;
- not depend on a developer's local Content Browser selection;
- not depend on source control being connected;
- use unique temporary package paths;
- delete generated fixtures before setup if a previous crashed run may have left them behind;
- clean generated fixtures after completion;
- avoid shared mutable fixture state between tests.

Recommended test package root:

```text
/Game/__ContentSanitizerTests/<RunOrTestId>/
```

A test that fails during cleanup must report that cleanup failure explicitly.

## 4. Test host project

The repository should include a minimal disposable host project when implementation begins, for example:

```text
Tests/
└─ ContentSanitizerTestHost/
   ├─ ContentSanitizerTestHost.uproject
   ├─ Config/
   └─ Content/
```

The host exists only to compile/load the plugin and run editor automation. Do not add production/sample art content unless a deterministic fixture cannot be generated in code.

Prefer fixtures generated in test setup over binary test assets because generated fixtures are easier to reason about, reset, and version.

## 5. Test source location

Keep tests close to the implementation module while preserving production/test separation.

Recommended:

```text
Source/
├─ ContentSanitizerCore/
│  └─ Private/
│     └─ Tests/
└─ ContentSanitizerEditor/
   └─ Private/
      └─ Tests/
```

Wrap tests with the appropriate engine automation-test compile guards so shipping/runtime code does not accidentally depend on test-only implementation.

## 6. Naming and granularity

Tests should describe one invariant or behavior.

Good:

```text
ContentSanitizer.Unit.Classification.SamePayloadDifferentSettingsIsReview
```

Bad:

```text
ContentSanitizer.AllTextureTests
```

When fixing a regression, add the smallest test that fails before the fix and passes after it.

## 7. Smoke tests

Only mark genuinely fast tests as smoke tests.

The engine's smoke-test convention is a speed promise; smoke tests are expected to complete in roughly one second or less. Do not mark asset-loading/consolidation integration tests as smoke merely to make them run frequently.

Suggested smoke candidates:

- pure classification matrix;
- canonical scoring;
- deterministic serialization of small in-memory fingerprint inputs.

## 8. Command-line execution

The eventual repository test instructions should support unattended execution through `UnrealEditor-Cmd.exe`.

Reference form:

```bat
UnrealEditor-Cmd.exe ^
  Tests\ContentSanitizerTestHost\ContentSanitizerTestHost.uproject ^
  -unattended ^
  -nop4 ^
  -NullRHI ^
  -ExecCmds="Automation RunTests ContentSanitizer; Quit" ^
  -TestExit="Automation Test Queue Empty" ^
  -log
```

The exact engine executable path is machine-specific and must not be committed as an absolute local path.

If a test genuinely requires rendering, do not use `-NullRHI` for that test lane. Core/provider/consolidation/orphan-deletion tests should be designed to avoid a rendering requirement.

## 9. Required CI/test lanes

When CI is added, use at least these conceptual lanes:

### Fast gate

- build plugin/test host;
- run `ContentSanitizer.Unit`;
- run fast provider tests.

### Full editor gate (both UE 5.5 and UE 5.8)

- build editor target;
- run all `ContentSanitizer` automation tests;
- include Asset Registry/provider/consolidation/orphan-deletion integration tests;
- verify process exit status and automation failures.

Do not require GitHub-hosted runners to magically contain Unreal Engine. CI implementation must use an explicitly provisioned/self-hosted environment or another legally/technically valid engine installation strategy.

## 10. Fingerprint regression testing

Every fingerprint provider has a `SchemaVersion`.

Rules:

- Changing which fields/data contribute to a fingerprint requires deliberate schema-version review.
- Tests should validate equivalence behavior, not merely hard-code an opaque hash forever.
- A hard-coded expected hash is acceptable only for a deliberately stable serialization fixture and must document why byte-level stability matters.
- Cache compatibility tests must include schema mismatch invalidation once caching exists.

## 11. Classification matrix

Maintain tests equivalent to this matrix:

| Payload | Settings | Deep Verify | Expected classification |
|---|---|---|---|
| Same | Same | Equal/not required | SafeDuplicate |
| Same | Different | Any | ReviewRequired |
| Different | Same | Any | Different / no duplicate group |
| Same | Same | Not provably equal | ReviewRequired or Conflict |
| Approx. similar only | Any | Any | Similar, never SafeDuplicate |
| Invalid/unsupported state | Any | Any | Conflict / structured error |

The implementation may use more detailed statuses, but it must never promote an uncertain state to `SafeDuplicate`.

Scanner regression coverage must also prove that wildcard cheap-bucket expansion produces one candidate per object path, cancellation reaches an explicit terminal state on an incremental tick, and reclaimable size excludes every non-safe classification.

Persistent-cache tests must cover deterministic round-trip serialization plus invalidation on change-identity, provider, and fingerprint-schema mismatch. Tests must use disposable cache paths and remove generated files.

## 12. Consolidation safety fixture

A canonical integration fixture should create at least:

```text
Canonical Texture A
Duplicate Texture B
Unrelated Texture C
Material/asset Ref1 -> B
Material/asset Ref2 -> A
Material/asset Ref3 -> C
```

After consolidating B into A, assert:

```text
Ref1 -> A
Ref2 -> A
Ref3 -> C
A exists
B no longer exists as the original asset
C exists and is unchanged
```

Then validate redirector behavior separately according to test policy.

## 13. Negative tests are mandatory

For destructive code, success-path testing is insufficient.

Add negative cases for:

- canonical asset missing before execution;
- duplicate asset missing before execution;
- fingerprints changed between scan and execution;
- incompatible asset class;
- empty action plan;
- duplicate source list containing the canonical asset;
- duplicate entries in the source list;
- cancellation before mutation;
- Unreal consolidation failure/reporting path.

The safe behavior for a failed preflight is **no mutation**.

## 14. Test quality rules for agents

Agents must not:

- delete or weaken a failing test merely to make a change pass;
- convert an assertion into a warning without explicit justification;
- depend on test order;
- use sleeps as the primary synchronization mechanism when a deterministic latent command/event is available;
- leave disabled tests without a tracking reason;
- write integration tests against the user's real project content.

When behavior intentionally changes, update both tests and the relevant specification document in the same change.

## 15. Merge/release gate

Before a change affecting scanner, provider, classification, or mutation code is considered complete:

1. affected modules compile;
2. relevant unit/provider tests pass;
3. all destructive-operation regression tests pass if mutation code changed;
4. no generated fixture packages remain;
5. logs contain no unexplained ensure/assert/automation errors;
6. documentation and fingerprint schema decisions are consistent with implementation.

Both engine lanes are mandatory for implementation acceptance; full stage release acceptance additionally requires all `ContentSanitizer` tests and editor workflow checks on each engine. Record missing environments as unvalidated. Documentation-only work uses diff, consistency, and link checks without builds or fixture mutation.

## 16. Active-stage regression matrix

### Scope and duplicate workflow

- A folder context action sets target only; no automatic scan/mutation.
- Duplicate comparison defaults to `/Game`, with sources restricted to target folders even when the service is invoked without a widget.
- External equivalent canonicals are preferred deterministically; manual override retains equivalence/source-scope restrictions.
- Scope, canonical, target, evidence, and impact changes invalidate prior preflight/confirmation.
- Referencers outside cleanup folders appear in impact; new referencers between review and execution require renewed review when they expand impact.
- Known references resolve to the retained canonical after consolidation, not merely away from the removed source; unrelated references remain intact.
- Redirector cleanup occurs only as an independent explicit operation after successful consolidation verification.

### Orphan evidence and eligibility

Use generated fixtures for every advertised asset family and representative supported subclasses/states. Cover:

| Fixture/condition | Required outcome |
|---|---|
| No observed external references | Candidate with explicit coverage; not an unconditional safety claim |
| External hard reference | Not deletion-eligible |
| External soft reference, zero hard references | Not deletion-eligible |
| Applicable management reference/protection | Block deletion |
| Referencer outside selected folders or supported candidate classes | Still detected and blocks deletion |
| Unready registry, failed required inspection, unsupported asset state | Unknown/blocked with a reason, never inferred unused |
| Dirty/unsaved state, loaded user, or package companion | Safely accounted for or blocked; no ignored relevant state |
| Project/default/engine/empty material slots on an otherwise identical mesh | Same orphan evidence classification; metadata/filter values may differ |
| Material metadata extraction failure | Unknown metadata, never proof of non-use |
| Mesh points to material | Outgoing dependency is not treated as an incoming mesh user |
| Internally referenced chain/cycle | No cluster deletion or recursive queue expansion |
| Dynamic path use not covered by registry | Coverage limitation remains visible through review and confirmation; no claim of exhaustive detection |

Also cover exclusions/protected roots, scope changes, deterministic ordering, cancellation before start and during processing, explicit canceled/failed terminal states, and prohibition on mutation from partial results. These checks must verify behavior, not merely enum labels or text.

### Orphan planning, preflight, execution, and verification

Required negative tests include empty/duplicate/invalid/out-of-target targets; changed type/policy/revision; missing assets; new hard/soft/management references after scan and after preflight; newly dirty/protected/read-only state; canceled confirmation; and impact expansion.

Assert preflight is read-only, blockers perform no deletion, scope is enforced without Slate, and immediate per-item revalidation rejects stale state. Review the chosen engine deletion API on both supported versions; a true return value alone is not a verification test.

Generate a fixture containing an unreferenced target, a dependency it points to, a protected or referenced asset, and unrelated content. After a valid individual deletion, verify target absence, expected package state, dependency survival, unrelated content/reference preservation, and that no newly exposed candidate is automatically deleted. Verify package-level effects and reject multi-object cases that cannot be handled safely.

Exercise API failure/possible partial mutation and verification failure through a controllable test seam where needed. Verify queue stop, accurate status, no fallback/automatic retry, cross-mode invalidation, and summary recalculation. Tests must not depend on provoking destructive failures in production content.

All tests must clean generated packages and temporary cache files, including failure paths, and explicitly report cleanup failures. Candidate and plan size estimates must avoid duplicate package accounting and never claim actual reclaimed bytes before verification.

## 17. Engine-lane tooling status

`Scripts/Build-Plugin.ps1` and `Scripts/Test-Plugin.ps1` accept 5.5 and 5.8. Test runs use unique temporary plugin copies and a dedicated C++ host Editor target, rather than the catch-all UnrealEditor target. Engine writes are prohibited. Build/test processes run serially, hidden, at reduced priority, with at most two compile actions. Reports, commands and fixture/host cleanup records are retained under `Saved/Validation`; real lane outcomes are recorded in `VALIDATION_2026-10-28.md`. A passing entry point does not establish both lanes or GUI acceptance.

### Native deletion acceptance fixtures

The deletion integration suite invokes the ordinary `ObjectTools::DeleteObjects` path on generated hard- and soft-referenced targets, asserting zero deletions, preserved references, and surviving packages. Positive service fixtures assert exact target/package absence and dependency/unrelated survival. Controllable service seams report native failure or false success to prove queue stop, possible-modification status, verification failure, and replay rejection without provoking destructive production failures.

The 5.5 source review establishes that `UEditorAssetSubsystem::DeleteAsset` calls `ForceDeleteObjects`, so it is prohibited in the product deletion path. `ObjectTools::DeleteObjects(..., false, CancelNotAllowed)` uses its ordinary delete model; the model refuses users before `DoDelete`. Source review is supplemented by fixtures on each engine; do not infer 5.8 acceptance solely from a header or the 5.5 source. Fixture-only teardown can use engine force cleanup on the unique disposable test root.

Fixture saves synchronously scan only their disposable root before registry assertions, avoiding races with directory-watcher delivery. Unregistered/missing dependency nodes are a blocking inspection failure. Provider regression includes sRGB/compression/address/filter settings and name/metadata/dimension behavior. An unattached Slate fixture checks scope-without-scan, explicit queues, canonical/preflight invalidation and cross-mode summary reset. Consolidation seams simulate reported failure and false success, assert replay rejection and require a fresh scan/plan before the real fixture operation.

External payload sidecars are excluded until a complete package-file policy can be verified. A disposable sidecar regression proves preflight blocks rather than claiming complete cleanup from main-package disappearance alone.

The final family fixture performs actual ordinary deletion of generated Texture2D, Material, MaterialInstanceConstant, StaticMesh and SkeletalMesh targets, verifies every target and package absent, and retains shared material/texture dependencies. Class enumeration and metadata tests alone are not used as deletion acceptance.

### Shared-reference duplicate batch regression

`ContentSanitizer.Integration.Consolidation.Batch` creates two independent texture pairs used by one generated asset's hard and soft properties. One reviewed confirmation must complete both native consolidations with exact destinations, preserved existing canonical references, and no additional impact. Preflight explicitly lists the shared outside-target package.

Negative cases add or change saved users before execution and during the first native submission, change a shared property's destination unexpectedly, alter only file evidence of a package outside the completed group's impact, reorder an already-reviewed batch, simulate reported failure on the second of three groups, simulate false success, and submit overlapping members. They assert no later native calls, exact per-plan untouched results, source survival, stop on unexpected audit/impact, and rejection of both batch replay and all old individual plans after any native submission. Cleanup operates only on a generated GUID fixture root. Existing single-group stale-evidence tests remain mandatory.

The unattached Slate test also presents a synthetic partial-batch result without invoking mutation: it asserts exact untouched-target reporting after executable-plan invalidation, and preservation of queued targets with cleared confirmation when no native call occurred.

Batch fixture teardown has its own ownership guard for the generated GUID root. After behavior assertions, it clears/saves generated reference-asset edges, waits for async package writes, and force-removes only fixture objects. The installed 5.8 bulk directory helper reports object removal yet leaves some package files, even after clearing edges/waiting. Fixture-only cleanup therefore inventories exact package filenames within the generated root, resets their loaders, removes any retained files, then independently asserts empty disk and registry state. The root must parse as a generated GUID and resolve inside project Content; the host script independently checks all fixture files before removing its owned host. No production teardown fallback is added.
