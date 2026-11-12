> 질문: C++ 본문(분기 · 반복 · 생성자 · 타입 · 문자열 · 병렬 안전 · 병렬 · 비동기 도구 고르기)은 어떻게 쓰는가?
> 독자: C++ 본문을 쓰거나 고치는 사람.
> 결론: `.clang-format` 을 따르고, 불리언은 `== false` 로 비교하고, 필드는 생성자에서 초기화하며, 반복문은 항상 중괄호. 병렬로 도는 코드는 공유 가변 상태에 쓰지 않는다.

## C++ style

### Layout and branches

- Follow `.clang-format`. A function body goes on its own lines; only a function defined inside a class body may stay on one line.
- Booleans: `if ( _bValid == false )`, never negation. Compare pointers to `nullptr` explicitly.
- `uint8` boolean members (`_b*`, bit-fields included) use `SW_TRUE` / `SW_FALSE`; real `bool` keeps `true` / `false`.
- **Braces**: none for a single-line `if` body; with `else` / `else if`, when one branch needs braces every branch takes them. Loops always take braces (empty body `{}`, not `;`).
- **`switch` over an enum**: handling every enumerator means no `default:` (`-Werror=switch` · `-Werror=covered-switch-default`); handling some means `default:`. Out-of-range values are rejected where they enter (deserialization); a function returning from every `case` returns its fallback after the switch. No per-file `#pragma` for switch diagnostics.
- **`case` bodies**: more than one statement takes braces (`break;` counts and goes inside). If any case takes braces, every case with a body does. A body holding a preprocessor directive is left alone.
- No `if` initializers. Name an unclear or three-part condition in a local first.
- Range comparison puts the value in the middle: `kMin <= value && value <= kMax`.

### Constructors and fields

- With a constructor, initialize fields there (not in the header), in declaration order, with braces, one per line, later lines starting with `,`. An initial value has exactly one home.
- An iterator pair takes parentheses (`_listValue( list.begin(), list.end() )`): braces pick the `initializer_list` constructor.
- Header defaults stay only for: a default constructor that is `= default` or inline in the header, a delegating constructor, a type with no constructor.
- A field default-initialization leaves indeterminate (integer, float, `bool`, enum, pointer, arrays of those, `atomic<scalar>`, bit-field) gets a value in every constructor with an initializer list (copy and move included) or a header default. Bit-fields have no header default in C++17, so such a class writes its constructor. Byte buffers (`utf8 _arrStaticBuffer[N]`) are exempt.
- **Bit packing**: order fields to minimize padding. A flag is `uint8 _bX : 1`; keep one access section's bit-fields together (another field between them starts a new byte). `CppShape` checks both; a split caused by a preprocessor branch is exempt. A flag stays `bool` with `NOLINT(sw-bool-flag): <reason>` only when its address or a `bool&` is taken (ImGui `&_bOpen`, `archive >>`), it is atomic, or its type is an aggregate.
- An aggregate is built only by braces (`T{ a, b }`, `{ ._x = … }`, `constexpr` tables), has no constructor, and keeps `bool _bX{ false }`. Any other type with a flag writes the constructor; a nested class or a class inside a `.cpp` defines it inline.

### Types, strings, functions

- `auto` only for iterators, structured bindings or similarly complex types. Avoid lambdas unless they buy performance. `const` wherever it does not cost performance.
- **Strings** are built with `formatstring` and `%#`, never by chaining `to_string` or `+`.
  - Text used once (log argument, UI label, a path opened right away): `fixed_string<constant::buffer::kMaxN>` via `formatstring( text.data(), text.capacity(), … )`, N from the content's upper bound.
  - Text that is kept (member, key, return value) or must never be cut (path, key, serialized text): `StringBuilder::appendFormat`, handed over as `string( builder.view() )` or `builder.view()`.
  - Log macros and `DebugHUDSectionWriter::addLineFormat` take the format themselves. No helper returns a formatted `string` (it hides an allocation; `formatToString` is a banned name). <!-- keep-old-name -->
- Read a boolean in text with `StringUtil::tryParseBool`, never a hand-written spelling list.
- No wrapper function with one caller; change the existing function's shape instead.
- **Inheritance: one base.** A subsystem or service that offers an interface holds the implementation as a member and registers it (`SubsystemScope::exposeMember`, module `getPublishedPart`, or `SW_MODULE_PART` for a stateless implementation — precedent `PhysicsMeshShapeTable::MeshDerivedCache`); a nested port class forwards to the owner. A third-party callback (Jolt `ContactListener`) is a member adapter too. `std::enable_shared_from_this` is not counted. Exceptions with reasons are in `Config/Rules/Lint/CppShapeExceptions.json` `multipleBase` (lint `cpp-shape`). Never a diamond. A capability (dev reload, tooling) is a separate object holding a reference to the target, registered from its own build unit; a choice fixed per build is a CMake file choice, not an interface.

