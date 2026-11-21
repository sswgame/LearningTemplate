# ExistingFacilities

> 결론: 흔한 일을 새로 짜기 전에 아래 표의 "Use" 를 쓴다. "Do not use" 는 리뷰가 되돌린다. 없는 기능은 지역 사본 대신 Core · Engine 에 하나 더한다(새 줄은 헤더와 API 를 적는다).

## Existing facilities

| Need | Use (header · API) | Do not use |
| --- | --- | --- |
| Log a line | `Core/Log/Logger.h` `SW_LOG_ERROR_ID` · `_WARNING_ID` · `_INFO_ID` · `_TRACE_ID` (every line has an `"Area.Event"` id); once per scope `Core/Log/LogOnce.h` `SW_LOG_ERROR_ONCE_ID` | `printf` · `fprintf` · `puts` · `std::cout` · `std::cerr` |
| Value or failure / lookup that may find nothing | `Core/Foundation/Expected.h` `sw::expected<T>` · `sw::optional<T>` · `sw::Error` ([Errors](Errors.md)) | `std::optional` · `bool f( …, string& outError )` |
| Report below the Log tier or before the logger starts | `Core/Foundation/Macros.h` `SW_LOW_LEVEL_ERROR` · `SW_LOW_LEVEL_WARNING` | `fprintf( stderr, … )` |
| Command-line output | `Core/Process/StandardOutput.h` `StandardOutput::write` | `fputs( …, stdout )` · `std::cout` |
| Assert | `SW_ASSERT`; with message + log in every build `SW_LOG_ASSERT` | `assert` |
| Time, elapsed, deadline | `Core/Time/MonotonicClock.h` `MonotonicClock::nowNanoseconds` · `Stopwatch` · `Deadline::afterMilliseconds` · `sleepUntilNanoseconds` | `std::chrono::*_clock` |
| Diagnostic count or time (tests · watches · profiling) | per frame `SW_PROFILE_SCOPE` · `SW_PROFILE_COUNT` (`Engine/Profiling/FrameProfiler.h`); cumulative `Core/Concurrency/DevCounter.h` `DevCounter` (empty in Shipping) | counter field or clock member + getter on a runtime class |
| UTC time, local calendar | `Core/Time/WallClock.h` (services take `nowMs` as a parameter) | `system_clock` · `localtime` · `gmtime` |
| Frame delta | `Core/Time/GameTimer.h` · `FixedTimestep.h` | own delta bookkeeping |
| Random value | `MathUtil::getRandom<T>` · `getRandomRange` · `getRandomBits64` | `rand` · `std::mt19937` · `std::random_device` |
| Deterministic sequence | `Core/Math/XorShift32.h`; 64-bit `HashUtil::splitMix64Next`; game rules `GameFramework/Runtime/Foundation/Random/GameRandom.h` | seeded `std::mt19937`, `std::shuffle` |
| Angle wrap and interpolation | `MathUtil::wrapAngle` · `lerpAngle` (radians) | hand `while ( a > kPi )` loops |
| Data-defined resource path | `Engine/Serialization/Serializer/DataPath.h` `DataPath` | string literal paths in headers |
| Field pointing at another asset | `Engine/Asset/AssetRef.h` `AssetRef<T>` | a `string` path field with `AssetPath` |
| Resolve a resource id, read it | `Engine/Content/AssetPaths.h`; `Engine/Content/AssetIO.h` `readTextAsset` · `readBinaryAsset` · `collectAssets` | disk paths in runtime game code |
| User data and config folder | `Core/File/UserDataPath.h` | `getenv( "HOME" )` · `%LOCALAPPDATA%` by hand |
| Path text | `Core/File/FileUtil.h` `joinPath` · `getExtension` · `removeExtension` · `getFileNamePart` · `getDirectoryPart` · `normalizeSeparators` · `splitPath` · `tryMakePathUnderRoot` | `std::filesystem::path`, `rfind( '.' )` loops |
| Read or write a whole file | `FileUtil::readFile` · `readTextFile` · `writeFile` · `writeTextFile` (`[[nodiscard]]`) | `fopen` · `std::ifstream` · `std::ofstream` |
| Change files | `FileUtil::appendTextFile` · `copyFile` · `removeFile` · `tryRemoveFile` · `removeDirectory` · `moveToRecycleBin` (all pass the write gate `isWriteAllowed`; a third-party save asks `isWriteAllowed` first) | `rename` · `unlink` · `mkdir` · `MoveFile` · `DeleteFile` · `PlatformFileUtil::*` |
| Read a size-0 file (`/proc`, `/sys`) | `FileUtil::readStreamTextFile` | `fopen` + `fgets` |
| Read without blocking | `Core/File/AsyncFileIO.h` | a thread that reads |
| Walk a folder, file stamps | `FileUtil::collectFiles` · `collectFolders` · `forEachDirectoryEntry` · `getFileStamp` · `tryGetFileSize` | `std::filesystem`, `PlatformFileSystem.h` |
| Shader print, log, assert | `gpu_message.hlsli` `SW_GPU_PRINT` · `SW_GPU_LOG` · `SW_GPU_ASSERT`; ids in `gpu_message_ids.hlsli`, format `SW_GPU_MESSAGE_FORMAT`; pass trait `kGPUMessageTarget`; run `-gv_gpuMessages=1` | debug colours in the output, own UAV, HLSL string literals |
| Name a GPU pass | `Engine/Graphics/Debug/GPUBreadcrumbs.h` `GPUPassScope`; textures `setTextureDebugName` | hand `beginEventMarker` · `endEventMarker` pairs |
| Dispatch over N items | `IRHICommandList::dispatchThreads( count…, groupSize… )` | `dispatchCompute( ( n + g - 1 ) / g, … )` |
| Upload or bind mesh vertices | `Engine/Graphics/RHI/RHIMeshVertexStreams.h` `write` · `bind`; data in colour or tangent → `Mesh::setVertexFormat( Full )` | `createVertexBuffer( listRHIVertex )` by hand |
| Vertex shader inputs | `VertexInput` (or `VertexPositionInput`) + `SW_INSTANCE_SLOT_INPUT( name )` from `common.hlsli`; `SV_VertexID`-built: no vertex input | own input struct |
| Environment variable | `Core/Process/ProcessEnvironment.h` `tryRead` · `tryReadNonEmpty` | `getenv` · `GetEnvironmentVariable` |
| Parse a number or bool | `StringUtil::parseInt` · `parseInt64` · `parseUint64` · `parseFloat` · `parseDouble` · `tryParseBool` · `parseIntList` · `parseExactFloats` | `atoi` · `strtol` · `std::stoi` · `sscanf` |
| Split text | `StringUtil::split( text, separators, StringSplitOptions{ _bTrim, _bDropEmpty } )` · `forEachToken` · `forEachLine` | `find` · `substr` loops, local `splitList` |
| Trim, compare, case | `StringUtil::trim` · `startsWith` · `endsWith` · `toLower` · `matchesWildcard` | hand `char` loops |
| UTF-8 and UTF-16 | `StringUtil::utf8ToUTF16` · `utf16ToUTF8` · `isValidUTF8` | `MultiByteToWideChar` at call sites |
| Format text, colours, Base64 | `Core/String/formatString.h` `formatstring`; `StringBuilder.h`; `Color::fromHex` · `toHex`; `Core/Encoding/Base64Util.h` | `snprintf` chains, string `+` |
| Bytes as hex | `Core/Encoding/HexUtil.h` | own `"0123456789abcdef"` table |
| Hash, interned name | `HashUtil::combine` · `computeStringHash64` · `mix64`; `Core/Name/hashed_string.h` | literal FNV or golden-ratio constants |
| Read a game catalog's root elements | `GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h` `XMLCatalogElement` · `readInDocumentOrder` · `readInTableOrder` | `root.findChild(` loops, per-catalog "unknown element" logs |
| Check catalog ids point at entries | one `<Reference>` row in `Content/engine/settings/datacatalogreferences.xml` (loader and cook run the same rows) | per-catalog `find( id ) == nullptr` loops |
| Component or scene field naming a catalog entry | `CatalogKey<EntryDef>` + `<CatalogKey>` and `<Field>` rows in `datacatalogreferences.xml` (single struct field: `<Struct>` row; catalog definition: `<Element>` row; test `FieldTableMatchesReflection`) | `hashed_string` · `string` id fields |
| Read hand-written data XML | `Engine/Serialization/XML/XMLReadUtil.h` `loadRootOrWarn` · `findRequiredID` · `readRequiredName` · `parseNameList` · `parseFloat3` · `readOptionalNumber` · `readOptionalFlag`; curves `GameDataXML::readFloatCurve`; reflection rows `XMLFields::readProperties` · `readInto` | per-kit copies of the readers |
| Serialize an object | `XMLSerializer.h` · `JSONSerializer.h` · `BinarySerializer.h` through reflection; raw `Archive.h` | `pugixml` or JSON library calls outside Serialization |
| Save or restore runtime state | `Engine/Serialization/Format/StateIO.h` `SW_DECLARE_STATE_IO()` + one `serializeState( StateIO& io )` (lint `common-shapes` `statePair`) | hand `writeState` / `readState` pair |
| Read a binary format, save or packet | `Core/Serialization/ByteStream.h` `ByteReader` `readCount` · `readList` · `readPodList` · `canHold` · `isFullyRead`, `tryMultiplyCounts`; floats finite only | `LittleEndianUtil::read*` with own cursor, `.resize( countFromFile )` |
| React to a shared asset reload | `Engine/Asset/Cache/AssetReloadWatch.h` `AssetReloadWatch<T>` | a `_seen…ReloadCount` field |
| Editor saved value + undo | `Editor/Common/Workspace/Undo/EditorDocumentState.h` (`EditSnapshotHistory`, `SaveReloadGuard<T>`) | `_savedText` + hand undo stack |
| Node graph removals, comments, frame end | `EditorNodeGraph::collectRemovals` · `addRequestedComment` · `removeComments` · `finishCanvas` | `ed::BeginDelete` in a panel |
| Save a `_base` document | `ResourceComposer::saveDelta` | `makeDelta` + write per format |
| Scenario step that only reads its schema | `&AutomationStep::validateProperties<TParams>`, `SW_AUTOMATION_STEP_SCHEMA`, `AutomationStepOptions` | a one-line `readProperties` validator |
| Watch value from the first component | `SW_AUTOMATION_GETTER_WATCH` · `SW_AUTOMATION_COMPONENT_WATCH` | hand first-component search |
| Callback, multicast event | `Core/Delegate/Delegate.h` `Delegate` · `MulticastDelegate` · `SW_DECLARE_DELEGATE` · `SW_DECLARE_MULTI_CAST_DELEGATE` | `std::function` |
| Publish and subscribe | `Core/Event/EventDispatcher.h` | hand listener list |
| Background work, parallel loop | `Core/Task/TaskManager.h` `emplaceTask` · `submit` · `runParallel` · `whenAll`; `TaskFuture.h` | `std::async` · a thread per job |
| Dedicated thread, locks | `Core/Concurrency/thread.h` `sw::thread`; `mutex.h` | `std::thread` |
| Load an asset | `AssetManager` asset caches (`getAssetCache`); text and bytes `AssetIO` | `FileUtil` reads of `Content/` in game code |
| Engine or game setting | `Engine/Config/ConfigManager.h` `getConfig<T>` | lookup by name string |
| Console variable, cheat, switch | `SW_GLOBAL_VARIABLE` · `SW_EXTERN_GLOBAL_VARIABLE`; text `GlobalString` / `GlobalDiskPath` read as a copy with `get()` | mutable `static` flag, plain `sw::string` global |
| User-facing text | `LocalizationManager::getString` · `formatText` | literal UI strings |
| Editor label on an ImGui-ID widget | `EditorThemeUtil::makeIconLabel( icon, SW_LOCKEY(...), "English" )` · `makeStableLabel`; windows `EditorPanelText::makeWindowName` · `makeWindowID`; data text `Meta = "Localizable"` + `EditorDataText::localize`; static tables `SW_LOCKEYREF` | translated text as the ID, `FindWindowByName( title )` |
| Add a localization key | entry in `editor.strings.xml` / `engine.strings.xml` → `App -export-po` → fill `po/<culture>.po` → `App -import-po=<po>` → `App --gather-text` → `App --check-text` (0 errors); `TextArgumentList::addDecimal`, ICU `select` · `plural`; no `[word]` in sources | English literals, a key without translations |
| Move a file that has `SW_LOCKEY` | rerun `App --gather-text`, commit `Content/**/localization/*.strings.xml` (ToolCheckText fails otherwise) | committing the move alone |
| Move, rename or delete an asset | `py -3 -m Scripts move-asset <old id> <new id>` (editor: Content Browser); pre-commit lint `asset_references.py` lists dangling refs; full check `py -3 -m Scripts asset-refs` · `App --check-asset-refs` | `git mv` or text replace of the path |
| Lint rule exceptions or ratchet | `Scripts/lint/rule_tables.py` `ExceptionTable` · `Baseline` (path from the rule id → `Config/Rules/Lint/<RuleId>.json`) | path constant + hand `json.loads` in the rule |
| Player input | `InputManager::getInputMap`, actions in `*.input.xml` queried by name ([DataInheritance](DataInheritance.md) "입력 맵 묶음") | polling `getKeyboard()` keys in game code |
| Run a child process | `Core/Process/Process.h` `Process::execute` · `launch` · `launchDetached` with `ProcessCommand::at( AbsolutePath, … )` or `fromSearchPath( "git", … )`; Python `Scripts/common/run_process.py` `run_process` | `system` · `popen` · `CreateProcess` · `fork` · `exec*`; Python `subprocess` |
| App · Server arguments from a script | `Scripts/common/app_arguments.py` `flag` · `option` · `variable` · `find_chain_command` · `read_chain_stages` | literal `"-scenario=…"` strings, parsing `ctest -N` |
| Load a shared library | `Core/Module/ModuleLibraryUtil.h` `loadLibraryAt( AbsolutePath )` · `loadSystemLibrary` · `findLoadedLibrary` · `getDynamicSymbol` | `LoadLibrary*` · `dlopen` · `GetProcAddress` · `dlsym`, candidate chains |
| Absolute disk path as a value | `Core/Path/AbsolutePath.h` `AbsolutePath::tryMake` | `FileUtil::isAbsolutePath` for validation |
| Read stdin commands | `Core/Process/StandardInputReader.h` | blocking `std::cin` loop |
| Containers | `Core/Container/` `vector` · `unordered_map` · `map` · `set` · `deque` · `string` · `span` (fix shared behaviour once in `SortedVectorBase` · `DenseHashTable`) | `std::` containers |
| Wrap a stream transport | `Core/Network/Transport/StreamTransportDecorator.h` | forwarding every `IStreamTransport` function |
| Stable handle to pooled data | `Core/Container/SlotHandleTable.h` · `SlotHandle.h` · `IndexFreeList.h`; objects `GameObjectHandle.h` · `ComponentHandle.h` | raw pointers or indices, hand slot + generation tables |
| Content version or object identity | `Core/Memory/IdentityToken.h` `IdentityToken::make`; object ids `ObjectIDAllocator.h` | process-wide `atomic<uint64>` counter |
| Own a byte block or object | `Core/Memory/MemoryBlock.h`; `make_unique<T>`; `vector<T>` | `malloc` · `Memory::allocate` · raw `new` |
| Many short-lived allocations | `Core/Memory/LinearAllocator.h` · `PoolAllocator.h` · `InlineAllocator.h` | private free list |
| Reach a service | `getSubsystem<T>()` · `findSubsystem<T>()` | singletons, `getInstance()`, mutable statics ([CppStyle](CppStyle.md)) |
| Static registration | `Core/Registry/RegistrationList.h` `SW_DEFINE_REGISTRATION_LIST` · `StaticRegistrar`; one registration `RegistrationSlot.h` | hand registrar struct, static `vector` of registrations |
| Well-known constants | `MathUtil` `kPi` …; `Core/Foundation/Constants.h` `constant::time` · `constant::memory` | retyped literals |

