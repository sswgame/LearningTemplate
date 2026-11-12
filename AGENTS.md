# SW Engine Project Instructions

> 질문: 코드를 고칠 때 지킬 공통 규칙은 무엇이고, 세부 규칙은 어느 파일에 있는가?
> 독자: C++ · CMake · Python · HLSL 을 고치는 에이전트와 사람(본문은 영어, 이 머리말만 한국어).
> 결론: 아래 아키텍처와 빌드 명령은 공통이다. 세부 규칙은 맨 아래 표에서 일에 맞는 `docs/agents/` 파일 하나만 읽는다.

Preserve existing project conventions unless the user explicitly requests otherwise.

## Rule files — read only the one your task needs

Korean examples for these rules are in [docs/04_CodingGuidelines.md](docs/04_CodingGuidelines.md); the rule files win when the two disagree.

| Read it when you ... | File | Holds |
| --- | --- | --- |
| name or rename anything in C++ | [NamingCpp](docs/agents/NamingCpp.md) | casing, prefixes, containers, class suffixes, function names |
| write CMake, Python or a shader, or add a resource asset | [NamingScripts](docs/agents/NamingScripts.md) | CMake · Python · HLSL · Resource Assets |
| add a file, a header or an include | [Includes](docs/agents/Includes.md) | include order, Core tiers, locks, subsystems, ownership |
| place, move or merge a file or folder | [FolderLayout](docs/agents/FolderLayout.md) | what each folder holds, no Util folders, DevTools vs Dev/, remaining moves in [NEXT](docs/NEXT.md) |
| write a function that can fail or return "nothing" | [Errors](docs/agents/Errors.md) | `sw::expected` · `sw::optional` (one type), `sw::Error`, no `string& outError`, no `std::optional`, compared with Unreal `TValueOrError` · `TOptional` |
| add or move a constant or tuning value | [Constants](docs/agents/Constants.md) | where a constant lives |
| add a helper or a TU-private type | [Helpers](docs/agents/Helpers.md) | owner type first; ownerless → `Util` / `Internal`, one anonymous namespace per file |
| write C++ bodies or code that runs in parallel | [CppStyle](docs/agents/CppStyle.md) | branches, loops, types, registration templates, no mutable statics (hidden singletons), parallel-safe code, thread and GPU resource ownership, which parallel or async tool (parallel for, stage, `whenAll`, own thread) |
| write an `onTick`, or add per-frame work that looks at other objects or the whole scene | [TickPlacement](docs/agents/TickPlacement.md) | main tick vs sub-tick prerequisite vs tick batch vs frame system vs post-tick queue vs registry; the scene iteration key (`SceneIterationKey`) |
| make code behave differently in Dev and Shipping (or Client and Server) | [BuildConfigSwitch](docs/agents/BuildConfigSwitch.md) | `build::kShipping` vs `#if` vs variant file |
| change or review an editor panel, shared widget, theme token or editor input | [EditorUI](docs/agents/EditorUI.md) | MVVM split (View · ViewModel · ViewState), tokens, app-screen rules, UI capture, review checklist, context-menu and input rules |
| write a render pipeline or render pass XML, or change what a pass reads from it | [RenderPipelineXml](docs/agents/RenderPipelineXml.md) | XML values are what runs; invalid combinations are load errors; compute and optional stages |
| write a data file that inherits another (`_base`), or put a new data format on that structure | [DataInheritance](docs/agents/DataInheritance.md) | field and list merge rules, schema for a new format, cooking, prefab variants, saving without default values |
| add a material graph node or change the graph compiler | [MaterialGraph](docs/agents/MaterialGraph.md) | node registration syntax, generated files, errors |
| are about to write a helper for a common job, or call a C runtime / `std` facility | [ExistingFacilities](docs/agents/ExistingFacilities.md) | what Core and Engine already provide, and what not to call; resource entrances and `AssetRef<T>` field rules |

## Project architecture

