# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Conventions live in AGENTS.md

**Read [AGENTS.md](AGENTS.md) before writing any C++, CMake, or Python in this repo.** It is the
authoritative rule set for naming (`_camelCase` members, `p`/`pp` pointer prefixes, `list`/`map`/`arr`/`unique`
container prefixes with **singular** names, `out`/`pOut` parameter prefixes), include ordering, header
declaration order, constructor initialization, and branch style. `.cursorrules` / `GEMINI.md` are Korean
translations of the same rules with extra examples; `docs/04_CodingGuidelines.md` expands on them.
The rules are machine-enforced — see Linting below.

Documentation and code comments in this repo are written in Korean (`/** @brief */` above declarations,
`/**<` beside member fields). Log and assert strings are never translated.

## Build

```powershell
py -3 Scripts/setup/SetupEnvironment.py       # toolchain (LLVM/Ninja/sccache) bootstrap
py -3 Scripts/setup/SetupVcpkg.py --install   # vcpkg manifest restore
cmake --preset Ninja-Debug
cmake --build --preset Ninja-Debug
```

- Presets: `Ninja-Debug`, `Ninja-Debug-ASAN`, `Ninja-Release`, `Ninja-Shipping` (Windows clang-cl),
  `WSL-*` (Linux clang), `CI-*` (used by `.github/workflows/ci.yml`).
- Outputs: `build/<preset>/Bin`. Compile DB: `build/<preset>/compile_commands.json` (`.clangd` points at `Ninja-Debug`).
- Key cache options (all `SW_*`, declared in `cmake/Config/BuildOptions.cmake`): `SW_SHIPPING_BUILD`,
  `SW_ACTIVE_GAME` (which `Source/Games/<name>` builds as `SWGame`), `SW_RHI_AS_MODULES`,
  `SW_REQUIRE_REFLECTION`, `SW_ENABLE_PCH`, `SW_USE_SCCACHE`.

## Test

Tests are a hand-rolled framework (`Test/TestFramework`), not gtest, but accept gtest-style flags:

```powershell
ctest --test-dir build/Ninja-Debug -L nogpu --output-on-failure   # CI-equivalent, no GPU needed
ctest --preset Ninja-Debug-lint                                   # lint tests only
build/Ninja-Debug/Bin/EngineTest.exe --test_filter=SceneTest.*     # one suite
build/Ninja-Debug/Bin/EngineTest.exe --test_filter=-RHITest.*      # leading '-' excludes
build/Ninja-Debug/Bin/EngineTest.exe --test_list                   # enumerate cases
```

- Executables: `CoreTest`, `EngineTest`, `ReflectionTest`, `SmokeTest`, `EditorTest`. Run them from
  `build/<preset>/Bin` (working directory matters — they resolve `Resource/` relative to it).
- CTest names are the target names plus `EngineTest_NoGPU`, which is `EngineTest` with RHI/Window/Shader
  suites filtered out. Labels: `nogpu` (CI-safe), `lint`, `unit`, `engine`.
- Cases are declared with `SW_TEST_CASE(Suite, Name)` and assert via `SW_EXPECT_*` / `SW_ASSERT_*`.

## Linting

`Scripts/lint/*.py` enforce the conventions; the same scripts run as `lint`-labelled CTest tests and as
the git pre-commit hook (`Scripts/setup/InstallGitHooks.py` installs it, `PreCommitLint.py` runs it over
staged files only).

```powershell
py -3 Scripts/lint/CheckCodeConventions.py                 # naming/style rules (CI gate)
py -3 Scripts/lint/CheckCodeConventions.py --files <path>  # single file
py -3 Scripts/lint/CheckIncludeOrder.py
py -3 Scripts/lint/CheckEngineLayers.py                    # Engine must not include Editor/GameFramework/Games
py -3 Scripts/lint/CheckResourceCasing.py                  # everything under Resource/ must be lowercase
py -3 Scripts/lint/FormatModified.py                       # clang-format the working-tree changes
```

## Architecture

`ARCHITECTURE.md` is the long-form guide; the essentials:

**Target graph.** `App.exe` is a thin launcher (`EngineLoop` + `ModuleHost`) that links only `Engine` and
`RuntimeAPI` — it has no compile-time knowledge of game or editor classes. `Core` (static, foundation:
log/memory/string/file/task/compression) is compiled as an OBJECT library that `Engine` absorbs and
re-exports. In **Dev**, `Engine` is a DLL and `EditorModule` / `SWGame` / `GF_*` kits / `RHI_*` backends
are dynamically loaded MODULEs supporting hot reload; in **Shipping** the editor is dropped and everything
links statically into one exe.

**The C-ABI boundary.** `Source/RuntimeAPI` is header-only `INTERFACE` — a pure `extern "C"` contract, never
implementations. Everything crossing App ↔ module goes through it. Export macros are distinct and not
interchangeable: `SW_API` (Engine.dll symbols), `SW_MODULE_API` (C-ABI entry points of any loadable plugin),
`SW_GF_API` (GameFramework.dll classes), `SW_GAMESERVICE_API` (the RuntimeAPI GameService locator only).

**Engine internal layers.** `Source/Engine` is one link unit but include direction is one-way and linted:
Utility → Reflection → Object/Scene/Serialization → Graphics → Input/Window/Audio/Physics/Animation.
Engine code must never include `Editor/`, `GameFramework/`, or `Games/`; reach the editor through
RuntimeAPI, delegates, or events instead. See `Source/Engine/README.md`.

**Reflection codegen.** `REFLECT` / `PROPERTY` / `FUNCTION` / `ENUM` macros in headers are parsed by
`Tools/ReflectionParser` (libclang) into `build/<preset>/generated/**/*.gen.cpp`, driven by
`sw_addReflectionStep` in `cmake/Engine/ReflectionCodeGen.cmake`. Scene loading, the inspector, hot
reload, and `addComponentByName` all depend on the generated `TypeInfo`/registrars, so `ReflectionParser`
must build before Engine. ReflectionParser links `Core` only, to avoid a cycle with Engine.dll.

**Resources.** `Resource/` splits into `engine/`, `common/`, and `game/<active game>/`. Paths are global ids
including the domain (`engine/pipeline/forward.xml`) and are lowercased via `normalizePath` at lookup —
hence the enforced lowercase rule. Rendering separates `RenderPassResource` (bind template: formats/clears,
under `renderpass/`) from `RenderPipelineResource` (the frame graph ordering passes, under `pipeline/`),
which `RenderGraph` topologically sorts at runtime.

## Gotchas

- **Never re-parent during tick.** `GameObjectManager::tick` runs `onTick()` across threads;
  `attachToParent`/`detach` on any object inside it is forbidden. Structural changes (`addComponent`,
  `addTag`) auto-defer via `deferPostTick`, so `addComponent` returns `nullptr` mid-tick — use
  `GameObjectManager::executeOrDeferPostTick` to spawn and initialize in one block.
- **RHI ABI stamps.** Changing `RHIModuleAbi.h` requires rebuilding the engine and *all* `RHI_*.dll`
  backends together; a stale backend DLL crashes immediately on mismatched function pointers.
- **Statics die on hot reload.** Class statics and singletons living in a reloadable module vanish or move
  when the DLL is swapped. State that must survive belongs in `Engine` or `App`.