## 경로 · 리소스 신원 규칙 (R1 ~ R9)

코드 주석이 `(R2)` 처럼 이 번호를 가리킨다.

| 규칙 | 뜻 |
|---|---|
| R1 OS 경계 하나 | 파일 · 폴더 · 라이브러리 · 환경 변수 · 실행 파일 OS 호출은 `PlatformFileUtil`(과 짝)만. `std::filesystem` 은 쓰지 않는다 |
| R2 디스크 I/O 는 절대 경로만 | `FileUtil` · `PlatformFileUtil` I/O 는 절대 경로만 받는다(상대 · `C:foo` · `\foo` 는 오류). 경로 글 연산은 대상 밖 |
| R3 리소스는 리소스 경로만 | `AssetPaths` · `AssetIO` 는 `ResourcePath{도메인(선택), 상대 경로}` 만 받는다. 문자열에서 도메인을 추측하지 않는다 |
| R4 쓰기는 FileUtil 이 절대 경로로 | `AssetPaths` · `AssetIO` 는 읽기와 경로 풀기만 |
| R5 상대 → 절대는 경계에서 한 번 | 명령줄 · 설정 · 콘솔 · 시나리오 값은 들어오는 자리에서 기동 때 고정한 루트(`ProcessRoots`) 기준으로 한 번. 실행 중 작업 폴더는 읽지 않는다 |
| R6 폴백 없음 | 필요한 값이 없으면 오류로 멈춘다 |
| R7 확인과 실행은 같은 해석 | 있는지 묻는 쪽과 읽고 쓰는 쪽이 같은 함수로 푼다 |
| R8 같은 일은 하나 | 같은 판정 · 정규화 · 표 · 파서는 하나. 답을 아는 쪽이 값을 넘긴다 |
| R9 구성 차이는 크기대로 | [BuildConfigSwitch](BuildConfigSwitch.md) |

