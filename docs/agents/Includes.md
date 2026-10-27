> 질문: 새 파일과 헤더, include 를 어떻게 두는가?
> 독자: C++ 파일 · 헤더 · include 를 더하거나 옮기는 사람.
> 결론: 헤더는 전방 선언을 우선하고 include 는 짝 헤더, 범위별 그룹, 플랫폼 분기 순으로 쓴다. 클래스 멤버는 정해진 순서로 선언하고, 아래 층이 위 층을 include 하지 않는다.

## C++ structure and includes

- In headers, prefer forward declarations. Include only when a forward
  declaration is not possible; never include ThirdParty headers directly from a
  project header.
- Class declaration order: public member variables; constructors/destructor;
  `initialize`/`shutdown`; `process`; getters/setters; private functions in a
  separate access section; private member variables last.
- In `.cpp` files, include `"pch.h"` first, followed by a blank line and the
  matching header. Group project headers by relative source scope, separated by
  blank lines. System, OS-specific, project-global, and ThirdParty headers use
  angle brackets and are separately grouped.
- **Every project include is a full path from a root** — `Source/` (`Engine/Scene/SceneManager.h`), `Resource/`, the
  generated folder, `Test/` · `Tools/` for those trees — or a bare name in the same folder (`Foo.h`). No folder under
  `Source/` is on an include path, so `Scene/SceneManager.h` does not compile; `"pch.h"` resolves through a generated entry
  header (`sw_addPchEntryDirectory`). The configure step fails when a target adds a `Source/` sub-folder
  (`sw_verifySourceIncludeRoots`), and `CheckEngineLayers.py` rejects a path relative to the file's own folder
  (`RHI/RHIDevice.h`, `../Scene/X.h`) — the layer gates read the path's leading folders.
- Sort includes within a group where doing so does not undermine a required
  ordering. Prefer `Core/Common/StdHeaders.h` and
  `Core/Common/PlatformOsHeaders.h` over scattered system/OS includes — except the heavy standard
  headers few files use (streams, `<random>`, `<regex>`, `<future>`, `<variant>`, `<condition_variable>`,
  `<csignal>`, `<queue>`, `<stack>`, `<bitset>`): those are not in `StdHeaders.h`, and the file that uses them
  includes them directly (every TU parses `StdHeaders.h`; declare stream operators with `<iosfwd>`).
- Platform-specific includes go in **one** `#if SW_PLATFORM_WINDOWS / #elif ... / #endif` chain after the
  unconditional includes; inside a branch, project headers, a blank line, then system headers. Never open a
  second block on the same condition family for the system headers (`CheckIncludeOrder.py` rejects it).
- **Core folders are tiers** (`Common` → `Concurrency` · `Math` → `Memory` → `Container` → … → `Diagnostics` → `LogSink`, table in
  `Scripts/lint/rules/CheckCoreLayers.toml`): a file includes only its own folder or a lower tier, `.cpp` included. A lower tier that must
  reach up takes an interface or a function pointer (`ILockObserver`, `IAllocationTracker`, `RaceDetectContext::setReportFunction`).
  `CheckCoreLayers.py` computes the tiers from the include graph and fails when the table differs (a table value above the computed one needs a reason in `[tierReason]`);
  recompute with `Scripts/lint/report/RunCoreLayerGraph.py`.
  Tiers below `Log` also get **no `Logger.h`**: their `.cpp` files build as `Core_low_objects` with `Core/pchLow.h` (no Logger, File or String), so
  `SW_LOG_*`, `StringBuilder`, `FileUtil` … fail to compile — report with `SW_LOW_LEVEL_*` and include what you use.
  Headers of those tiers compile under the full pch in other TUs, so `CheckCoreLayers.py` bans `SW_LOG_*` there and keeps `pchLow.h` equal to `CoreMinimal.h` minus the `Log`-and-above headers.
- Use the project type aliases from `Types.h`. Prefer Core and Engine facilities over STL or direct system facilities when they meet the need.
- **Ask platform, architecture and compiler questions with the macros CMake defines**, never with compiler
  built-ins (`_WIN32`, `__linux__`, `_MSC_VER`, `__clang__`, `_M_X64`, ...): `SW_PLATFORM_WINDOWS` / `SW_PLATFORM_LINUX`, `SW_X64` / `SW_ARM64`,
  `SW_COMPILER_CLANG` (clang-cl included) / `SW_COMPILER_MSVC` (cl.exe only) / `SW_COMPILER_GCC`. clang-cl defines both
  `__clang__` and `_MSC_VER`, so "may I use an MSVC extension" (`__forceinline`, `__declspec`, `__FUNCSIG__`) is `SW_PLATFORM_WINDOWS`
  (`CheckTargetMacros.py`).
