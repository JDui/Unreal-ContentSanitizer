# AGENTS.md

This repository is developed with coding agents in mind. This file is the authoritative engineering contract for automated contributors. Read it before modifying code, tests, build files, or documentation.

## 1. Product mission

**Unreal Content Sanitizer** is an Unreal Engine Editor asset-hygiene toolkit.

The active stage, confirmed by the user on 2026-10-28, is **duplicate consolidation plus single-asset orphan cleanup**. Deliver two complete review/plan/preflight/execute/verify workflows. Section 11 defines the stage; it does not declare the existing implementation complete.

Duplicate detection asks whether assets are interchangeable. Orphan detection asks whether an individual asset has observable external users. These are independent properties with different classification and mutation policies.

The primary product promise is **safety and explainability**, not maximum deletion rate.

## 2. Authority order

When instructions conflict, use this order:

1. Explicit user/task instructions.
2. `AGENTS.md`.
3. Documents under `docs/`.
4. Existing implementation patterns.

Do not silently relax a safety rule because existing code violates it. Report the conflict and fix the code when the task permits.

## 3. Supported baseline

- Unreal Engine: **5.5 and 5.8**, both mandatory build and automation acceptance targets.
- Language: **C++ using the engine-default C++ standard**.
- Product type: **Editor plugin**.
- UI: **Slate**.
- Primary development platform: Windows Editor.
- External runtime dependencies: **none by default**.

Do not add Python, UMG, web UI, Electron, third-party hashing libraries, or unrelated dependencies without an explicit architecture decision.

See `docs/TECH_STACK.md`.

## 4. Required architecture boundaries

The plugin must maintain two logical layers/modules:

- `ContentSanitizerCore`: scanning, records, fingerprints, reference evidence, classification, rules, action planning. No Slate dependencies or destructive operations.
- `ContentSanitizerEditor`: tabs, menus, Content Browser integration, thumbnails, user interaction, consolidation/deletion execution and verification, source-control/editor integration.

Core algorithms must not depend on a widget existing.

### 4.1 Scan pipeline

The duplicate scan pipeline is:

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

Orphan scanning instead uses Asset Registry inventory, external-reference evidence, exclusions/protection checks, and candidate classification. It does not require duplicate fingerprints. Both scans are read-only, incremental, cancelable, and publish explicit completed/canceled/failed states.

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

Orphan deletion is a separate operation. It must consume an explicit deletion plan, recheck current references and constraints immediately before execution, use a reviewed Unreal-supported deletion path, and verify the outcome. A raw deletion API call from a Slate selection handler is not an accepted safety boundary. An API's availability or name does not prove it protects references; check its behavior on both supported engines.

Known external references, protected content, failed required inspection, or stale/unconfirmed impact block deletion. Confirmation cannot override blockers. Do not silently delete dependencies, recursively delete newly exposed candidates, replace references with null, or force-delete to bypass a blocker.

Once a mutation API is called, failure may mean partial modification. Stop the queue, invalidate affected results/plans, report the observed state, and require rescan/review before retry. Do not promise automatic rollback or Ctrl+Z recovery.

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

Also require orphan reference/protection classification, material-independent mesh classification, deletion-plan scope enforcement, stale-reference rejection, generated-fixture deletion and post-deletion verification. See `docs/TESTING.md` for the active-stage matrix.

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

1. Build the affected plugin modules on UE 5.5 and UE 5.8.
2. Run the relevant `ContentSanitizer` automation suite on both engines.
3. Run broader tests when touching shared scanner/mutation code.
4. Check that no test leaves generated assets/packages behind.
5. Review the diff for accidental destructive behavior.
6. Update documentation when a public rule, architecture decision, test contract, or GUI behavior changed.

Documentation-only tasks require diff, consistency, and link checks, not engine builds. For implementation work, unavailable engine/toolchain lanes must be reported as unvalidated, never passed. Stage acceptance requires the full `ContentSanitizer` suite on both engines plus editor workflow checks.

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
- Do not calculate file hashes merely to check ordinary edits; use Git diffs, file inspection, and build/test results. Hashes are allowed for semantic asset fingerprints, required cache identity, integrity/security, binary identity, otherwise unobservable changes, or explicit user requests.

## 11. Active stage: duplicate consolidation and single-asset orphan cleanup

### 11.1 Confirmed decisions

| Decision | Binding requirement |
|---|---|
| Product scope | Complete duplicate consolidation and orphan scan/plan/preflight/delete/verify workflows. |
| Duplicate types | Exact `Texture2D` only; no new duplicate providers in this stage. |
| Orphan definition | Individual assets with no observed external use under the documented inspection policy. No cluster or root-reachability analysis. |
| Orphan types | Textures, materials/material instances, static meshes, skeletal meshes; unsupported subclasses/states must be identified and excluded from deletion. |
| Material binding | Outgoing mesh-to-material links are inspector/filter information, not evidence that the mesh is used or unused. |
| Duplicate scope | Only source assets inside the cleanup target may be consolidated; default comparison scope is all `/Game`. |
| Canonical selection | Prefer an equivalent asset outside the cleanup target; allow manual override among proven equivalent members. |
| Impact | Show and explicitly confirm affected reference packages outside the cleanup target. |
| Engine support | UE 5.5 and UE 5.8 both require build and test acceptance. |

