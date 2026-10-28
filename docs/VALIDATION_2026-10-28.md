# 2026-10-28 implementation and isolated validation

This record separates implemented behavior from acceptance. Full stage acceptance still requires both engine suites and direct editor workflow checks. No release, commit, deployment, or production cleanup is authorized by this report.

## Isolation

- Preserve the pre-existing uncommitted source, orphan implementation, tests and stage-definition documents; no reset or checkout restoration.
- Copy plugin sources and a dedicated C++ test host to a unique temporary directory for each lane. No junction into the working plugin and no production-project content.
- Build `ContentSanitizerTestHostEditor` with engine/plugin changes prohibited, no hot reload, at most two actions. Launch only the owned `UnrealEditor-Cmd` host with unattended/NullRHI, hidden and at reduced priority. Do not clear global Live Coding state or use the front editor to unlock a build.
- Preserve logs, command lines, automation JSON and fixture/temporary-host cleanup records under ignored `Saved/Validation/`. Ordinary source checks use Git and compiler/test output, without file checksums.
- Direct GUI acceptance is pending: no front editor UI, focus, map, shortcuts, Live Coding, loaded plugin binaries, or production assets were used for validation.

## Implementation covered by this change

- Core orphan cancellation/failure states, candidate/blocker evidence, exact-class deletion eligibility, management/primary/protected-root checks and visible dynamic-use limitations.
- Material metadata independent of mesh use classification; unknown metadata remains explicit.
- Core deletion plans carry exact targets, cleanup/exclusion scope, evidence and policy identity. Editor preflight and per-item revalidation check current registry, loaded hard/soft/undo/editor users, dirty/unsaved/multi-object/read-only packages and source control.
- Ordinary native deletion, target/package and dependency-survival verification, failure stop and consumed-operation journal. No force-delete, reference-nulling or recursive expansion in the product path.
- Duplicate source scope enforcement, canonical override/recommendation, affected/out-of-target package review, fresh deep verification, evidence/impact confirmation binding, and serialized reference-destination/count checks.
- Independent conservative redirector cleanup, reviewed after verified consolidation; no implicit fix-up.
- Slate target input, filters including material metadata, inspector, removable queues, visible preflight impact, confirmation and cross-mode invalidation.

## Iteration evidence

| Lane / attempt | Result | Evidence / action |
|---|---|---|
| 5.8 first/second | Compilation failed | Fixed archive API access, 5.8 metadata compatibility, and fixture file API usage. Temporary hosts removed. |
| 5.8 third | Modules compiled; automation did not start | Startup `Null assigned to TNotNull` before tests. The same failure reproduced in a minimal host without this plugin. Removing extra no-sound/no-shader-compile switches allowed the minimal host into command processing. Only the owned failed/background probes were stopped. |
| 5.8 fourth | Compilation failed | Corrected Editor subsystem include/module dependency. Temporary host removed. |
| 5.5 first | Safely blocked before compilation | Catch-all `UnrealEditor` target wanted to rebuild an engine CaptureData binary; `NoEngineChanges` rejected it. Replaced with a dedicated disposable host target. |
| 5.5 second/third | Compilation failed | Corrected material-domain include and used Asset Manager public fixture APIs instead of protected registry methods; corrected asset-bundle header. Temporary hosts removed. |

Subsequent iterations fixed redirector resolution following the destination rather than obtaining the redirector object, the AssetTools modal-report path, fixture registry-watcher races, a test-array self-reference assertion, metadata pointer/reference compatibility, and synchronous post-save dependency publication. Dynamic management coverage remains tested; primary protection uses the on-disk primary-tag contract because the installed 5.8 engine discards dynamic assets' optional disk paths on management refresh. Failed native consolidation/deletion and false-success seams now require a new scan/plan and reject replay.

Earlier successful snapshots passed 31 and then 32 tests on both engines. A mode-visibility assertion prompted an explicit synchronous visibility update. The final suite adds actual verified individual deletion for all five advertised exact classes, preserving their shared material/texture dependencies.

## Initial regression lanes (before the shared-package correction)

| Engine / toolchain | Module build | Full suite | Fixture files after tests | Temporary host |
|---|---|---|---|---|
| UE 5.5.4, CL 40574608 / MSVC 14.38 | Passed | 33/33, including 9 with warnings | 0 | Removed |
| UE 5.8.2, CL 56702186 / MSVC 14.51 | Passed | 33/33, including 9 with warnings | 0 | Removed |

