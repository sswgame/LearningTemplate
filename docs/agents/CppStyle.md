> 질문: C++ 본문(분기 · 반복 · 생성자 · 타입 · 문자열 · 병렬 안전 · 병렬 · 비동기 도구 고르기)은 어떻게 쓰는가?
> 독자: C++ 본문을 쓰거나 고치는 사람.
> 결론: `.clang-format` 을 따르고, 불리언은 `== false` 로 비교하고, 생성자에서만 필드를 초기화하며, 반복문에는 항상 중괄호를 쓴다. 공유 가변 상태에 쓰지 않는다.

## C++ style

### Layout and branches

- Follow `.clang-format`.
- A function body goes on its own lines. Only a function defined inside a class body may stay on one line (trivial accessors). Free `inline` functions and out-of-class definitions are expanded.
- Booleans: `if (_bValid == false)`, never negation. Compare pointers to `nullptr` explicitly.
- `uint8` boolean members (`_b*`, bit-fields included) use `SW_TRUE` / `SW_FALSE`, never `true`/`false`/`1`/`0`. Real `bool` members keep `true`/`false`.
- **Braces**: omit them for a single-line `if` body. With `else`/`else if`, a multi-line block takes braces, and when one branch needs them every branch keeps them. Loops always keep braces (an empty body is `{}`, not `;`).
- **`switch` over an enum**: handling **every** enumerator means **no** `default:` (`-Werror=switch` catches a forgotten `case`, `-Werror=covered-switch-default` rejects `default:`). Handling only some means `default:`. Out-of-range values are rejected where they enter (deserialization refuses unknown enumerators); a function that returns from every `case` returns its fallback after the switch. No per-file `#pragma` for switch diagnostics.
- **`case` bodies**: more than one statement takes braces, one does not. `break;` counts as a statement and goes **inside** the braces. A body holding a preprocessor directive is left alone. If any case in a `switch` takes braces, every case with a body does.
- No `if` initializers. Name an unclear or three-part condition in a local first.
- Range comparison puts the variable in the middle: `kMin <= value && value <= kMax`.

### Constructors and fields

- With a constructor, initialize fields there (not in the header), in declaration order, with brace initialization. One initializer per line, later lines begin with `,`. An initial value has exactly one home.
- An iterator pair takes parentheses (`_listValue( list.begin(), list.end() )`): braces pick the `initializer_list` constructor.
- Header defaults are kept in three cases: a default constructor that is `= default` or defined inline in the header, a delegating constructor, a type with no constructor.
- A field that default-initialization leaves indeterminate (integer, float, `bool`, enum, pointer, array of those, `atomic<scalar>`, bit-field) gets its value **somewhere**: every constructor with an initializer list (copy and move included) lists it, or it has a header default. A bit-field cannot have a header default before C++20, so such a class writes its constructor. Byte buffers (`utf8 _arrStaticBuffer[N]`) are exempt.
- Arrange fields to minimize padding; use bit packing where appropriate. Keep the bit-fields of one access section together. Another field between them starts a new run that takes its own byte, plus the next field's alignment. `CppShape` checks this. A split caused by a preprocessor branch is exempt.
- A flag field is `uint8 _bX : 1` (compare and assign with `SW_TRUE` / `SW_FALSE`). `CppShape` checks it. It stays `bool` with `NOLINT(sw-bool-flag): <reason>` on the line only when a bit-field cannot work: its address or a `bool&` is taken (ImGui `&_bOpen`, `getArgument( …, bool& )`, `archive >>`), it is atomic, or its type is an aggregate.
- An aggregate is built only by braces, such as `T{ a, b }`, `{ ._x = … }` or a `constexpr` table. It keeps no constructor, since a constructor would stop it being an aggregate, and its flags stay `bool _bX{ false }`. A C++17 bit-field cannot have a default member initializer, so `T x;`, a derived type or a member of another aggregate would leave the bit indeterminate. Any other type with a flag writes the constructor the rule above asks for. A nested class or a class inside a `.cpp` defines that constructor inline in its body.

### Types, strings, functions

