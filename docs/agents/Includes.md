> 질문: 새 파일과 헤더, include 를 어떻게 두는가?
> 독자: C++ 파일 · 헤더 · include 를 더하거나 옮기는 사람.
> 결론: 헤더는 전방 선언을 우선하고 include 는 짝 헤더, 범위별 그룹, 플랫폼 분기 순으로 쓴다. 아래 층이 위 층을 include 하지 않는다. 소유 · 잠금 · 서비스 접근 규칙도 이 문서가 정본이다.

## C++ structure and includes

### Headers and include order

- Headers prefer forward declarations. Include only when a forward declaration is impossible; never include ThirdParty headers from a project header.
- Class declaration order: public member variables; constructors/destructor; `initialize`/`shutdown`; `process`; getters/setters; private functions in a separate access section; private member variables last.
- In `.cpp`: `"pch.h"` first, a blank line, then the matching header. Then project headers grouped by relative source scope, separated by blank lines. System, OS-specific, project-global and ThirdParty headers use angle brackets, each in its own group.
- **Every project include is a full path from a root**: `Source/` (`Engine/Scene/SceneManager.h`), `Content/`, the generated folder, or `Tools/` for that tree; or a bare name in the same folder (`Foo.h`). No folder under `Source/` is on an include path, so `Scene/SceneManager.h` does not compile.
- Sort includes within a group where that does not break a required order.
- Prefer `Core/Foundation/StdHeaders.h` and `Core/Foundation/PlatformOSHeaders.h` over scattered system/OS includes. Heavy standard headers few files use (streams, `<random>`, `<regex>`, `<future>`, `<variant>`, `<condition_variable>`, `<csignal>`, `<queue>`, `<stack>`, `<bitset>`) are included directly by the file that uses them (declare stream operators with `<iosfwd>`).
- Platform-specific includes go in **one** `#if SW_PLATFORM_WINDOWS / #elif ... / #endif` chain after the unconditional includes; inside a branch, project headers, a blank line, then system headers.
- Use the type aliases from `Types.h`. Prefer Core and Engine facilities over STL or system calls ([ExistingFacilities](ExistingFacilities.md)).

### Core tiers

Core folders are tiers (`Config/Rules/Layers/CoreLayers.toml`, sub-folder tiers in `[subtier]`; each tier is its own object part with `#error` stubs for every higher-tier header): `Foundation` → `Concurrency` · `Math` → `Memory` → `Container` → … → `Diagnostics` → `Log/Sink` (implements the `ILogSink` port `Log` declares, so it sits on top). A file includes only its own folder or a lower tier.

- A lower tier that must reach up takes an interface or a function pointer (`ILockObserver`, `IAllocationTracker`).
- Each tier's pch holds only the `CoreMinimal.h` headers at or below it, so tiers below `Log` get **no `Logger.h`**: `SW_LOG_*`, `StringBuilder` and `FileUtil` fail to compile there. Report with `SW_LOW_LEVEL_*`.

### Target gates

`Source/` is the include root of every target, so include direction across targets is enforced by `#error` stubs first on the include path (`Config/Rules/Layers/TargetLayers.toml`, `sw_applyTargetLayerGuard`). Fix a gate error by changing the dependency, not by adding an exception.

- Core sees nothing above it; Engine sees no GameFramework · Editor · host; GameFramework base sees no kit; the reflection parser sees only the Engine headers in `[allow] reflectionparser`.
- A kit, editor extension or game sees another module's headers only if that module is in its manifest's `_listDependency` closure. Files with no owning module (`Kits/Feature/Network/NetKitMessageRange.h`) are shared.
- **Dedicated server** (gate `serverbuild`) sees no `Engine/Renderer` · `Engine/UI` · `Engine/Audio` header, no folder named `Client` (`SW_CLIENT_ONLY_SOURCE_PATTERNS`, `[gatefolder]`), and nothing in `SW_UNBUILT_ENGINE_FOLDERS` (configure checks the gate lists them all). Platform implementations swap by folder (`Window/Headless` · `Input/Headless` instead of `Window/Linux` · `Input/Linux`). Shared code reaches client work only through interfaces:
  - engine loop: `IEngineRenderDriver` · `IEngineUIDriver` · `IEngineAudioDriver`; scene rendering `ISceneRenderBinding`; a render feature contributes through its scene service (`ISceneService::contributeLights` …), never named in the renderer;
  - sound `ISoundPlayer` (`GameSound`), lip-sync voice `IVoiceDecoder`, ability cues `IAbilityCueSink`, audio user settings self-register (`SW_USER_SETTINGS_HANDLER_SET`);
  - game code asks `IInputCaptureQuery` · `IPlayerNotificationSink` · `ISubtitleSink` · `IUILayerVisibility` · `IHUDViewModelBinder` · `IDamageNumberSink` · `IGameInstanceClientHook`, not `UISystem`; a game's client-only part is `IGameClientPart`; tooling `IShaderCookService` · `IDevToolsClientPart`.
  - Scenes on a dedicated server skip client-only component types instead of keeping a `MissingComponent` (`Config/Engine/ClientOnlyTypes.generated.json`, regenerate with `py -3 -m Scripts client-only-types`).
