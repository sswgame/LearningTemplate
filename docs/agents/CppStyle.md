# CppStyle

> 결론: C++ 를 쓸 때의 규칙 — 모양, 상수, 실패 값, 도우미 자리, 헤더 · 층 · 서비스, 구성 차이, 있는 장치, 리플렉션, 병렬 · 소유, 되풀이 결함. 린트 · 컴파일이 강제하는 줄은 "강제" 로 표시한다. 틱 · 게임플레이는 [Gameplay](Gameplay.md), 이름은 [NamingCpp](NamingCpp.md), 테스트는 [Testing](Testing.md).

## C++ style

### Layout and branches

- `.clang-format` (`py -3 -m Scripts format`). A function body goes on its own lines unless defined inside a class body.
- **Booleans**: `if ( _bValid == false )`, never negation; compare pointers to `nullptr`. `uint8` flags use `SW_TRUE` / `SW_FALSE`.
- **Braces**: none for a single-line `if` body; if one branch of an `if`/`else` chain needs braces, all take them. Loops always take braces.
- `switch` over an enum: all enumerators handled → no `default:` (컴파일 `-Werror=switch` · `covered-switch-default` 강제), otherwise `default:`. A `case` with more than one statement takes braces, and then every case with a body does.
- No `if` initializers; name an unclear or three-part condition in a local. Range checks read `kMin <= value && value <= kMax`.

### Constructors and fields

- 초기값은 한 곳에: with a constructor, initialize every field there in declaration order, braces, one per line, leading `,`. An iterator pair takes parentheses.
- Header defaults only for a `= default` / inline default constructor, a delegating constructor, or a type with no constructor.
- A field default-initialization leaves indeterminate gets a value in every constructor (copy · move included) or a header default; byte buffers are exempt.
- **Bit packing**: order fields to minimize padding; a flag is `uint8 _bX : 1`, kept together per access section (린트 `cpp-shape` 강제). A type with no constructor (an aggregate) keeps `bool _bX{ false }` — a C++17 bit-field cannot have a default member initializer; the lint skips such types. In a type with a constructor, `bool` + `NOLINT(sw-bool-flag): <reason>` only when its address is taken or it is atomic.
- An aggregate (built by braces, no constructor) keeps `bool _bX{ false }`; any other type with a flag writes the constructor.
- Declaration order: public fields; constructors; `initialize`/`shutdown`; `process`; getters/setters; private functions; private fields last (린트 `cpp-shape` 강제).

### Types, strings, functions

- `auto` only for iterators, structured bindings or similarly complex types. Lambdas only when they buy performance. `const` wherever free.
- **Strings**: `formatstring` and `%#` (no `operator+` — 컴파일 강제). Text used once: `fixed_string<constant::buffer::kMaxN>`; text kept or never cut (path, key, serialized): `StringBuilder::appendFormat`. No helper returns a formatted `string`. Booleans in text: `StringUtil::tryParseBool`.
- No wrapper with one caller; change the existing function's shape.
- **Inheritance: one base** (린트 `cpp-shape` 강제). Expose an interface by holding the implementation as a member and registering it (`SubsystemScope::exposeMember`, `SW_MODULE_PART`); a capability is a separate object holding a reference; a per-build choice is a CMake file choice.
- Split a class only into bundles with their own state and rules; never split a file for size alone.
- Vectors: no erase-in-loop (mark, then `VectorUtil::removeIf` · `removeAtSwap`); dedupe with a set or `VectorUtil::sortUnique`, not `contains` before `push_back`.

### Registries and failure

- Static registration tables are `RegistrationList` · `RegistrationSlot<T>` with `SW_DEFINE_*` / `SW_STATIC_REGISTRATION` registrars, removed at module unload; a cached reader compares `getGeneration()`.
- A name-keyed registry does not check empty or duplicate names with its own `if`: `RegistrationList` (`bRequireName`), `NamedEntryRegistry<T>` (named static entries found across modules) or `NameRegistry`, and the caller reports `add`. Duplicate ids of one kind in XML are errors.
- Subscriptions go through `RegistrationList::addSubscribed`, `ItemSubscriptionList<T>` or a `MulticastDelegate`; never turn an id back into an address.
- 델리게이트는 `SW_DELEGATE_FUNCTION` · `SW_DELEGATE_METHOD` · `SW_DELEGATE_LAMBDA` 로 붙인다(린트 `cpp-shape` 강제). 꺾쇠 안 쉼표가 있으면 `SW_DECLARE_DELEGATE` 별칭.
- Cannot go on: `reportFatalError( "...%#", value )` (below the log tier `SW_LOW_LEVEL_FATAL`), never `std::abort()`.
- Never discard a graphics device result: `isVkSuccess` / `isHrSuccess`, `requireVk` / `requireHr`, or `FAILED()` with a comment (SDK types cannot be `[[nodiscard]]` — review keeps this).

