# CppStyle

> 결론: C++ 를 쓸 때의 규칙 — 모양, 상수, 실패 값, 도우미 자리, 헤더 · 층 · 서비스, 구성 차이, 있는 장치, 리플렉션, 병렬 · 소유, 되풀이 결함. 린트 · 컴파일이 강제하는 줄은 "강제" 로 표시한다. 틱 · 게임플레이는 [Gameplay](Gameplay.md), 이름은 [NamingCpp](NamingCpp.md), 테스트는 [Testing](Testing.md).

## C++ style

### Layout and branches

- Follow `.clang-format` (`py -3 -m Scripts format`). A function body goes on its own lines; only a function defined inside a class body may stay on one line.
- Booleans: `if ( _bValid == false )`, never negation. Compare pointers to `nullptr` explicitly.
- `uint8` boolean members (`_b*`, bit-fields included) use `SW_TRUE` / `SW_FALSE`; real `bool` keeps `true` / `false`.
- **Braces**: none for a single-line `if` body; with `else` / `else if`, if one branch needs braces every branch takes them. Loops always take braces (empty body `{}`).
- **`switch` over an enum**: every enumerator handled → no `default:` (컴파일 `-Werror=switch` · `-Werror=covered-switch-default` 강제); some handled → `default:`. Reject out-of-range values where they enter. No per-file `#pragma` for switch diagnostics.
- **`case` bodies**: more than one statement (`break;` counts) takes braces; if any case takes braces, every case with a body does.
- No `if` initializers. Name an unclear or three-part condition in a local first.
- Range comparison puts the value in the middle: `kMin <= value && value <= kMax`.

### Constructors and fields

- 초기값은 한 곳에: with a constructor, initialize fields there (not in the header), in declaration order, with braces, one per line, later lines starting with `,`.
- An iterator pair takes parentheses (`_listValue( list.begin(), list.end() )`): braces pick `initializer_list`.
- Header defaults only for: a `= default` or inline default constructor, a delegating constructor, a type with no constructor.
- A field default-initialization leaves indeterminate (scalar, enum, pointer, arrays of those, `atomic<scalar>`, bit-field) gets a value in every constructor (copy and move included) or a header default. Byte buffers are exempt.
- **Bit packing**: order fields to minimize padding; a flag is `uint8 _bX : 1`; keep one access section's bit-fields together (린트 `cpp-shape` 강제). A flag stays `bool` with `NOLINT(sw-bool-flag): <reason>` only when its address or a `bool&` is taken, it is atomic, or its type is an aggregate.
- An aggregate is built only by braces, has no constructor, and keeps `bool _bX{ false }`. Any other type with a flag writes the constructor.
- Class declaration order: public member variables; constructors/destructor; `initialize`/`shutdown`; `process`; getters/setters; private functions in a separate section; private member variables last (린트 `cpp-shape` 규칙 1 강제).

### Types, strings, functions

- `auto` only for iterators, structured bindings or similarly complex types. Avoid lambdas unless they buy performance. `const` wherever it does not cost performance.
- **Strings** are built with `formatstring` and `%#` (`sw::basic_string` has no `operator+` — 컴파일 강제).
  - Text used once (log argument, UI label, a path opened right away): `fixed_string<constant::buffer::kMaxN>` via `formatstring( text.data(), text.capacity(), … )`.
  - Text that is kept or must never be cut (path, key, serialized text): `StringBuilder::appendFormat`, handed over as `string( builder.view() )` or `builder.view()`.
  - Log macros and `DebugHUDSectionWriter::addLineFormat` take the format themselves. No helper returns a formatted `string` (`formatToString` is a banned name). <!-- keep-old-name -->
- Read a boolean in text with `StringUtil::tryParseBool`.
- No wrapper function with one caller; change the existing function's shape instead.
- **Inheritance: one base** (린트 `cpp-shape` 강제, exceptions `Config/Rules/Lint/CppShape.json` `_multipleBase`). A subsystem offering an interface holds the implementation as a member and registers it (`SubsystemScope::exposeMember`, module `getPublishedPart`, `SW_MODULE_PART`); a third-party callback is a member adapter. A capability (dev reload, tooling) is a separate object holding a reference to the target; a per-build choice is a CMake file choice, not an interface.
- Split a large class only into bundles with their own state and rules (one class stays in one file); never split a file for size alone.

