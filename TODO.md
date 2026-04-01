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
- [ ] Feature set: extrude, revolve, sweep, loft, shell, draft
- [x] Booleans: union/subtract/intersect
- [x] Fillet/chamfer baseline with reference persistence strategy
- [ ] Robust reference geometry (planes/axes/points)

## M4: CAM MVP
- [ ] 2.5D ops: facing, pocket, contour, drilling
- [ ] Tool library with feeds/speeds presets
- [ ] Stock/WCS setup
- [ ] Toolpath preview + collision/gouge baseline checks
- [ ] First production post-processor target

## M5: Drawings, Interop, DFM
- [ ] Drawing workspace with key views/sections/dimensions
- [ ] Export: PDF, DXF
- [ ] STEP import/export baseline
- [ ] DFM checks: wall thickness, minimum radius, drillability heuristics

## M6: Hardening and Release
- [ ] Regression suites (geometry, determinism, CAM snapshots)
- [ ] Crash reporting + structured logs
- [ ] Performance budgets and profiling passes
- [ ] Linux/Windows release pipeline hardening

## Working Rules
- [ ] No schema changes without migration code
- [ ] No feature merge without undo/redo + save/load coverage
- [ ] Every milestone item needs measurable acceptance criteria before close
