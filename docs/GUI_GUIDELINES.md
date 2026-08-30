# GUI Design Guidelines

This document defines the UI/UX contract for Unreal Content Sanitizer. Coding agents must preserve these rules unless a task explicitly changes the product design.

## 1. UI goals

The interface must make large-scale asset cleanup:

- understandable;
- reversible where Unreal permits it;
- explicit before destructive action;
- efficient on projects with large asset counts;
- visually consistent with Unreal Editor;
- usable without relying on color perception alone.

The UI should feel like an Unreal Editor diagnostic tool, not a standalone consumer application.

## 2. Framework

Use Slate for the main interface.

Preferred editor systems:

- dockable/nomad tab registration;
- `ToolMenus` for menu entries;
- Content Browser context-menu integration;
- `FAppStyle` for standard icons, typography, spacing, colors, brushes, and controls;
- `SListView` / `STreeView` for large result sets;
- standard Unreal notifications and Message Log for operation reporting.

Do not use UMG/Editor Utility Widgets as the primary GUI implementation.

## 3. Main information architecture

The default main tab should use this hierarchy:

```text
+--------------------------------------------------------------------+
| Toolbar / Scope / Scan controls                                    |
+--------------------------------------------------------------------+
| Summary strip                                                      |
+---------------+-------------------------------------+--------------+
| Filters       | Duplicate groups                    | Inspector    |
|               |                                     | / Diff       |
|               |                                     |              |
+---------------+-------------------------------------+--------------+
| Action queue / preflight summary / execution controls              |
+--------------------------------------------------------------------+
```

This is the baseline layout. Individual panels may collapse or resize, but the workflow order should remain visible:

```text
Scope -> Scan -> Review -> Plan -> Preflight -> Execute -> Verify
```

## 4. Toolbar

The toolbar must expose scan intent before scan execution.

Recommended controls:

- Scope selector.
- Scope path summary.
- Asset-type filter shortcut.
- Scan button.
- Cancel button while scanning.
- Refresh/re-scan when results are stale.

Initial scope options:

```text
Entire /Game
Selected Folder
Selected Assets
Custom Paths
```

Advanced inclusion options may live behind a menu:

```text
Include Plugin Content
Include Developer Content
Include Engine Content
```

Defaults should favor project-owned content and avoid surprising engine/plugin scans.

## 5. Summary strip

After a scan, show a compact quantitative summary.

Recommended metrics:

- assets inventoried;
- candidates deeply inspected;
- duplicate groups;
- safe groups;
- review-required groups;
- reclaimable size estimate;
- scan duration.

Do not overstate reclaimable space. Label values as estimates when package/redirector/save behavior makes exact disk savings uncertain.

## 6. Result classifications

The UI must expose classification text and iconography, not only color.

Required conceptual states:

- `Safe Duplicate`.
- `Review Required`.
- `Similar`.
- `Conflict`.
- `Different` where surfaced during comparison.

Use concise user-facing labels. Internal enum names may differ.

### Safety rule

Only a proven safe state may be eligible for default batch consolidation.

`Review Required`, `Similar`, and `Conflict` must not enter a destructive action queue silently.

## 7. Result list/tree

The central result view is optimized for many groups.

Use a virtualized `SListView` or `STreeView` rather than manually constructing all rows.

Recommended group row fields:

| Column | Purpose |
|---|---|
| Status | Safe / Review / Conflict |
| Group | Canonical display name |
| Type | Asset class |
| Copies | Number of assets in group |
| Reclaimable | Estimated duplicate disk footprint |
| Canonical | Recommended keep asset |
| References | Optional aggregate/reference signal |
| Path | Canonical or common path summary |

`Reclaimable` counts only non-canonical members of groups currently eligible for a safe action plan. Review, similar, conflict, and invalid groups contribute zero to the safe-reclaimable summary.

Expanded group rows show individual assets.

Recommended asset row fields:

