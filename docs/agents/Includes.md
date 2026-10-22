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
  Recompute the table with `Scripts/lint/report/RunCoreLayerGraph.py`. Enforced by `CheckCoreLayers.py`, which also computes the tiers from the include graph and
  fails when the table differs (a table value above the computed one needs a reason in `[tierReason]`).
- Use the project type aliases from `Types.h`.
- Prefer Core and Engine facilities over STL or direct system facilities when
  they meet the need.
- **Ask platform, architecture and compiler questions with the macros CMake defines**, never with compiler
  built-ins (`_WIN32`, `__linux__`, `_MSC_VER`, `__clang__`, `__GNUC__`, `_M_X64`, `__x86_64__`, `__aarch64__`, ...):
  `SW_PLATFORM_WINDOWS` / `SW_PLATFORM_LINUX` (macOS is not a supported platform), `SW_X64` / `SW_ARM64`, and
  `SW_COMPILER_CLANG` (clang-cl included) / `SW_COMPILER_MSVC` (cl.exe only) / `SW_COMPILER_GCC`. clang-cl defines both
  `__clang__` and `_MSC_VER`, so "may I use an MSVC extension" (`__forceinline`, `__declspec`, MS intrinsics,
  `__FUNCSIG__`) is `SW_PLATFORM_WINDOWS` — Windows builds only with an MS-ABI toolchain — not `SW_COMPILER_MSVC`.
  The engine builds 64-bit only, so there are no 32-bit branches. The one file that reads built-ins is
  `Core/Common/TargetMacroCheck.h`, which fails the build when CMake's choice disagrees with the compiler.
  Enforced by `CheckTargetMacros.py`.
- **Ask the build target (`SW_TARGET_TYPE`: Game, Client, Server) only through `SW_WITH_CLIENT_CODE` (Game, Client),
  `SW_WITH_SERVER_CODE` (Game, Server) and `sw::build::kTargetName`**, and only inside `.cpp` bodies — never gate reflection
  declarations (`REFLECT`, `PROPERTY`) or a header's class layout on them. A feature that splits between client and server
  splits into modules instead: shared `GF_<X>`, server-only `GF_Server_<X>`, client-only `GF_Client_<X>` (manifest `_listTarget`).
- **Read time through `Core/Time/MonotonicClock.h`**, never a `std::chrono` clock (`steady_clock`, `high_resolution_clock`,
  `system_clock` — aliases and `using namespace` included): `MonotonicClock::nowNanoseconds()`, `Stopwatch` for elapsed time,
  `Deadline::afterMilliseconds( ms )` + `isExpired()` for a bounded wait. Tests too. Duration values (`sleep_for`) are fine.
  A UTC timestamp (expiry, record time, event windows) is `WallClock::nowUnixMilliseconds()` (`Core/Time/WallClock.h`); services take it
  as a `nowMs` parameter so tests can pass a fake time.
  Exceptions live in one table with their reason. Enforced by `CheckClockReads.py`.
- **Start threads with `sw::thread`** (`Core/Concurrency/thread.h`), never `std::thread` in `Source/`: it has the same shape, tells the lock
  observer what `join` waits for (so a join cycle shows up in the deadlock detector), and asserts instead of hanging when `join` runs under the
  loader lock (`DllMain`, a module's static destructor). Tests may use `std::thread`. Enforced by `CheckStdThread.py`.
- **Reach a service through the window of its lifetime**: `getSubsystem<T>()` (must exist, returns `T&`) or `findSubsystem<T>()` (may be missing,
  returns `T*`; check it) on `engine::`, `game::`, `editor::` or the owning object (`GameObjectManager` for a scene, `EditorContext`, `GameInstanceBase`).
  A new service is a subsystem added to a scope; an engine service read on hot paths also gets a row in `EngineServiceList.xxx`. No named accessors
  (`engine::getFooManager()`) and no `static getInstance()` (stateless codecs and link-time registries are the listed exceptions). Tests inject mocks through
  scopes (`test::ScopedEngineServiceOverride`, `game::ScopedGameScope`, `editor::ScopedEditorScope`). Enforced by `CheckServiceAccess.py`.
- Construct into memory you already hold with `sw_placement_new( pMemory ) T( ... )`
  (`Core/Memory/Memory.h`), never a bare `new ( pMemory ) T( ... )`. Enforced by
  `CheckCodeConventions.py` (`Style/PlacementNew`).
- Allocate on the heap through the sw allocator, never a bare `new T` / `new T[n]`: `sw_new T( ... )` / `make_unique<T>`
  for one object, `sw_new_array<T>( n )` + `sw_delete_array( p, n )`, `make_unique<T[]>( n )` or `vector<T>` for arrays. CRT `new` is
  invisible to memory tags and leak checks. Enforced by `CheckCodeConventions.py` (`Style/RawNew`).
- **Borrow with a pointer, keep with a handle.** A `GameObject*` / `Component*` you do not own is valid
  only inside the current call (at most the current frame). Anything kept across frames — members,
  selection lists, undo records — holds a `GameObjectHandle` / `ComponentHandle` (`Core/Container`) and
  resolves it on each use through the owning manager (`resolveGameObject` / `resolveComponent`, `nullptr`
  once the target is gone). Handles are ids that are never reused, so they survive renames; editor undo,
  play-session restore and hot reload recreate objects with their original ids. Ids are unique across the
  process, but a handle resolves only through the manager (scene) that owns the object. Structural links the
  object model maintains itself (owner, scene hierarchy, registries) stay raw pointers.
- **Saved state never points at another object by name.** A parent reference is the parent's id (a scene file's
  own entity id, or the runtime id in same-process snapshots), resolved by `ObjectStateBatch` after every
  object of the batch is loaded. Any new path that loads object state goes through a batch
  (`ObjectLoadContext::_pBatch` + `finish()`); names are uniquified by the manager, so a lookup by name lands on
  a namesake.
- Do not spell out buffer/path-size magic numbers (e.g. `char buf[64]`,
  `fixed_string<256>`, `StringBuilder<32>`). Use the sentinels in the
  `constant` namespace (`Core/Common/Defines.h`) instead: `kMaxBuffer16`
  through `kMaxBuffer8192`, and `kMaxPathSize` for filesystem paths.