### Registries and failure

- A static registration table is a `RegistrationList` (`SW_DEFINE_REGISTRATION_LIST` / `SW_DEFINE_LOCKED_REGISTRATION_LIST`); a one-registration slot is a `RegistrationSlot<T>` (`SW_DEFINE_REGISTRATION_SLOT`). Registrars come from `SW_DEFINE_STATIC_REGISTRAR` / `SW_STATIC_REGISTRATION`, never hand-written. Every registration is removed at module unload; a reader that caches the list compares `getGeneration()`. On the user-configuration side the name is `Handler`.
- A name-keyed registry does not check empty or duplicate names with its own `if`: borrowed pointers go in a `RegistrationList` (`bRequireName`), owned values in a `NameRegistry`; the caller reports the result of `add`. XML 의 같은 종류 id 겹침은 오류다(코드 `add*` 교체는 유지).
- Never turn a `Subscription` id back into an address by hand: `RegistrationList::addSubscribed`, `ItemSubscriptionList<T>`, or a `MulticastDelegate` / `LockedMulticastDelegate`. No per-connection control block, global id table or hazard pointers for subscriptions.
- 델리게이트는 `SW_DELEGATE_FUNCTION( 형, &f )` · `SW_DELEGATE_METHOD( 형, &T::m, p )` · `SW_DELEGATE_LAMBDA( 형, 람다 )` 로 붙인다(린트 `cpp-shape` 가 맨 `create` 를 막고, `py -3 -m Scripts delegate-macros` 가 바꾼다). 형에 꺾쇠 안 쉼표가 있으면 `SW_DECLARE_DELEGATE` 별칭을 둔다.
- When the program cannot go on: `SW_FATAL( "...%#", value )` (below the log tier `SW_LOW_LEVEL_FATAL`), never `std::abort()`.
- Never discard a graphics device result (`VkResult`, `HRESULT`): `isVkSuccess` / `isHrSuccess`, `requireVk` / `requireHr`, or `if ( FAILED( … ) )` with a comment when failure is normal. SDK types cannot carry `[[nodiscard]]`, so review keeps this rule.

### Vectors

- No erasing one element at a time in a loop: mark, then `VectorUtil::removeIf` (order kept) or `removeAtSwap` (order free).
- Dedupe with a set, not `contains` before `push_back`; when order is free, collect then `VectorUtil::sortUnique` once.

## Constants

한 의미는 한 곳에만 둔다 — 둘째 사본은 한쪽이 바뀌는 날 어긋난다. 이름 없는 리터럴은 린트 `magic-literals` 래칫이 막는다.

| Used in | Put it in |
| --- | --- |
| One `.cpp` | That file's `XxxInternal` struct as `static constexpr` (or inside the function) |
| One module | The owning type (`MeshAssetFormat::kExtension`) or the module namespace in its types header (`audio::kSampleRate`). No per-folder `*Constants.h` |
| A contract between modules or backends | `sw::constant::<category>` in its layer's one file: `Core/Foundation/Constants.h`, `Engine/Foundation/EngineDefines.h`, `RHIDescriptors.h` (`render`), `ReflectionConstants.h`; shader ↔ C++ in `binding_slots.hlsli` (read as `shaderslot::k*`). A per-side copy with a "must equal X" comment is the bug |