- keep/replacement role;
- asset name;
- package path;
- disk/source size where available;
- reference count;
- concise reason for recommendation/warning.

Do not load full-size thumbnails for every off-screen row. Thumbnail work should follow visible rows and editor-standard thumbnail mechanisms.

Long scans must show the current stage, processed/total count, percentage, and current asset. Scan work advances incrementally so the cancel control remains actionable. The Content Browser folder context menu may open the same panel with selected package paths as the explicit scan scope; action plans must not include assets outside that scope.

The result area must offer an explicit classification filter for all results, `SafeDuplicate`, `ReviewRequired`, `Similar`, and `Conflict`. The summary reports visible rows, cache hits, and fingerprints recomputed during the current incremental scan.

## 8. Filtering

The left filter pane should support at minimum:

- asset class;
- classification;
- path/root;
- minimum reclaimable size;
- text search;
- safe-only view.

Later filters may include reference count, provider, package size, plugin source, and warning type.

Filtering must not mutate scan results; it only changes presentation/selection.

## 9. Inspector / diff panel

Selecting a group should explain **why** it was classified the way it was.

Recommended sections:

### Preview

- asset thumbnail where useful;
- side-by-side canonical/candidate preview when practical.

### Identity

- asset name;
- package path;
- asset class;
- source dimensions/type summary where relevant.

### Fingerprint status

Show human-readable outcomes, for example:

```text
Payload: Same
Behavior settings: Different
Classification: Review Required
```

Raw hashes may be available in an advanced/debug section but should not be the primary explanation.

### Property differences

When payload is the same but settings differ, show only relevant differences by default.

Example:

```text
Property              Canonical        Candidate
sRGB                  true             false
CompressionSettings   Default          Normalmap
```

Changed rows should be visually distinguishable, but text/value differences must remain understandable without color.

### References

Show enough reference information to assess canonical choice and impact.

Do not block the entire UI on recursively resolving every reference graph unless the user requests deeper inspection.

## 10. Canonical asset selection

The tool may recommend which asset to keep, but the UI must make the choice explicit and overridable.

Recommended scoring inputs:

- preferred path/root;
- higher reference count;
- cleaner naming;
- avoidance of temporary/developer/test paths;
- path depth/stability;
- explicit user preference.

The UI should show a short recommendation reason, for example:

```text
Recommended: highest reference count and preferred /Game/Shared path
```

Do not present heuristic scoring as proof of correctness.

## 11. Action queue

Destructive work is staged in an action queue separate from scan results.

Each queued group should clearly show:

- canonical asset;
- source assets to consolidate;
- affected-reference estimate/summary;
- reclaimable-size estimate;
- warnings;
- whether preflight currently passes.

Users must be able to remove individual groups from the queue before execution.

## 12. Preflight

A visible preflight step is mandatory before mutation.

Preflight must revalidate, at minimum:

- canonical asset still exists;
- source assets still exist;
- class/provider compatibility;
- fingerprints/equivalence have not become stale;
- canonical asset is not accidentally listed as a source;
- action plan is not empty/duplicated/corrupt;
- editor/source-control write constraints where relevant.

If preflight fails, the default result is **no mutation for the failed action**.

The UI must distinguish:

- ready;
- warning but permitted;
- blocked.

## 13. Destructive action controls

Do not place destructive execution adjacent to ordinary scan buttons without clear separation.

Recommended final controls:

```text
[Dry Run / Revalidate]    [Consolidate N Assets]
```

Execution button rules:

- disabled when queue is empty;
- disabled while scan data is stale until revalidated;
- disabled for blocked actions;
- label should include scope/count when possible;
- confirmation dialog should summarize the operation, not merely ask "Are you sure?".

Example confirmation content:

```text
13 duplicate groups
28 assets will be consolidated
13 canonical assets will be retained
Estimated reclaimable size: 2.3 GB
```

Do not hide the fact that Unreal consolidation removes source assets and redirects references.

## 14. Redirector handling

Redirector cleanup is a separate post-consolidation concern.

