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

The repository is currently in the **design/specification stage**. Implementation should begin with Milestone 0 in [`docs/IMPLEMENTATION_PLAN.md`](docs/IMPLEMENTATION_PLAN.md): plugin/module skeleton, dockable Slate tab, minimal test host, and automation-test discovery.