- One home: π family · √2 · e in `MathUtil` (shaders `common.hlsli`); gravity `PhysicsSystem::getConfiguredGravity(Magnitude)`; golden ratio · splitmix64 · FNV-1a in `HashUtil`; four-character tags `FourCCUtil::make`; time and byte units `constant::time::` · `constant::memory::` (never `1e9`, `1024 * 1024`); fixed step `constant::time::kDefaultFixedStepSeconds` · `kDefaultMaxFrameSeconds`; `constant::network::kMaxPort`; shared online limits `OnlineLimit.h`; buffer and path sizes `constant::buffer::kMax16` .. `kMax8192`, `constant::file::kMaxPathSize` (never `char buf[64]`).
- **Sentinels:** "none / not found" is `constant::invalid_index::kInt32` (by slot type) or a domain name from it, and a value never stored or sent is `sw::optional`; "unlimited / all / default" is a named constant on the owning type plus a predicate (`ShopStockDef::kUnlimitedStock` + `hasFiniteStock`); sign and `-1..1` stay `-1`; external API values stay literal. Choose a sentinel by domain meaning and save · wire compatibility; naming one never changes saved or replicated bytes.
- No aliases (`kFrameCount = constant::render::kMaxFrameCountInFlight;`); a local name only when it adds meaning.
- **Tuning values are data:** gameplay tuning is a `PROPERTY` defaulted in the constructor or a field of the kit's settings / catalog struct (a constructor default of a `PROPERTY` is not a hard-coded value). Performance thresholds are named constants next to their use; one that must change without a rebuild becomes a `gv_*`. Asset paths are data — no `"engine/…"` literal in code. Format, protocol and math tables are specification and stay in code.
- A missing required value stops with an error — no fallback (`*OrDefault`, `fallback` parameters, discarded load results).
- `kPascalCase`; a value with a unit says it unless the type does (`kTimeoutSeconds`). A file or blob format declares `kMagic` · `kVersion` · `kExtension` on its owning type; a state section `kStateTag` · `kStateVersion`. Format versions stay with their format even when numbers match.
- Literal stays literal: `0` · `1` · `-1` · `2` in obvious uses, test expectations, initializer-table rows, log and assert strings, reflection metadata, a reflected `FUNCTION` default argument, an all-bits mask.
- **The ratchet** (린트 `magic-literals`, `Config/Rules/Lint/MagicLiteralsBaseline.json`): grown → name it or move it to data; shrunk → `py -3 -m Scripts lint magic-literals --ratchet`. Raise a baseline only with `--accept <file…>` (integrator merging pre-rule code). A false positive gets a `--self-check` case.
- 프로세스 종료 코드는 `Config/Engine/ProcessExitCodes.json` 한 곳 + 생성기 — `Scripts/common/exit_code.py` 와 합치지 않는다.

## Errors

값을 못 줄 수 있는 함수는 `sw::expected<T, E>`(`Core/Foundation/Expected.h`, `[[nodiscard]]`) 한 벌을 돌려준다(린트 `result-types` 강제, 예외 `Config/Rules/Lint/ResultTypes.json` `_exemptions`).

| The function can fail because … | Return | Failure |
| --- | --- | --- |
| the thing is not there (one reason) | `sw::optional<T>` | `return nullopt;` |
| several reasons, or the caller reports why | `sw::expected<T>` | `return unexpected( Error{ ErrorKind::InvalidArgument, "not a boolean", value } );` |
| several reasons, no value | `sw::expected<void>` | success `return {};` |
| the reason carries data the caller needs | `sw::expected<T, DomainError>` | `return unexpected( DomainError{ … } );` |

- Branch on `kind()`, never on the text. `message()` is a fixed English literal; `detail()` is copied inline. Log where it is reported: `SW_LOG_ERROR_ID( "Area.Event", "%#: %# (%#)", key, error.message(), error.detail() )`. Add context with `error.prependDetail( ": " ).prependDetail( path )`.
- **No output parameter for the reason** (`string& outError`, `string* pOutError`, a text output written only before a failure return). A check collecting several problems fills a caller's list or returns a report as the error. A reply text that answers on success *and* failure (`outReply` of a dev command) stays.
- **No sentinel for "none".** A lookup (`find*` · `pick*` · `select*`) returns `sw::optional<uint32>` (`optional<int32>` where the API indexes in `int32`). A closed set of slots is a scoped enum with a none enumerator; "one of a set or nothing" is `optional<Enum>`; several special values in one `int` become a kind enum plus the index. An API whose "none" means "nothing to do" takes the optional. Only a stored data or wire format keeps its -1: name it (`kNoSegment`) and write it with `value_or`.
- No `std::optional` (wrap a third-party one where it is called). No fallback through `value_or` for a required value.
- MSVC `<eh.h>` declares a global `unexpected()`: inside `namespace sw` write `unexpected( … )`, with only `using namespace sw;` (tests) write `sw::unexpected( … )`.

## Helpers

먼저 주인 타입을 찾는다. 첫 "예" 에서 멈춘다.

