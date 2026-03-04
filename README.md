# OpenBW

Instructions for baseline OpenBW + BWAPI usage are at https://github.com/OpenBW/bwapi.

## Fork Delta (This Repository)

This fork adds forward-compatibility work for StarCraft: Remastered map/replay content,
with deterministic simulation correctness as the primary goal.

Implemented deltas vs upstream baseline include:

- Remastered tile semantic support in runtime map loading (`CV5`/`VF4` based).
- Effective map classification that considers both CHK `VER` and `ERA`+`MTXM` tile usage.
- Fail-fast validation for unsupported/out-of-range tile semantics.
- Optional SCR semantic-pack selection at runtime (classic vs SCR data paths).
- Replay/map classifier tooling (`openbw_asset_classify`) and fixture utilities.
- `gfxtest` executable target with `--replay` argument for direct replay playback tests.

## Assumptions and Requirements

### 1) Data Files (MPQs)

Runtime expects classic data MPQs available via BWAPI-compatible paths
(for example `..\bwapi\mpq\Patch_rt.mpq`, `BrooDat.mpq`, `StarDat.mpq`).

### 2) SCR Semantic Pack (Required for remastered-effective maps)

For maps classified as remastered-effective (including classic `VER` maps that use new tiles),
OpenBW requires extracted SCR semantic files (`*.cv5` and `*.vf4` for all 8 tilesets).

Lookup order:

1. `OPENBW_SCR_TILESET_DIR`
2. `./tileset_data`

If required semantic data is missing, map loading hard-fails with contextual errors.

### 3) Replay Classifier zlib Dependency

`openbw_asset_classify` performs native SCR replay parsing for many modern replays.
For compressed SCR replay blocks, a zlib runtime library must be discoverable (PATH).
If unavailable, the tool falls back to identifier-based classification when possible.

### 4) Local Fixture Data

Fixture-heavy tests are local-only and intentionally excluded from git.
Enable fixture tests explicitly with:

```powershell
cmake -S mini-openbwapi -B build-tests -G Ninja -DCMAKE_BUILD_TYPE=Release -DOPENBW_ENABLE_LOCAL_FIXTURES=ON
ctest --test-dir build-tests --output-on-failure
```

## Quick Commands

Build UI and run replay smoke:

```powershell
cmake -S ui -B build-ui -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-ui -j 8
.\build-ui\gfxtest.exe --replay "<replay-path>" --bwapi-dir "..\bwapi"
```

Classify one asset:

```powershell
.\build-tests\openbw_asset_classify.exe "<map-or-replay-path>" --json
```

## Notes

- This fork does not ship copyrighted game assets.
- Keep extracted MPQs/replays/tilesets local.
