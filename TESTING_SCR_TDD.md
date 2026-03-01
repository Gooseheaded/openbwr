# SCR Tile Compatibility Test Suite (TDD) - Harness Decision

## Goal

Define a strict, simple, and reliable test harness for Remastered tile forward-compatibility work, with fast local iteration and deterministic results.

## Constraints

- Deterministic simulation correctness has highest priority.
- Missing SCR semantic pack support must hard-fail with clear errors.
- Single-test execution must be easy and fast for TDD loops.
- Keep dependency footprint minimal.
- Target pre-merge runtime <= 3 minutes.

## Alternatives

| Option | Summary | Pros | Cons | Single-Test UX |
|---|---|---|---|---|
| A. CTest-native C++ binary | Add a small dedicated test executable and register tests via CMake/CTest | No new runtime dependency; deterministic in same language/runtime as core loader; clear CI output; native test filtering | Requires small CMake/test boilerplate | `ctest --test-dir <build> -R <name> --output-on-failure` |
| B. Standalone checker + golden files | Build one CLI that emits digests and compare outputs in scripts | Very simple mental model; easy artifact inspection | Weaker standardized test reporting; more custom scripting | Medium (script-level filtering) |
| C. Python harness | Python runner calls binaries and compares artifacts | Fast scripting and fixture manipulation | Adds Python/env dependency; potential environment variance | Good, but external dependency |

## Decision

Select **Option A (CTest-native C++ binary)**.

Rationale:

1. Best fit for deterministic behavior checks in core C++ code paths.
2. Lowest operational risk for contributors (no extra language runtime required).
3. Strong single-test ergonomics with CTest regex filtering.
4. Integrates cleanly with existing CMake build flow.

## Test Suite Shape

Layer 1 (fast contract tests):

- Version policy (`VER`) acceptance/rejection.
- Hard-fail behavior when SCR semantic pack is missing.
- Hard-fail behavior on out-of-range tile group or megatile references.

Layer 2 (semantic snapshot tests):

- Load fixture maps.
- Produce deterministic digest over tile semantic flags.
- Compare against checked-in expected digest artifacts.

Layer 3 (determinism smoke tests):

- Run selected classic and SCR replays twice.
- Assert identical frame signatures and no desync.

## Proposed Layout

- `tests/scr_compat/` - test sources
- `tests/testdata/maps/` - fixture maps
- `tests/testdata/replays/` - fixture replays
- `tests/testdata/expected/` - expected digest/signature files

Fixture triage utility:

- `openbw_asset_classify <path>` - classify map/replay asset by CHK `VER`
- `openbw_asset_classify <path> --json` - machine-readable output for inventory scripts
- `tools/scan-openbw-assets.ps1` - batch-scan local collections and emit summary CSV/JSONL reports
- `tools/promote_scan_fixtures.py` - promote shortlisted scan candidates into `tests/testdata/`

Current fixture manifests:

- `tests/testdata/maps/manifest.csv` - strict version assertions enabled
- `tests/testdata/replays/manifest.csv` - confidence-tagged replay fixtures (`high` vs `low`)
- `tests/testdata/negative/manifest.csv` - malformed/error fixtures for hard-error checks

Manifest validation utility:

- `tools/validate_scr_fixture_inventory.py` validates expected behavior from all three manifests
- wired as CTest test `classifier.fixtures.validate` when Python is available
- `tools/validate_map_snapshot.py` validates stable map snapshot digest (`snapshot.maps.digest`)
- `tools/replay_determinism_smoke.py` validates replay output determinism and smoke digest (`smoke.replay.determinism`)

## Initial Command Contract

- Configure/build tests (expected path once tests are wired into `mini-openbwapi`):
  - `cmake -S mini-openbwapi -B build-tests -G Ninja -DCMAKE_BUILD_TYPE=Release`
  - `cmake --build build-tests -j 8`
- Run all tests:
  - `ctest --test-dir build-tests --output-on-failure`
- Run one test:
  - `ctest --test-dir build-tests -R "<test-name-regex>" --output-on-failure`

## Single-Test Workflow (TDD)

Recommended test naming prefixes:

- `contract.*` for version/error-policy checks
- `snapshot.*` for semantic digest checks
- `smoke.*` for replay determinism checks

Fast development loop:

1. list tests:
   - `ctest --test-dir build-tests -N`
2. run one test:
   - `ctest --test-dir build-tests -R "^contract\.missing_scr_pack$" --output-on-failure`
3. run one layer:
   - `ctest --test-dir build-tests -R "^contract\." --output-on-failure`
4. run all suite layers before handoff:
   - `ctest --test-dir build-tests --output-on-failure`

Execution policy:

- During implementation: run only impacted tests first.
- Before merging: run all three layers.
- For deterministic failures: rerun the same test twice before triage to confirm reproducibility.

## Done Criteria for This Decision

- Harness architecture is explicitly selected (Option A).
- Comparison and rationale are documented.
- Downstream test tasks can implement against this contract.
