# SW Engine Project Instructions

> 질문: 코드를 고칠 때 지킬 공통 규칙은 무엇이고, 세부 규칙은 어느 파일에 있는가?
> 독자: C++ · CMake · Python · HLSL 을 고치는 에이전트와 사람(본문은 영어, 이 머리말만 한국어).
> 결론: 아래 아키텍처와 빌드 명령을 공통으로 따르고, 세부 규칙은 맨 아래 표에서 일에 맞는 `docs/agents/` 파일 하나만 읽는다.

These instructions apply to all work in this repository. Preserve existing
project conventions unless the user explicitly requests otherwise.

## Project architecture

- This is a C++ game and editor engine template using CMake, Ninja, and Clang.
- **Dev** builds include editor tooling and support `LiveReload` through dynamic
  DLL loading. **Shipping** builds exclude editor code and statically link into
  a single executable.
- `Source/Core` is the foundational utility static library, shared with Engine
  through object compilation.
- `Source/Engine` contains objects, RHI/graphics, physics, scenes, and other
  engine facilities. It is a shared DLL in Dev builds.
- `Source/RuntimeAPI` is a header-only `INTERFACE` library defining the pure
  C-ABI contract between App and Editor/Game modules. Do not put implementations
  in it.
- `Source/App` is a thin executable launcher. In Dev it links only Engine and
  RuntimeAPI; in Shipping it also statically links `SWGame`. It uses `Engine`
  for the main loop and `ModuleHost` to manage hot-reload state serialization
  and async-task fencing.
- `Source/Editor` is Dev-only. `Source/GameFramework` contains genre-common
  frameworks/kits. `Source/Games` contains concrete games; `SW_ACTIVE_GAME`
  selects the game to build.
- **DLL Export / Import (API) Macros**:
  - `SW_API`: Used to export/import symbols from **Engine.dll**. Responds to the `SW_EXPORTS` definition.
  - `SW_MODULE_API`: A generic C-ABI entry point macro used across **all dynamically loaded plugin modules** (EditorModule.dll, SWGame.dll, RHI backends, etc.). Responds to `SW_MODULE_EXPORTS`.
  - `SW_GF_API`: Used to export/import **GameFramework.dll** (Base) class symbols. Responds to `SW_GF_EXPORTS`.
  - `SW_KIT_API`: Used by headers under `GameFramework/Kits` to export/import **kit DLL** class symbols. Responds to `SW_KIT_EXPORTS` (kit TUs) / `SW_KIT_IMPORTS`. Never use `SW_GF_API` in a kit header: kit TUs do not define `SW_GF_EXPORTS`, so a Base class would be imported, not re-exported.
  - `SW_KIT_SERVER_API`: Used by headers of **server kits** (`GF_Server_<X>`, folder `Server/`). Responds to `SW_KIT_SERVER_EXPORTS`; server TUs do not define `SW_KIT_EXPORTS`, so a shared-kit header they include stays imported. Pure interfaces (pure virtuals / `= default` only) carry no export macro.
  - `SW_EDITOR_API`: Used to export/import **EditorModule.dll** symbols (registries, widgets, selection, undo) for editor extension modules. Responds to `SW_EDITOR_EXPORTS`.
  - `SW_GAMESERVICE_API`: Used by the GameService locator (`bindGameService` / `getRawService`) in RuntimeAPI. This is not the GameFramework class-export macro.

## Build workflow

```powershell
py -3 Scripts/setup/SetupEnvironment.py
py -3 Scripts/setup/SetupVcpkg.py --install
cmake --preset Ninja-Debug
cmake --build --preset Ninja-Debug  # App, Server, modules
```

- Build outputs are in `build/Ninja-Debug/Bin`.
- The compilation database is `build/Ninja-Debug/compile_commands.json`.

## Writing — docs, comments, commit messages

- Docs, comments and commit messages use
  established loanwords and English terms, no coined native words. Log and assert strings stay English.

## Rule files — read only the ones your task needs

The rules are split by topic under `docs/agents/`. Do not read all of them: open the file that matches the work. Korean examples for these rules are in
[docs/04_CodingGuidelines.md](docs/04_CodingGuidelines.md); the rule files win when the two disagree.

| File | What it holds | Read it when |
| --- | --- | --- |
| [docs/agents/NamingCpp.md](docs/agents/NamingCpp.md) | `Naming` for C++: casing, prefixes, containers, `Class suffixes`, `Function names` | you name or rename anything in C++ |
| [docs/agents/NamingScripts.md](docs/agents/NamingScripts.md) | `Naming` for CMake, Python, HLSL and Resource Assets | you write CMake, Python or a shader, or add a resource asset |
| [docs/agents/Includes.md](docs/agents/Includes.md) | `C++ structure and includes` | you add a file, a header or an include |
| [docs/agents/Constants.md](docs/agents/Constants.md) | `Constants — where a constant lives` | you add or move a constant or a tuning value |
| [docs/agents/Helpers.md](docs/agents/Helpers.md) | `Helpers: Util vs Internal`, one anonymous namespace per file | you add a helper function or a TU-private type |
| [docs/agents/CppStyle.md](docs/agents/CppStyle.md) | `C++ style`, `Parallel-safe code` | you write C++ bodies: branches, loops, types, thread use, code that runs in parallel |
| [docs/agents/ExistingFacilities.md](docs/agents/ExistingFacilities.md) | `Existing facilities`: what Core and Engine already provide (log, time, random, files, parsing, tasks …) and the direct calls not to use | you are about to write a helper for a common job, or call a C runtime or `std` facility directly |

`Project architecture`, `Build workflow` and `Writing` stay in this file.