### Registries and failure

- A static registration table is a `RegistrationList` (`SW_DEFINE_REGISTRATION_LIST` / `SW_DEFINE_LOCKED_REGISTRATION_LIST`); a slot for exactly one registration is a `RegistrationSlot<T>` (`SW_DEFINE_REGISTRATION_SLOT`). The registrar is never hand-written: `SW_DEFINE_STATIC_REGISTRAR` makes a `StaticRegistrar<TItem>` (unregisters only if the register was accepted), and the feature's macro uses `SW_STATIC_REGISTRATION`. Every registration is removed at module unload; a reader that caches the list compares `getGeneration()`. On the user-configuration side the name is `Handler`.
- A name-keyed registry does not check empty or duplicate names with its own `if`: borrowed pointers go in a `RegistrationList` (`bRequireName`), owned values in a `NameRegistry`; the caller reports the result of `add`.
- A `Subscription` id is never turned back into an address by hand: a `RegistrationList` issues it (`addSubscribed`), an O(1) or order-keeping list uses `ItemSubscriptionList<T>` per kind, a callback list is a `MulticastDelegate` / `LockedMulticastDelegate`. `SubscriptionList` (plain `uint64` ids) is for id-keyed entries carrying values.
- When the program cannot go on: `SW_FATAL( "...%#", value )` (below the log tier `SW_LOW_LEVEL_FATAL`), never `std::abort()`.
- Never discard a graphics device result (`VkResult`, `HRESULT`): `isVkSuccess` / `isHrSuccess`, `requireVk` / `requireHr`, or `if ( FAILED( … ) )` with a comment when failure is a normal answer. SDK types cannot carry `[[nodiscard]]`, so review keeps this rule.

### Vectors

- No erasing one element at a time in a loop (O(N²)): mark, then erase once with `VectorUtil::removeIf` (order kept) or `removeAtSwap` (order free).
- Dedupe with a set, not `contains` before `push_back`. A unique pointer list is `unordered_set<T*>`; when order is free, collect then `VectorUtil::sortUnique` once.

## Parallel-safe code

Anything that may run in parallel (job system, cooker, ticks, test processes) shares no mutable place.

- **Tests** never write to `Bin`, a fixed file name or a fixed port: per-run temporary path, sockets on port 0.
- **Asset importers**: `importSource` is `const` and stateless and returns its result per file; an order between sources is data. Editor asset code adds no mutable `static` or global cache.
- **Cook and import** produce the same bytes serially or in parallel: each job writes its own slot, a serial pass merges in a fixed order.
- **Ticks, services, workers** get what they need passed in, not from mutable global or singleton state. A tick touches only its own object ([TickPlacement](TickPlacement.md)). A worker body touches pointers and scratch slots, not containers another worker may resize.
- **No mutable statics.** A changing function-local static, namespace-scope variable or `static` data member is a hidden singleton: it outlives an engine restart, leaks between tests and hides its writer. Put the state in its owner as a member; an id issuer becomes an `IdentityToken` or the owner's counter (`ObjectIDAllocator`); a one-time warning is `SW_LOG_*_ONCE_ID`. Allowed: `const` / `constexpr`, `thread_local`, the registration templates. A real process-lifetime floor (crash handler buffers, profiler zone sites, the service-table root) goes in `Config/Rules/Lint/MutableStaticExceptions.json` with `_why` (lint `mutable-statics`, commit hook).
- A job-style interface declares what it reads and writes as pure virtuals with no default (`getPoseAccess`).
- **Lazy initialization** is a function-local `static const` (`static const Loader s_loader = makeLoader();`) or `std::call_once`, never a `static bool s_bTried` checked by an `if`. A main-thread-only check-then-fill says so in a comment.

## Parallel and async — which tool where

Pick by the shape of the work, not today's object count. APIs: `Source/Core/Task/TaskManager.h`, `Engine/Foundation/EngineParallel.h` (Unreal `ParallelFor` · Task Graph · `FRunnable`, Unity `IJobParallelFor` · `JobHandle` · `Thread`).

