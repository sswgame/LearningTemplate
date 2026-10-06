# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Pending work lives in docs/06_Backlog.md

**Read [docs/06_Backlog.md](docs/06_Backlog.md) before starting work.** It is the shared to-do list
across machines and sessions and holds only two things: the work that is still open (with the traps to
know before touching it) and a reference section of lasting lessons from finished work. Update it in the
same commit as the work it describes: when an item is done, delete it and move any lesson worth keeping
into the reference section in a line or two. History lives in `git log`; the full old backlog with every
dated "recently finished" entry is `git show 7ce95fc8:docs/06_Backlog.md`.

## Conventions live in AGENTS.md

**Read [AGENTS.md](AGENTS.md) before writing any C++, CMake, or Python in this repo.** It is the
authoritative rule set for naming (`_camelCase` members, `p`/`pp` pointer prefixes, `list`/`map`/`arr`/`unique`
container prefixes with **singular** names, `out`/`pOut` parameter prefixes), function-name vocabulary
(acronyms are camelCase words; one verb per concept; predicates read as questions), include ordering,
header declaration order, constructor initialization, and branch style.
`docs/04_CodingGuidelines.md` is a Korean collection of examples per AGENTS.md section; it does not restate the rules
(a new rule goes into AGENTS.md first, an example into docs/04 in the same commit).
The rules are machine-enforced — see Linting below.

Documentation and code comments in this repo are written in Korean (`/** @brief */` above declarations,
`/**<` beside member fields). Log and assert strings are never translated.

## Documentation map

`docs/02_DocumentMap.md` says which document owns which fact and lists every README. **One fact lives in one place**;
other documents link to it. A module README holds that folder's contracts, traps and open work — usage lives in the
header comments (`/** @brief */`), not in a README. Write docs in Korean and in the present tense; how something came
to be goes in the commit message, and a past defect is written as a present-tense caution.
**How to write Korean docs is `docs/10_WritingDocs.md`** — the four document kinds, the module README shape, sentence
rules, and the term table (keep established loanwords such as 빌드 · 버전 · 슬롯 · 레지스트리; never coin native words).
Comments and commit messages use the same term table; reword a comment when you touch its function, never by word replacement.
`Scripts/lint/gate/CheckDocPaths.py` checks every relative link, heading anchor and backticked repository path, and that
every README is on the map. A placeholder path is written with angle brackets (`Source/Games/<Game>/`).

## Build

```powershell
py -3 Scripts/setup/SetupEnvironment.py       # toolchain (LLVM/Ninja/sccache) bootstrap
py -3 Scripts/setup/SetupVcpkg.py --install   # vcpkg manifest restore
cmake --preset Ninja-Debug
cmake --build --preset Ninja-Debug
```