1. **주인 우선 — one type owns it?** A function that reads or keeps one type's data or invariants is that type's member (`static` if it needs no instance, `private` if only the type calls it). A static-only type beside a data type in the same header, or functions over `vector<T>`, belong to that type (린트 `cpp-shape` 규칙 4 강제, 예외 `_staticOwner`). A `.cpp` state struct whose public fields static functions change in sequence becomes a class with those functions as members (린트 `cpp-shape` 규칙 10 강제). Split a mixed `Util` among its owners.
2. **No owner, several translation units** → a `XxxUtil` static struct in a header. Never call these `Internal`.
3. **No owner, one `.cpp`** → that file's anonymous namespace.

- Before adding a struct: same shape and meaning already there (search lower layers by field types — `Ray`, `HTTPHeader`)? Use it. A struct that only carries one function's arguments is not needed unless there are many.
- Don't split what has one user: no new `.h`/`.cpp` pair until a second user, reflection, a registration macro or a test target needs a header. A `.cpp` without a header is only a self-registering unit, a platform implementation or a module entry point. An editor console command lives next to the feature's command file.

### Internal 규칙

- Ownerless TU-only helpers: constants only → constants in the anonymous namespace; one function → a free function there with a file-unique name; two or more functions, or a function with constants → one `XxxInternal` struct named after **the translation unit** (`D3D11RHIDevice.cpp` → `D3D11RHIDeviceInternal`) — unity builds keep names unique.
- One class or struct definition per named `namespace` block (close after each `};`, reopen).
- At most one anonymous namespace per `.cpp`, non-empty, a separate `namespace sw` block above the implementation; everything TU-local lives in it (린트 `cpp-shape` 규칙 3 강제). No free `static` functions. Out of it: exclusive preprocessor branches (count as one), `REFLECT`-generated types, `SW_GLOBAL_VARIABLE`, `main`, functions declared in a header.

## Includes and layers

- Headers prefer forward declarations (`ObjectFwd.h` · `ReflectionFwd.h`); never include ThirdParty headers from a project header. Diet headers have a fan-in ceiling (린트 `header-include-limits` 강제).
- 헤더를 줄이는 커밋은 `py -3 -m Scripts header-fanin --json` 전후를 견주고 점수 합이 줄지 않으면 하지 않는다. 다이어트한 헤더는 `Config/Rules/Lint/HeaderIncludeLimits.json` 에 상한을 더한다.
- `.cpp` include order and groups: clang-format 강제 (`"pch.h"`, matching header, then groups); a header others depend on goes first through `SortPriority` in `.clang-format`.
- Every project include is a full path from a root (`Source/`, `Content/`, the generated folder, `Tools/`) or a bare name in the same folder.
- Prefer `Core/Foundation/StdHeaders.h` and `PlatformOSHeaders.h`. Heavy standard headers few files use (streams, `<random>`, `<regex>`, `<future>`, `<variant>`, `<condition_variable>`, `<queue>` …) are included by the file that uses them (`<iosfwd>` for stream operators).
- Platform-specific includes go in **one** `#if SW_PLATFORM_… / #elif / #endif` chain after the unconditional includes.
- **Layers:** a lower layer never includes an upper one (`#error` 껍데기 · 린트 `layer-includes` 강제, 표 `Config/Rules/Layers/*.toml`). Fix a gate error by changing the dependency, not by adding an exception. A lower tier that must reach up takes an interface or a function pointer (`ILockObserver`). Core tiers below `Log` report with `SW_LOW_LEVEL_*`.
- A kit, editor extension or game sees another module's headers only through its manifest's `_listDependency` closure. Kit dependencies and module kinds live only in `*.module.json` — never copy them into another registry.
- **Dedicated server** sees no `Engine/Renderer` · `Engine/UI` · `Engine/Audio`, no `Client/` folder: shared code reaches client work only through interfaces (`IEngineRenderDriver` · `IEngineUIDriver` · `IEngineAudioDriver`, `ISceneRenderBinding`, `ISoundPlayer`, `IGameClientPart` …); game code asks `IInputCaptureQuery` · `IPlayerNotificationSink` · `IUILayerVisibility`, not `UISystem`. The client target builds no `Server/` folder; an in-process server starts only through `IOnlineLocalServerFactory`. A shared type does not live in a folder named after one side.
- **Game code** sees no `Engine/Graphics/RHI` or `Engine/Graphics/Shader` header, even transitively; the one opened RHI header is `RHIEnums.h`. A game-visible GPU constant is its own constant checked by a `static_assert` in its `.cpp`.
- Ask platform with `SW_PLATFORM_WINDOWS` · `_LINUX` · `_ANDROID` · `_WEB` · `_POSIX`, `SW_X64` · `SW_ARM64` (never `_WIN32`, `__linux__`); compiler with `SW_COMPILER_CLANG` · `_MSVC` · `_GCC` (never `_MSC_VER`); "may I use an MSVC extension" is `SW_PLATFORM_WINDOWS`. Build target `sw::build::kWithClientCode` · `kWithServerCode` · `kTargetName`, only in `.cpp` bodies — never gate reflection declarations or class layout on them. A client/server feature splits into `GF_<X>`, `GF_Server_<X>`, `GF_Client_<X>`.