- 디스크 입구는 `AbsolutePath`(`tryMake` · `tryResolve`, `FileUtil::requireAbsolutePath`)로 받는다 — `FileUtil::isAbsolutePath` 는 `C:foo` 도 절대로 본다.
- `FileUtil` 디스크 I/O(`exists` · `readTextFile` · `writeFile` …)는 `const AbsolutePath&` 만 받는다. 글은 `FileUtil::toDiskPath( 글 )` 로 올린다(남은 자리 · 병합 뒤 변환은 `py -3 -m Scripts disk-path-args`, 린트 `cpp-shape` 규칙 14).
- `ByteReader` 한 값 읽기는 값을 돌려준다(`readPod<T>()` · `readString()` · `readCount( n )`, 실패는 `hasOverflowed()` · `finish()`) — 출력 인자 꼴은 없다(`byte-reader-values`, 규칙 13).
- 루트는 `ProcessRoots`(기동 때 한 번) — `"Saved"` 리터럴 · 작업 폴더 의존 금지.
- `ResourcePath` 는 문자열에서 암시적으로 못 만든다. 도메인 판정은 `DomainTable::split` 하나.
- 명령줄 디스크 경로는 `CommandLineDiskPath`, 개발 명령은 `readDiskPathArgument`.

## 리소스 읽기 입구와 에셋 참조