## Constants

한 의미는 한 곳에만 둔다. 이름 없는 리터럴은 린트 `magic-literals` 래칫이 막는다.

| Used in | Put it in |
| --- | --- |
| One `.cpp` | That file's `XxxInternal` struct as `static constexpr` (or the function) |
| One module | The owning type (`MeshAssetFormat::kExtension`) or the module namespace in its types header. No per-folder `*Constants.h` |
| A contract between modules or backends | `sw::constant::<category>` in its layer's file (`Constants.h`, `EngineDefines.h`, `RHIDescriptors.h`, `ReflectionConstants.h`); shader ↔ C++ in `binding_slots.hlsli`. A per-side copy is the bug |

- One home: math constants `MathUtil`; gravity `PhysicsSystem::getConfiguredGravity`; hashes `HashUtil`; FourCC `FourCCUtil::make`; units `constant::time::` · `constant::memory::` (never `1e9`, `1024 * 1024`); buffer sizes `constant::buffer::kMax*`, `constant::file::kMaxPathSize`.
- **Sentinels:** "not found" is `constant::invalid_index::*` or a domain name from it, a value never stored is `sw::optional`; "unlimited / default" is a named constant plus a predicate on the owning type. Naming a sentinel never changes saved or replicated bytes.
- No aliases of another constant; a local name only when it adds meaning.
- **Tuning values are data** (`PROPERTY` defaulted in the constructor, or the kit's settings · catalog); performance thresholds are named constants next to their use (`gv_*` if they must change without a rebuild); asset paths are data. Format, protocol and math tables stay in code.
- A missing required value stops with an error — no fallback (`*OrDefault`, `fallback` parameters, discarded load results).
- `kPascalCase` with the unit unless the type says it. Byte formats (save, state, cooked asset, cache, store record) declare one `static constexpr FormatDef kFormat{ "TAGS" };` (state sections `kStateFormat`) and `kExtension` on their owning type: an upgradable format passes its step array (the version is the step count), others raise the number. `FormatDef` writes, checks and refuses the head — no hand-written magic / version compare.
- Literals stay literal in obvious `0` · `1` · `-1` · `2`, test expectations, table rows, log · assert strings, reflection metadata, all-bits masks.
- **The ratchet** (`Config/Rules/Lint/MagicLiteralsBaseline.json`): grown → name it or move it to data; shrunk → `py -3 -m Scripts lint magic-literals --ratchet`; raise only with `--accept <file…>`; a false positive gets a `--self-check` case.
- 프로세스 종료 코드는 `Config/Engine/ProcessExitCodes.json` 한 곳 + 생성기.

## Errors

값을 못 줄 수 있는 함수는 `sw::expected<T, E>`(`[[nodiscard]]`) 한 벌을 돌려준다(린트 `result-types` 강제).

| Fails because … | Return |
| --- | --- |
| the thing is not there (one reason) | `sw::optional<T>` |
| several reasons, or the caller reports why | `sw::expected<T>` (`unexpected( Error{ ErrorKind::…, "message", detail } )`) |
| several reasons, no value | `sw::expected<void>` |
| the reason carries data | `sw::expected<T, DomainError>` |

- Branch on `kind()`, never on text. `message()` is a fixed English literal; log where reported with `SW_LOG_ERROR_ID`; add context with `prependDetail`.
- No output parameter for the reason (`string& outError`); a multi-problem check fills a caller's list or returns a report. A reply text written on success and failure (`outReply`) stays.
- No sentinel for "none": lookups return `sw::optional<uint32>`; closed slot sets are scoped enums with a none enumerator. Only a stored or wire format keeps `-1`, named and written with `value_or`.
- No `std::optional`; no `value_or` fallback for a required value.
- Inside `namespace sw` write `unexpected( … )`; with only `using namespace sw;` write `sw::unexpected( … )` (MSVC `<eh.h>`).

## Helpers

먼저 주인 타입을 찾는다. 첫 "예" 에서 멈춘다.

1. **주인 우선 — one type owns it?** A function over one type's data or invariants is its member (`static` / `private` as needed); static-only types or `vector<T>` functions beside a data type belong to it (린트 `cpp-shape` 규칙 4 강제); a `.cpp` state struct mutated by static functions becomes a class (린트 `cpp-shape` 규칙 10 강제).
2. **No owner, several TUs** → a `XxxUtil` static struct in a header (never `Internal`).
3. **No owner, one `.cpp`** → that file's anonymous namespace.

- Before adding a struct, search lower layers for the same shape (`Ray`, `HTTPHeader`). No argument-only struct unless there are many arguments.
- No new `.h`/`.cpp` pair until a second user, reflection, a registration macro or a test needs a header. A header-less `.cpp` is only a self-registering unit, a platform implementation or a module entry.

### Internal 규칙

- TU-only helpers: constants only → anonymous namespace; one function → a free function there with a file-unique name; more → one `XxxInternal` struct named after the TU (`D3D11RHIDevice.cpp` → `D3D11RHIDeviceInternal`).
- One class definition per named `namespace` block.
- One non-empty anonymous namespace per `.cpp` holding everything TU-local, no free `static` functions (린트 `cpp-shape` 규칙 3 강제).

## Includes and layers

- Headers prefer forward declarations (`ObjectFwd.h` · `ReflectionFwd.h`), never include ThirdParty headers. Diet headers have a fan-in ceiling (린트 `header-include-limits` 강제); a diet commit adds the ceiling.
- `.cpp` include order (clang-format 강제). Includes are full paths from a root or bare same-folder names.
- Prefer `StdHeaders.h` · `PlatformOSHeaders.h`; heavy rarely-used standard headers are included by their user. Platform includes go in **one** `#if SW_PLATFORM_…` chain after the others.
- **Layers:** a lower layer never includes an upper one (`#error` 껍데기 · 린트 `layer-includes` 강제). Fix the dependency, not the table; reach up through an interface or function pointer. Below `Log` report with `SW_LOW_LEVEL_*`.
- A kit, editor extension or game sees other modules only through its manifest `_listDependency` closure; kit dependencies live only in `*.module.json`.
- **Dedicated server** sees no `Engine/Renderer` · `UI` · `Audio` and no `Client/` folder: shared code reaches client work through interfaces (`IEngineRenderDriver`, `ISoundPlayer`, `IGameClientPart` …). The client builds no `Server/` folder; an in-process server starts through `IOnlineLocalServerFactory`.
- **Game code** sees no `Engine/Graphics/RHI` · `Shader` header except `RHIEnums.h`; a game-visible GPU constant is its own constant with a `static_assert` in its `.cpp`.
- Platform and compiler only through `SW_PLATFORM_*` · `SW_X64` · `SW_ARM64` · `SW_COMPILER_*`. Build target `sw::build::kWithClientCode` · `kWithServerCode` only in `.cpp` bodies, never in reflection or layout. A client/server feature splits into `GF_<X>`, `GF_Server_<X>`, `GF_Client_<X>`.

### Locks, services, memory

- Locks are `sw::mutex` · `sw::shared_mutex` · `SpinLock` with `ScopedLock.h` guards, never `std::` locks; contracts `SW_GUARDED_BY` · `SW_REQUIRES_LOCK` · `SW_EXCLUDES_LOCK`, "this thread only" is a `ThreadRole`. Leaf locks: never log or call out while holding one.
- `SpinLock` only for a leaf lock taking no other lock. No new lock-free structures. A fixed-capacity queue never stands in for an unbounded list.
- Services: `getSubsystem<T>()` / `findSubsystem<T>()` on `engine::` · `game::` · `editor::`; no named accessors or `getInstance()`. One Engine and one GameInstance per process.
- A new engine service declares `SW_SERVICE_SLOT` and registers with one `SW_ENGINE_SERVICE_SUBSYSTEM` · `SW_ENGINE_SUBSYSTEM` · `SW_ENGINE_SERVICE` line; a dynamic module uses `SW_MODULE_SUBSYSTEM`. Tests inject through scopes (`game::ScopedGameScope`).
- Online protocols build on `OnlineProtocolService`.
- Placement construction is `sw_placement_new`; heap allocation goes through an owning type.
- **Borrow with a pointer, keep with a handle:** a `GameObject*` / `Component*` you do not own is valid only in the current call; keep `GameObjectHandle` / `ComponentHandle`. Saved state refers to other objects by id, never by name.

## Build configuration switches

모든 구성에서 컴파일되는 가장 작은 꼴을 고른다.

| The difference is ... | Use |
| --- | --- |
| A value only — both branches compile everywhere | `build::kShipping` (or `kWithClientCode` · `kWithServerCode` · `kStaticModules`) with `if` / `if constexpr` |
| A few lines needing a header or module one configuration lacks | `#if defined( SW_SHIPPING )` (or `SW_WITH_DYNAMIC_MODULES`) at that spot |
| A whole feature (automation runner, DevTools, module reload, editor) | Self-registers from its own folder; CMake excludes the folder; startup fails with the reason when the run asked for it |

- **Platform chains** end in `#else` + `#error "Unsupported platform"` (린트 `platform-chains` 강제). Scripts read the host OS only from `host_platform.py`. Platform folders are chosen by CMake; no whole-file `#if`.
- No file-name switches or stub files (`*Shipping.cpp`, `<Name>Off.cpp`); dev code needing headers Shipping lacks goes in a `Dev/` folder listed in `DevFolders.json` (configure 강제).
- Never re-derive the configuration from other macros; no alias macros (컴파일 강제).
- Costly diagnostics compile into every non-Shipping build and are chosen at startup (`-rhi-validation` · `-rhi-dred` · `-deadlock-detection`); cheap checks under `SW_DO_CHECKS`; Debug-only via `build::kDebug` + `if constexpr`.
- **Test and dev hooks stop at Release** (`shipping-check` 강제): per-frame numbers through `SW_PROFILE_SCOPE` · `SW_PROFILE_COUNT`, test-only counts as `DevCounter`, no measurement · watch · test accessor as a public API. Dev substitutes register through `DevOnlineExtension.h`; dev config lives under `Config/Dev`.
- Reflection editor metadata lives in dev-only `.gen.meta.cpp`; layout is the same in every configuration.

## Existing facilities

새로 짜기 전에 이 장치를 쓴다. 없는 기능은 Core · Engine 에 하나 더한다. 목록 밖은 graphify 로 찾는다.

- Log `SW_LOG_*_ID` (`"Area.Event"`, once `*_ONCE_ID`), command-line output `StandardOutput::write`; assert `SW_ASSERT`. Never `printf` · `assert`.
- Time `MonotonicClock` · `Stopwatch` · `Deadline` · `WallClock`; random `MathUtil::getRandom*`, deterministic `XorShift32` · `GameRandom`. Never `std::chrono` clocks · `rand`.
- Files only through `FileUtil` (`[[nodiscard]]`, writes pass `isWriteAllowed`) and `AsyncFileIO`; disk I/O takes `const AbsolutePath&` (string overloads are private). Path rules: [DataInheritance](DataInheritance.md) "경로 · 리소스 신원".
- Environment `ProcessEnvironment::tryRead`, child process `Process::execute`, shared library `ModuleLibraryUtil::loadLibraryAt`. Never `getenv` · `system` · `dlopen` · Python `subprocess`.
- Text `StringUtil` · `HexUtil` · `Base64Util` · `hashed_string`; never `atoi` · `sscanf` · hand split loops.
- Binary `ByteReader` · `Archive` · `BitReader` return values, no output parameter (the old overloads do not exist); a target outside the function gets a local moved in only after the failure check. Never a hand cursor or `.resize( countFromFile )`.
- Diagnostics: `SW_PROFILE_*` per frame, `DevCounter` cumulative — never a counter field + getter on a runtime class.
- Callbacks `Delegate` · `MulticastDelegate`, events `EventDispatcher`, background work `TaskManager`, threads `sw::thread`. Never `std::function` · `std::async`.
- Containers `Core/Container/`; handles `SlotHandleTable` · `GameObjectHandle`; identity `IdentityToken::make`; memory `MemoryBlock` · `LinearAllocator` · `PoolAllocator`. Never `std::` containers · `malloc` · raw `new`.
- Settings `ConfigManager::getConfig<T>`, console variables `SW_GLOBAL_VARIABLE`, user text `LocalizationManager`. Player input through `InputManager` actions.
- GPU: `SW_GPU_PRINT` · `SW_GPU_ASSERT`, `GPUPassScope`, `dispatchThreads`, `RHIMeshVertexStreams`, `VertexInput`.
- Lint exceptions through `rule_tables.py` `ExceptionTable` · `Baseline`; App arguments from scripts through `app_arguments.py`.

## Reflection

- 캐시 · 등록 · 언로드를 바꾸면 무효화 순서와 모듈 재등록 테스트를 함께 쓴다.
- 내장 형식은 `ReflectBuiltins.xxx` 한 곳에 적는다. 손 처리기는 Layout 이 none 인 State 형식만.
- 세이브에서 핸들을 빼지 않는다. 집합 · 맵은 RuntimeState 로 쓰지 않는다. 중첩 형식은 public 으로만 반사한다.
- FUNCTION: 주인 밖을 바꾸는 함수는 대상을 인자(핸들)로 드러내 편집 명령을 탄다. `callWithText` · `bindEventToFunction` 배선은 허용 목록 · 수명 · 핫 리로드 검증과 함께만.
- 복제: `Quantize` 키 · 양자화 형식을 두지 않고, 손 본문 넷 메시지를 고정 배치로 옮기지 않는다.
- 손으로 두는 이름 표: `DialogBackend`, `ShaderBindingValidatorInternal::RegisterClass` · `GlNamespace`, `TextChoiceKind`, `RichTextParser::TagKind`, `StageReportResult`, `WaterBodyComponent::MaterialNames` · `FoliageComponent::ViewMaterialNames`.

## Parallel and async

Pick by the shape of the work (N 비례 독립 · 사슬 · N²), not today's count. APIs: `TaskManager.h`, `EngineParallel.h`. Never a new thread pool. Anything that may run in parallel shares no mutable place.

| Work | Tool |
| --- | --- |
| N independent items in one step | `engine::runParallelByCost` — body writes only `out[index]`, shared writes afterwards in list order |
| One or several output slots per item | `runParallelIntoByCost<T>` · `runParallelRangeInto` — never capture the output container |
| Sum, min, max, count | `runParallelReduce` — same bits for any worker count, not atomics |
| Work outliving its frame | `emplaceTask` + `addToStage( ownerStage )`, owner waits with `waitStage`; never `waitAll` |
| B needs A | `whenAll` / `then`, not polling `isCompleted`; a site needing the result waits on the stage, never `take` + sleep |
| Every N ms | The frame loop submits one task when the interval passed and the previous finished |
| Waiting on the OS (socket, file I/O, child process, render thread …) | Own thread (`sw::thread` / `DedicatedWorker<T>`) with owner in `ThreadCreationKey` (린트 `thread-owners` 강제) |

- **Workers read, the owner writes:** no resizing containers others read, no lock the waiting caller may hold, no dirty transforms. Every parallel body comments what it reads, its one slot, and where the shared write happens.
- Importers are `const` and stateless; cook and import produce the same bytes serially or in parallel (own slot, fixed-order merge).
- **No mutable statics** (린트 `mutable-statics` 강제): state goes in its owner; lazy init is a function-local `static const` or `std::call_once`.
- A job-style interface declares reads and writes as pure virtuals (`getPoseAccess`).
- Sleep only for real time. 측정 전에는 결함으로 세지 않는다: 헤더 전파 · 시작 시간 비중 · 배열 복사 · 프레임 경로 intern.

## Thread and GPU resource ownership

Each object has one owning thread; review a change by its ownership map.

- **GPU resources** have one create and one destroy path on the render side: changes after creation → render proxy; never changes → create once; streamed → publish an atomic handle. Never cover two creators with a lock.
- Render work from another thread goes through `RenderThread::executeAndWait`; render-side objects are handed over once in `RenderThread::attach`.
- Load workers construct and deserialize only; component hooks run on the game thread at the end of the batch.
- Offscreen scene renderers: one per scene for its lifetime; only the PSO cache is shared.
- **Frame packets carry full state; one-shot diffs live in `RenderFramePacket::_deltas`** (빌드 크기 검사 강제).
- Worker results: a slot under a lock plus a generation (`SW_GUARDED_BY`); workers produce bytes, the game thread owns the object.
- Scene registries: writes take the lock; a read window (`ComponentRegistry::View`) is valid only while nothing registers.

## Recurring defect shapes

Change the type or API so the shape cannot be written.

1. **Identity from a number, sequence, path or address without a generation.** Use a stable id with a generation, an asset GUID, or `IdentityToken`.
2. **One fact known in two places.** One owner; the other side reads it.
3. **Failure or truncation not returned as a value.** `sw::expected` / `[[nodiscard]]`.
4. **Platform hooks that still compile when left out.** Pure virtual.
5. **Saves and settings with no version upgrade.** `REFLECT( Version = N )` + `SW_TYPE_VERSION_PATCH`, `FormatDef` steps, `SW_ASSET_FORMAT_UPGRADE` (from release on; before release every version stays 0 — `format-release` only reports); runtime state is `PROPERTY( RuntimeState )` via `StateIO::reflected`.
6. **State changes handled by call-order rules.** Put the state in a value the next step reads.
