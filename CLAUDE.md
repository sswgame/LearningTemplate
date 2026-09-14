# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Pending work lives in docs/06_Backlog.md

**Read [docs/06_Backlog.md](docs/06_Backlog.md) before starting work.** It is the shared to-do list
across machines and sessions: what is left, why it is ordered that way, the traps to know before
touching it, and what was recently finished (so the same ground is not re-covered). Update it in the
same commit as the work it describes.

## Conventions live in AGENTS.md

**Read [AGENTS.md](AGENTS.md) before writing any C++, CMake, or Python in this repo.** It is the
authoritative rule set for naming (`_camelCase` members, `p`/`pp` pointer prefixes, `list`/`map`/`arr`/`unique`
container prefixes with **singular** names, `out`/`pOut` parameter prefixes), function-name vocabulary
(acronyms are camelCase words; one verb per concept; predicates read as questions), include ordering,
header declaration order, constructor initialization, and branch style.
`docs/04_CodingGuidelines.md` is the Korean expansion of the same rules with extra examples.
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
build/Ninja-Debug/Bin/EngineTest.exe --test_filter=SceneTest.*      # one suite
build/Ninja-Debug/Bin/EngineTest.exe --test_filter=-RHIDeviceTest.* # leading '-' excludes
build/Ninja-Debug/Bin/EngineTest.exe --test_list                   # enumerate cases
```

- Executables: `CoreTest`, `EngineTest`, `ReflectionTest`, `SmokeTest`, `EditorTest`.
  **Always run them with `build/<preset>/Bin` as the working directory** — they walk up from the current
  directory to find `Resource/`, and `Bin` is where that walk succeeds. This is what CTest does.
  In Shipping the binaries themselves live in `build/Ninja-Shipping/TestBin` (so the shipped `Bin` stays
  free of test binaries and DXC), but the working directory is still `Bin`:
  `cd build/Ninja-Shipping/Bin && ../TestBin/EngineTest.exe`. Running them from `TestBin` used to
  segfault mid-run; the resource asserts now fail the affected cases loudly instead.
- CTest names are the target names plus `EngineTest_NoGPU`, which is `EngineTest` with the suites CI
  cannot run filtered out (`RHIDeviceTest`, `RenderPassGpuTest`, `WindowTest`, `ShaderCompilerTest`,
  `LiveShaderTest`). **A test that creates an RHI device belongs in `RenderPassGpuTest`.**
- **Suite names are a convention, and `CheckTestSuites.py` enforces it**: every suite is `XxxTest`
  (no underscore), lives in exactly one file, and a suite CI cannot run declares
  `// SW_TEST_REQUIRES_HOST( SuiteName ): <reason>` in its file — the lint cross-checks those markers
  against the `EngineTest_NoGPU` filter in both directions, and keeps such suites in a file of their own.
- Labels: `nogpu` (CI-safe), `lint`, `unit`, `core`, `engine`, `editor`, `module`, `reflection`.
- Cases are declared with `SW_TEST_CASE(Suite, Name)` and assert via `SW_EXPECT_*` / `SW_ASSERT_*`.

## Linting

`Scripts/lint/**/*.py` enforce the conventions; the same scripts run as `lint`-labelled CTest tests and
as the git pre-commit hook (`Scripts/setup/InstallGitHooks.py` installs it, `PreCommitLint.py` runs it
over staged files only). **The folder says what a script does to you** — that is the whole taxonomy:

| folder | does | exit code |
|--------|------|-----------|
| `lint/gate/` | fails the build and blocks the commit | non-zero on any violation |
| `lint/fixer/` | rewrites your files | 0 (or non-zero under `--check`) |
| `lint/report/` | prints, you decide | always 0 |
| `lint/selftest/` | checks the **lints**, not the code | non-zero if a lint went blind |

`PreCommitLint.py` stays at `lint/` because it orchestrates all four; `LintGate.py` stays there because
every gate inherits from it.

**Adding a gate is dropping a file into `lint/gate/`.** A gate is one `LintGate` subclass that implements
`scan(repositoryRoot, args) -> GateResult`; the base owns `--root`, UTF-8 output, violation printing and
the exit code (`0` clean, `1` violations, `2` raise `GateError` — the check could not run), and the module
exports it as `main = XxxGate.run`. `CheckLintsAreAlive.py` enumerates the folder, so a new gate is picked
up with no list to edit — and it must carry a `selfTestCases` snippet proving it still catches something
(or a `selfTestSkipReason` saying why it cannot), or the self-test fails.

```powershell
py -3 Scripts/lint/gate/CheckCodeConventions.py                # naming/style rules (CI gate)
py -3 Scripts/lint/gate/CheckCodeConventions.py --files <path> # single file
py -3 Scripts/lint/gate/CheckIncludeOrder.py                   # check only; `--fix` to rewrite
py -3 Scripts/lint/gate/CheckEngineLayers.py                   # Engine must not include Editor/GameFramework/Games
py -3 Scripts/lint/gate/CheckResourceCasing.py                 # everything under Resource/ must be lowercase
py -3 Scripts/lint/gate/CheckFunctionVocabulary.py            # one verb per concept; acronyms are camelCase words
py -3 Scripts/lint/fixer/FormatBranchBraces.py --check         # if/case 중괄호 규칙 검사
py -3 Scripts/lint/fixer/FormatModified.py                     # clang-format the working-tree changes
py -3 Scripts/lint/report/RunBuildWarnings.py                  # compiler warnings still in the tree
py -3 Scripts/lint/report/RunClangTidy.py                      # static analysis
py -3 Scripts/lint/selftest/CheckLintsAreAlive.py              # do the gates still bite? (CI gate)
py -3 Scripts/lint/selftest/CheckCodeConventionsSelfTest.py    # do its 30 rules still bite? (CI gate)
```

- **Grepping a build for `warning:` does not work.** A warning is printed only when that TU is compiled,
  and ninja never recompiles unchanged files — so an existing warning is invisible on every build after
  the one that introduced it. `RunBuildWarnings.py` re-asks the question over the whole tree
  (`-fsyntax-only`, real build flags from the compile DB) in ~1.5 min per preset, and defaults to
  sweeping Debug · Release · Shipping because **the warning set differs per configuration**.
  The build you just ran already reports warnings your own change introduced (it recompiled exactly the
  affected TUs); this answers the other question — what is left in the tree. Run it when finishing a
  chunk of work, not on every edit.

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