- **Client target** (gate `clientbuild`) builds no folder named `Server` (`SW_SERVER_ONLY_SOURCE_PATTERNS` — `GameFramework/Runtime/Online/Server`, server secrets `Engine/Config/Server`); shared code starts an in-process server only through `IOnlineLocalServerFactory`.
- There is no client/server macro — only `build::kWithClientCode` · `kWithServerCode` (`BuildInfo.h`), whose `if constexpr` branches compile in every configuration so the gates still see them. GCC and Clang poison the old `SW_WITH_CLIENT_CODE` · `SW_WITH_SERVER_CODE`.
- **Game code** (gates `gameframework` · `kit` · `game`) sees no `Engine/Graphics/RHI` or `Engine/Graphics/Shader` header, even transitively: it uses `LightKind`, `InstanceHighlight`, `MeshBuildVertex`, materials and meshes whose GPU parts live in render proxies (`MeshRenderData`, `MaterialRenderProxy`), and `Engine/Graphics/RenderState`. The one opened RHI header is the value enum `RHIEnums.h`. A game-visible GPU constant is its own constant checked by a `static_assert` in its `.cpp` (`CanvasBatch::kMaxTextureCount`).

### Platform, compiler, build target

| Question | Ask with | Never |
| --- | --- | --- |
| Platform / architecture | `SW_PLATFORM_WINDOWS` · `SW_PLATFORM_LINUX` · `SW_PLATFORM_ANDROID` · `SW_PLATFORM_WEB` · `SW_PLATFORM_POSIX`, `SW_X64` · `SW_ARM64` | `_WIN32`, `__linux__` |
| Compiler | `SW_COMPILER_CLANG` (clang-cl included) · `SW_COMPILER_MSVC` (cl.exe only) · `SW_COMPILER_GCC` | `_MSC_VER`, `__clang__` |
| Build target | `sw::build::kWithClientCode` · `kWithServerCode` · `kTargetName` (constants — there is no client/server macro), only in `.cpp` bodies | gating reflection declarations or a header's class layout on them |

clang-cl defines both `__clang__` and `_MSC_VER`, so "may I use an MSVC extension" is `SW_PLATFORM_WINDOWS`. A feature that splits between client and server splits into modules: shared `GF_<X>`, server-only `GF_Server_<X>`, client-only `GF_Client_<X>`.

### Locks and threads

- Lock with `sw::mutex` / `sw::shared_mutex` / `SpinLock` and the guards in `Core/Concurrency/ScopedLock.h` (`ScopedLock`, `ScopedSharedLock`, `UniqueLock`, `ScopedLockPair`), never the `std::` ones (clang `-Wthread-safety` does not see them). Start threads with `sw::thread`.
- Write the lock contract on the declaration, not in a comment: `SW_GUARDED_BY( _mutex )`, `SW_REQUIRES_LOCK( _mutex )`, `SW_EXCLUDES_LOCK( _mutex )` (`Core/Concurrency/ThreadSafety.h`). A "this thread only" rule is a `ThreadRole` member.
- Locks are leaf locks: never log or call out while holding one.

### Services

Reach a service through the window of its lifetime: `getSubsystem<T>()` (must exist, `T&`) or `findSubsystem<T>()` (may be missing, `T*`) on `engine::`, `game::`, `editor::` or the owning object.

