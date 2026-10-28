# Active Stage Implementation Plan

Stage definition confirmed by the user on **2026-10-28**. `../AGENTS.md` is authoritative. This plan replaces the earlier v0.1-only sequence; it does not change the plugin version or declare a release.

## 1. Confirmed scope

Deliver two complete workflows on **UE 5.5 and UE 5.8**:

- Exact `Texture2D` duplicate review, canonical selection, scoped consolidation, and verification.
- Single-asset orphan inspection for textures, materials/material instances, static meshes, and skeletal meshes, followed by a deletion queue, preflight, confirmation, revalidation, and verification.

Material bindings are display/filter metadata, not a mesh-use criterion. Duplicate sources stay inside the cleanup target; comparison defaults to `/Game`; prefer external equivalent canonicals and support manual override. Show and confirm every affected package, including references outside the target.

No cluster/reachability analysis, additional duplicate types, quarantine/moves, or automatic dependency cleanup is included.

## 2. Observed baseline, not acceptance evidence

Source review on 2026-10-28 found the following. No build, automation, or live-editor acceptance was performed as part of this definition.

| Area | Observed implementation | Remaining stage work |
|---|---|---|
| Duplicate scan | Texture2D provider, payload/settings comparison, incremental scan, persistent cache, scope filtering | Validate behavior and failures on both engines |
| Consolidation | Plan, fresh fingerprint preflight, native API, post-operation checks | Service-enforced scope/impact; affected-package review; reference-destination verification |
| Canonical choice | Automatic recommendation, including external-target preference in UI | Manual override and testable non-widget scope/recommendation policy |
| Orphan scan | Uncommitted implementation for four asset families; package hard/soft referencer queries | Evidence coverage/protections, honest candidate labels, material-independent mesh policy |
| Orphan cancellation | Request changes state to CancelRequested, but TickScan accepts only Analyzing | Repair terminal-state transition and pass cancellation regression |
| Orphan deletion | Selected-row confirmation followed by DeleteAsset in widget | Separate plan/preflight/execution/verification service; no direct destructive row shortcut |
| Orphan clusters | Enum/text exists | Not implemented and explicitly excluded; do not advertise this capability |
| Redirectors | Described in documentation | Separate optional action after verified consolidation; no implicit cleanup |
| Automation tooling | Build script accepts 5.5/5.8; test script accepts only 5.8 | Add and validate a genuine 5.5 automation lane; isolate engine artifacts |

Preserve pre-existing source changes. Their presence does not authorize bypassing this contract or prove that tests pass.

## 3. Milestone A: safety models and scan foundations

Deliver:

- operation-specific plans carrying cleanup scope, evidence revision, and expected impact;
- structured candidate/review/blocked states distinct from action readiness;
- cross-mode invalidation after scope changes, asset changes, and mutations;
- consistent cancellation and failure terminal states;
- reference-evidence and protection checks independent of Slate;
- no direct orphan deletion route until the reviewed service workflow is available.

Acceptance:

- scan/review/preflight leave project content unchanged;
- reference discovery includes users outside selected folders and asset families;
- no references observed is never presented as absolute non-use proof;
- known references, failed required checks, or protected targets cannot be overridden by confirmation;
- canceled/partial scans cannot authorize mutations;
- adding/removing material bindings alone does not change orphan classification.

## 4. Milestone B: complete duplicate review and execution

Deliver:

- manual canonical selection among proven equivalent members;
- deterministic external-target preference with explainable recommendation;
- cleanup-source scope enforcement in plan/execution services;
- inspectable affected-package list, including out-of-target referencers;
- fresh equivalence and impact checks, with renewed review when impact changes;
- native consolidation and reference-destination verification;
- independent optional redirector cleanup after successful verification.

Acceptance:

- equivalent assets outside the target can be retained but never silently become sources;
- same-payload/different-settings groups remain review-only;
- manual canonical edits invalidate preflight and confirmation;
- stale evidence or expanded impact cannot execute under an old confirmation;
- partial/API/verification failure stops the queue without force-delete fallback or automatic retry.

## 5. Milestone C: single-asset orphan deletion workflow

Deliver:

- per-type inventory with explicit unsupported-state handling;
- incoming-reference evidence separate from outgoing dependency/material metadata;
- registry/protection/management checks and visible dynamic-use coverage limitations;
- exact deletion-target queue, per-item blockers/warnings, and expected package effects;
- revalidation immediately before each item using current references and editor state;
- reviewed Unreal-supported deletion API behavior on both engines;
- post-deletion verification and cross-mode invalidation/reporting.

Acceptance:

- known hard/soft/management users block deletion;
- dirty/unsaved/loaded/package-companion uncertainty cannot silently pass preflight;
- only explicitly queued eligible individual assets are deleted;
- referenced dependencies and newly exposed candidates are never automatically queued or deleted;
- zero registered references remains a bounded observation, with residual limitations disclosed at confirmation;
- mutation failures report possible partial change and require rescan/review;
- candidate size and executable-plan size are separate estimates.

## 6. Milestone D: dual-engine acceptance and documentation

For each of **UE 5.5 / Win64 Editor** and **UE 5.8 / Win64 Editor**:

1. Build both plugin modules and a disposable automation host.
2. Run the full `ContentSanitizer` suite, including both mutation workflows and failure paths.
3. Inspect failures, warnings, ensures/asserts, and generated fixture/cache cleanup.
4. Exercise tab/context-menu scope, scan/cancel, filters, inspector, manual canonical override, queue/preflight/confirmation, result invalidation, and outcomes in the editor.
5. Record engine version, command/configuration, results, and unvalidated behavior. Headless automation does not establish GUI acceptance.

The test entry point now supports isolated lanes for both engines; actual results and remaining limitations are tracked in `VALIDATION_2026-10-28.md`. An unavailable or failed lane remains unvalidated and blocks stage release acceptance.

## 7. Stage completion

All milestones must meet `AGENTS.md`, `ARCHITECTURE.md`, `GUI_GUIDELINES.md`, and `TESTING.md`. README must describe delivered behavior and limitations, not merely plans. Do not commit generated assets, binaries, IDE state, or local engine paths. A documentation-only stage-definition change requires consistency/link/diff checks, not an engine build or version bump.

## 8. Implementation follow-through (2026-10-28)

The baseline table above remains historical. Follow-through now includes orphan terminal cancellation/failure states, candidate/blocker evidence, material-independent mesh classification, exact deletion plans and Editor service, ordinary deletion with immediate revalidation and outcome checks, consumed-plan replay prevention, and cross-mode invalidation. Duplicate planning now carries service-enforced source scope, deterministic external canonical preference/manual override, affected/out-of-target packages, fresh deep equivalence and reference-destination counts. Independent conservative redirector cleanup has its own plan/preflight/confirmation. The Slate panel exposes custom target input, filters, inspector, editable queues and separate execution controls.

Generated tests and isolated dual-engine tooling accompany these changes. These entries describe implementation, not stage acceptance. Read `VALIDATION_2026-10-28.md` before treating either engine lane, the full matrix, or direct editor interactions as verified.

### Shared-package batch correction

Independent duplicate groups now use complete-batch preflight and exact intermediate evidence transitions. Shared impact is presented before the single confirmation; overlapping member packages and inter-group dependencies are rejected. Only verified planned substitutions and saves of already-reviewed packages advance pending evidence. Unexpected changes stop the batch, retain untouched-item outcome details, and invalidate all original executable plans. This corrects the independently reproduced stop-after-first-group defect; the original 33-test snapshots did not cover it. See the batch follow-up in `VALIDATION_2026-10-28.md` for new results.