Recommended v0.1 behavior:

- consolidation first;
- verification second;
- optional redirector cleanup afterward;
- redirector cleanup is not silently bundled into scanning.

If an "auto fix redirectors" preference is later added, the operation summary must still report that the step will occur.

## 15. Progress and cancellation

Long scans must show progress without freezing editor interaction unnecessarily.

Progress should expose useful stage information rather than a fake smooth percentage.

Examples:

```text
Inventory: 12,481 assets
Candidate bucketing: 8,212 / 12,481
Deep fingerprint: 73 / 411 candidate assets
```

Cancellation requirements:

- cancel request is explicit;
- scanner reaches a consistent canceled state;
- no mutation is performed by scan cancellation;
- partial results are either clearly labeled partial or discarded according to session policy;
- UI never implies scan completion after cancellation.

## 16. Empty/error states

Design explicit states for:

- no scan yet;
- scan in progress;
- no duplicates found;
- scan canceled;
- provider unsupported;
- scan failed;
- stale results;
- action queue ready;
- consolidation completed with warnings;
- consolidation failed.

Avoid empty blank panels that require reading the Output Log to understand what happened.

## 17. Visual style

Use Unreal Editor's existing visual language.

Rules:

- prefer `FAppStyle` brushes/icons over custom assets;
- avoid hard-coded RGB values unless there is a documented reason;
- avoid decorative gradients and excessive visual chrome;
- keep density appropriate for professional editor tooling;
- align controls and columns consistently;
- do not use large marketing-style cards inside the main working view;
- custom branding should be limited to plugin identity, not override editor conventions.

## 18. Accessibility

- Never rely on color alone for status.
- Use text + icon + optional color.
- Provide tooltips for non-obvious icons.
- Preserve readable contrast by inheriting editor theme styles.
- Do not force fixed colors that break light/dark editor themes.
- Make important actions reachable by keyboard/focus where Slate controls support it.
- Avoid status information that appears only on hover.

## 19. Text and terminology

Use stable terminology throughout the UI:

- **Scan**: read-only discovery/classification.
- **Candidate**: asset that survived cheap filtering and requires deeper inspection.
- **Duplicate Group**: assets grouped by semantic duplicate rules.
- **Canonical Asset**: asset selected to remain.
- **Source Asset**: duplicate asset to be consolidated into the canonical asset.
- **Preflight**: non-destructive validation immediately before execution.
- **Consolidate**: Unreal reference replacement + removal of source assets through the supported editor consolidation API.

Avoid vague destructive labels such as "Clean Everything" or "Fix All" for v0.1.

## 20. Performance rules

The GUI must not turn a scalable scanner into an unscalable tool.

- Virtualize long lists.
- Avoid constructing property-diff widgets for unselected groups.
- Avoid eagerly resolving deep reference graphs for every row.
- Avoid synchronously loading thumbnails/source data for the full result set.
- Debounce expensive text/filter recomputation if needed.
- Keep scanner results in a UI-independent model and refresh views incrementally.

## 21. Separation of concerns

Slate widgets may:

- render state;
- collect user intent;
- invoke scanner/session/action services;
- display structured errors.

Slate widgets must not become the authoritative implementation of:

- duplicate equivalence;
- fingerprint construction;
- canonical scoring;
- action-plan validation;
- consolidation safety rules.

Those belong in testable non-widget code.

## 22. v0.1 screen acceptance criteria

The first shippable UI is complete when a user can:

1. open a dockable Content Sanitizer tab;
2. choose `/Game`, selected folder/assets, or custom scope;
3. scan and cancel safely;
4. filter/sort duplicate Texture2D groups;
5. understand Safe vs Review Required classifications;
6. inspect same/different settings;
7. choose or override a canonical asset;
8. add safe groups to an action queue;
9. run preflight/dry-run;
10. execute consolidation intentionally;
11. see success/warnings/failures without opening raw logs;
12. optionally perform redirector cleanup after verified consolidation.