| Work | Tool | Shape |
| --- | --- | --- |
| N independent items inside one step | `engine::runParallelByCost( count, body )` | The body reads shared input and writes only `out[index]`; shared writes happen afterwards on the calling thread in list order, so the result equals the serial run (`MeshLODSystem::update`, `SceneAudio::updateEmitters`). `engine::runParallel( count, threshold, body )` only when per-item cost is known |
| A fork-join you would write as `createStage` + one `emplaceTask` per item + `waitStage` | `runParallelByCost` instead | The caller helps, no node per item, no "could not create, run here" fallback (`FoliageComponent::computeAllLayerInstances`) |
| Work that outlives its frame, with an owner that waits at shutdown or cancel | `emplaceTask` + `addToStage( ownerStage, … )` before `submit`; the owner waits with `waitStage( stage, owner )` | One long-lived stage per owner (`AssetStreamingQueue`, `SceneManager::_loadStage`, `EditorBackgroundTask::_jobStage`). Never `waitAll` |
| B needs A's result | `whenAll( { a, … }, continuation )` or `a.then( … )` | A graph edge, not a loop asking `isCompleted` |
| A site that needs a background result before going on (shutdown, stop) | Wait on the stage (`EditorBackgroundTask::waitAndTake`) | Never `take` + sleep in a loop |
| Every N ms (poll a server, retry) | The frame loop submits one task when the interval passed and the previous one finished (`isStageComplete`, `ServerPoseOverlay::schedulePoll`) | Not a sleeping thread |
| Waiting on the OS: socket, file I/O, file watch, stdin, child process, driver-serial PSO compile, DB driver, `getaddrinfo`, crash reporting, the render thread | Own thread (`sw::thread` or `DedicatedWorker<T>`) | The owner goes into the `ThreadCreationKey` friend list in `Core/Concurrency/thread.h` with its reason — `sw::thread` and `DedicatedWorker::start` take that key. Raw `std::thread` · `CreateThread` · `pthread_create` stay in `Config/Rules/Lint/ThreadOwners.json` (lint `thread-owners`). Blocking a pool worker starves the frame |

- **Workers read, the owner writes.** A body must not resize a container another body reads, take a lock the waiting caller may hold (the caller helps run tasks while waiting — `SceneAudio` releases its lock before its parallel step), or read a dirty transform (flush first when `hasDirtySceneTransforms()`).
- **Say why it is safe.** Every parallel body comments what it reads, the one slot it writes, and where the shared write happens.
- **Sleep only for real time** (frame pacing, a fence without an event, a process without an exit event, a backoff before retrying a locked file). A loop that sleeps while asking whether a task finished becomes a completion chain.

## Thread and GPU resource ownership

Each object has one owning thread; review a change by its ownership map (object → thread, renderer → owner, device caller), not only by names and include layers.

- **GPU resources** are owned by the render side with one create path and one destroy path. The game-thread shape follows whether the data changes:
  - changes after creation → **render proxy** (`Material` / `MaterialRenderProxy`, Unreal `FMaterialRenderProxy`);
  - never changes (a change makes a new object) → **create once** (`Mesh`, terrain chunk, particle arrays, Unreal `FStaticMeshRenderData`);
  - the render side swaps the handle (streaming) → **publish** an atomic handle · SRV (`Texture2D`, Unreal `FTextureReference`). Never cover two creators with a lock.
- **Render work from another thread** goes through `RenderThread::executeAndWait`; device frame, `GPUScene` and proxy entry check the render-thread role (Debug). Render-side objects the render thread holds (device, main renderer, offscreen scene renderers) are handed over once in `RenderThread::attach` — no setters while running.
- **Load workers** do construction, deserialization and engine bookkeeping only; component hooks (`onRegister`, `onPostLoad`) run on the game thread at the end of the batch (`GameObjectManager::beginDeferredRegistration`). Hook authors never think about threads.
- **Offscreen scene renderers**: one renderer per scene (its own pass-constant ring and command lists), living as long as the scene (`OffscreenSceneRenderers`); main-renderer-only work is injected (`FrameRendererHost`). Only the PSO cache is shared.
- **Frame packets carry full state only.** A packet may be dropped without drawing, so a one-shot diff or notification (glyph atlas upload `GlyphAtlasUpload::_sequence`, retired preview scene keys `_listPendingRetiredSceneKey`) is taken the moment the render thread receives the packet (head of `RenderThread::executePacket`). A new diff added to the packet is taken at the same place.
- **Worker results and outside notifications** (job results, PSO builder, physics contacts, log listeners): a slot under a lock plus a generation number, with the lock contract in the type (`SW_GUARDED_BY`, clang `-Wthread-safety`). No async read that mutates an object through a raw `this` on a worker — workers produce bytes, the game thread owns the object.
- **Scene registries** (`ComponentRegistry`, `CameraRegistry`, `TagIndex`): writes take the lock; a read window (`ComponentRegistry::View`) is valid only while nothing registers or unregisters.
