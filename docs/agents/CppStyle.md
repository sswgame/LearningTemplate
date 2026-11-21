# CppStyle

> 결론: `.clang-format` 을 따르고, 불리언은 `== false` 로 비교하고, 필드는 생성자에서 초기화하며, 반복문은 항상 중괄호. 병렬로 도는 코드는 공유 가변 상태에 쓰지 않는다.

## C++ style

### Layout and branches

- Follow `.clang-format` (`py -3 -m Scripts format`). A function body goes on its own lines; only a function defined inside a class body may stay on one line.
- Booleans: `if ( _bValid == false )`, never negation. Compare pointers to `nullptr` explicitly.
- `uint8` boolean members (`_b*`, bit-fields included) use `SW_TRUE` / `SW_FALSE`; real `bool` keeps `true` / `false`.
- **Braces**: none for a single-line `if` body; with `else` / `else if`, if one branch needs braces every branch takes them. Loops always take braces (empty body `{}`).
- **`switch` over an enum**: every enumerator handled → no `default:` (compiler `-Werror=switch` · `-Werror=covered-switch-default` enforces); some handled → `default:`. Reject out-of-range values where they enter. No per-file `#pragma` for switch diagnostics.
- **`case` bodies**: more than one statement (`break;` counts) takes braces; if any case takes braces, every case with a body does.
- No `if` initializers. Name an unclear or three-part condition in a local first.
- Range comparison puts the value in the middle: `kMin <= value && value <= kMax`.

### Constructors and fields

- 초기값은 한 곳에: with a constructor, initialize fields there (not in the header), in declaration order, with braces, one per line, later lines starting with `,`.
- An iterator pair takes parentheses (`_listValue( list.begin(), list.end() )`): braces pick `initializer_list`.
- Header defaults only for: a `= default` or inline default constructor, a delegating constructor, a type with no constructor.
- A field default-initialization leaves indeterminate (scalar, enum, pointer, arrays of those, `atomic<scalar>`, bit-field) gets a value in every constructor (copy and move included) or a header default. Byte buffers are exempt.
- **Bit packing**: order fields to minimize padding; a flag is `uint8 _bX : 1`; keep one access section's bit-fields together. Lint `cpp-shape` enforces both. A flag stays `bool` with `NOLINT(sw-bool-flag): <reason>` only when its address or a `bool&` is taken, it is atomic, or its type is an aggregate.
- An aggregate is built only by braces, has no constructor, and keeps `bool _bX{ false }`. Any other type with a flag writes the constructor.

### Types, strings, functions

- `auto` only for iterators, structured bindings or similarly complex types. Avoid lambdas unless they buy performance. `const` wherever it does not cost performance.
- **Strings** are built with `formatstring` and `%#` (`sw::basic_string` has no `operator+` — compile error).
  - Text used once (log argument, UI label, a path opened right away): `fixed_string<constant::buffer::kMaxN>` via `formatstring( text.data(), text.capacity(), … )`.
  - Text that is kept or must never be cut (path, key, serialized text): `StringBuilder::appendFormat`, handed over as `string( builder.view() )` or `builder.view()`.
  - Log macros and `DebugHUDSectionWriter::addLineFormat` take the format themselves. No helper returns a formatted `string` (`formatToString` is a banned name). <!-- keep-old-name -->
- Read a boolean in text with `StringUtil::tryParseBool`.
- No wrapper function with one caller; change the existing function's shape instead.
- **Inheritance: one base** (lint `cpp-shape`, exceptions in `Config/Rules/Lint/CppShape.json` `_multipleBase`). A subsystem offering an interface holds the implementation as a member and registers it (`SubsystemScope::exposeMember`, module `getPublishedPart`, `SW_MODULE_PART`); a third-party callback is a member adapter. Never a diamond. A capability (dev reload, tooling) is a separate object holding a reference to the target; a per-build choice is a CMake file choice, not an interface.

### Registries and failure

