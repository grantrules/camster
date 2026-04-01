# CAD/CAM Roadmap

## M1: Core Architecture Stabilization
- [x] Lock geometry strategy for current cycle (mesh-first boundary + B-Rep seam definition)
- [x] Add versioned project metadata validation and migration hooks for `.nut` format
- [x] Add deterministic rebuild ordering contract and diagnostics
- [x] Normalize command/undo behavior for object create/delete/rename/visibility/lock/color
- [x] Add save/load round-trip checks for currently supported entities

## M2: Sketch and Parametric Foundation
- [x] Sketch entities: line, arc, circle, rectangle, polyline
- [x] Constraint set: coincident, H/V, parallel, perpendicular, equal, dimensions
- [x] Constraint conflict detection/reporting
- [x] Timeline-integrated sketch editing lifecycle

## M3: Solid Modeling v1
- [x] Feature set: extrude, revolve, sweep, loft, shell, draft
- [x] Booleans: union/subtract/intersect
- [x] Fillet/chamfer baseline with reference persistence strategy
- [x] Robust reference geometry (planes/axes/points)

### M3+ Expansion (Solid Modeling v1 Hardening)
- [x] Boolean robustness pass: non-overlap handling, coplanar-face edge cases, predictable naming/selection after operations
- [x] Fillet/chamfer continuity pass: stable edge identity across undo/redo and chained operations
- [x] Solid feature editability: lightweight parameter edit for draft/shell/revolve/sweep/loft after creation
- [x] Feature failure diagnostics: user-facing reason codes + highlight failing inputs
- [x] Reference geometry associativity: keep derived references valid or gracefully mark broken dependencies
- [x] Topology validation gates: manifold checks and triangle-normal consistency after each solid op
- [x] Performance pass on large meshes: timed benchmarks for combine/fillet/shell and regression thresholds
- [x] Timeline replay determinism for solids: same inputs -> byte-stable mesh output checks

## M4: CAM MVP
- [x] 2.5D ops: facing, pocket, contour, drilling
- [x] Tool library with feeds/speeds presets
- [x] Stock/WCS setup
- [x] Toolpath preview + collision/gouge baseline checks
- [x] First production post-processor target

## M5: Drawings, Interop, DFM
- [x] Drawing workspace with key views/sections/dimensions
- [x] Export: PDF, DXF
- [x] STEP import/export baseline
- [x] DFM checks: wall thickness, minimum radius, drillability heuristics

## M6: Hardening and Release
- [x] Regression suites (geometry, determinism, CAM snapshots)
- [x] Crash reporting + structured logs
- [x] Performance budgets and profiling passes
- [x] Linux/Windows release pipeline hardening

## Working Rules
- [ ] No schema changes without migration code
- [ ] No feature merge without undo/redo + save/load coverage
- [ ] Every milestone item needs measurable acceptance criteria before close