길: 리소스 파일 → `ResourceComposer`(부모 → 조건 → 층) → 종류 정의 로더 → `SharedAssetCache<T>`(경로 하나에 객체 하나) → `AssetRef<T>` 가 꺼낸다. 실행 상태는 쓰는 쪽이 따로 가진다. 파일에 적는 법은 [DataInheritance](DataInheritance.md).

- **종류 등록**은 `SW_ASSET_TYPE( id, 정의.withPathRule( … ).withLoader( AssetTypeLoader::of<T>( 메모리 태그 ) ) )` 한 줄, 공유 캐시는 `SW_SHARED_ASSET_CACHE`(Engine) · `SW_MODULE_SHARED_ASSET_CACHE`(모듈) 한 줄. 검증 전용 로더를 두지 않는다 — 스키마 테스트도 진짜 로더를 부른다.
- 쿠킹이 알아야 하는 합성 규칙은 데이터(`datacataloginheritance.xml`)에 둔다 — 팩 쿠킹은 게임 모듈 전에 돈다.
- `request( 경로 )` 는 핸들, `acquire( 경로 )` 는 `SharedAssetRef<T>`. `XMLCatalog` 에 감싸개를 두지 않는다. 다시 읽은 횟수는 `AssetReloadWatch<T>` 로만 본다.
- 다시 읽으면 새 판이 생긴다. 진행 중 세션 · 워커 일감 · 번호를 지어 둔 쪽은 `shared_ptr<const T>`(`.snapshot()`)로 쥐고, 지어 둔 것은 `AssetReloadWatch` 로 다시 짓는다.
- **사본을 만들지 않는다**: `loadFromAsset` 은 비공개. 게임 범위 서비스는 `GameInstance::loadRequiredCatalog`, 테스트 · 미리 보기는 `loadFromXMLText`, 쿠커가 읽는 형식만 `loadCopyFromAsset`. 사본이 맞는 것은 기동 때 서비스 상태로 옮기는 엔진 설정, 쓰는 쪽이 고치는 입력(`InputMap` · `NavMeshAsset` 등), 도구 입력뿐.
- 공유 객체는 `const`. 실행 상태가 섞인 카탈로그는 정의와 실행으로 나눈다(`BehaviorTree` · `BehaviorTreeRunner`).
- **리소스는 id 로 읽는다**(조건 · 층 · 다시 읽기 · 접속 대조를 받으려고): `ConfigManager::readComposedXMLDocument` · `XMLReadUtil::parseResourceRoot` · `XMLDocument::loadResource` · `loadResourceAs` · `parseResourceText`. 읽은 글을 id 없이 글 입구로 넘기는 것은 린트 `common-shapes` `assetEntrance` 가 막는다.
- **다른 에셋을 가리키는 칸은 `AssetRef<T>`** — 스스로 읽지 않는다(`acquire()` · `request()`), 접미사가 다르면 로드 오류. 칸이 만들어지는 곳에서 `T` 는 완전해야 한다(못 읽으면 생성자를 `.cpp` 로). 종류가 하나로 정해지지 않는 칸만 `PROPERTY( AssetPath )`.
- 경로 칸은 리플렉션 선언에서만 온다 — 손으로 쓴 이름 표를 두지 않는다(생성 목록 `Config/Dev/AssetReferenceFields.generated.json`, 낡음은 `--check-asset-refs`).
- **쿠킹은 닿는 것만 담는다**(`PackReachability`). 코드가 경로를 조립해 읽는 파일은 뿌리(`_cookRules` · `<RequiredData>` · `SW_ASSET_COOK_ROOT`)나 곁 파일(`SW_ASSET_COMPANION`)로 선언한다 — 아니면 배포에서 빠진다.

## 리플렉션 스키마로 읽지 않고 손으로 두는 것

데이터 파일의 칸은 `PROPERTY` 구조체 + `XMLFields` · `JSONFields::readProperties` 로 읽는다. 손으로 읽는 것은 다음뿐: 바깥 표준 · 원문을 지키는 것(VRM · glTF · TMX · JWT · 영수증 · 트레이스 · CSS 철자 스타일 시트 등), 양이 많거나 적었는지가 뜻인 칸(타일맵 타일, 문화권 표, `<Requires>` 같은 조건 원소), 고유 문법(`ScheduleCondition`, 이름=수 목록, 비트 합치는 낱말 칸, 격자 글), `TagContainer`(`addTag` 불변식), 키가 동적인 JSON, Core 층(리플렉션 없음). 새로 손으로 읽으려면 이 목록 가운데 어디에 드는지 주석에 적는다.

에디터 패널의 그리기와 로직 분리는 [EditorUI](EditorUI.md) MVVM 절.