- A new engine service registers itself — there is no central list. Its class body declares the slot (`SW_SERVICE_SLOT( T, Required | Optional )` or `SW_SERVICE_SLOT_CONST`, `Core/Subsystem/ServiceSlot.h`; the class is `SW_API`), and the `.cpp` that puts the object into a scope has one registration line (`Engine/Foundation/EngineSubsystemRegistration.h`):
  - `SW_ENGINE_SERVICE_SUBSYSTEM( T, GameVisible | HostOnly, Services | Startup, ( TypesBuiltFirst... ), factory )` — builds the subsystem and fills its slot (`SW_ENGINE_DEFAULT_FACTORY( T )` for a plain `emplaceSubsystem<T>()`).
  - `SW_ENGINE_SUBSYSTEM( T, Services | Startup, ( TypesBuiltFirst... ), factory )` — builds a subsystem without a slot (found only through scopes).
  - `SW_ENGINE_SERVICE( T, GameVisible | HostOnly )` — a slot for an object another subsystem binds (a value bundle, an object inside a subsystem); the line sits next to that `bind`.
  - Put a type in the parentheses only when the factory looks it up (`scope.get<T>()`); the build order is a topological sort of those (ties by name), and a cycle, an unregistered type or a type from another stage stops startup. Init/shutdown order still comes from `ISubsystem::getSubsystemDependencies`.
  - `engine::getSubsystem<T>()` reads the slot in O(1) and compiles only for a `Required` slot whose header is included; `engine::findSubsystem<T>()` reads an `Optional` slot, or walks the scopes for a type without a slot. A required slot left empty stops startup with the type name (`engine::bindEngineServices`). `GameVisible` slots are bound into game module scopes (`game::findSubsystem<T>()`); editor scopes get every slot.
- No named accessors, no `static getInstance()`.
- Tests inject mocks through scopes (`game::ScopedGameScope`, `editor::ScopedEditorScope`); the registration rules are tested with a local `engine::EngineRegistrationList` (`Engine/Foundation/Tests/TestEngineSubsystemRegistration.cpp`).
- A dynamically loaded module (GameFramework, a kit) owns its services as **module subsystems**: `SW_MODULE_SUBSYSTEM( Process | Engine, T, PublishedTypes... )` in a `.cpp`. Loading the module mounts them in a child scope named after the module (`ModuleRegistrationHost`); unloading removes the child scope. Only published types are visible from the parent scope, so a lower layer finds a higher layer's implementation by interface type (`engine::findSubsystem<INetSecurityProvider>()`) — no function slot, no function-local static provider. `Process` is the root of the scope chain (the host scope) and outlives engine and game restarts.
- A host subsystem without a slot that the editor must find (a `DevTools` subsystem, `ConfigManager`) declares it in its class: `static constexpr SubsystemModuleVisibility kModuleVisibility = SubsystemModuleVisibility::HostModules;`. The editor module's scope reaches it through `ModuleRegistrationHost::getHostModuleLocator`; game module scopes never see it. The host keeps no hand list of what to bind into the editor scope.

### Memory and ownership

- Construct into held memory with `sw_placement_new( pMemory ) T( ... )`, never a bare `new ( pMemory ) T( ... )`.
- Allocate on the heap through an **owning type**: `make_unique<T>( ... )`, `vector<T>`, `make_unique<T[]>( n )`, `MemoryBlock` for a raw byte block. A bare `new` is rejected. Where ownership must leave the type system (lock-free slot table, COM refcount, C-ABI handle), write `make_unique<T>( ... ).release()`.
- **Borrow with a pointer, keep with a handle.** A `GameObject*` / `Component*` you do not own is valid only inside the current call (at most the current frame). Anything kept across frames (members, selection lists, undo records) holds a `GameObjectHandle` / `ComponentHandle` and resolves it on each use through the owning manager (`resolveGameObject` / `resolveComponent`, `nullptr` once the target is gone). A handle resolves only through the manager (scene) that owns the object. Structural links the object model maintains itself (owner, hierarchy, registries) stay raw pointers.
- **Saved state never points at another object by name.** A parent reference is the parent's id, resolved by `ObjectStateBatch` after every object of the batch is loaded. Any new path that loads object state goes through a batch (`ObjectLoadContext::_pBatch` + `finish()`).