These decisions define product scope, not blanket authorization to mutate a user's project. In-product mutation still requires a reviewed plan and explicit execution confirmation.

### 11.2 Scope and protection boundaries

Keep three concepts distinct in models and UI:

1. **Cleanup target**: assets eligible to become consolidation sources or deletion targets.
2. **Evidence scope**: duplicate comparison scope or reference discovery coverage. Reference discovery must not be restricted to the cleanup folders or supported orphan asset types.
3. **Mutation impact**: every package expected to change, including canonical/reference packages outside the cleanup target.

Opening the tab or invoking a folder context action sets scope only; neither scans nor mutates automatically. Defaults target `/Game` and exclude developer/engine/plugin content unless explicitly included by policy. Evidence outside the target must not be discarded because those packages cannot themselves be cleaned.

Plans carry scope and operation identity and enforce them in services, not only widgets. Changing scope, canonical selection, target membership, evidence, or impact invalidates prior preflight/confirmation. Expanded impact requires a newly reviewed plan.

### 11.3 Duplicate workflow

`Scope -> Scan -> Inspect payload/settings -> Choose canonical -> Queue -> Preflight -> Confirm impact -> Revalidate -> Consolidate -> Verify`

- Only proven `SafeDuplicate` assets may enter consolidation plans. Settings differences remain review-only; selection cannot waive equivalence.
- Manual canonical selection must not broaden source scope. Automatic moves to shared directories are excluded.
- Recompute equivalence at execution preflight; cached fingerprints only accelerate scanning.
- Verify source removal, canonical survival, and expected reference destinations. A successful API return or disappearance of an old reference alone is insufficient proof.
- Redirector cleanup is a separate optional action after successful verification, with its own scope/impact and outcome reporting.

### 11.4 Single-asset orphan workflow

`Target scope -> Inventory -> Gather reference/protection evidence -> Candidate review -> Deletion queue -> Preflight -> Confirm -> Revalidate -> Delete -> Verify`

- Zero incoming references means **no references observed**, not globally proven unused or unconditionally safe to delete. Use candidate/review/blocked/action-ready terminology that separates evidence from permission; legacy `SafeOrphan` labels must not overstate certainty.
- Inspect registered hard/soft references across the available project registry and applicable management/protection signals. Record registry readiness, evidence coverage, reasons, and unsupported checks. Known soft or management references block deletion even with zero hard references.
- Configuration/code/string-based dynamic use cannot generally be disproved by registry queries. Show that coverage limitation at review and confirmation. Distinguish an inherent documented limitation from a failed required check: the former requires informed confirmation, the latter blocks execution. Do not require or claim exhaustive program analysis.
- Account for dirty/unsaved state, package companions, loaded editor references, protected roots/assets, and write/source-control constraints. If relevant state cannot be safely checked, block the affected item with a reason. Never discard same-package users merely because self-package edges are omitted from external counts.
- Project/default/engine/no material bindings must not independently change mesh orphan classification. Material extraction failure is unknown metadata, not proof of non-use.
- Plans list exact individual targets, evidence/policy revision, scope, warnings, expected package effects, and per-item preflight state. Recheck references/protections immediately before each deletion; never execute from an old scan result alone.
- No dependency closure, cluster deletion, fixed-point cleanup, or automatic queue expansion. Newly exposed candidates require a new explicit scan/review/plan.
- Verify target absence and expected package state; preserve/report unrelated content and unexpected effects. Invalidate overlapping results in both modes after mutation and recompute summaries. Candidate disk size is not guaranteed reclaimable space.

### 11.5 Outside this stage

- orphan clusters, cycles, and unused dependency-chain removal;
- map/startup-root reachability analysis;
- material/mesh/Blueprint or other additional duplicate equivalence providers;
- perceptual similarity or AI-based deletion;
- automatic relocation/quarantine or shared-folder reorganization;
- broad one-click unused-content deletion, forced deletion, or unattended cleanup;
- unrelated sanitizers such as naming repair, oversized-asset cleanup, or broken-reference repair.

### 11.6 Delivery and acceptance

Follow `docs/IMPLEMENTATION_PLAN.md` for milestones and baseline gaps. Existing code is implementation evidence, not acceptance proof. Completion requires both workflows to satisfy this contract, both engines to pass the full regression suite with disposable fixtures cleaned, verified editor interactions, and documentation that distinguishes supported behavior from limitations.