- `auto` only for iterators, structured bindings, or similarly complex types.
- Avoid lambdas unless they offer a performance benefit.
- `const` wherever appropriate unless it harms performance.
- Build strings with `formatstring` and its `%#` placeholder, never by chaining `to_string` or `+`. Text used once (log argument, UI label, a mark, a path opened right away) goes into `fixed_string<constant::buffer::kMaxN>` via `formatstring( text.data(), text.capacity(), … )` — pick N from the content's upper bound. Text that is kept (member, map key, return value, list item) or must never be cut (path, key, serialized text) is built with `StringBuilder::appendFormat` and handed over as `string( builder.view() )`, or as `builder.view()` to a `string_view` parameter. Log macros and `DebugHUDSectionWriter::addLineFormat` take the format themselves. There is no helper that returns a formatted `string` (`formatToString` was removed, user 2026-11-08 — it hid an allocation; the name is banned).
- Read a boolean in text with `StringUtil::tryParseBool`, never a hand-written spelling list.
- No wrapper function with one caller; change the existing function's shape instead.
- Inheritance: one base; keep multiple inheritance to a minimum. A subsystem or service that offers an interface does not inherit it next to `ISubsystem`: it holds the implementation as a member and registers that member (`SubsystemScope::exposeMember`, module `getPublishedPart`, or `SW_MODULE_PART` for a stateless interface implementation) — the precedent is `PhysicsMeshShapeTable::MeshDerivedCache`; a nested port class forwards to the owner. A third-party callback (Jolt `ContactListener`) is a member adapter too. `std::enable_shared_from_this` is not counted. The only exceptions are listed with a reason in `Config/Rules/Lint/CppShapeExceptions.json` (`multipleBase`: empty-base optimization in `pair.h`, a platform COM object, pure role-interface bundles such as `IPhysicsScene`), and the lint `cpp-shape` enforces it. Never a diamond. To add a capability (dev-only reload, tooling), use a separate object that holds a reference to the target and is registered from its own build unit (composition). When the choice is fixed per build (Dev or Shipping), pick an implementation file in CMake and use no interface at all.

### Registries and failure