- C++ game and editor engine template: CMake, Ninja, Clang.
- **Dev** builds include editor tooling and `LiveReload` (dynamic DLL loading). **Shipping** builds exclude editor code and link statically into one executable.

| Folder | Role |
| --- | --- |
| `Source/Core` | Foundational utility static library, shared with Engine through object compilation |
| `Source/Engine` | Objects, RHI/graphics, physics, scenes; a shared DLL in Dev. Folders are layered (`Config/Rules/Layers/EngineLayers.toml`; each layer builds as its own part and an upper-layer include fails to compile); a feature folder owns its components (`Physics/Component`) |
| `Source/ModuleAPI` | Header-only `INTERFACE` library: the pure C-ABI contract between App and Editor/Game modules. No implementations |
| `Source/App` | Thin launcher. Dev links Engine + ModuleAPI; Shipping also links `SWGame`. `Engine` runs the main loop, `GameModuleManager` (`Source/ModuleManager`, shared with Server) manages game-module loading, hot reload and async-task fencing |
| `Source/Editor` | Dev only. **Every editor feature lives here or under a kit's `Editor/`** — Engine and Core hold no function only the editor calls (no `WITH_EDITOR` regions). Engine offers read APIs, read/write-paired serialization and extension points; the editor edits a data copy, writes it with the engine serializer and the runtime reloads. Editor state that must survive a module reload or backend swap is handed over as bytes through the host's `IModuleStateStore` (`ModuleAPI/Service`); undo steps are data (`SW_UNDO_STEP`) |
| `Source/GameFramework` | Genre-common frameworks and kits. `Runtime/` folders are layered like Engine (`Config/Rules/Layers/GameFrameworkLayers.toml`) |
| `Source/Games` | Concrete games; `SW_ACTIVE_GAME` selects the one to build |

### DLL export / import macros

| Macro | Exports / imports | Responds to |
| --- | --- | --- |
| `SW_API` | `Engine.dll` symbols | `SW_EXPORTS` |
| `SW_MODULE_API` | C-ABI entry point of every dynamically loaded module (EditorModule, SWGame, RHI backends ...) | `SW_MODULE_EXPORTS` |
| `SW_GF_API` | `GameFramework.dll` (Base) classes | `SW_GF_EXPORTS` |
| `SW_KIT_API` | Kit DLL classes, in headers under `GameFramework/Kits` | `SW_KIT_EXPORTS` / `SW_KIT_IMPORTS` |
| `SW_KIT_SERVER_API` | Server kit (`GF_Server_<X>`, folder `Server/`) classes | `SW_KIT_SERVER_EXPORTS` |
| `SW_KIT_CLIENT_API` | Client kit (`GF_Client_<X>`, folder `Client/`) classes | `SW_KIT_CLIENT_EXPORTS` |
| `SW_EDITOR_API` | `EditorModule.dll` symbols (registries, widgets, selection, undo) | `SW_EDITOR_EXPORTS` |
| `SW_DEVTOOLS_API` | `DevTools.dll` symbols (dev-only tools: reload, console, HUD; not built for Shipping) | `SW_DEVTOOLS_EXPORTS` |
| `SW_GAMESERVICE_API` | The GameService locator (`bindGameService` / `getRawService`) in ModuleAPI; not a class-export macro | — |

- Never use `SW_GF_API` in a kit header: kit TUs do not define `SW_GF_EXPORTS`, so a Base class would be imported, not re-exported.
- Server TUs do not define `SW_KIT_EXPORTS`, so a shared-kit header they include stays imported. Pure interfaces (pure virtuals / `= default` only) carry no export macro.

## Build workflow

```powershell
py -3 Scripts/setup/Environment.py
py -3 Scripts/setup/Vcpkg.py --install
cmake --preset Ninja-Debug
cmake --build --preset Ninja-Debug  # App, Server, modules
```

Outputs are in `build/Ninja-Debug/Bin`; the compilation database is `build/Ninja-Debug/compile_commands.json`.

## Writing — docs, comments, commit messages

Use established loanwords and English terms, no coined native words. Log and assert strings stay English.
