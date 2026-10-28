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
- Prefer `Core/Common/StdHeaders.h` and `Core/Common/PlatformOsHeaders.h` over scattered system/OS includes. Heavy standard headers few files use (streams, `<random>`, `<regex>`, `<future>`, `<variant>`, `<condition_variable>`, `<csignal>`, `<queue>`, `<stack>`, `<bitset>`) are included directly by the file that uses them (declare stream operators with `<iosfwd>`).
- Platform-specific includes go in **one** `#if SW_PLATFORM_WINDOWS / #elif ... / #endif` chain after the unconditional includes; inside a branch, project headers, a blank line, then system headers.
- Use the type aliases from `Types.h`. Prefer Core and Engine facilities over STL or direct system calls ([ExistingFacilities](ExistingFacilities.md)).

### Core tiers

Core folders are tiers: `Common` → `Concurrency` · `Math` → `Memory` → `Container` → … → `Diagnostics` → `Log/Sink` (the sink sub-folder implements the `ILogSink` port that `Log` declares, so it sits on top). A file includes only its own folder or a lower tier.

- A lower tier that must reach up takes an interface or a function pointer (`ILockObserver`, `IAllocationTracker`).
- Tiers below `Log` get **no `Logger.h`**: their `.cpp` files build with `Core/pchLow.h`, so `SW_LOG_*`, `StringBuilder` and `FileUtil` fail to compile. Report with `SW_LOW_LEVEL_*`.

### Platform, compiler, build target

| Question | Ask with | Never |
| --- | --- | --- |
| Platform / architecture | `SW_PLATFORM_WINDOWS` · `SW_PLATFORM_LINUX`, `SW_X64` · `SW_ARM64` | `_WIN32`, `__linux__` |
| Compiler | `SW_COMPILER_CLANG` (clang-cl included) · `SW_COMPILER_MSVC` (cl.exe only) · `SW_COMPILER_GCC` | `_MSC_VER`, `__clang__` |
| Build target | `SW_WITH_CLIENT_CODE` · `SW_WITH_SERVER_CODE` · `sw::build::kTargetName`, only in `.cpp` bodies | gating reflection declarations or a header's class layout on them |

clang-cl defines both `__clang__` and `_MSC_VER`, so "may I use an MSVC extension" is `SW_PLATFORM_WINDOWS`. A feature that splits between client and server splits into modules: shared `GF_<X>`, server-only `GF_Server_<X>`, client-only `GF_Client_<X>`.

### Locks and threads

- Lock with `sw::mutex` / `sw::shared_mutex` / `SpinLock` and the guards in `Core/Concurrency/ScopedLock.h` (`ScopedLock`, `ScopedSharedLock`, `UniqueLock`, `ScopedLockPair`), never the `std::` ones (clang `-Wthread-safety` does not see them). Start threads with `sw::thread`.
- Write the lock contract on the declaration, not in a comment: `SW_GUARDED_BY( _mutex )`, `SW_REQUIRES_LOCK( _mutex )`, `SW_EXCLUDES_LOCK( _mutex )` (`Core/Concurrency/ThreadSafety.h`). A "this thread only" rule is a `ThreadRole` member.
- Locks are leaf locks: never log or call out while holding one.

### Services

Reach a service through the window of its lifetime: `getSubsystem<T>()` (must exist, `T&`) or `findSubsystem<T>()` (may be missing, `T*`) on `engine::`, `game::`, `editor::` or the owning object.

- A new service is a subsystem added to a scope; an engine service read on hot paths also gets a row in `EngineServiceList.xxx`.
- No named accessors, no `static getInstance()`.
- Tests inject mocks through scopes (`test::ScopedEngineServiceOverride`, `game::ScopedGameScope`, `editor::ScopedEditorScope`).

### Memory and ownership

- Construct into held memory with `sw_placement_new( pMemory ) T( ... )`, never a bare `new ( pMemory ) T( ... )`.
- Allocate on the heap through an **owning type**: `make_unique<T>( ... )`, `vector<T>`, `make_unique<T[]>( n )`, `MemoryBlock` for a raw byte block. A bare `new` is rejected. Where ownership must leave the type system (lock-free slot table, COM refcount, C-ABI handle), write `make_unique<T>( ... ).release()`.
- **Borrow with a pointer, keep with a handle.** A `GameObject*` / `Component*` you do not own is valid only inside the current call (at most the current frame). Anything kept across frames (members, selection lists, undo records) holds a `GameObjectHandle` / `ComponentHandle` and resolves it on each use through the owning manager (`resolveGameObject` / `resolveComponent`, `nullptr` once the target is gone). A handle resolves only through the manager (scene) that owns the object. Structural links the object model maintains itself (owner, hierarchy, registries) stay raw pointers.
- **Saved state never points at another object by name.** A parent reference is the parent's id, resolved by `ObjectStateBatch` after every object of the batch is loaded. Any new path that loads object state goes through a batch (`ObjectLoadContext::_pBatch` + `finish()`).
- No magic buffer or path sizes (`char buf[64]`, `fixed_string<256>`); use `constant::kMaxBuffer16` .. `kMaxBuffer8192` and `kMaxPathSize` ([Constants](Constants.md)).