- A static registration table is a `RegistrationList` (`SW_DEFINE_REGISTRATION_LIST` / `SW_DEFINE_LOCKED_REGISTRATION_LIST`); a one-registration slot is a `RegistrationSlot<T>` (`SW_DEFINE_REGISTRATION_SLOT`). Registrars come from `SW_DEFINE_STATIC_REGISTRAR` / `SW_STATIC_REGISTRATION`, never hand-written. Every registration is removed at module unload; a reader that caches the list compares `getGeneration()`. On the user-configuration side the name is `Handler`.
- A name-keyed registry does not check empty or duplicate names with its own `if`: borrowed pointers go in a `RegistrationList` (`bRequireName`), owned values in a `NameRegistry`; the caller reports the result of `add`.
- Never turn a `Subscription` id back into an address by hand: `RegistrationList::addSubscribed`, `ItemSubscriptionList<T>`, or a `MulticastDelegate` / `LockedMulticastDelegate`.
- When the program cannot go on: `SW_FATAL( "...%#", value )` (below the log tier `SW_LOW_LEVEL_FATAL`), never `std::abort()`.
- Never discard a graphics device result (`VkResult`, `HRESULT`): `isVkSuccess` / `isHrSuccess`, `requireVk` / `requireHr`, or `if ( FAILED( … ) )` with a comment when failure is normal. SDK types cannot carry `[[nodiscard]]`, so review keeps this rule.

### Vectors

- No erasing one element at a time in a loop: mark, then `VectorUtil::removeIf` (order kept) or `removeAtSwap` (order free).
- Dedupe with a set, not `contains` before `push_back`; when order is free, collect then `VectorUtil::sortUnique` once.

## Parallel-safe code

Anything that may run in parallel (job system, cooker, ticks, test processes) shares no mutable place.

- **Tests** never write to `Bin`, a fixed file name or a fixed port: per-run temporary path, sockets on port 0.
- **Asset importers**: `importSource` is `const` and stateless; no mutable `static` or global cache in editor asset code.
- **Cook and import** produce the same bytes serially or in parallel: each job writes its own slot, a serial pass merges in a fixed order.
- **Ticks, services, workers** get what they need passed in. A tick touches only its own object ([TickPlacement](TickPlacement.md)). A worker body never touches containers another worker may resize.
- **No mutable statics.** A changing function-local static, namespace-scope variable or `static` data member is a hidden singleton: put the state in its owner; an id issuer is an `IdentityToken` or the owner's counter; a one-time warning is `SW_LOG_*_ONCE_ID`. Lint `mutable-statics` enforces; a real process-lifetime floor goes in `Config/Rules/Lint/MutableStatics.json` with `_why`.
- A job-style interface declares what it reads and writes as pure virtuals with no default (`getPoseAccess`).
- **Lazy initialization** is a function-local `static const` or `std::call_once`, never a `static bool s_bTried`.

## Parallel and async — which tool where

Pick by the shape of the work, not today's object count. APIs: `Source/Core/Task/TaskManager.h`, `Engine/Foundation/EngineParallel.h`.

| Work | Tool |
| --- | --- |
| N independent items inside one step | `engine::runParallelByCost( count, body )` — body reads shared input, writes only `out[index]`; shared writes afterwards on the caller in list order. `runParallel( count, threshold, body )` only when per-item cost is known |
| One output slot per item | `engine::runParallelIntoByCost<T>( out, body( index, T& slot ) )` — never capture the output container (trips the race detector) |
| Several output slots per item, or a batch call per range | `engine::runParallelRangeInto( count, threshold, body( start, end, span<T0> out0, … ), … )` (`…ByCost`); inputs stay `const` |
| A sum, min, max or count | `engine::runParallelReduce( count, blockSize, identity, body, combine )` — same bits for any worker count; not atomics |
| A fork-join (`createStage` + one task per item + `waitStage`) | `runParallelByCost` instead |
| Work that outlives its frame | `emplaceTask` + `addToStage( ownerStage, … )`; the owner waits with `waitStage( stage, owner )`. One long-lived stage per owner. Never `waitAll` |
| B needs A's result | `whenAll( { a, … }, continuation )` or `a.then( … )`, not a loop asking `isCompleted` |
| A site that needs a background result before going on | Wait on the stage (`EditorBackgroundTask::waitAndTake`), never `take` + sleep |
| Every N ms (poll, retry) | The frame loop submits one task when the interval passed and the previous one finished (`isStageComplete`) |
| Waiting on the OS (socket, file I/O, file watch, stdin, child process, driver-serial PSO compile, DB driver, crash reporting, render thread) | Own thread (`sw::thread` or `DedicatedWorker<T>`), owner in the `ThreadCreationKey` friend list (`Core/Concurrency/thread.h`). Raw `std::thread` · `CreateThread` · `pthread_create`: lint `thread-owners` |