- A static registration table is a `RegistrationList` (defined with `SW_DEFINE_REGISTRATION_LIST` / `SW_DEFINE_LOCKED_REGISTRATION_LIST`); a slot that takes exactly one registration is a `RegistrationSlot<T>` (`SW_DEFINE_REGISTRATION_SLOT`). The registrar object is never hand-written: `SW_DEFINE_STATIC_REGISTRAR` makes a `StaticRegistrar<TItem>` (register in the constructor, unregister in the destructor only if the register was accepted — unregistering a rejected one would remove an earlier entry of the same name), and the feature's macro uses `SW_STATIC_REGISTRATION`. Every registration is removed again at module unload. A reader that caches the list compares `getGeneration()`. On the user-configuration side the name is `Handler`.
- A name-keyed registry does not reject empty or duplicate names with its own `if`: borrowed pointers go in a `RegistrationList` (`bRequireName`), owned values in a `NameRegistry`, and the caller only reports the result of `add`.
- A registration that returns a `Subscription` for a borrowed pointer never turns the subscription id back into an address by hand: a `RegistrationList` issues it (`addSubscribed`, release function taking `T*` given to the list's constructor), an O(1) or order-keeping list uses an `ItemSubscriptionList<T>` per kind (no kind bits in the id), and a callback list is a `MulticastDelegate` / `LockedMulticastDelegate`. `SubscriptionList` (plain `uint64` ids) is for id-keyed entries that carry values.
- When the program cannot go on: `SW_FATAL( "...%#", value )` (below the log tier: `SW_LOW_LEVEL_FATAL`), never `std::abort()`.
- Never discard a graphics device result (`VkResult`, `HRESULT`). Take it with `isVkSuccess` / `isHrSuccess` or `requireVk` / `requireHr`, or `if ( FAILED( … ) )` when failure is a normal answer (say so in a comment). SDK types cannot carry `[[nodiscard]]`, so review keeps this rule.

### Vectors

- No erasing one element at a time inside a loop (O(T²)). Mark finished elements, then erase once with `VectorUtil::removeIf` (order kept) or `removeAtSwap` (order free).
- Dedupe with a set, not `contains` before `push_back`. A pointer list that must stay unique is `unordered_set<T*>` (dense array: O(1) insert/erase, contiguous iteration; erase moves the last element). When order is free, collect first and `VectorUtil::sortUnique` once.

## Parallel-safe code

Anything that may run in parallel (job system, cooker, ticks, test processes) shares no mutable place. Prefer a shape that cannot race.

- **Tests** never write to `Bin`, a fixed file name or a fixed port: use a per-run temporary path and bind sockets to port 0.
- **Asset importers**: `importSource` is `const` and stateless, and returns its result per file. A source that needs another's result declares that order as data. Editor asset code adds no mutable `static` and no global cache.
- **Cook and import** produce the same bytes serially or in parallel: each job writes its own result slot, and a serial pass merges the slots in a fixed order.
- **Ticks, services, workers** do not read mutable global or singleton state; pass what they need in. A tick touches only its own object — where cross-object and scene-wide work goes is in [TickPlacement](TickPlacement.md). A worker body touches pointers and scratch slots, not containers another worker may resize.
- **No mutable statics** (user 2026-11-08). A function-local static, a namespace-scope variable (anonymous namespace, `static`, `atomic<…> s_…`) or a `static` data member that changes is a hidden singleton: it outlives an engine restart, leaks between tests and hides who writes it. Put the state in its owner (subsystem, manager, device, registry) as a member; an id issuer becomes an `IdentityToken` or the owner's counter (`ObjectIDAllocator`); a one-time warning is `SW_LOG_*_ONCE_ID`. Allowed: `const` / `constexpr`, `thread_local`, the registration templates above. A real process-lifetime floor (crash handler buffers, profiler zone sites, the service-table root) goes in `Config/Rules/Lint/MutableStaticExceptions.json` with `_why`. Lint `py -3 -m Scripts lint mutable-statics` (commit hook) enforces it.
- A job-style interface declares what it reads and writes as pure virtuals with no default (`getPoseAccess`).
- **Lazy initialization** is a function-local static (`static const Loader s_loader = makeLoader();`) or `std::call_once`, never a bare `static bool s_bTried` checked by an `if` (two threads both pass it). The same goes for check-then-fill of a static or file-scope variable; a main-thread-only site says so in a comment.

## Parallel and async — which tool where

Pick by the shape of the work, not by today's object count (a scene grows; the shape is what stays). APIs: `Source/Core/Task/TaskManager.h`, `Engine/Foundation/EngineParallel.h`. Commercial equivalents: Unreal `ParallelFor` · Task Graph prerequisites · `FRunnable`, Unity `IJobParallelFor` · `JobHandle.CombineDependencies` · `Thread`.

| Work | Tool | Shape |
| --- | --- | --- |
| N independent items inside one step (per mesh, per emitter, per layer) | `engine::runParallelByCost( count, body )` | The body reads shared input and writes only its own slot `out[index]`. Writes to anything shared (a map, an engine command queue, a sibling component, a registry that is not lock-free) happen afterwards on the calling thread, in list order — the result stays identical to the serial run (`MeshLODSystem::update`, `SceneAudio::updateEmitters`). Use `engine::runParallel( count, threshold, body )` only when the per-item cost is known. |
| A fork-join you are about to write as `createStage` + one `emplaceTask` per item + `waitStage` | `runParallelByCost` instead | The caller helps, there is no node per item, and no "task could not be created, run it here" fallback (`FoliageComponent::computeAllLayerInstances`). |
| Work that outlives the frame that started it, with an owner that waits for it at shutdown or cancel | `emplaceTask` + `addToStage( _ownerStage, … )` before `submit`; the owner waits with `waitStage( stage, owner )` | One long-lived stage per owner (`AssetStreamingQueue`, `SceneManager::_loadStage`, `EditorBackgroundTask::_jobStage`). Never `waitAll` — it waits for other owners' work too. |
| B needs A's result | `whenAll( { a, … }, continuation )` or `a.then( … )` | The order is a graph edge, not a loop that asks `isCompleted`. |
| A site that needs a background result before it can go on (shutdown, stop) | Wait on the stage (`EditorBackgroundTask::waitAndTake`) | Never `take` + sleep in a loop — the wait helps run tasks and returns the moment the work ends. |
| Something that must happen every N ms (poll a server, retry) | The frame loop submits one task when the interval passed and the previous one finished (`isStageComplete`) — `ServerPoseOverlay::schedulePoll` | Not a thread that sleeps between polls. |
| Waiting on the OS: socket, file I/O completion, file watch, stdin, child process exit or pipe, driver-serial PSO compile, DB driver call, `getaddrinfo`, crash reporting, the render thread | Own thread (`sw::thread` or `DedicatedWorker<T>`) | The owner class goes into the `ThreadCreateKey` friend list in `Core/Concurrency/thread.h` with its reason — `sw::thread` and `DedicatedWorker::start` take that key, so any other thread does not compile. Raw `std::thread` · `CreateThread` · `pthread_create` (names we cannot hide) stay in `Config/Rules/Lint/ThreadOwners.json` (`py -3 -m Scripts lint thread-owners`). Blocking a pool worker for long starves the frame. |

- **Workers read, the owner writes.** A body must not resize a container another body reads, take a lock the waiting caller may already hold (the caller helps run other tasks while it waits — `SceneAudio` releases its registration lock before its parallel step), or read a dirty transform (`ensureWorldCache` writes): flush first when `hasDirtySceneTransforms()`.
- **Say why it is safe.** Every parallel body carries a comment naming what it reads, the only slot it writes, and where the shared write happens.
- **Sleep only for real time.** A sleep loop is allowed when it waits on a device or the OS (frame pacing, a GPU fence without an event, a process without an exit event, a backoff before retrying a locked file). A loop that sleeps while asking whether a task finished becomes a completion chain (stage, `whenAll`, callback).
