# Unreal Content Sanitizer

Unreal Content Sanitizer is an Unreal Engine Editor asset-hygiene toolkit focused first on **safe, explainable duplicate-asset detection and consolidation**.

Initial target: **Unreal Engine 5.8**.

The first release is intentionally conservative: exact Texture2D duplicate detection, behavior/settings comparison, review tooling, explicit preflight, and Unreal-native asset consolidation.

## Engineering documents

Coding agents and contributors should read these before implementation:

- [`AGENTS.md`](AGENTS.md) - authoritative agent engineering contract.
- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) - scanner, fingerprint, classification, action-plan, and safety architecture.
- [`docs/TECH_STACK.md`](docs/TECH_STACK.md) - UE 5.8 technical stack and dependency/threading constraints.
- [`docs/TESTING.md`](docs/TESTING.md) - mandatory Unreal Automation testing strategy and destructive-operation regression contract.
- [`docs/GUI_GUIDELINES.md`](docs/GUI_GUIDELINES.md) - Slate UI/UX specification.
- [`docs/IMPLEMENTATION_PLAN.md`](docs/IMPLEMENTATION_PLAN.md) - milestone sequence and v0.1 release boundary.

## Core principles

1. Scan first; mutation is a separate explicit phase.
2. Do not eagerly load the entire project to find duplicates.
3. Do not use raw `.uasset` equality as semantic duplicate proof.
4. Separate payload equivalence from behavior/settings equivalence.
5. Uncertain or merely similar assets are never automatically destructive.
6. Use Unreal's supported consolidation API for reference replacement/removal.
7. Revalidate immediately before mutation.
8. Destructive behavior requires automated regression coverage.

## Planned v0.1 workflow

```text
Scope
 -> Asset Registry inventory
 -> Cheap candidate bucketing
 -> Texture2D source extraction
 -> Payload fingerprint
 -> Settings fingerprint
 -> Safe / Review classification
 -> Inspect / choose canonical asset
 -> Action queue
 -> Dry-run / preflight
 -> Consolidate
 -> Verify
 -> Optional redirector cleanup
```

## Status

v0.1 implementation is available for exact `Texture2D` duplicate workflows:

- Asset Registry-first `/Game` scan with developer-content exclusion by default;
- schema-versioned source-payload fingerprints covering every block/layer/mip and conservative behavior-settings fingerprints, with `Safe Duplicate`, `Review Required`, and blocked conflict results;
- dockable Slate tab, scan summary, virtualized result list, inspector text, and safe-only action queue;
- verified immutable action plans, mandatory revalidation/read-only-package preflight, explicit execution confirmation, Unreal-native consolidation, and asset/reference post-operation verification;
- UE Automation test discovery sources and a disposable test-host project.

## Build packages

On a Windows machine with the matching Unreal Engine installation, create a packaged plugin with:

```powershell
.\Scripts\Build-Plugin.ps1 -EngineVersion 5.5
.\Scripts\Build-Plugin.ps1 -EngineVersion 5.8
```

The resulting Win64 Editor plugin folders are written to `dist\UE5.5` and `dist\UE5.8`.

Build the disposable UE 5.8 test host and run all `ContentSanitizer` automation tests with:

```powershell
.\Scripts\Test-Plugin.ps1
```

The test script creates a temporary project-plugin junction for Unreal's normal plugin discovery and removes it after the run.