Reproduction commands, using a dedicated disposable host and Win64 Editor Development:

```powershell
.\Scripts\Test-Plugin.ps1 -EngineVersion 5.5 -CompilerVersion 14.38.33130 -MaxParallelActions 2 -ResultsDirectory .\Saved\Validation\2026-10-28-UE5.5-acceptance
.\Scripts\Test-Plugin.ps1 -EngineVersion 5.8 -MaxParallelActions 2 -ResultsDirectory .\Saved\Validation\2026-10-28-UE5.8-acceptance
```

Each evidence directory contains `commands.txt`, `build.log`, `editor.log`, `Automation/index.json`, `fixture-leftovers.txt`, `result.txt` and `cleanup.txt`. The command record expands the actual engine/project paths and test flags; no local-machine paths are added to tracked source. Automation uses `-unattended -nop4 -NullRHI -NoLiveCoding`, the `ContentSanitizer` prefix, and `Automation Test Queue Empty` as the exit condition. Both modules and the test host compile from copied sources, with engine/plugin writes prohibited.

The suite covers provider settings and payload invariants, cache behavior, scope/canonical rules, cancellation before and during scanning, unsupported/missing evidence, hard/soft/management/primary protection, stale/dirty/read-only/companion/sidecar rejection, exact deletion and dependency survival for Texture2D/Material/MaterialInstanceConstant/StaticMesh/SkeletalMesh, native hard/soft-user rejection, both mutation failure classes and replay prevention, actual reference destinations, independent redirector cleanup, and unattached Slate queue/mode/invalidation behavior.

## Diagnostics and limits

- No compiler warning originates in the plugin source in the final successful runs. The installed 5.8 toolchain reports that MSVC 14.51 is newer than its preferred version; engine public headers emit deprecation warnings. These were not suppressed or represented as plugin fixes.
- Integration warnings include disabled optional engine plugins (ACL/Niagara defaults), generated empty-mesh render data, expected refused native deletion in negative cases, and file-watcher tombstone/rescan notifications during disposable teardown. Reports retain warnings; passing test counts include them explicitly.
- The installed 5.8 engine emits 13 startup `LogAutomationTest: Error: Condition failed` lines before the plugin suite. The same output is present in the retained minimal no-plugin probe. The plugin suite has zero failed/error tests and no ensure/assert/fatal in the final successful runs. The engine startup diagnostic's root cause is unresolved; a strict completely-clean-log release gate remains open. No engine/global setting was changed to hide it.
- Direct visual/editor workflow acceptance remains unvalidated. The Slate test creates an unattached widget and never opens a window or changes focus. Rendering, production-content behavior, source-control-provider workflows and large real-project performance are not established by these generated fixtures.
- Supported deletion classes are the five exact classes listed above. Other subclasses/states, protected roots/assets, external packages/payload sidecars and failed inspections are blocked. Dynamic/code/configuration/non-reflected use remains a disclosed coverage limitation; candidate bytes are not guaranteed reclaimed space.
- Redirector cleanup only handles reviewed zero-observed-user single-object writable packages with source control disabled. Its review retains dynamic-use limitations and management checks; no general reference fix-up or collection rewrite is performed.
- Formal test hosts and their disposable fixtures are removed. Logs from the independent startup probes were copied to `Saved/Validation/UE5.8-StartupProbeEvidence`. Automatic approval rejected removal of `Saved/Validation/StartupProbe` with only a policy-block reason, so that ignored probe directory remains. No alternate deletion mechanism was used to bypass the rejection.

Implementation and automated regression are recorded separately from stage release acceptance. Direct editor acceptance, the installed-engine startup diagnostic, and the blocked probe cleanup remain open. No commit, push, deployment or production asset mutation was performed.


## Shared-package batch follow-up

Independent review reproduced a missing scenario after the 33-test snapshots: two independent duplicate groups shared a reference package; the first native consolidation changed its file evidence/reference audit, so the second group's old single-plan review was rejected. This was a conservative stop, with no external edits, rather than successful batch acceptance. Evidence is retained under `Saved/Validation/2026-10-28-review-shared-package`.

The correction introduces service-owned batch preflight, shared impact presentation, independent-member checks, and exact intermediate reference-count substitution checks before permitting reviewed package-save evidence to advance. It retains strict single-group evidence comparison. External impact/reference changes and unrelated package-file changes still block; native or verification failure stops, consumes all original plans, and retains exact per-plan untouched results for the panel's read-only outcome report.