### Locks, services, memory

- Lock with `sw::mutex` / `sw::shared_mutex` / `SpinLock` and the guards in `Core/Concurrency/ScopedLock.h`, never `std::` locks (`-Wthread-safety` does not see them). Contracts on the declaration: `SW_GUARDED_BY` · `SW_REQUIRES_LOCK` · `SW_EXCLUDES_LOCK`; "this thread only" is a `ThreadRole` member. Locks are leaf locks: never log or call out while holding one.
- `SpinLock` only for a leaf lock that takes no other `sw::` lock inside (it bypasses the deadlock detector). Use existing lock-free structures, never write a new one. A fixed-capacity queue never stands in for an unbounded list.
- Reach a service through its lifetime window: `getSubsystem<T>()` (must exist) or `findSubsystem<T>()` (may be missing) on `engine::`, `game::`, `editor::` or the owner. No named accessors, no `static getInstance()`. One Engine and one GameInstance per process (a duplicate is a startup error); several game instances would need an explicit game context, never thread-local.
- A new engine service declares its slot (`SW_SERVICE_SLOT( T, Required | Optional )`) and registers with one line: `SW_ENGINE_SERVICE_SUBSYSTEM( T, GameVisible | HostOnly, Services | Startup, ( TypesBuiltFirst... ), factory )`, `SW_ENGINE_SUBSYSTEM( … )` (no slot) or `SW_ENGINE_SERVICE( T, … )` next to its `bind`. List a type in the parentheses only when the factory looks it up. Registration `after` and `getSubsystemDependencies()` mean different things — do not merge them.
- A dynamically loaded module owns its services as `SW_MODULE_SUBSYSTEM( Process | Engine, T, PublishedTypes... )`; a lower layer finds a higher layer's implementation by interface type. A slotless host subsystem the editor must find declares `kModuleVisibility = SubsystemModuleVisibility::HostModules`. Tests inject mocks through scopes (`game::ScopedGameScope`, `editor::ScopedEditorScope`).
- Online protocols build on `OnlineProtocolService`; do not add a second server abstraction.
- Placement construction is `sw_placement_new( pMemory ) T( ... )`. Heap allocation goes through an owning type (`make_unique`, `vector`, `MemoryBlock`); where ownership leaves the type system: `make_unique<T>( ... ).release()`.
- **Borrow with a pointer, keep with a handle.** A `GameObject*` / `Component*` you do not own is valid only inside the current call; anything kept across frames holds a `GameObjectHandle` / `ComponentHandle`. Saved state never points at another object by name: a parent reference is the parent's id, resolved by `ObjectStateBatch`.

## Build configuration switches

모든 구성에서 컴파일되는 가장 작은 꼴을 고른다 — 짓지 않은 구성이 썩지 않게.

| The difference is ... | Use |
| --- | --- |
| A value only — both branches use symbols every configuration has | `build::kShipping` (or `kWithClientCode` · `kWithServerCode` · `kStaticModules`, `Core/Foundation/BuildInfo.h`) with `if` / `if constexpr`. What the dead branch calls needs no definition |
| A few lines, one branch needs a header, type or module the other configuration does not build | `#if defined( SW_SHIPPING )` (or `SW_WITH_DYNAMIC_MODULES`) at that spot, in one file |
| A whole feature the configuration does not have (automation runner, DevTools host, module reload, editor module) | The feature registers itself (`SW_MODULE_SUBSYSTEM` publishing an interface) from its own folder; CMake excludes the folder. The host skips it when absent; when the run asked for it (`-scenario`, `-EnableEditor`), startup fails with the reason |

