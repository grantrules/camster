# M6 Hardening and Release Baseline

## Scope

This document defines the baseline hardening/release gates delivered for M6.

## Regression Suite

- Executable: `camster_regression_suite`
- Source: `tests/regression_suite.cpp`
- Coverage:
  - Geometry determinism hashing for repeated extrude generation
  - CAM snapshot generation and persisted metrics
  - Drawing generation and PDF/DXF export baseline
  - STEP export/import round-trip baseline
  - DFM heuristic run on a known mesh

Run via Docker:

```bash
./scripts/docker-build-debug.sh
```

The debug build runs `ctest` and fails on any regression/perf test error.

## Crash Reporting and Structured Logs

- Module: `src/core/StructuredLog.*`
- Output: JSONL logs under `./logs/`
- Crash hooks:
  - `std::set_terminate`
  - `SIGSEGV`, `SIGABRT`, `SIGFPE`, `SIGILL`
- Runtime events:
  - startup/shutdown
  - file-open success/failure
  - draw-frame failure
  - periodic frame-budget overrun warning

## Performance Budgets

- Runtime frame budget:
  - threshold: `16.7ms` (`~60 FPS`)
  - displayed in Actions panel and logged when repeatedly exceeded
- Bench budgets (`tests/perf_bench.cpp`):
  - geometry generation loop budget: `400ms`
  - CAM generation loop budget: `250ms`
- Benchmark output:
  - `dist-debug-docker/perf-bench.txt`

## Release Pipeline Hardening

Scripts:

- `scripts/docker-build-debug.sh`
  - builds debug with tests enabled
  - runs `ctest --output-on-failure`
- `scripts/docker-build-release.sh`
  - builds release with tests enabled
  - runs `ctest --output-on-failure`
  - emits `dist-release-docker/bin/SHA256SUMS.txt`
- `scripts/docker-build-windows.sh`
  - cross-builds Windows release
  - validates `camster.exe` exists
  - emits `dist-windows-docker/SHA256SUMS.txt`
- `scripts/docker-release-ci.sh`
  - orchestrates debug + release + windows builds
  - validates both checksum manifests

## Acceptance

M6 is considered complete when:

1. Debug Docker build passes with tests.
2. Release Docker build passes with tests and Linux checksums are generated.
3. Windows Docker build passes and Windows checksums are generated.
4. `TODO.md` M6 items are marked complete.
