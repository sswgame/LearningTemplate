# SW Engine Project Instructions

> 결론: 아래 공통 규칙과 빌드 명령을 지키고, 세부 규칙은 표에서 일에 맞는 `docs/agents/` 파일 하나만 읽는다(본문은 영어 · 한국어 섞임). 린트 · 훅이 강제하는 규칙은 각 파일에 "강제" 한 줄로만 적혀 있다.

Preserve existing project conventions unless the user explicitly requests otherwise.

## Rule files — read only the one your task needs

| Read it when you ... | File |
| --- | --- |
| name or rename anything in C++ | [NamingCpp](docs/agents/NamingCpp.md) |
| write CMake, Python or a shader, or add a resource asset | [NamingScripts](docs/agents/NamingScripts.md) |
| add a file, a header or an include; locks, services, ownership | [Includes](docs/agents/Includes.md) |
| place, move or merge a file or folder | [FolderLayout](docs/agents/FolderLayout.md) |
| write a function that can fail or return "nothing" | [Errors](docs/agents/Errors.md) |
| add or move a constant or tuning value | [Constants](docs/agents/Constants.md) |
| add a helper or a TU-private type | [Helpers](docs/agents/Helpers.md) |
| write C++ bodies, parallel or async code, thread / GPU resource ownership | [CppStyle](docs/agents/CppStyle.md) |
| write an `onTick` or per-frame work that looks at other objects or the scene | [TickPlacement](docs/agents/TickPlacement.md) |
| make code differ between Dev and Shipping (or Client and Server) | [BuildConfigSwitch](docs/agents/BuildConfigSwitch.md) |
| change an editor panel, shared widget, theme token or editor input | [EditorUI](docs/agents/EditorUI.md) |
| write a render pipeline / pass XML or change what a pass reads | [RenderPipelineXml](docs/agents/RenderPipelineXml.md) |
| write a data file that inherits another (`_base`), an input map bundle, or a new data format | [DataInheritance](docs/agents/DataInheritance.md) |
| add a material graph node or change the graph compiler | [MaterialGraph](docs/agents/MaterialGraph.md) |
| touch Android, web (WebGPU), a capability flag or a format fallback | [Platform](docs/agents/Platform.md) |
| are about to write a helper for a common job, call a C runtime / `std` facility, or handle paths and resource ids | [ExistingFacilities](docs/agents/ExistingFacilities.md) |

## Architecture rules

- **Dev** builds include editor tooling and `LiveReload` (dynamic DLLs); **Shipping** excludes editor code and links statically into one executable.
- Engine and GameFramework `Runtime/` folders are layered (`Config/Rules/Layers/*.toml`); an upper-layer include fails to compile. A feature folder owns its components (`Physics/Component`).
- `Source/ModuleAPI` is a header-only C-ABI contract — no implementations.
- **Every editor feature lives in `Source/Editor` or a kit's `Editor/`.** Engine and Core hold no function only the editor calls (no `WITH_EDITOR`). The editor edits a data copy, writes it with the engine serializer, and the runtime reloads. State that must survive a module reload goes through `IModuleStateStore` as bytes; undo steps are data (`SW_UNDO_STEP`).
- `SW_ACTIVE_GAME` selects the game to build.

### DLL export macros

| Macro | Exports | Defined by |
| --- | --- | --- |
| `SW_API` | `Engine.dll` | `SW_EXPORTS` |
| `SW_MODULE_API` | C-ABI entry point of every dynamically loaded module | `SW_MODULE_EXPORTS` |
| `SW_GF_API` | `GameFramework.dll` (Base) | `SW_GF_EXPORTS` |
| `SW_KIT_API` | kit DLL classes (`GameFramework/Kits`) | `SW_KIT_EXPORTS` / `SW_KIT_IMPORTS` |
| `SW_KIT_SERVER_API` / `SW_KIT_CLIENT_API` | `GF_Server_<X>` / `GF_Client_<X>` | `SW_KIT_SERVER_EXPORTS` / `SW_KIT_CLIENT_EXPORTS` |
| `SW_EDITOR_API` | `EditorModule.dll` | `SW_EDITOR_EXPORTS` |
| `SW_DEVTOOLS_API` | `DevTools.dll` (not built for Shipping) | `SW_DEVTOOLS_EXPORTS` |
| `SW_GAMESERVICE_API` | GameService locator in ModuleAPI (not a class macro) | — |

- Never use `SW_GF_API` in a kit header — kit TUs do not define `SW_GF_EXPORTS`, so the class would be imported.
- Server TUs do not define `SW_KIT_EXPORTS`; pure interfaces (pure virtuals / `= default` only) carry no export macro.

## Build

```powershell
py -3 Scripts/setup/environment.py
py -3 Scripts/setup/vcpkg.py --install
cmake --preset Ninja-Debug
cmake --build --preset Ninja-Debug
```

- Build only in the main working folder; workers never build (hook enforced). Bundle verification is the `preflight` skill (Release · ASan · Server, no Debug).
- Asserts are on in every non-Shipping build (`SW_DO_CHECKS`). A Debug-only diagnostic branches on `build::kDebug` with `if constexpr` so it still compiles in verified builds.

## Writing

Use established loanwords and English terms, no coined native words. Log and assert strings stay English. Commit messages: summary line, then `원인:` → `조치:` → `결과:` (commit-msg hook enforced).