Before the teardown correction, the UE 5.5 final-labeled snapshot passes 43/43 tests (24 without warnings, 19 with warnings), with zero failed/not-run/in-process tests, no plugin-source compiler warnings or assert/ensure/fatal, zero fixture files and the temporary host removed. The first UE 5.8 follow-up (directory suffix `UE5.8-final`) passed all batch behavior assertions but failed the ten new fixture-cleanup assertions: 33 passed, 10 failed. Its native bulk fixture teardown left package files, which the owned-host script recorded before removing that temporary host. This is retained as a failed lane, not a pass. A second attempt (`UE5.8-cleanup`) also failed only the same cleanup assertions; clearing generated edges and waiting for writes did not fix native bulk directory removal. Fixture-only teardown now inventories exact package filenames within a GUID-validated project Content root, removes generated objects with the engine, resets retained package loaders, explicitly removes inventoried leftover files, and asserts both disk/registry empty. This is test restoration, not a product deletion fallback. The first explicit-teardown build (`UE5.8-owned-teardown`) failed on a fixture-only `RemovePath` FName/FString mismatch; corrected to the documented FString argument. With corrected explicit fixture teardown, UE 5.8 passes 43/43 (24 without warnings, 19 with warnings), zero failed/not-run/in-process tests, no plugin-source compiler warnings or assert/ensure/fatal, zero fixture files and the temporary host removed. Its 13 previously documented startup condition diagnostics remain. The same final source also passes UE 5.5: 43/43 (24 without warnings, 19 with warnings), zero failed/not-run/in-process, no plugin-source compiler warning or assert/ensure/fatal, zero fixture files and its temporary host removed. These two lanes supersede earlier source snapshots for the batch correction. The first 42-test 5.5 snapshot also passed before the additional file-evidence/UI assertions. Evidence: `Saved/Validation/2026-10-28-batch-fix-UE5.5-final`.

The new cases cover shared hard/soft users, changes before/during execution, unchanged-reference package-file evidence outside the completed impact, batch reordering, a failed second group with a third unsubmitted, false success, overlapping members and replay. Tests remain hidden, unattended, isolated, serial and limited to one compile action.

The independent review source copy's deletion was also rejected by automatic approval with only a policy-block reason. It remains untouched; no alternate cleanup was attempted. Both that copy and the original startup probe are separate from the disposable test hosts. GUI/release acceptance and the installed 5.8 startup diagnostics remain open.

Follow-up reproduction commands (Win64 Editor Development; complete `ContentSanitizer` suite):

```powershell
.\Scripts\Test-Plugin.ps1 -EngineVersion 5.5 -CompilerVersion 14.38.33130 -MaxParallelActions 1 -ResultsDirectory .\Saved\Validation\2026-10-28-batch-fix-UE5.5-owned-teardown-final
.\Scripts\Test-Plugin.ps1 -EngineVersion 5.8 -MaxParallelActions 1 -ResultsDirectory .\Saved\Validation\2026-10-28-batch-fix-UE5.8-owned-teardown-2
```

The follow-up did not send UI input, change focus, invoke Live Coding, copy binaries into production or terminate any process. Read-only process inspection currently identifies the protected VsingerLTS Editor as PID 120052 (the former PID 121576 is no longer present); this task did not stop or restart either editor. The independent review source copy and previously blocked startup probe were not touched.

Final corrected-batch lanes (these supersede the earlier snapshots for this correction):

| Engine | Module build | Full suite | Fixture files | Temporary host | Evidence directory under Saved/Validation |
|---|---|---|---|---|---|
| UE 5.5.4 / MSVC 14.38 | Passed | 43/43; 19 with warnings | 0 | Removed | `2026-10-28-batch-fix-UE5.5-owned-teardown-final` |
| UE 5.8.2 / MSVC 14.51 | Passed | 43/43; 19 with warnings | 0 | Removed | `2026-10-28-batch-fix-UE5.8-owned-teardown-2` |

Final diff/relative-link checks pass. Product mutation still uses the supported native consolidation path; exact batch evidence transitions do not remove or bypass the standalone evidence comparison. No commit, push, deployment, production asset mutation, foreground interaction or process termination was performed by this follow-up. Direct GUI/release acceptance remains open.
