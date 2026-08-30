# Implementation Plan

This plan defines the intended delivery sequence for Unreal Content Sanitizer. Agents should prefer completing a milestone vertically and with tests instead of spreading partial implementation across later features.

## Milestone 0 - Plugin skeleton

Goal: load a clean UE 5.8 Editor plugin with stable module boundaries.

Deliverables:

- `ContentSanitizer.uplugin`.
- `ContentSanitizerCore` module.
- `ContentSanitizerEditor` module.
- dedicated log category.
- dockable Content Sanitizer tab.
- menu/command registration.
- Content Browser context action placeholders.
- minimal test host project.
- first automation test proving the plugin/test suite is discoverable.

Acceptance:

- Editor target compiles.
- Plugin loads without ensure/assert.
- Tab can be opened/closed repeatedly.
- `ContentSanitizer.*` automation test is visible and runnable.

## Milestone 1 - Asset inventory

Goal: scalable read-only scope enumeration.

Deliverables:

- scan scope model;
- default `/Game` scope;
- selected folder/assets/custom path scopes;
- exclusion policy;
- Asset Registry inventory service;
- lightweight asset record model;
- deterministic ordering;
- scan progress/session state;
- cancellation;
- basic summary counts.

Tests:

- default scope behavior;
- exclusions;
- cancellation terminal state;
- deterministic ordering;
- unsupported assets do not cause loading/mutation.

Acceptance:

- project content can be inventoried without eagerly loading all assets;
- cancel leaves editor/project state unchanged.

## Milestone 2 - Texture2D exact dedup core

Goal: detect exact semantic Texture2D duplicates safely.

Deliverables:

- fingerprint provider interface;
- Texture2D provider;
- cheap fingerprint/bucket key;
- immutable source snapshot extraction;
- `FIoHashBuilder` payload fingerprint;
- behavior/settings fingerprint;
- provider/schema version;
- duplicate grouping;
- `SafeDuplicate` vs `ReviewRequired` classification;
- reclaimable-size estimate method.

Required behavior:

```text
same source + same settings -> SafeDuplicate
same source + different behavior setting -> ReviewRequired
different source -> not exact duplicate
```

Tests:

- texture provider classification matrix;
- pixel/source differences;
- sRGB difference;
- compression difference;
- renamed/path-moved equivalent textures;
- deterministic fingerprinting/grouping.

Acceptance:

- no destructive code is required yet;
- results are explainable from structured comparison data.

## Milestone 3 - Review GUI

Goal: make scan results usable at scale.

Deliverables:

- toolbar and scope selector;
- scan/cancel controls;
- summary strip;
- left filters;
- virtualized duplicate group tree/list;
- asset rows;
- status text/icons;
- inspector;
- side-by-side relevant settings diff;
- canonical recommendation display;
- manual canonical override;
- Content Browser navigation/show-in-browser action.

Tests:

- filter/view-model behavior;
- sorting;
- action eligibility mapping;
- summary calculations;
- canonical override state.

Acceptance:

- user can identify why a group is Safe or Review Required without consulting Output Log;
- UI remains responsive with large synthetic result sets.

## Milestone 4 - Action plan, preflight, consolidation

Goal: safely perform real cleanup.

Deliverables:

- action queue;
- immutable/versioned action-plan model;
- dry-run/preflight service;
- staleness revalidation;
- source-control/writeability checks where applicable;
- `UEditorAssetSubsystem::ConsolidateAssets` integration;
- structured operation results;
- post-operation verification;
- Message Log/notification reporting;
- optional redirector cleanup after verification.

Tests:

- generated disposable consolidation fixture;
- reference replacement;
- unrelated reference preservation;
- missing canonical/source preflight rejection;
- changed fingerprint rejection;
- invalid source-list rejection;
- failure never falls back to force delete;
- test fixture cleanup.

Acceptance:

- no source asset is modified during scan/review/preflight;
- blocked preflight performs no mutation;
- successful consolidation is verified and clearly reported.

## v0.1.0 release boundary

v0.1.0 is complete after Milestone 4.

