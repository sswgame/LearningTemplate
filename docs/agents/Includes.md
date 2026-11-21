# Includes

> 결론: 헤더는 전방 선언을 우선하고, 아래 층은 위 층을 include 하지 않는다(빌드 `#error` 껍데기 · 린트 `layer-includes` 강제). include 순서는 clang-format, 선언 순서는 린트 `cpp-shape` 규칙 1 이 강제한다. 소유 · 잠금 · 서비스 접근 규칙도 여기가 정본이다.

## C++ structure and includes

### Headers and include order

- Headers prefer forward declarations (`ObjectFwd.h` · `ReflectionFwd.h`); never include ThirdParty headers from a project header. Diet headers have a fan-in ceiling (lint `header-include-limits`).
- Class declaration order: public member variables; constructors/destructor; `initialize`/`shutdown`; `process`; getters/setters; private functions in a separate section; private member variables last (lint `cpp-shape` rule 1).
- `.cpp`: `"pch.h"`, blank line, matching header, then groups by first folder; system, OS and ThirdParty headers in angle brackets last. clang-format enforces (`py -3 -m Scripts format`); a header others depend on goes first through `SortPriority` in `.clang-format`, not hand order.
- Every project include is a full path from a root (`Source/`, `Content/`, the generated folder, `Tools/`) or a bare name in the same folder.
- Prefer `Core/Foundation/StdHeaders.h` and `PlatformOSHeaders.h`. Heavy standard headers few files use (streams, `<random>`, `<regex>`, `<future>`, `<variant>`, `<condition_variable>`, `<queue>` …) are included by the file that uses them (`<iosfwd>` for stream operators).
- Platform-specific includes go in **one** `#if SW_PLATFORM_… / #elif / #endif` chain after the unconditional includes.
- Use the aliases from `Types.h`; prefer Core and Engine facilities over STL or system calls ([ExistingFacilities](ExistingFacilities.md)).

### Core tiers

Core folders are tiers (`Config/Rules/Layers/CoreLayers.toml`): a file includes only its own folder or a lower tier. Folder roles are in [FolderLayout](FolderLayout.md) 2.

- A lower tier that must reach up takes an interface or a function pointer (`ILockObserver`, `IAllocationTracker`).
- Tiers below `Log` get no `Logger.h` (`SW_LOG_*`, `StringBuilder`, `FileUtil` do not compile): report with `SW_LOW_LEVEL_*`.

### Target gates

Include direction across targets is enforced by `#error` stubs (`Config/Rules/Layers/TargetLayers.toml`). Fix a gate error by changing the dependency, not by adding an exception.

- Core sees nothing above it; Engine sees no GameFramework · Editor · host; GameFramework base sees no kit.
- A kit, editor extension or game sees another module's headers only through its manifest's `_listDependency` closure.
- **Dedicated server** (gate `serverbuild`) sees no `Engine/Renderer` · `Engine/UI` · `Engine/Audio`, no `Client/` folder, nothing in `SW_EXCLUDED_ENGINE_FOLDERS`. Shared code reaches client work only through interfaces (`IEngineRenderDriver` · `IEngineUIDriver` · `IEngineAudioDriver`, `ISceneRenderBinding`, `ISoundPlayer`, `IGameClientPart` …); game code asks `IInputCaptureQuery` · `IPlayerNotificationSink` · `IUILayerVisibility` …, not `UISystem`.
- **Client target** (gate `clientbuild`) builds no `Server/` folder; shared code starts an in-process server only through `IOnlineLocalServerFactory`.
- No client/server macro — only `build::kWithClientCode` · `kWithServerCode` with `if constexpr` ([BuildConfigSwitch](BuildConfigSwitch.md)).
- **Game code** (gates `gameframework` · `kit` · `game`) sees no `Engine/Graphics/RHI` or `Engine/Graphics/Shader` header, even transitively; the one opened RHI header is `RHIEnums.h`. A game-visible GPU constant is its own constant checked by a `static_assert` in its `.cpp`.

### Platform, compiler, build target

