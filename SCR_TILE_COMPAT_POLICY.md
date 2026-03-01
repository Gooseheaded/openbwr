# SCR Tile Forward-Compatibility Policy

## Scope

This policy defines map-version handling and semantic-pack requirements for deterministic simulation compatibility with Remastered-era tiles.

Priority order:

1. Deterministic simulation correctness
2. Correct tile semantics (pathing/placement/elevation/creep)
3. Rendering stability (visual parity can follow later)

## Version Acceptance Matrix (`VER `)

Policy mode: strict, fail-fast.

| Map `VER` value | Mode | Required semantic pack | Behavior |
|---|---|---|---|
| `59`, `63`, `205` | Classic | Classic tileset semantics | Accept |
| `64` | SCR-era compatibility | SCR semantic pack | Accept if pack present, else hard error |
| `>= 206` | SCR-era compatibility | SCR semantic pack | Accept if pack present, else hard error |
| Any other value | Unsupported | N/A | Hard error |

Notes:

- The `64` and `>=206` rules are based on mapping-community guidance gathered for this effort.
- This matrix can be expanded later, but should not silently broaden at runtime.

## Hard-Error Contract

No silent tile fallback is permitted in simulation paths.

Must hard-fail on:

- SCR-era map with missing SCR semantic pack
- Tile group index out of range for loaded `CV5`
- Megatile index out of range for loaded `VF4`
- Unsupported `VER` value

Error messages must include:

- map version
- tileset id/name
- failing index and valid range
- source context (`MTXM`, `CV5`, `VF4`, pack mode)

## SCR Semantic-Pack Contract

Phase 1 requires semantic assets only:

- `*.cv5` and `*.vf4` for all 8 tilesets

Expected logical tileset set:

- badlands
- platform (space)
- install
- ashworld
- jungle
- desert
- ice
- twilight

Required validations at load/startup:

1. All 8 tilesets are present for both formats.
2. `CV5` file size is a multiple of `52` bytes.
3. `VF4` file size is a multiple of `32` bytes.
4. Every megatile index referenced by every `CV5` entry is `< vf4_entry_count`.

Current extracted dataset in this repo (`tileset_data`) satisfies rule 4 based on pre-checks performed during planning.

## Logging Contract

At map load, log:

- selected mode (`classic` or `scr`)
- map version and tileset
- semantic-pack location and entry counts
- max tile group index observed in `MTXM`

On failure, emit one explicit fatal error with enough context to reproduce.

## Test-Driven Development Contract

Any implementation change for this feature must start with failing tests for:

1. version acceptance/rejection behavior
2. missing semantic-pack hard error
3. out-of-range tile index hard errors

Then implementation may proceed, followed by semantic snapshot and determinism smoke tests.