- **Platform chains** end in `#else` + `#error "Unsupported platform"` and name the platform in every branch (린트 `platform-chains` 강제). Scripts read the host OS only from `Scripts/common/host_platform.py`. Platform folders are chosen by CMake (`sw_filter_platform_sources`); no whole-file `#if` in a platform-folder `.cpp`.
- **No file-name switches** (`*DevCommands.cpp`, `*Shipping.cpp`) and no stub implementation files (`<Name>Off.cpp`): self-registering dev code stays in the feature's `.cpp`; code needing headers Shipping does not build goes in a `Dev/` folder listed in `Config/Rules/Layers/DevFolders.json` (configure 강제).
- Never re-derive the configuration from other macros (`#if !defined( _DEBUG )`); no alias macros (old client/server macros are poisoned — 컴파일 강제).
- Costly diagnostics compile into every non-Shipping build and are chosen at startup (`-rhi-validation` · `-rhi-dred` · `-rhi-breadcrumbs` · `-deadlock-detection`, defaults in `Config/Engine/EngineConfig.json` `_rhi`). Cheap checks under `SW_DO_CHECKS`. Too costly for Release → `build::kDebug` with `if constexpr`, never `#if defined( SW_DEBUG )` (verification builds no Debug).
- **Test and dev hooks stop at Release** (`shipping-check` 강제). Per-frame numbers through `SW_PROFILE_SCOPE` · `SW_PROFILE_COUNT`; a count only tests read is a `DevCounter` (getter returns `const DevCounter&`); no measurement, watch or test accessor as a public API. Dev-only behaviour (plain ports, dev certificates, fake providers) goes through a registered dev substitute (`Core/Network/DevOnlineExtension.h`). Dev-only config fields live under `Config/Dev`, not `EngineConfig`.
- Reflection editor metadata lives in dev-only `<이름>.gen.meta.cpp` tables; struct layout is the same in every configuration.

## Existing facilities

새로 짜기 전에 이 장치를 쓴다. 없는 기능은 지역 사본 대신 Core · Engine 에 하나 더한다. 목록에 없는 장치는 graphify 로 찾는다.

- Log `SW_LOG_*_ID` (`"Area.Event"` id; once `SW_LOG_*_ONCE_ID`), never `printf` · `std::cout`; below the log tier `SW_LOW_LEVEL_*`; command-line output `StandardOutput::write`. Assert `SW_ASSERT` / `SW_LOG_ASSERT`, never `assert`.
- Time `MonotonicClock` · `Stopwatch` · `Deadline`, wall time `WallClock` (services take `nowMs`), never `std::chrono` clocks · `localtime`. Random `MathUtil::getRandom*`; deterministic `XorShift32` · `HashUtil::splitMix64Next` · `GameRandom`, never `rand` · `std::mt19937`. Angles `MathUtil::wrapAngle` · `lerpAngle`.
- Files and paths only through `FileUtil` (`readFile` · `writeTextFile` · `collectFiles` · `getFileStamp` …, `[[nodiscard]]`, writes pass `isWriteAllowed`) and `AsyncFileIO`; never `std::filesystem`, `fopen`, streams, `rename`, OS calls. `FileUtil` disk I/O takes only `const AbsolutePath&`; lift text with `FileUtil::toDiskPath( 글 )` (린트 `cpp-shape` 규칙 14 강제, 남은 자리 · 병합 뒤 변환은 `py -3 -m Scripts disk-path-args`). Path rules R1 ~ R9: [DataInheritance](DataInheritance.md) "경로 · 리소스 신원".
- Environment `ProcessEnvironment::tryRead`; child process `Process::execute` with `ProcessCommand::at( AbsolutePath, … )` (Python `run_process`); shared library `ModuleLibraryUtil::loadLibraryAt`; stdin `StandardInputReader` — never `getenv` · `system` · `CreateProcess` · `dlopen` · Python `subprocess`.
- Text `StringUtil` (`parseInt` · `split` · `trim` · `utf8ToUTF16` …), `formatstring` · `StringBuilder`, `HexUtil` · `Base64Util`, `HashUtil` · `hashed_string`; never `atoi` · `sscanf` · `snprintf` chains · hand split loops.
- Binary formats `ByteReader` · `Archive` · `BitReader`: one value is returned (`readPod<T>()` · `readString()` · `readCount( n )` · `readInt( lo, hi )`, failure through `hasOverflowed()` · `isError()` · `finish()`) — no output-parameter form (린트 `byte-reader-values` 강제); when the target is outside the function (output parameter · member · `*p`), read into a local and move it only after the failure check; floats finite only; never a hand cursor or `.resize( countFromFile )`.
- Diagnostic count or time: `SW_PROFILE_*` per frame, `DevCounter` cumulative — never a counter field + getter on a runtime class.
- Callbacks `Delegate` · `MulticastDelegate` (never `std::function`); publish/subscribe `EventDispatcher`; background work `TaskManager` (never `std::async`); threads `sw::thread`.
- Containers `Core/Container/` (never `std::` containers); pooled handles `SlotHandleTable` · `IndexFreeList` · `GameObjectHandle`; identity `IdentityToken::make` · `ObjectIDAllocator` (never a process-wide atomic counter); memory `MemoryBlock` · `LinearAllocator` · `PoolAllocator` (never `malloc`, raw `new`, private free lists). Keep `string` SSO as is.
- Settings `ConfigManager::getConfig<T>`; console variables `SW_GLOBAL_VARIABLE` (text `GlobalString` read with `get()`), never a mutable static flag; user-facing text `LocalizationManager::getString` · `formatText`.
- Player input through `InputManager::getInputMap` actions, never key polling in game code.
- GPU: shader messages `SW_GPU_PRINT` · `SW_GPU_LOG` · `SW_GPU_ASSERT`; pass names `GPUPassScope`; dispatch `IRHICommandList::dispatchThreads`; mesh streams `RHIMeshVertexStreams`; vertex input `VertexInput` + `SW_INSTANCE_SLOT_INPUT`.
- Lint rule exceptions and ratchets go through `Scripts/lint/rule_tables.py` `ExceptionTable` · `Baseline`; scripts pass App arguments through `Scripts/common/app_arguments.py`.