- **Ask the build target only through `SW_WITH_CLIENT_CODE` (Game, Client), `SW_WITH_SERVER_CODE` (Game, Server) and `sw::build::kTargetName`**,
  and only inside `.cpp` bodies — never gate reflection declarations (`REFLECT`, `PROPERTY`) or a header's class layout on them. A feature that splits
  between client and server splits into modules instead: shared `GF_<X>`, server-only `GF_Server_<X>`, client-only `GF_Client_<X>`.
- **Read time through `Core/Time/MonotonicClock.h`** (`MonotonicClock::nowNanoseconds()`, `Stopwatch`, `Deadline`), never a `std::chrono` clock; a UTC timestamp is
  `WallClock::nowUnixMilliseconds()` and services take it as a `nowMs` parameter so tests can pass a fake time (`CheckClockReads.py`).
- **Start threads with `sw::thread`**, never `std::thread` in `Source/` (the lock observer sees what `join` waits for; tests may use `std::thread`; `CheckStdThread.py`).
- **Lock with `sw::mutex` / `sw::shared_mutex` / `SpinLock`**, never `std::mutex` / `std::shared_mutex` in `Source/` (`CheckStdMutex.py`). Write the lock
  contract on the declaration, not in a comment: `SW_GUARDED_BY( _mutex )` on members, `SW_REQUIRES_LOCK( _mutex )` on "call with the lock held"
  helpers, `SW_EXCLUDES_LOCK( _mutex )` on functions that lock inside (`Core/Concurrency/ThreadSafety.h`). Lock with the guards in
  `Core/Concurrency/ScopedLock.h` — `ScopedLock`, `ScopedSharedLock`, `UniqueLock` (condition variables, `std::defer_lock` + `try_lock`), `ScopedLockPair` —
  never `std::scoped_lock` / `lock_guard` / `unique_lock` / `shared_lock` (clang `-Wthread-safety` does not see them; `CheckStdMutex.py`). A "this thread
  only" rule is a `ThreadRole` member: `SW_GUARDED_BY( _role )` on the data, `assumeHeld()` at the entry callback. Locks are leaf locks: never log or call
  out while holding one (Debug asserts it in the logger).
- **Reach a service through the window of its lifetime**: `getSubsystem<T>()` (must exist, `T&`) or `findSubsystem<T>()` (may be missing, `T*`) on `engine::`, `game::`, `editor::`
  or the owning object. A new service is a subsystem added to a scope; an engine service read on hot paths also gets a row in `EngineServiceList.xxx`. No named accessors
  and no `static getInstance()`. Tests inject mocks through scopes (`test::ScopedEngineServiceOverride`, `game::ScopedGameScope`, `editor::ScopedEditorScope`) (`CheckServiceAccess.py`).
- Construct into memory you already hold with `sw_placement_new( pMemory ) T( ... )`, never a bare `new ( pMemory ) T( ... )` (`Style/PlacementNew`).
- Allocate on the heap through an **owning type**, never a bare pointer: `make_unique<T>( ... )`, `vector<T>` or `make_unique<T[]>( n )`,
  `MemoryBlock` for a raw byte block. A bare `new` is rejected (`Style/RawNew`). Where ownership has to leave the type system (a lock-free slot table,
  a COM refcount, a C-ABI handle), write `make_unique<T>( ... ).release()`. `Memory::allocate` outside `Source/Core/Memory` is listed with a reason in
  `Scripts/lint/rules/CheckRawAllocation.toml`.
- **Borrow with a pointer, keep with a handle.** A `GameObject*` / `Component*` you do not own is valid
  only inside the current call (at most the current frame). Anything kept across frames — members,
  selection lists, undo records — holds a `GameObjectHandle` / `ComponentHandle` (`Core/Container`) and
  resolves it on each use through the owning manager (`resolveGameObject` / `resolveComponent`, `nullptr`
  once the target is gone). Handles are never-reused ids, so they survive renames, undo and hot reload, but a handle resolves only
  through the manager (scene) that owns the object. Structural links the object model maintains itself (owner, hierarchy, registries) stay raw pointers.
- **Saved state never points at another object by name.** A parent reference is the parent's id, resolved by `ObjectStateBatch` after every
  object of the batch is loaded. Any new path that loads object state goes through a batch
  (`ObjectLoadContext::_pBatch` + `finish()`); names are uniquified by the manager, so a lookup by name lands on a namesake.
- Do not spell out buffer/path-size magic numbers (e.g. `char buf[64]`,
  `fixed_string<256>`, `StringBuilder<32>`). Use the sentinels in the
  `constant` namespace (`Core/Common/Defines.h`) instead: `kMaxBuffer16`
  through `kMaxBuffer8192`, and `kMaxPathSize` for filesystem paths.