Included:

- UE 5.8 Editor plugin;
- Texture2D exact duplicate scanning;
- payload/settings distinction;
- Safe vs Review classification;
- canonical selection;
- review/diff GUI;
- action queue;
- dry-run/preflight;
- safe consolidation;
- verification;
- optional redirector cleanup;
- automated regression suite.

Explicitly excluded:

- perceptual image similarity for automatic cleanup;
- Blueprint equivalence;
- SkeletalMesh equivalence;
- broad unused-asset deletion;
- custom reference-rewrite engine;
- Python/UMG frontend.

## Milestone 5 - Cache and additional low-risk providers

Candidates:

- fingerprint cache;
- TextureCube;
- MaterialInstance;
- SoundWave.

Cache requirements:

- schema-aware invalidation;
- package/source change invalidation;
- corruption/miss falls back to recomputation;
- cache does not change correctness semantics.

Each new provider must define:

- cheap fingerprint;
- payload domain;
- settings/behavior domain;
- schema version;
- Safe/Review rules;
- provider regression matrix.

## Milestone 6 - StaticMesh

Do not implement until equivalence semantics are documented.

Expected dimensions include:

- geometry/index data;
- LODs;
- sections/material slot semantics;
- build settings;
- normals/tangents;
- UVs;
- Nanite-related behavior where relevant;
- collision-related behavior where relevant.

A mesh that only looks identical is not automatically a SafeDuplicate.

## Milestone 7 - SkeletalMesh research/prototype

This is deliberately late because equivalence is high risk.

Potential dimensions include:

- geometry/LODs;
- skeleton/bone mapping;
- skin weights;
- morph targets;
- material slots;
- clothing;
- build settings;
- Nanite/skinning features;
- physics/skeleton references.

No automatic destructive path should ship before an explicit equivalence specification and comprehensive tests exist.

## Future sanitizer families

After duplicate cleanup is stable, the product may grow into additional independent sanitizer rules:

- redirector inventory/cleanup;
- unused asset candidates;
- broken reference checks;
- oversized asset reports;
- source-file missing checks;
- naming/path policy checks.

These features should reuse scan/session/result infrastructure where appropriate without forcing unrelated rules into the duplicate fingerprint provider abstraction.

## Suggested repository layout

```text
Unreal-ContentSanitizer/
├─ AGENTS.md
├─ ContentSanitizer.uplugin
├─ Source/
│  ├─ ContentSanitizerCore/
│  │  ├─ ContentSanitizerCore.Build.cs
│  │  ├─ Public/
│  │  │  ├─ Model/
│  │  │  ├─ Scanner/
│  │  │  ├─ Providers/
│  │  │  └─ Operations/
│  │  └─ Private/
│  │     └─ Tests/
│  └─ ContentSanitizerEditor/
│     ├─ ContentSanitizerEditor.Build.cs
│     ├─ Public/
│     └─ Private/
│        ├─ UI/
│        ├─ Operations/
│        └─ Tests/
├─ Tests/
│  └─ ContentSanitizerTestHost/
├─ Resources/
└─ docs/
   ├─ ARCHITECTURE.md
   ├─ GUI_GUIDELINES.md
   ├─ IMPLEMENTATION_PLAN.md
   ├─ TECH_STACK.md
   └─ TESTING.md
```

Directory names under modules may be refined when code appears, but maintain the Core/Editor and domain/UI separation.

## Per-task agent checklist

For every implementation task:

1. State the milestone/subsystem affected.
2. Read the relevant architecture/test/GUI rules.
3. Add or update tests before declaring completion.
4. Keep mutation separate from scan logic.
5. Build affected modules.
6. Run relevant `ContentSanitizer` tests.
7. Review failures, ensures, and cleanup state.
8. Update docs if behavior or contract changed.

## First coding task recommendation

Start with Milestone 0 only.

Do not implement the Texture2D hashing algorithm in the same initial commit as plugin/module/tab/test-host scaffolding. Establish a compileable/testable baseline first, then add inventory and fingerprint behavior in independently reviewable steps.