- **Workers read, the owner writes.** A body must not resize a container another body reads, take a lock the waiting caller may hold (the caller helps run tasks while waiting), or read a dirty transform (flush first when `hasDirtySceneTransforms()`).
- **Say why it is safe**: every parallel body comments what it reads, the one slot it writes, and where the shared write happens.
- **Sleep only for real time** (frame pacing, a fence without an event, a backoff before retrying a locked file).

## Thread and GPU resource ownership

Each object has one owning thread; review a change by its ownership map (object → thread, renderer → owner, device caller).

- **GPU resources** are owned by the render side with one create path and one destroy path:
  - changes after creation → **render proxy** (`Material` / `MaterialRenderProxy`);
  - never changes → **create once** (`Mesh`, terrain chunk);
  - the render side swaps the handle (streaming) → **publish** an atomic handle · SRV (`Texture2D`). Never cover two creators with a lock.
- **Render work from another thread** goes through `RenderThread::executeAndWait`. Render-side objects the render thread holds are handed over once in `RenderThread::attach` — no setters while running.
- **Load workers** do construction, deserialization and bookkeeping only; component hooks (`onRegister`, `onPostLoad`) run on the game thread at the end of the batch.
- **Offscreen scene renderers**: one renderer per scene, living as long as the scene (`OffscreenSceneRenderers`); only the PSO cache is shared.
- **Frame packets carry full state only; one-shot diffs live in `RenderFramePacket::_deltas`** (`RenderFramePacketDeltas`) — a packet may end without drawing. A new diff is a field of that struct (a size check fails the build until `appendFrom` · `clear` carry it).
- **Worker results and outside notifications**: a slot under a lock plus a generation number, lock contract in the type (`SW_GUARDED_BY`). Workers produce bytes; the game thread owns the object.
- **Scene registries** (`ComponentRegistry`, `CameraRegistry`, `TagIndex`): writes take the lock; a read window (`ComponentRegistry::View`) is valid only while nothing registers.

## Recurring defect shapes

Change the type or API so the shape cannot be written, not one symptom at a time.

1. **Identity from a number, sequence or path string without a generation** (`name#index`, ids restarting at 1, 32-bit name hashes, a cache keyed by an object's address). Use a stable id with a generation, a GUID for assets, or the object's `IdentityToken`.
2. **One fact known in two places** (writer and reader versions, a CPU count beside a GPU list). Keep one owner; the other side reads it.
3. **Failure or truncation not returned as a value.** Return `sw::expected` / `[[nodiscard]]` ([Errors](Errors.md)).
4. **Platform hooks that still compile when left out.** Make the hook pure virtual.
5. **Saves and settings with no version upgrade.** Every saved format carries a version on the type (`REFLECT( Version = N )` + `SW_TYPE_VERSION_PATCH`; file kinds `SW_SAVE_FORMAT_UPGRADE` / `SW_ASSET_FORMAT_UPGRADE`). Runtime state fields are `PROPERTY( RuntimeState )` read by `StateIO::reflected`.
6. **State changes handled by call-order rules.** Put the state in a value the next step reads.