- Presets: `Ninja-Debug`, `Ninja-Debug-ASAN`, `Ninja-Release`, `Ninja-Shipping` (Windows clang-cl),
  `WSL-*` (Linux clang), `CI-*` (used by `.github/workflows/ci.yml`).
  Each test game has its own Debug preset `Ninja-Debug-<Game>` (own build folder, `SW_ACTIVE_GAME=<Game>`) — switch games by preset,
  not by re-configuring one folder (two jobs sharing a build folder break each other's builds). `Ninja-Debug` is the Empty game.
  Dedicated-server presets `*-Server` (`Ninja-Debug-Server`, `Ninja-Shipping-Server`, `WSL-*-Server`, `CI-Shipping-Server`) build the
  Server target; the `*-Shipping` presets are the Client target.
- Outputs: `build/<preset>/Bin`. Compile DB: `build/<preset>/compile_commands.json` (`.clangd` points at `Ninja-Debug`).
- Key cache options (all `SW_*`, declared in `cmake/Config/BuildOptions.cmake`): `SW_SHIPPING_BUILD`,
  `SW_TARGET_TYPE` (`Game` · `Client` · `Server` — the Unreal TargetType slot; empty = Shipping→Client, else Game; code reads only
  `SW_WITH_CLIENT_CODE` / `SW_WITH_SERVER_CODE` / `sw::build::kTargetName`, and only inside `.cpp` bodies — split code goes into modules by `_listTarget`),
  `SW_ACTIVE_GAME` (which `Source/Games/<name>` builds as `SWGame`, and which game preset `Config/Game/<name>.json` — pack root,
  window title — the runtime reads; Shipping bakes that file in; the startup scene is the pack's `data/gamesettings.xml`),
  `SW_SHIPPING_RHI_BACKEND` (the one RHI backend Shipping links statically, a cook-table name exactly — `DirectX11` · `DirectX12` · `Vulkan` · `OpenGL`; anything else, including the `-dx12`-style aliases, stops the configure;
  a backend missing on that platform stops the configure; the backend table is `Config/Engine/CookContract.json`; Dev always loads every `RHI_*` module),
  `SW_REQUIRE_REFLECTION`, `SW_ENABLE_PCH`, `SW_USE_SCCACHE`.

## Test

Tests are a hand-rolled framework (`Test/TestFramework`), not gtest, but accept gtest-style flags:

```powershell
ctest --test-dir build/Ninja-Debug -L nogpu --output-on-failure   # CI-equivalent, no GPU needed
ctest --test-dir build/Ninja-Shipping -L hostgpu --output-on-failure  # what CI CANNOT run — run this before you finish
ctest --preset Ninja-Debug-lint                                   # lint tests only
build/Ninja-Debug/Bin/EngineTest.exe --test_filter=SceneTest.*      # one suite
build/Ninja-Debug/Bin/EngineTest.exe --test_filter=-RHIDeviceTest.* # leading '-' excludes
build/Ninja-Debug/Bin/EngineTest.exe --test_filter=InputManagerTest.*:WindowTest.*-WindowTest.Recreate*  # gtest form: ':' (or ',') joins, everything after the first '-' excludes
build/Ninja-Debug/Bin/EngineTest.exe --test_list                   # enumerate cases
py -3 -m Scripts test SceneTest.* [--preset Ninja-Shipping]         # by name: finds the exe (Bin/TestBin) and runs it from Bin
build/Ninja-Debug/Bin/CoreTest.exe --test_filter=ProcessTest.* --test_repeat=50  # flaky hunt; a failure names its iteration
build/Ninja-Debug/Bin/EngineTest.exe --test_shuffle                # order dependence; prints the seed, --test_shuffle=<seed> replays
build/Ninja-Debug/Bin/ReflectionTest.exe --test_shard=0/2          # one shard (also GTEST_SHARD_INDEX / GTEST_TOTAL_SHARDS)
```

- Executables: `CoreTest`, `EngineTest`, `ReflectionTest`, `SmokeTest`, `EditorTest`, `EditorUiTest`, `AppTest`, `ServerTest`.
  `ServerTest` launches the built `Server` (Game · Server targets) without a window or GPU and runs under `nogpu` on both platforms;
  the Server target builds no editor or App tests, and its presets run `-L nogpu` only (a server build has no GPU suite).
  **Always run them with `build/<preset>/Bin` as the working directory** — they walk up from the current
  directory to find `Resource/`, and `Bin` is where that walk succeeds. This is what CTest does, in every
  configuration (`sw_registerTestRun`). In Shipping the binaries themselves live in
  `build/Ninja-Shipping/TestBin` (so the shipped `Bin` stays free of test binaries and DXC), but the working
  directory is still `Bin`: `cd build/Ninja-Shipping/Bin && ../TestBin/EngineTest.exe`.
- **A suite CI cannot run declares it in code**: `SW_TEST_REQUIRES_HOST( SuiteName, "reason" );` in the
  suite's own file. That declaration is the whole classification — an executable registered with
  `sw_addTestExecutable( ... HOST_SPLIT )` gets two CTest entries, `<Target>_NoGPU` (`--host_suites=exclude`,
  label `nogpu`, what CI runs) and `<Target>_HostOnly` (`--host_suites=only`, label `hostgpu`). No suite name
  is written in CMake. Today `EngineTest` and `AppTest` split this way; the others are one entry each, except
  `ReflectionTest`, which is sharded (below). **A test that creates an RHI device belongs in `RenderPassGpuTest`.**
  A host-suite case fails on any unexpected `[Error]` log line, since validation-layer and driver errors only log;
  wrap a deliberate rejection in `SW_TEST_DEFENSIVE_SCOPE( "reason" )`.
- **A suite that needs an outside server declares it too**: `SW_TEST_REQUIRES_ENVIRONMENT( SuiteName, "SW_TEST_POSTGRES_URL", "reason" );`.
  With the variable unset the suite is not selected and prints one `[ SKIP SUITE ]` line (not a failure, not a host suite).
- **`-L hostgpu` is the part CI can never run. Run it in Shipping before you call work done**, on the
  machine with the GPU. Nothing else covers it: CI skips those suites and local habit is Debug-only, so a
  Shipping-only GPU failure otherwise sits in the tree unnoticed. A `--host_suites=only` run that selects nothing
  fails, and so does a declaration naming a suite that has no cases.
- **Suite names are a convention, and `CheckTestSuites.py` enforces it**: every suite is `XxxTest`
  (no underscore), lives in exactly one file, a host suite has its file to itself, and every
  `SW_TEST_REQUIRES_HOST` sits in a folder whose CMakeLists says `HOST_SPLIT` (otherwise nothing reads it and
  CI runs the suite). The same gate keeps `CoreTest` free of Engine headers and `engine::` calls (Engine tests go
  in `EngineTest`) and checks that the Editor sources `EditorTest` lists by hand exist and do not include ImGui.
- **A slow executable is split with `SHARDS <n>`** in `sw_addTestExecutable`: it registers `<Target>_Shard1..n` (with `HOST_SPLIT`,
  `<Target>_NoGPU_Shard1..n`; `HOST_SHARDS <m>` splits `_HostOnly` into `<Target>_HostOnly_Shard1..m`, still serial — `EngineTest` uses 3 and 2), each with
  `--test_shard=<k>/<n>`, and cases are dealt out **within each suite** so one slow suite is halved (`ReflectionTest` — its
  parser suite was ~21 s of a 30 s limit). A suite split across shards is not judged by the "every case skipped" check, so a
  suite whose cases skip when a prerequisite is missing keeps one case that asserts the prerequisite
  (`ReflectionParserTest.ParserExecutableIsBuilt`).
- Labels: `nogpu` (CI-safe), `hostgpu` (GPU/display/DXC — CI cannot), `lint`, `unit`, `core`, `engine`, `editor`, `app`, `server`, `module`, `reflection`.
- Cases are declared with `SW_TEST_CASE(Suite, Name)` and assert via `SW_EXPECT_*` / `SW_ASSERT_*`. To test a path
  that trips an engine assert (`SW_ASSERT` / `SW_LOG_ASSERT` break in Debug), hold a `test::ScopedAssertCapture`.

## Linting

`Scripts/lint/**/*.py` enforce the conventions; the same scripts run as `lint`-labelled CTest tests and
as the git pre-commit hook (`Scripts/setup/InstallGitHooks.py` installs it, `PreCommitLint.py` runs it
over staged files only). **The folder says what a script does to you** — that is the whole taxonomy:

| folder | does | exit code |
|--------|------|-----------|
| `lint/gate/` | fails the build and blocks the commit | non-zero on any violation |
| `lint/fixer/` | rewrites your files | 0 (or non-zero under `--check`) |
| `lint/report/` | prints, you decide (one `LintReport` subclass per script) | 0 (unless asked: `RunBuildWarnings.py --fail-on`, used by CI) |
| `lint/selftest/` | checks the **lints**, not the code | non-zero if a lint went blind |

`PreCommitLint.py` and `RunLintSuite.py` stay at `lint/` because they orchestrate all four; `LintGate.py`, `LintFixer.py` and `LintReport.py`
stay there because every gate, fixer and report inherits from them (a report owns `--root` · `--preset`/`--build-dir` · `--jobs` ·
`--filter` · `--out` through the base; `selftest/CheckReportsRun.py` checks every report still starts).

**Adding a gate is dropping a file into `lint/gate/`.** A gate is one `LintGate` subclass that implements
`scan(repositoryRoot, args) -> GateResult`; the base owns `--root`, UTF-8 output, violation printing and
the exit code (`0` clean, `1` violations, `2` raise `GateError` — the check could not run), and the module
exports it as `main = XxxGate.run`. `CheckLintsAreAlive.py` enumerates the folder, so a new gate is picked
up with no list to edit — and it must carry a `selfTestCases` snippet proving it still catches something
(or a `selfTestSkipReason` saying why it cannot), or the self-test fails.
A gate that takes `--files` picks its files with `addFilesArgument` / `selectTargetFiles` (same rule for the hook's
staged subset and the full scan; it never descends into `kNotOurDirNames` — build output and downloaded tools).

**CMake has no lint list either.** `Scripts/lint/LintCatalog.py` walks `gate/` and `selftest/`, and
`Scripts/generate/GenerateLintTargets.py` turns that into the `add_custom_target` / `sw_registerScriptTest` block CMake
`include()`s at configure time. What differs per lint travels with the lint: a gate — and a `selftest/` script, which is
a `LintGate` too — declares `buildComment` (the English line ninja prints), `timeoutSeconds` and `listCtestArgument` on its class. A
`CONFIGURE_DEPENDS` glob watches both folders, so dropping a file in there re-runs configure by itself.
**A new gate is one file** — no CMake edit, no path constant.

**The commit hook has no lint list either.** `PreCommitLint.py` walks `gate/` the same way, and each gate
says when it should run: `preCommitPattern` (fnmatch globs against staged repo-relative paths — empty
means always), `preCommitFileArgument` (`"--files"`, or `""` for whole-tree gates), and
`preCommitSkipReason` for a gate the hook cannot run (`CheckSourceGlob` needs a build directory). A commit
that touches only `.cmake`, `.py` or data still runs every gate whose pattern matches it.
A gate whose whole-tree run takes minutes sets `ctestSkipReason`: it is not registered as a CTest lint and runs
only in the commit hook (and directly) — the reason names the place that does run it over the whole tree
(`CheckHeaderSelfContained`: the daily `header-self-contained` CI workflow).
Whole-tree gates (`preCommitFileArgument = ""`) start first as child processes and run alongside the file-argument gates;
their output is printed in gate order, so the hook's floor is its slowest whole-tree gate — prefer `--files` for a new gate.
**A merge commit checks only new content file by file**: while `MERGE_HEAD` exists, the file-argument gates,
the fixers and clang-format get only the staged files whose blob differs from that path in *every* parent
(conflict resolutions, auto-merged files) — a file byte-identical to one parent was checked when that parent
was committed. Whole-tree gates still run. Cross-file relations between files taken from different parents
are what `ctest -L lint` (and CI) re-checks over the whole tree after the merge. `CheckMergeCommitScope.py`
guards the reduction with a throwaway git repository.

**Adding a fixer is dropping a file into `lint/fixer/`.** A fixer is one `LintFixer` subclass whose
`listPass` holds its text transforms (`(text) -> (newText, bChanged)`) plus what to call each one under
`--check` and after a fix; the base owns target-file selection (`--files` — the gates' spelling and rule,
`common.resolveFileArguments` — > `--all` > git-modified > everything), concurrency, byte-faithful IO (a file that is not UTF-8 is
reported, never rewritten), and the exit code (`--check` + findings = `1`). Scripts that
are not fixers but pick files the same way (`FormatClangFormat.py`) use `addFileArguments` /
`selectFixerTargetFiles` from the same module. Anything in `fixer/` that is not a fixer states why in
`kFixerSkipReason` (`FormatModified.py` is an orchestrator, not a fixer).

**Every `FixPass` carries two snippets**, and `CheckFixersAreAlive.py` runs both: `badSample` it MUST
rewrite (otherwise the transform is dead) and `goodSample` it must NOT touch. The second one matters more
than for a gate — a gate that over-fires prints a red line, a fixer that over-fires **rewrites 969 files**.

```powershell
py -3 Scripts/lint/gate/CheckCodeConventions.py                # naming/style rules (CI gate)
py -3 Scripts/lint/gate/CheckCodeConventions.py --files <path> # single file
py -3 Scripts/lint/gate/CheckIncludeOrder.py                   # check only — fixer/FormatIncludeOrder.py rewrites
py -3 Scripts/lint/fixer/FormatIncludeOrder.py --files <path>  # include order/dupes (same rule as the gate)
py -3 Scripts/lint/gate/CheckEngineLayers.py                   # Engine must not include Editor/GameFramework/Games; RuntimeAPI must not include Engine/App
py -3 Scripts/lint/gate/CheckModuleTargets.py                  # module targets (_listTarget): GF_Server_/GF_Client_ names, dependency and include direction
py -3 Scripts/lint/gate/CheckEngineRootFiles.py                # Source/Engine root holds only the startup/shutdown wiring files
py -3 Scripts/lint/gate/CheckDocPaths.py                       # links, anchors and backticked repo paths in *.md exist; every README is on docs/02_DocumentMap.md
py -3 Scripts/lint/gate/CheckResourceCasing.py                 # everything under Resource/ must be lowercase
py -3 Scripts/lint/gate/CheckFunctionVocabulary.py            # one verb per concept; acronyms are camelCase words
py -3 Scripts/lint/gate/CheckFallibleNodiscard.py              # bool-returning fallible verbs (load/save/apply…) are [[nodiscard]]
py -3 Scripts/lint/gate/CheckTargetMacros.py                   # platform/arch/compiler via SW_* macros, never compiler built-ins
py -3 Scripts/lint/gate/CheckStdFilesystemIsolation.py         # std::filesystem only inside Core/File/Std (engine code asks FileUtil)
py -3 Scripts/lint/gate/CheckTextureFolders.py                 # textures/ holds DDS only; source images live in textures_raw/
py -3 Scripts/lint/gate/CheckConfigReference.py                # docs/Config matches the code; every config file is in ConfigCatalog.py
py -3 Scripts/generate/GenerateConfigReference.py              # regenerate docs/Config after changing a config field, gv, argument or SW_* option
py -3 Scripts/lint/gate/CheckWin32WideCalls.py                 # Win32 calls name the W variant (UNICODE is not defined)
py -3 Scripts/lint/gate/CheckWellKnownConstants.py             # π/√2/e/gravity/hash constants only in their home (MathUtil, HashUtil, …)
py -3 Scripts/lint/gate/CheckKitNamespaces.py                  # state tags unique (comment = little-endian bytes); kits read no raw keys, prefix kit settings keys
py -3 Scripts/lint/gate/CheckControlBoundary.py                # only player controllers, player views and command directors read input; pawns read ControlIntent
py -3 Scripts/lint/gate/CheckScriptCommonHelpers.py            # Scripts/ use common's one place for processes, build dirs, console, generated files
py -3 Scripts/lint/gate/CheckScriptLayout.py                   # Scripts/ file-name prefix per folder and lint base classes (Scripts/README.md layout table)
py -3 Scripts/lint/fixer/FormatBranchBraces.py --check         # if/case 중괄호 규칙 검사
py -3 Scripts/lint/fixer/FormatModified.py                     # clang-format the working-tree changes
py -3 Scripts/lint/report/RunBuildWarnings.py                  # compiler warnings still in the tree
py -3 Scripts/lint/report/RunHeaderSelfContained.py            # headers that only compile thanks to someone else
py -3 Scripts/lint/report/RunForwardDeclarationCandidates.py  # includes a header could replace with a forward declaration (`--apply` rewrites; then build + RunHeaderSelfContained)
py -3 Scripts/lint/report/RunClangTidy.py                      # static analysis
py -3 Scripts/lint/report/RunPaddingReport.py                  # per-record size, padding and the reorder floor (libclang)
py -3 Scripts/lint/report/RunRepeatedConstants.py              # constants/literals defined in more than one place
py -3 Scripts/lint/report/RunDocStyle.py                       # prose shape per doc: noun chains, nested parens, long sentences, folder trees, coined terms
py -3 Scripts/lint/report/RunFolderFileCount.py                # folders over 40 code files, single-file Source folders
py -3 Scripts/lint/selftest/CheckLintsAreAlive.py              # do the gates still bite? (CI gate)
py -3 Scripts/lint/selftest/CheckFixersAreAlive.py             # do the fixers still rewrite — and still hold back? (CI gate)
py -3 Scripts/lint/RunLintSuite.py [--hook-sample 1,10]         # every gate + self-test without a build dir, with timings (what the CI lint job runs)
py -3 Scripts/lint/selftest/CheckCodeConventionsSelfTest.py    # do its rules still bite? (CI gate)
```

- **A header that compiles is not a header that stands alone.** A header that forgets an include still
  builds as long as something else included that name first — until the day that something else is
  tidied and the break lands in an unrelated file. `RunHeaderSelfContained.py` compiles each header on
  its own (`-fsyntax-only`, real flags borrowed from the nearest TU in the compile DB) and names the
  ones that do not stand. ~3 min for `Source/`, so the whole tree is a report, not a gate — run it after a folder
  sweep or an include cleanup; CI runs it daily (`header-self-contained.yml`) and the commit hook checks the staged
  headers (`CheckHeaderSelfContained`, when a build folder exists). **Keep force-included headers thin:** the generated `FlagOps.gen.h` is
  force-included (`/FI`) into every TU of a target, so anything it `#include`s is "already there" everywhere
  and hides every omission. It carries only opaque enum forward declarations plus the `IsBitFlagEnum`
  specializations, and includes nothing but `Core/Common/BitFlagTrait.h` (`<type_traits>` only) — `EnumUtil.h`
  would drag `Macros.h` into every TU (`ReflectionParserTest.FlagTraitHeaderIncludesOnlyTheTrait`).
- **Grepping a build for `warning:` does not work.** A warning is printed only when that TU is compiled,
  and ninja never recompiles unchanged files — so an existing warning is invisible on every build after
  the one that introduced it. `RunBuildWarnings.py` re-asks the question over the whole tree
  (`-fsyntax-only`, real build flags from the compile DB) in ~1.5 min per preset, and defaults to
  sweeping Debug · Release · Shipping because **the warning set differs per configuration**.
  The build you just ran already reports warnings your own change introduced (it recompiled exactly the
  affected TUs); this answers the other question — what is left in the tree. Run it when finishing a
  chunk of work, not on every edit. **A warning in the build log can also be an old one replayed by
  sccache** (a cache hit replays the recorded stderr) — the tell is that the source line clang prints
  does not match that line number in the file. `SCCACHE_RECACHE=1` forces a real compile; this report
  never goes through the cache, so when the two disagree, the report is right.

## Architecture

`ARCHITECTURE.md` is the long-form guide; the essentials:

**Target graph.** `App.exe` is a thin launcher (`EngineLoop` + `ModuleHost`) that links only `Engine` and
`RuntimeAPI` — it has no compile-time knowledge of game or editor classes. `Core` (static, foundation:
log/memory/string/file/task/compression) is compiled as an OBJECT library that `Engine` absorbs and
re-exports. In **Dev**, `Engine` is a DLL and `EditorModule` / `SWGame` / `GF_*` kits / `RHI_*` backends
are dynamically loaded MODULEs supporting hot reload; in **Shipping** the editor is dropped and everything
links statically into one exe.
`Server` is the dedicated-server launcher (Game · Server targets, `Source/Server`): the same `EngineLoop` in the `DedicatedServer` role
(no window · RHI · user-settings steps, a null audio device) + `ModuleHost` through the shared `AppHost` static library, ticking the game
at a fixed rate; its operator config is `Config/Server/<game>.json` (read from disk even in Shipping).

**The C-ABI boundary.** `Source/RuntimeAPI` is header-only `INTERFACE` — a pure `extern "C"` contract, never
implementations. It includes no Engine or App header (`CheckEngineLayers.py`); the engine service table that defines
the service ids (`Source/RuntimeAPI/Service/EngineServiceList.xxx`) lives here and Engine includes it.
Everything crossing App ↔ module goes through it. Export macros are distinct and not
interchangeable: `SW_API` (Engine.dll symbols), `SW_MODULE_API` (C-ABI entry points of any loadable plugin),
`SW_GF_API` (GameFramework.dll classes), `SW_GAMESERVICE_API` (the RuntimeAPI GameService locator only).

**Engine internal layers.** `Source/Engine` is one link unit but its folder include graph is a DAG, linted by
`CheckEngineLayers.py`: foundation (`Common`) at the bottom, `Graphics/Renderer` and the root files (`EngineLoop`) at the top.
The RHI does not know the window, the world does not know the renderer, the renderer reads the scene. Engine code must never include `Editor/`,
`GameFramework/`, or `Games/`; reach the editor through RuntimeAPI, delegates, or events instead. The tier
table is in `Source/Engine/README.md` (recompute it with `Scripts/lint/report/RunEngineLayerGraph.py`).

**Startup and shutdown are one table.** `Source/Engine/EngineInitStepList.xxx` lists every step with the
steps it waits for; `EngineInitSequence` sorts it, brings steps up in that order and down in reverse, and each
step is one struct (`initialize` / `shutdown` / `destroy`). `EngineLoop` and the test harness share
`EngineBootstrap`. Scenes load only after `ModuleTypes` (every type provider registered), and `ModuleHost` brings
the game up before the editor. Engine services are the rows of `Source/RuntimeAPI/Service/EngineServiceList.xxx`.

**Reflection codegen.** `REFLECT` / `PROPERTY` / `FUNCTION` / `ENUM` macros in headers are parsed by
`Tools/ReflectionParser` (libclang) into `build/<preset>/generated/**/*.gen.cpp`, driven by
`sw_addReflectionStep` in `cmake/Engine/ReflectionCodeGen.cmake`. Scene loading, the inspector, hot
reload, and `addComponentByName` all depend on the generated `TypeInfo`/registrars, so `ReflectionParser`
must build before Engine. ReflectionParser links `Core` only, to avoid a cycle with Engine.dll.
To see what the parser extracted from a header (why a property is missing, a range, a container kind), run
`build/<preset>/BuildTools/ReflectionParser.exe --dump --input <header> ...` (full arguments in `Tools/ReflectionParser/README.md`).

**Resources.** `Resource/` splits into `engine/`, `common/`, and `game/<active game>/`. Paths are global ids
including the domain (`engine/pipeline/forwardpipeline.xml`) and are lowercased via `normalizePath` at lookup —
hence the enforced lowercase rule. Rendering separates `RenderPassAsset` (bind template: formats/clears,
under `renderpass/`) from `RenderPipelineAsset` (the frame graph ordering passes, under `pipeline/`);
`FrameRenderer` builds a `RenderGraph` from the pipeline and topologically sorts it at runtime. Textures are read
as DDS only — source images live in `textures_raw/` and are imported with `App --import-textures`; glTF models live in
`models_raw/` and are imported to `models/*.mesh` with `App --import-models` (read by `MeshCache`, named by `MeshComponent::_meshId`). Data is read in
its current shape only: no old-format readers, and a rename rewrites the data instead of adding `Alias` / `ValueAlias` (those are for after shipped data exists)
(`ResourceDataSchemaTest`). Command-line arguments are listed in `Source/Core/Predefined/ArgumentList.xxx`.

## Gotchas

- **The first `REFLECT`/`ENUM` in a header needs a re-configure.** The reflected-header list is scanned at
  configure time (`sw_addReflectionStep` auto-scan), so a header that never had one is not parsed until
  `cmake --preset <preset>` runs again. The symptom is an undefined `X::StaticType()` link error. A base that
  cannot be instantiated is still registered, as `REFLECT( Abstract )` — every reflected parent must be
  registered (`ReflectionTypeInfoTest.EveryReflectedParentIsRegistered`).
- **Re-parenting during tick is deferred.** `GameObjectManager::tick` runs `onTick()` across threads;
  `attachToParent` / `detachFromParent` and other structural changes (`addComponent`, `addTag`) made inside it
  go to the structural-change queue (`deferStructuralChange`) and apply right after the tick in call order — do
  not expect the changed hierarchy within the same tick. `addComponent` returns `nullptr` mid-tick — use
  `GameObjectManager::executeOrDeferPostTick` to spawn and initialize in one block.
- **RHI ABI stamps.** Changing `RHIModuleAbi.h` requires rebuilding the engine and *all* `RHI_*.dll`
  backends together; a stale backend DLL crashes immediately on mismatched function pointers.
- **Statics die on hot reload.** Class statics and singletons living in a reloadable module vanish or move
  when the DLL is swapped. State that must survive belongs in `Engine` or `App`.
