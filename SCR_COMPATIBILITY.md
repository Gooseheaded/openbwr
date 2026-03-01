# SCR Tile Compatibility

This repository now includes phase-1 support for Remastered-era tile semantics with a strict fail-fast policy.

## Scope

- Priority is deterministic simulation correctness.
- Visual parity is secondary.
- Unsupported or ambiguous semantic data must fail clearly, not silently.

## Semantic Pack Selection

At map load time, OpenBW selects semantic data by CHK `VER` policy:

- classic (`59`, `63`, `205`) -> classic tileset semantics
- remastered (`64`, `>=206`) -> SCR semantic pack

Heuristic override:

- if `ERA` + `MTXM` indicates new-tile usage (tile group index exceeds classic max for the tileset), OpenBW upgrades effective handling to SCR semantics even when `VER` is classic.

If a remastered map is loaded without an SCR semantic pack, OpenBW hard-fails with a clear error.

## SCR Semantic Pack Sources

OpenBW searches for SCR semantic data in this order:

1. `OPENBW_SCR_TILESET_DIR` environment variable
2. `./tileset_data`

Required files are `*.cv5` and `*.vf4` for all 8 tilesets.

## Runtime Hard-Error Rules

OpenBW now hard-fails on semantic correctness violations including:

- unsupported map versions
- remastered map without SCR semantic pack
- out-of-range tile group index during `MTXM` processing
- out-of-range megatile index during `MTXM` processing

## Diagnostics

Map loading emits concise diagnostics for:

- selected semantic pack (`classic` or `scr`)
- tileset loader entry counts (`cv5` and `vf4`)
- `MTXM` max group index observed

## Classifier and Fixture Tooling

Utilities added for inventory and validation:

- `openbw_asset_classify` (`build-tests/openbw_asset_classify.exe`)
- `tools/scan-openbw-assets.ps1`
- `tools/promote_scan_fixtures.py`
- `tools/validate_scr_fixture_inventory.py`
- `tools/validate_map_snapshot.py`
- `tools/replay_determinism_smoke.py`

## Test Gate

The CTest suite includes:

- contract tests (`contract.*`)
- negative classifier tests (`classifier.negative.*`)
- manifest checks (`classifier.fixtures.validate`)
- map snapshot digest (`snapshot.maps.digest`)
- replay determinism smoke (`smoke.replay.determinism`)
