> 질문: 로그 · 시간 · 난수 · 파일 같은 흔한 일에 Core · Engine 의 무엇을 쓰고, 무엇을 직접 부르지 않는가?
> 독자: C++ 코드에서 흔한 일을 처음 구현하려는 사람과 에이전트.
> 결론: 새로 짜기 전에 아래 표의 "Use" 를 찾아 쓴다. "Do not use" 를 쓴 곳은 리뷰가 되돌린다. 없는 기능은 직접 짜지 말고 Core · Engine 에 하나 더한다.

## Existing facilities

Search here before writing a helper for a common job. If the job is missing, search the header tree, then add the facility to Core or Engine once instead of a local copy. Where a type can make the direct API unreachable it does (`DataPath` has no literal constructor); review covers the rest.

| Need | Use (header · API) | Do not use |
| --- | --- | --- |
| Log a line | `Core/Log/Logger.h` `SW_LOG_ERROR` · `SW_LOG_WARNING` · `SW_LOG_INFO` · `SW_LOG_TRACE` (`_ID` variants); once per scope: `Core/Log/LogOnce.h` `SW_LOG_ERROR_ONCE` | `printf` · `fprintf` · `puts` · `std::cout` · `std::cerr` |
| Report below the Log tier or before the logger starts | `Core/Common/Macros.h` `SW_LOW_LEVEL_ERROR` · `SW_LOW_LEVEL_WARNING` (stderr until the logger registers) | `fprintf( stderr, … )` |
| Command-line output (help text, lists) | `Core/Process/StandardOutput.h` `StandardOutput::write` | `fputs( …, stdout )` · `std::cout` |
| Assert | `Core/Common/Macros.h` `SW_ASSERT`; with a message and a log line in every build: `Core/Log/Logger.h` `SW_LOG_ASSERT` | `assert` |
| Current time, elapsed time, wait deadline | `Core/Time/MonotonicClock.h` `MonotonicClock::nowNanoseconds` · `Stopwatch` · `Deadline::afterMilliseconds` · `sleepUntilNanoseconds` | `std::chrono::steady_clock` · `high_resolution_clock` · `system_clock` |
| UTC time, local calendar | `Core/Time/WallClock.h` (services take `nowMs` as a parameter) | `system_clock` · `localtime` · `gmtime` |
| Frame delta | `Core/Time/GameTimer.h` · `Core/Time/FixedTimestep.h` | own delta bookkeeping |
| Random value (engine) | `Core/Math/MathUtil.h` `MathUtil::getRandom<T>` · `getRandomRange` · `getRandomBits64` | `rand` · `srand` · `std::mt19937` · `std::random_device` |
| Deterministic sequence (tests, replay, procedural) | `Core/Math/XorShift32.h`; 64-bit: `Core/Common/HashUtil.h` `HashUtil::splitMix64Next`; game rules: `GameFramework/Base/Foundation/Utility/Random/GameRandom.h` | `std::mt19937` with a seed, `std::shuffle` (its algorithm differs per standard library) |
| Angle wrap and interpolation | `Core/Math/MathUtil.h` `wrapAngle` · `lerpAngle` (radians) | `while ( a > kPi ) a -= kTwoPi` loops, degree copies of them |
| Data-defined resource path | `Engine/Serialization/Serializer/DataPath.h` `DataPath` (only data files fill it) | string literal paths in headers |
| Resolve a resource id | `Engine/Asset/AssetPaths.h`; read: `Engine/Asset/AssetIO.h` `readTextAsset` · `readBinaryAsset` · `collectResources` | disk paths in runtime game code |
| User data and config folder | `Core/File/UserDataPath.h` `getDataDirectory` · `getConfigDirectory` · `resolve` | `getenv( "HOME" )` · `%LOCALAPPDATA%` by hand |
| Path text (join, extension, normalize) | `Core/File/FileUtil.h` `joinPath` · `getExtension` · `removeExtension` · `getFileNamePart` · `getDirectoryPart` · `normalizeSeparators` · `splitPath`; part under a root: `tryMakePathUnderRoot` | `std::filesystem::path`, `rfind( '.' )` · `find_last_of( '/' )` loops |
| Read or write a whole file | `Core/File/FileUtil.h` `readFile` · `readTextFile` · `writeFile` · `writeTextFile` (`[[nodiscard]] bool`) | `fopen` · `_wfopen` · `std::ifstream` · `std::ofstream` |
| Change files (append, copy, delete, recycle bin) | `Core/File/FileUtil.h` `appendTextFile` · `copyFile` · `removeFile` · `tryRemoveFile` · `removeDirectory` · `moveToRecycleBin` — all pass the automation write gate `isWriteAllowed`; a third-party save that opens the path itself (DDS, SQLite) asks `isWriteAllowed` first | `rename` · `unlink` · `mkdir` · `MoveFile` · `DeleteFile` · `open( … O_WRONLY )` · `PlatformFileUtil::openFile` / `replaceFile` / `moveToRecycleBin` |
| Read a file that reports size 0 (`/proc`, `/sys`) | `Core/File/FileUtil.h` `readStreamTextFile` | `fopen` + `fgets` |
| Read a file without blocking | `Core/File/AsyncFileIO.h` (priority, cancel, `TaskFuture`) | a thread that reads |
| Walk a folder, file stamps | `Core/File/FileUtil.h` `collectFiles` · `collectFolders` · `forEachDirectoryEntry` · `getFileWriteTime` | `std::filesystem::directory_iterator` |
| Environment variable | `Core/Process/ProcessEnvironment.h` `tryRead` · `tryReadNonEmpty` | `getenv` · `GetEnvironmentVariable` |
| Parse a number or bool | `Core/Container/StringUtil.h` `parseInt` · `parseInt64` · `parseUint64` · `parseFloat` · `parseDouble` · `tryParseBool` · `parseIntList`; exactly N floats: `parseExactFloats` | `atoi` · `strtol` · `strtod` · `std::stoi` · `sscanf` |
| Split text into a list | `Core/Container/StringUtil.h` `split( text, separators, StringSplitOptions{ _bTrim, _bDropEmpty } )`; one token at a time without allocation: `forEachToken`; lines: `forEachLine` | `find` · `substr` loops, a local `splitList` / `splitTokens` |
| Trim, compare, case | `Core/Container/StringUtil.h` `trim` · `startsWith` · `endsWith` · `toLower` · `matchesWildcard` | hand loops over `char` |
| UTF-8 and UTF-16 | `Core/Container/StringUtil.h` `utf8ToUtf16` · `utf16ToUtf8` · `isValidUtf8` | `MultiByteToWideChar` at call sites |
| Format text, hex, colors, Base64 | `Core/Container/formatString.h` `formatstring` (`%x`, `%X`, `Format::hex`); build: `Core/String/StringBuilder.h`; colors: `Core/Math/Color.h` `Color::fromHex` · `toHex`; Base64: `Core/String/Base64Util.h` | `snprintf` chains, own hex tables |
| Bytes or fixed-width integers as hex | `Core/String/HexUtil.h` `encodeBytes` · `appendBytes` · `appendUint64` · `decodeBytes` (`HexCase`) | a `"0123456789abcdef"` table and nibble shifts |
| Hash, interned name | `Core/Common/HashUtil.h` `combine` · `computeStringHash64` · `mix64`; `Core/String/hashed_string.h` | literal FNV or golden-ratio constants |
| Read hand-written data XML attributes | `Engine/Serialization/XML/XMLReadUtil.h` `loadRootOrWarn` · `findRequiredID` · `readRequiredName` · `parseNameList` · `parseFloat3` · `readOptionalNumber` · `readOptionalFlag` · `forEachNamedNumber`; attribute present: `XMLNode::hasAttribute`; curves: `GameDataXML::readFloatCurve`; reflection Def rows: `GameDataXML::readFields` · `readInto` | per-kit copies of the same readers, aliases that forward to them |
| Serialize an object | `Engine/Serialization/Format/XMLSerializer.h` · `JSONSerializer.h` · `BinarySerializer.h` through reflection; raw binary: `Engine/Serialization/Format/Archive.h` | `pugixml` or JSON library calls outside Serialization |
| Callback, multicast event | `Core/Delegate/Delegate.h` `Delegate` · `MulticastDelegate` · `SW_DECLARE_DELEGATE` · `SW_DECLARE_MULTI_CAST_DELEGATE` | `std::function` |
| Publish and subscribe by channel | `Core/Event/EventDispatcher.h` `subscribe` · `publish` · `push` | a hand-rolled listener list |
| Background work, parallel loop | `Core/Task/TaskManager.h` `emplaceTask` · `submit` · `runParallel` · `whenAll`; result: `Core/Task/TaskFuture.h` | `std::async` · a thread per job |
| A dedicated thread | `Core/Concurrency/thread.h` `sw::thread`; locks: `Core/Concurrency/mutex.h` | `std::thread` |
| Load an asset | `Engine/Asset/AssetManager.h` asset caches (`getAssetCache`); text and bytes: `AssetIO` | `FileUtil` reads of `Content/` in game code |
| Engine or game setting | `Engine/Config/ConfigManager.h` `getConfig<T>` (keyed by type) | config lookup by a name string |
| Console variable, cheat, switch | `Core/GlobalVariable/GlobalVariableManager.h` `SW_GLOBAL_VARIABLE` · `SW_EXTERN_GLOBAL_VARIABLE` | a mutable `static` tuning flag |
| User-facing text | `Engine/Localization/LocalizationManager.h` `getString` · `formatText` through `engine::getSubsystem<LocalizationManager>()` | literal UI strings |
| Player input | `Engine/Input/InputManager.h` `getInputMap`; actions are defined in `*.input.xml` and queried by name | polling `getKeyboard()` keys in game code |
| Run a child process | `Core/Process/Process.h` `Process::execute` · `launch` · `launchDetached`; Python: `Scripts/common/RunProcess.py` `runProcess` | `system` · `popen` · `CreateProcess` · `fork` · `exec*`; Python `subprocess` |
| Read stdin commands | `Core/Process/StandardInputReader.h` | a blocking `std::cin` loop |
| Containers | `Core/Container/` `vector` · `unordered_map` · `map` · `set` · `deque` · `string` · `span` · `sparse_set` | `std::` containers |
| Stable handle to pooled data | `Core/Container/SlotHandleTable.h` · `SlotHandle.h` · `IndexFreeList.h`; objects: `GameObjectHandle.h` · `ComponentHandle.h` | raw pointers or indices into pooled storage |
| Own a byte block or object | `Core/Memory/MemoryBlock.h` `MemoryBlock`; `make_unique<T>`; `vector<T>` | `malloc` · `Memory::allocate` · raw `new` |
| Many short-lived allocations | `Core/Memory/LinearAllocator.h` · `PoolAllocator.h`; per frame: `Core/Container/FrameArenaAllocator.h`; small inline: `InlineAllocator.h` | a private free list |
| Reach a service | `getSubsystem<T>()` · `findSubsystem<T>()` in the owning scope | singletons, `getInstance()` |
| Well-known constants | `Core/Math/MathUtil.h` `kPi` …; units: `Core/Common/Defines.h` `constant::` | retyped literals |

A new row names the header and the API.