## Reflection

- 캐시를 더하거나 등록 · 언로드를 바꾸면 무효화 순서와 모듈 재등록 테스트를 함께 쓴다.
- 내장 형식은 `ReflectBuiltins.xxx` 줄(`Widget` · `Layout` · `Roles`) 한 곳에 적고 소비처는 그 칸에서 만든다. 손 처리기는 Layout 이 none 인 State 형식만(`ReflectBuiltinTraitTable::hasStateHandHandler`).
- 세이브에서 핸들을 빼지 않는다(대상의 고정 지역 id 로 적힌다). 집합 · 맵은 RuntimeState 로 쓰지 않는다.
- 중첩 형식은 public 으로만 반사한다(감추려면 등록 변수를 `Registrar` 정적 멤버로 옮기는 생성기 변경이 먼저).
- FUNCTION: 주인 오브젝트 밖을 바꾸는 함수는 대상을 인자(핸들)로 드러내 편집 명령을 타게 한다. `Meta="Transactional"` 같은 선언은 두지 않는다. `callWithText` · `bindEventToFunction` 배선은 허용 목록 · 수명 · 핫 리로드 검증과 함께만.
- 복제: `Quantize` 키 · 양자화 형식은 두지 않는다. 손 본문 넷 메시지는 고정 배치로 옮기지 않는다(값마다 인코딩 · 예산 개수라 대역이 는다).
- 손으로 두는 이름 표: `DialogBackend`, `ShaderBindingValidatorInternal::RegisterClass` · `GlNamespace`, `TextChoiceKind`, `RichTextParser::TagKind`, `StageReportResult`, `WaterBodyComponent::MaterialNames` · `FoliageComponent::ViewMaterialNames`.

## Parallel-safe code

Anything that may run in parallel (job system, cooker, ticks, test processes) shares no mutable place.

- **Asset importers**: `importSource` is `const` and stateless; no mutable `static` or global cache in editor asset code.
- **Cook and import** produce the same bytes serially or in parallel: each job writes its own slot, a serial pass merges in a fixed order.
- **Ticks, services, workers** get what they need passed in. A worker body never touches containers another worker may resize.
- **No mutable statics** (린트 `mutable-statics` 강제; a real process-lifetime floor goes in `Config/Rules/Lint/MutableStatics.json` with `_why`): put the state in its owner; an id issuer is an `IdentityToken` or the owner's counter; a one-time warning is `SW_LOG_*_ONCE_ID`.
- A job-style interface declares what it reads and writes as pure virtuals with no default (`getPoseAccess`).
- **Lazy initialization** is a function-local `static const` or `std::call_once`, never a `static bool s_bTried`.

