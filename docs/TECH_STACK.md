# Technical Stack and Engineering Constraints

This document defines the approved technical stack for Unreal Content Sanitizer and the constraints coding agents must preserve.

## 1. Baseline

- Engine targets: **Unreal Engine 5.5 and 5.8**, both mandatory acceptance lanes.
- Language: C++ using Unreal Build Tool's engine-default language standard.
- Plugin class: Editor tooling plugin.
- UI framework: Slate.
- Primary host: Unreal Editor on Windows.
- Third-party runtime dependencies: none unless explicitly approved.

The plugin should remain portable across editor platforms where Unreal APIs permit it, but Windows is the initial development and test target.

## 2. Modules

The repository should evolve toward two modules:

### `ContentSanitizerCore`

Responsibilities:

- asset inventory models;
- scan sessions and cancellation;
- cheap fingerprint generation;
- deep fingerprint generation;
- duplicate grouping;
- equivalence classification;
- canonical-asset scoring;
- action-plan data models;
- orphan reference/protection evidence and deletion-plan rules;
- pure validation logic;
- testable provider interfaces.

Constraints:

- no Slate dependency;
- no widget types in public APIs;
- no direct destructive editor operations;
- minimize `UnrealEd` coupling where practical.

### `ContentSanitizerEditor`

Responsibilities:

- dockable Slate tab;
- menus and commands;
- Content Browser integration;
- thumbnails and asset navigation;
- inspector/property-diff presentation;
- source-control/editor state integration;
- consolidation execution;
- orphan deletion execution and verification through a separately preflighted service;
- redirector cleanup;
- notifications and Message Log integration.

## 3. Unreal modules and APIs

Prefer these Unreal-native systems:

| Need | Preferred technology |
|---|---|
| Asset discovery | `AssetRegistry`, `IAssetRegistry`, `FARFilter`, `FAssetData` |
| Package metadata | Asset Registry package data |
| Hashing | `FIoHash`, `FIoHashBuilder` |
| Texture source data | `FTextureSource` |
| Reference discovery | Asset Registry dependency/referencer APIs |
| Consolidation | `UEditorAssetSubsystem::ConsolidateAssets` |
| Redirector cleanup | Ordinary `ObjectTools` deletion of verified unreferenced redirectors; broader fix-up remains excluded |
| UI | `Slate`, `SlateCore`, `FAppStyle` |
| Menus | `ToolMenus` |
| Docking | Global tab manager / nomad tab registration |
| Content Browser hooks | `ContentBrowser` APIs |
| Logging | dedicated log category + `FMessageLog` for actionable editor reports |
| Tests | Unreal Automation Framework / Automation Spec |
| Background pure work | UE task/async facilities |

Do not introduce a third-party hashing implementation when `FIoHashBuilder` is sufficient.

## 4. Build dependency policy

Keep dependency direction one-way:

```text
ContentSanitizerEditor
        |
        v
ContentSanitizerCore
```

Core must never include Editor UI headers merely to satisfy implementation convenience.

Typical Core dependencies may include:

- Core
- CoreUObject
- Engine
- AssetRegistry

Typical Editor dependencies may include, as needed:

- ContentSanitizerCore
- Core
- CoreUObject
- Engine
- UnrealEd
- Slate
- SlateCore
- InputCore
- ToolMenus
- ContentBrowser
- AssetRegistry
- AssetTools
- Projects
- SourceControl

Only add a module dependency when code directly requires it. Avoid broad dependency lists copied from template plugins.

## 5. Asset discovery policy

Project-wide scanning must start from the Asset Registry.

Required principles:

- Do not eagerly load every asset in `/Game`.
- Use metadata to eliminate impossible matches first.
- Only candidate duplicate buckets progress to expensive extraction/hash work.
- Default scan scope is project content (`/Game`) unless the user explicitly includes plugin/engine/developer content.
- Scan results must be deterministic for a stable project state.

The intended high-level pipeline is:

```text
Asset Registry inventory
        -> cheap bucket key
        -> candidate groups only
        -> source/payload extraction
        -> payload fingerprint
        -> settings fingerprint
        -> deep verify if needed
        -> classification
```

## 6. Fingerprint policy

A semantic duplicate is not the same thing as an identical `.uasset` file.

Do not use raw package bytes or package-level hash as the sole proof of duplicate equivalence.

Every provider should conceptually expose:

```cpp
struct FAssetFingerprint
{
    FIoHash PayloadHash;
    FIoHash SettingsHash;
    uint32 SchemaVersion = 0;
};
```

The concrete structure may evolve, but the separation is mandatory:

- **Payload** answers whether the meaningful underlying content is equivalent.
- **Settings** answers whether Unreal behavior is equivalent.

Example for textures:

Payload inputs can include source dimensions, layer/block/mip structure, source format, and source data.

Settings inputs can include behavior-relevant properties such as sRGB, compression settings, texture group, mip generation, addressing, filtering, virtual texture streaming, and other properties proven to affect runtime/editor behavior.

When payload matches but behavior settings differ, classify as `ReviewRequired`, not `SafeDuplicate`.

## 7. Provider model

Avoid central type-switch sprawl.

Preferred abstraction:

```cpp
class IAssetFingerprintProvider
{
public:
    virtual ~IAssetFingerprintProvider() = default;

    virtual bool Supports(const FAssetData& AssetData) const = 0;
    virtual FSanitizerCheapFingerprint BuildCheapFingerprint(const FAssetData& AssetData) const = 0;
    virtual FAssetFingerprint BuildDeepFingerprint(UObject* Asset) const = 0;
};
```

