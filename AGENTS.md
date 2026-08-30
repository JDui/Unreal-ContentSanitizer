# AGENTS.md

This repository is developed with coding agents in mind. This file is the authoritative engineering contract for automated contributors. Read it before modifying code, tests, build files, or documentation.

## 1. Product mission

**Unreal Content Sanitizer** is an Unreal Engine Editor asset-hygiene toolkit.

The first production feature is safe duplicate-asset detection and consolidation. The architecture must remain extensible to later sanitizers such as unused assets, redirectors, broken references, oversized assets, and naming/path checks.

The primary product promise is **safety and explainability**, not maximum deletion rate.

## 2. Authority order

When instructions conflict, use this order:

1. Explicit user/task instructions.
2. `AGENTS.md`.
3. Documents under `docs/`.
4. Existing implementation patterns.

Do not silently relax a safety rule because existing code violates it. Report the conflict and fix the code when the task permits.

## 3. Supported baseline

- Unreal Engine: **5.8 first**.
- Language: **C++ using the engine-default C++ standard**.
- Product type: **Editor plugin**.
- UI: **Slate**.
- Primary development platform: Windows Editor.
- External runtime dependencies: **none by default**.

Do not add Python, UMG, web UI, Electron, third-party hashing libraries, or unrelated dependencies without an explicit architecture decision.

See `docs/TECH_STACK.md`.

## 4. Required architecture boundaries

The plugin must maintain two logical layers/modules:

- `ContentSanitizerCore`: scanning, records, fingerprints, classification, rules, action planning. No Slate dependencies.
- `ContentSanitizerEditor`: tabs, menus, Content Browser integration, thumbnails, user interaction, consolidation execution, source-control/editor integration.

Core algorithms must not depend on a widget existing.

### 4.1 Scan pipeline

The default scan pipeline is:

1. Asset Registry inventory without loading every asset.
2. Cheap metadata bucketing.
3. Load/extract data only for candidate groups.
4. Payload fingerprint.
5. Behavior/settings fingerprint.
6. Deep verification where required.
7. Classification.
8. Read-only review.
9. Explicit action plan.
10. Preflight.
11. Consolidation.
12. Verification.

Never replace this with an eager `LoadObject()` pass over the entire project.

### 4.2 Duplicate classification

Results are not a boolean. At minimum support:

- `SafeDuplicate`: payload and behavior-relevant settings are equivalent.
- `ReviewRequired`: payload is equivalent but behavior-relevant settings differ or equivalence is not proven.
- `Similar`: approximate/semantic similarity only; never automatically destructive.
- `Conflict`: assets cannot be safely consolidated under the current rules.

**Never mutate project content based only on similarity.**

### 4.3 Fingerprints

Do not use the raw `.uasset` package hash as semantic duplicate proof.

Each supported asset class should provide, conceptually:

- a cheap fingerprint/bucket key;
- a payload fingerprint;
- a settings/behavior fingerprint;
- optional deep-equality verification;
- an explicit fingerprint schema version.

Prefer an extensible provider interface over `if/else` chains on asset class.

### 4.4 Asset mutation

For duplicate consolidation, use Unreal Editor's supported consolidation path (`UEditorAssetSubsystem::ConsolidateAssets`) instead of implementing ad-hoc reference replacement/deletion.

Prohibited as a duplicate-cleanup strategy:

- force-deleting duplicates first;
- manually rewriting arbitrary object references as the primary consolidation implementation;
- silently fixing redirectors before post-operation verification;
- mutating assets during scanning.

Every destructive operation must have a read-only preflight representation that the UI can show before execution.

## 5. Threading and UObject safety

Assume UObject/editor APIs are **not thread-safe unless documented otherwise**.

Required pattern:

- Game thread: Asset Registry interactions that require it, UObject loading, UObject/source-data extraction, editor mutation.
- Worker threads: hashing immutable copied buffers, pure comparison, grouping, scoring, sorting.
- Game thread: publish session results and update Slate models.

Do not retain unsafe raw UObject pointers across asynchronous work. Prefer `TWeakObjectPtr`, asset paths, `FAssetData`, or immutable snapshots as appropriate.

## 6. GUI rules

The main tool is a dockable Slate tab. The default information architecture is:

- toolbar/scope controls;
- scan summary;
- left filter pane;
- central virtualized result tree/list;
- right inspector/diff pane;
- bottom action queue / execution controls.

Use Unreal Editor visual language (`FAppStyle`) before custom styling. Status must never be communicated by color alone. Destructive actions must be visually and behaviorally distinct from scan/review actions.

See `docs/GUI_GUIDELINES.md`.

## 7. Testing contract

Behavior changes require automated tests unless the change is documentation-only.

At minimum maintain tests for:

- fingerprint determinism;
- cheap-bucket invariants;
- payload-vs-settings classification;
- canonical-asset recommendation/scoring;
- duplicate grouping;
- supported asset providers;
- scan scope and exclusions;
- consolidation preflight;
- consolidation on disposable generated fixtures;
- post-consolidation reference correctness;
- cancellation and failure paths where practical.

Tests must not depend on the developer's production project content. Mutation tests must operate only on generated/disposable fixtures and restore/clean their state.

See `docs/TESTING.md`.

## 8. Agent workflow

Before implementation:

1. Read this file and relevant `docs/` pages.
2. Identify which architectural boundary the change belongs to.
3. Identify safety implications.
4. Identify or add the tests that prove the change.

During implementation:

1. Keep diffs scoped.
2. Prefer Unreal-native APIs and containers.
3. Do not introduce dependencies casually.
4. Do not mix UI state with scanner/domain state.
5. Do not hide failures; return/report structured errors.
6. Avoid broad refactors unless required by the task.

Before declaring completion:

1. Build the affected plugin modules.
2. Run the relevant `ContentSanitizer` automation suite.
3. Run broader tests when touching shared scanner/mutation code.
4. Check that no test leaves generated assets/packages behind.
5. Review the diff for accidental destructive behavior.
6. Update documentation when a public rule, architecture decision, test contract, or GUI behavior changed.

## 9. Definition of done

A change is done only when:

- implementation follows the architecture contract;
- relevant automated tests pass;
- no new compiler warnings are knowingly introduced;
- failure/cancellation paths are handled;
- user-visible destructive behavior is preflighted and explicit;
- documentation is consistent with code;
- no debug assets, generated binaries, IDE state, or local machine paths are committed.

## 10. Code hygiene

- Follow Unreal Engine naming and formatting conventions.
- Keep public headers minimal.
- Prefer forward declarations where practical.
- Use a project log category rather than `LogTemp` for production diagnostics.
- No emoji in source code or comments.
- Avoid hard-coded absolute paths.
- Avoid magic values; centralize thresholds and schema versions.
- Prefer deterministic ordering for scan output and tests.
- Treat warnings during consolidation/preflight as first-class structured results, not only log strings.

## 11. Scope discipline for v0.1

The first shippable scope is intentionally narrow:

- scan `/Game` or explicit user scope;
- exact duplicate `Texture2D` detection;
- same-payload/different-settings review classification;
- result inspection and property differences;
- canonical asset selection;
- dry-run/preflight action queue;
- safe consolidation;
- verification;
- optional redirector cleanup after verification.

Do not add perceptual image similarity, Blueprint equivalence, SkeletalMesh equivalence, or broad unused-asset deletion before the exact Texture workflow is proven and tested.