## Parallel and async

Pick by the shape of the work (N 비례 독립 · 앞단이 뒤를 막는 사슬 · N²), not today's object count. APIs: `Source/Core/Task/TaskManager.h`, `Engine/Foundation/EngineParallel.h`. Never create a new thread pool.

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
| Waiting on the OS (socket, file I/O, file watch, stdin, child process, driver-serial PSO compile, DB driver, crash reporting, render thread) | Own thread (`sw::thread` or `DedicatedWorker<T>`), owner in the `ThreadCreationKey` friend list (`Core/Concurrency/thread.h`). Raw `std::thread` · `CreateThread` · `pthread_create`: 린트 `thread-owners` 강제 |

- **Workers read, the owner writes.** A body must not resize a container another body reads, take a lock the waiting caller may hold (the caller helps run tasks while waiting), or read a dirty transform (flush first when `hasDirtySceneTransforms()`).
- **Say why it is safe**: every parallel body comments what it reads, the one slot it writes, and where the shared write happens.
- **Sleep only for real time** (frame pacing, a fence without an event, a backoff before retrying a locked file).
- A parallel cook or scene step is not a loop wrapped in tasks: cache isolation and a result merge come first.
- 측정 전에는 결함으로 세지 않는다: 헤더 전파 · TU 비용, 시작 시간 단계 비중, 배열 복사, 프레임 경로 intern.

## Thread and GPU resource ownership

Each object has one owning thread; review a change by its ownership map (object → thread, renderer → owner, device caller).

- **GPU resources** are owned by the render side with one create path and one destroy path: changes after creation → **render proxy** (`Material` / `MaterialRenderProxy`); never changes → **create once** (`Mesh`, terrain chunk); the render side swaps the handle (streaming) → **publish** an atomic handle · SRV (`Texture2D`). Never cover two creators with a lock.
- **Render work from another thread** goes through `RenderThread::executeAndWait`. Render-side objects the render thread holds are handed over once in `RenderThread::attach` — no setters while running.
- **Load workers** do construction, deserialization and bookkeeping only; component hooks (`onRegister`, `onPostLoad`) run on the game thread at the end of the batch.
- **Offscreen scene renderers**: one renderer per scene, living as long as the scene (`OffscreenSceneRenderers`); only the PSO cache is shared.
- **Frame packets carry full state only; one-shot diffs live in `RenderFramePacket::_deltas`** (`RenderFramePacketDeltas`) — a packet may end without drawing. A new diff is a field of that struct (빌드 크기 검사 강제).
- **Worker results and outside notifications**: a slot under a lock plus a generation number, lock contract in the type (`SW_GUARDED_BY`). Workers produce bytes; the game thread owns the object.
- **Scene registries** (`ComponentRegistry`, `CameraRegistry`, `TagIndex`): writes take the lock; a read window (`ComponentRegistry::View`) is valid only while nothing registers.

## Recurring defect shapes

Change the type or API so the shape cannot be written, not one symptom at a time.

1. **Identity from a number, sequence or path string without a generation** (`name#index`, ids restarting at 1, 32-bit name hashes, a cache keyed by an object's address). Use a stable id with a generation, a GUID for assets, or the object's `IdentityToken`.
2. **One fact known in two places** (writer and reader versions, a CPU count beside a GPU list). Keep one owner; the other side reads it.
3. **Failure or truncation not returned as a value.** Return `sw::expected` / `[[nodiscard]]`.
4. **Platform hooks that still compile when left out.** Make the hook pure virtual.
5. **Saves and settings with no version upgrade.** Every saved format carries a version on the type (`REFLECT( Version = N )` + `SW_TYPE_VERSION_PATCH`; file kinds `SW_SAVE_FORMAT_UPGRADE` / `SW_ASSET_FORMAT_UPGRADE`). Runtime state fields are `PROPERTY( RuntimeState )` read by `StateIO::reflected`.
6. **State changes handled by call-order rules.** Put the state in a value the next step reads.