Exact signatures may change to improve threading/error reporting, but provider responsibilities should remain isolated by asset class.

Initial provider: `Texture2D`.

The active stage remains `Texture2D`-only for duplicates. Other asset types in orphan inspection do not imply duplicate-equivalence support. Possible providers after this stage, subject to a separate scope decision:

1. `TextureCube`.
2. `MaterialInstance`.
3. `SoundWave`.
4. `StaticMesh`.
5. `SkeletalMesh` only after equivalence rules are rigorously specified.

## 8. Threading policy

Treat UObject/editor access as game-thread-only unless the specific API is explicitly safe.

Preferred sequence:

```text
Game Thread
  - discover/load candidate UObject
  - extract immutable source snapshot
          |
          v
Worker Thread
  - hash copied bytes
  - compare fingerprints
  - group/sort/score
          |
          v
Game Thread
  - publish results
  - update Slate model
  - execute editor mutation
```

Forbidden patterns:

- arbitrary `LoadObject()` from a worker task;
- hashing through mutable UObject state concurrently with editor modification;
- carrying unguarded raw UObject pointers into long-running worker tasks;
- executing consolidation from a worker thread.

## 9. Mutation policy

Scanning and classification are read-only.

Mutation is a separate explicit phase with these stages:

1. Create action plan.
2. Dry-run/preflight.
3. Revalidate asset existence and equivalence.
4. Check editor/source-control constraints where relevant.
5. Execute Unreal's supported consolidation API.
6. Verify references and result state.
7. Offer redirector cleanup after successful verification.

Do not use force delete as a shortcut for duplicate cleanup.

## 10. Error model

Prefer structured operation results over boolean-only APIs for new project code.

A scanner/provider/operation result should be able to report:

- success/failure;
- machine-readable error/status code;
- user-facing summary;
- asset/package context;
- warnings that do not invalidate the entire operation.

Errors must remain visible to the caller and UI. Logging alone is not an API contract.

## 11. Logging

Use a dedicated category such as:

```cpp
DECLARE_LOG_CATEGORY_EXTERN(LogContentSanitizer, Log, All);
```

Do not use `LogTemp` in production code.

Use Message Log for user-actionable batch operation output where it improves editor workflow.

## 12. Configuration

Project/user settings may later use `UDeveloperSettings`.

Settings should cover policy, not duplicate implementation state. Examples:

- default scan paths;
- excluded paths;
- whether plugin content is included;
- preferred canonical asset roots;
- default redirector behavior;
- cache enablement;
- safe-only filters.

Fingerprint schema versions must be code-owned constants, not editable project preferences.

## 13. Caching

Fingerprint caching is implemented as a scan optimization, never a prerequisite or authority for mutation correctness.

A cache entry should be invalidated by at least:

- package/source change identity;
- fingerprint provider/schema version;
- asset class/provider mismatch.

Package-level saved hash can be useful as a cache invalidation signal, but not as semantic duplicate proof.

## 14. Source code style

- Follow Unreal coding conventions.
- Prefer explicit domain names over generic utility classes.
- Keep public headers small.
- Avoid unnecessary templates in public interfaces.
- Prefer Unreal containers/types where they improve engine integration.
- Use deterministic ordering for user-visible results.
- Avoid mutable global state.
- Avoid hidden singleton services unless the Unreal subsystem model clearly fits the lifetime.
- No emoji in source code or comments.

## 15. Explicitly rejected stack choices for the active stage

Do not introduce these without a new architecture decision:

- Python implementation for core scanning.
- Editor Utility Widgets / UMG as the main UI.
- Qt.
- WebView/HTML UI.
- External databases.
- Third-party image similarity/AI libraries.
- Perceptual hashing used for automatic deletion.
- Custom reference rewriting replacing Unreal's consolidation system.

## 16. Dual-engine delivery

UE 5.5 and 5.8 must both build the Core/Editor modules and pass the full stage acceptance suite. Keep compatibility handling localized without weakening safety behavior on either engine. Do not assume identical fingerprints across engine versions; cache/provider schema compatibility must remain explicit.

Both build and test entry points accept 5.5/5.8. `Test-Plugin.ps1` copies the plugin and a dedicated C++ Editor host to a unique temporary directory, prohibits engine/plugin changes, runs unattended/NullRHI automation serially, and retains reports plus cleanup evidence outside the temporary host. It does not link or build the repository plugin in place. Tooling availability is not acceptance evidence; see `VALIDATION_2026-10-28.md` for actual lane outcomes and GUI limitations.

### Operation state and compatibility

An Editor subsystem keeps an in-memory journal of operation identifiers already submitted to native mutation APIs. Such plans cannot replay, including after reported failure; new scan/review/plan is required. This state is separate from widgets and stores no raw asset pointers. Plans and reviewed preflights bind exact scope, membership, evidence and impact. Compatibility wrappers localize 5.5/5.8 package-object enumeration and package metadata changes.

### Conservative redirector deletion decision

UE 5.5 `IAssetTools::FixupReferencers` always opens a modal report, including with checkout prompting disabled. The active workflow therefore only removes already-unreferenced redirectors through the same ordinary reference-checking `ObjectTools::DeleteObjects` boundary. It performs no reference rewrite or collection fix-up. Scope, destination, users, write state and package companions are preflighted; package absence and destination survival are verified. Referenced redirectors and active source control are blocked by this deliberately narrower policy.