| Question | Ask with | Never |
| --- | --- | --- |
| Platform / architecture | `SW_PLATFORM_WINDOWS` · `_LINUX` · `_ANDROID` · `_WEB` · `_POSIX`, `SW_X64` · `SW_ARM64` | `_WIN32`, `__linux__` |
| Compiler | `SW_COMPILER_CLANG` (clang-cl included) · `SW_COMPILER_MSVC` · `SW_COMPILER_GCC` | `_MSC_VER`, `__clang__` |
| Build target | `sw::build::kWithClientCode` · `kWithServerCode` · `kTargetName`, only in `.cpp` bodies | gating reflection declarations or class layout on them |

"May I use an MSVC extension" is `SW_PLATFORM_WINDOWS` (clang-cl defines both). A feature split between client and server splits into modules: `GF_<X>`, `GF_Server_<X>`, `GF_Client_<X>`.

### Locks and threads

- Lock with `sw::mutex` / `sw::shared_mutex` / `SpinLock` and the guards in `Core/Concurrency/ScopedLock.h`, never `std::` locks (`-Wthread-safety` does not see them). Start threads with `sw::thread`.
- Lock contracts on the declaration: `SW_GUARDED_BY` · `SW_REQUIRES_LOCK` · `SW_EXCLUDES_LOCK` (`ThreadSafety.h`); "this thread only" is a `ThreadRole` member.
- Locks are leaf locks: never log or call out while holding one.

### Services

Reach a service through its lifetime window: `getSubsystem<T>()` (must exist, `T&`) or `findSubsystem<T>()` (may be missing, `T*`) on `engine::`, `game::`, `editor::` or the owner. No named accessors, no `static getInstance()`.

- A new engine service registers itself: the class declares its slot (`SW_SERVICE_SLOT( T, Required | Optional )`, `Core/Subsystem/ServiceSlot.h`, class `SW_API`) and the `.cpp` has one line (`Engine/Foundation/EngineSubsystemRegistration.h`):
  - `SW_ENGINE_SERVICE_SUBSYSTEM( T, GameVisible | HostOnly, Services | Startup, ( TypesBuiltFirst... ), factory )` (`SW_ENGINE_DEFAULT_FACTORY( T )`);
  - `SW_ENGINE_SUBSYSTEM( T, Services | Startup, ( … ), factory )` — no slot;
  - `SW_ENGINE_SERVICE( T, GameVisible | HostOnly )` — a slot another subsystem binds, next to that `bind`.
  - List a type in the parentheses only when the factory looks it up; a cycle or unregistered type stops startup. Init/shutdown order comes from `ISubsystem::getSubsystemDependencies`.
- Tests inject mocks through scopes (`game::ScopedGameScope`, `editor::ScopedEditorScope`).
- A dynamically loaded module owns its services as module subsystems: `SW_MODULE_SUBSYSTEM( Process | Engine, T, PublishedTypes... )`. A lower layer finds a higher layer's implementation by interface type — no function slot, no function-local static provider.
- A slotless host subsystem the editor must find declares `static constexpr SubsystemModuleVisibility kModuleVisibility = SubsystemModuleVisibility::HostModules;` — no hand list.

### Memory and ownership

- Placement construction is `sw_placement_new( pMemory ) T( ... )`, never bare `new ( pMemory )`.
- Heap allocation goes through an owning type (`make_unique`, `vector`, `MemoryBlock`); a bare `new` is rejected. Where ownership leaves the type system (lock-free slot, COM refcount, C-ABI handle): `make_unique<T>( ... ).release()`.
- **Borrow with a pointer, keep with a handle.** A `GameObject*` / `Component*` you do not own is valid only inside the current call. Anything kept across frames holds a `GameObjectHandle` / `ComponentHandle` and resolves it through the owning manager each use. Structural links the object model maintains stay raw pointers.
- **Saved state never points at another object by name**: a parent reference is the parent's id, resolved by `ObjectStateBatch`; a new object-state load path goes through a batch (`ObjectLoadContext::_pBatch` + `finish()`).
