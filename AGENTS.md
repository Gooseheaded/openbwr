# AGENTS.md

## Purpose

This file guides coding agents working in the `openbw` repository.
Prefer small, deterministic, reviewable changes.
Preserve gameplay determinism above all else.

## Instruction Sources Checked

I checked for additional agent instructions in these locations:

- `.cursor/rules/`
- `.cursorrules`
- `.github/copilot-instructions.md`

Result: none were found in this repository at the time of writing.
If any of these files are later added, treat them as authoritative and merge their guidance into this document.

## Core Workflow Requirement (Important)

Use `bd` and `git` constantly, and often in tandem.
Treat issue state and code state as coupled, not separate.

Minimum rhythm for every task:

1. `bd ready` (or `bd list`) to select work
2. `git status -sb` to confirm working tree
3. implement smallest useful change
4. `git diff` to review exactly what changed
5. `bd update <id> ...` to record status/context
6. run relevant build/test command(s)
7. `bd sync` and `git status -sb` before handoff/commit

If `bd` is not initialized in this repo yet:

- `bd init`
- then continue workflow above

Useful `bd` commands:

- `bd ready`
- `bd show <id>`
- `bd update <id> --notes "..."`
- `bd close <id>`
- `bd sync`
- `bd prime` (workflow context)

## Repository Snapshot

Primary code is header-heavy C++ with two CMake entry points:

- `mini-openbwapi/CMakeLists.txt`
- `ui/CMakeLists.txt`

Top-level helper scripts:

- `build.ps1` (builds through sibling `bwapi` repo scripts)
- `run.ps1` (runs via sibling `bwapi` launcher)

No top-level `CMakeLists.txt` exists in this repo root.

## Build Commands

### A) Preferred integration build (via sibling BWAPI repo)

From repo root (`openbw`):

```powershell
pwsh ./build.ps1 -BwapiDir ..\bwapi -Configuration Release -EnableUi 0 -Jobs 8
pwsh ./build.ps1 -BwapiDir ..\bwapi -Configuration Release -EnableUi 1 -Jobs 8
```

Notes:

- `build.ps1` expects `..\bwapi\configure.ps1` and `..\bwapi\build.ps1`.
- It builds BWAPI targets (`BWAPILauncher`, `ExampleAIModule`) that include OpenBW components.

Run launcher path:

```powershell
pwsh ./run.ps1 -BwapiDir ..\bwapi
```

### B) Direct local build for `mini-openbwapi` library

```powershell
cmake -S mini-openbwapi -B build-mini -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-mini -j 8
cmake --build build-mini --target mini-openbwapi
```

Enable UI in this mode:

```powershell
cmake -S mini-openbwapi -B build-mini-ui -G Ninja -DCMAKE_BUILD_TYPE=Release -DOPENBW_ENABLE_UI=ON
cmake --build build-mini-ui -j 8
```

### C) Build UI library directly

```powershell
cmake -S ui -B build-ui -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-ui -j 8
```

## Lint / Format Commands

This repo currently has no dedicated lint target and no checked-in formatter config (`.clang-format` not found).

Use these practical checks:

1. clean rebuild of touched target(s)
2. compile with warnings enabled when needed
3. keep edits style-consistent with neighboring code (no bulk reformat)

Optional warning-heavy configure example:

```powershell
cmake -S mini-openbwapi -B build-mini-warn -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-Wall -Wextra -Wpedantic"
cmake --build build-mini-warn -j 8
```

Do not introduce repository-wide formatting churn.

## Test Commands (and Single-Test Guidance)

Current state in this repository:

- No first-party CMake test suite was found (`add_test(...)` not present).
- `ctest` exists in toolchain, but this repo does not define test targets itself.

Practical validation commands:

```powershell
cmake --build build-mini --target mini-openbwapi
pwsh ./run.ps1 -BwapiDir ..\bwapi
```

If using BWAPI-side test harnesses (when available in sibling repo):

- run all tests:
```powershell
ctest --test-dir <bwapi-build-dir> --output-on-failure
```
- run a single test by name regex:
```powershell
ctest --test-dir <bwapi-build-dir> -R "<single-test-regex>" --output-on-failure
```

For this repo alone, "single test" usually means a targeted smoke scenario (specific replay/map path) rather than a unit test target.

## Code Style Guidelines

### Language / Standard

- C++14 is the configured standard (`CMAKE_CXX_STANDARD 14`).
- Keep new code C++14-compatible unless build config is intentionally upgraded.

### Formatting

- Use tabs for indentation (match existing files).
- Brace style: opening brace on same line for functions/blocks.
- Keep style local: copy the surrounding file's formatting patterns.
- Avoid unrelated whitespace-only diffs.

### Includes / Imports

- In headers: include project headers first, then standard headers.
- In cpp files: include corresponding header first, then local/project, then std.
- Prefer explicit includes over transitive assumptions.

### Naming

- `snake_case` for functions, local variables, and most members.
- many internal types use `_t` suffix (for example `unit_type_t`, `vf4_entry`).
- constants commonly use `static const` and `std::array`.
- bit flags often use `flag_*` naming.
- preserve existing public API names in `OpenBWAPI` namespace.

### Types

- Use fixed-width integer types for binary/data-format code (`uint8_t`, `uint16_t`, etc.).
- Use `size_t` for counts/indices.
- Prefer project container aliases where already used:
  - `a_vector`, `a_string`, `a_map`, `a_unordered_map`, etc.
- Preserve deterministic numeric behavior; avoid accidental signed/unsigned mixing.

### Data Parsing / Bounds

- Validate all externally loaded data before use.
- When indexing data derived from files/replays/maps, bounds-check first.
- Follow existing defensive parse patterns in map/replay loaders.

### Error Handling

- Project convention is fail-fast using `bwgame::error(...)`.
- `error(...)` throws `bwgame::exception` (see `util.h`).
- Include concrete context in error messages:
  - filename/chunk/tag/index/value
- Prefer explicit hard errors over silent fallback for semantic correctness paths.

### Logging / Diagnostics

- Keep logs actionable and specific.
- For compatibility work, log both:
  - what was accepted
  - why something was rejected
- Avoid noisy per-frame logging in hot loops unless gated.

## Determinism and Simulation Safety Rules

- Deterministic simulation is higher priority than rendering fidelity.
- Do not add nondeterministic behavior (time-based, unordered iteration dependence, random seeding drift).
- Keep replay/map parsing behavior explicit and version-gated.
- Never silently coerce unknown tile/semantic data in core simulation paths.

## Scope and Change Hygiene

- Make minimal, focused patches.
- Do not modify vendored third-party code in `deps/` unless explicitly requested.
- Avoid committing generated build outputs (`build-mini/`, temporary artifacts) unless explicitly requested.
- Keep untracked local asset folders out of commits unless required (for example `tileset_data/`).

## Git + bd Tandem Checklist Before Handoff

1. `bd show <id>` reflects current status/notes
2. `git status -sb` is understood
3. `git diff` reviewed for scope correctness
4. relevant build/validation command(s) run
5. `bd sync`
6. final `git status -sb`

This tandem loop is mandatory: use `bd` and `git` constantly, and often together.
