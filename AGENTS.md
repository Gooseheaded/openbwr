# AGENTS.md

## Purpose
Guidance for coding agents in `openbw`.
Prefer small, reviewable, deterministic changes.
Simulation correctness is higher priority than visual fidelity.

## Additional Rule Files Checked
Checked for extra instructions in:
- `.cursor/rules/`
- `.cursorrules`
- `.github/copilot-instructions.md`
Result at time of writing: none found.
If they appear later, treat them as authoritative.

## Required Workflow (bd + git)
Use `bd` and `git` constantly, often in tandem.
Issue tracking and code changes should move together.

Minimum loop per task:
1. `bd ready` (or `bd list`) to select work
2. `git status -sb` to inspect workspace state
3. implement the smallest useful change
4. `git diff` to verify exact scope
5. `bd update <id> --notes "..."`
6. run relevant build/test command(s)
7. `bd sync` and final `git status -sb`

If beads is not initialized, run `bd init`.
Useful commands: `bd ready`, `bd show <id>`, `bd update <id> ...`, `bd close <id>`, `bd sync`, `bd prime`.

## Repository Snapshot
- C++14 codebase, header-heavy.
- Main CMake entry points:
  - `mini-openbwapi/CMakeLists.txt`
  - `ui/CMakeLists.txt`
- Helper scripts:
  - `build.ps1`
  - `run.ps1`
- No top-level `CMakeLists.txt` in repo root.

## Build Commands

### Preferred integration path (via sibling BWAPI repo)
```powershell
pwsh ./build.ps1 -BwapiDir ..\bwapi -Configuration Release -EnableUi 0 -Jobs 8
pwsh ./build.ps1 -BwapiDir ..\bwapi -Configuration Release -EnableUi 1 -Jobs 8
pwsh ./run.ps1 -BwapiDir ..\bwapi
```

### Local mini-openbwapi build
```powershell
cmake -S mini-openbwapi -B build-mini -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-mini -j 8
```

Enable UI code in this path:
```powershell
cmake -S mini-openbwapi -B build-mini-ui -G Ninja -DCMAKE_BUILD_TYPE=Release -DOPENBW_ENABLE_UI=ON
cmake --build build-mini-ui -j 8
```

### UI build (includes `gfxtest`)
```powershell
cmake -S ui -B build-ui -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-ui -j 8
```

Replay smoke run:
```powershell
.\build-ui\gfxtest.exe --replay "<replay-path>" --bwapi-dir "..\bwapi"
```

## Lint / Formatting
No dedicated lint target and no repo `.clang-format`.
Use targeted compile checks and keep style consistent with surrounding code.
Avoid repository-wide formatting churn.

Optional warning-heavy build:
```powershell
cmake -S mini-openbwapi -B build-mini-warn -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-Wall -Wextra -Wpedantic"
cmake --build build-mini-warn -j 8
```

## Test Commands (Single-Test Focus)
Default contract suite:
```powershell
cmake -S mini-openbwapi -B build-tests -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-tests -j 8
ctest --test-dir build-tests --output-on-failure
```

Run one test:
```powershell
ctest --test-dir build-tests -R "^contract\.version\.classic$" --output-on-failure
```

Run one group:
```powershell
ctest --test-dir build-tests -R "^contract\." --output-on-failure
```

Fixture-data tests are local-only and disabled by default.
Enable explicitly:
```powershell
cmake -S mini-openbwapi -B build-tests -G Ninja -DCMAKE_BUILD_TYPE=Release -DOPENBW_ENABLE_LOCAL_FIXTURES=ON
ctest --test-dir build-tests --output-on-failure
```

## Code Style Guidelines

### Language and Compatibility
- Keep code C++14-compatible.
- Favor deterministic logic in simulation-critical paths.

### Formatting
- Match local file style (tabs are common).
- Keep brace style consistent with nearby code.
- Avoid unrelated whitespace-only edits.

### Includes
- Headers: project headers first, then standard headers.
- CPP files: matching header first, then local/project, then std.
- Prefer explicit includes over transitive assumptions.

### Naming and Types
- `snake_case` for functions, locals, and members.
- `_t` suffix is common for internal types (`unit_type_t`, `vf4_entry`).
- Use fixed-width integers for binary parsing paths.
- Use `size_t` for counts and indices.
- Use project aliases where already adopted (`a_vector`, `a_string`, `a_map`, etc.).

### Parsing and Error Handling
- Validate external data before use.
- Bounds-check all map/replay-derived indices.
- Use fail-fast `bwgame::error(...)` with contextual messages.
- Do not silently coerce unknown semantic data.

### Logging
- Keep diagnostics actionable and concise.
- Log both acceptance and rejection reasons for compatibility branches.
- Avoid noisy per-frame logging unless explicitly gated.

## Scope and Hygiene
- Keep patches focused and minimal.
- Avoid editing vendored code under `deps/` unless requested.
- Do not commit generated outputs (`build-*`, scan artifacts).
- Do not commit copyrighted/local game assets (MPQs, replay corpora, extracted tilesets).

## Handoff Checklist
1. `bd show <id>` reflects latest status
2. `git status -sb` understood
3. `git diff` reviewed for scope
4. relevant build/tests executed
5. `bd sync`
6. final `git status -sb`

Use `bd` and `git` together throughout the task lifecycle.
