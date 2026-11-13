> 질문: 개발 · 배포(· 플랫폼 · 타깃) 차이를 코드에서 어떻게 가르는가?
> 독자: 구성마다 다르게 동작해야 하는 코드를 쓰는 사람.
> 결론: 값뿐이면 `build::` 상수, 갈래 하나가 다른 구성에 없는 헤더 · 모듈을 쓰면 그 자리 `#if`, 기능 전체가 없으면 CMake 가 폴더를 뺀다. 파일 짝을 만들지 않는다.

## Build configuration switches

Pick the smallest form that fits. One decision point, and code compiled in every configuration whenever possible, so the configuration you did not build cannot rot.

| The difference is ... | Use | Example |
| --- | --- | --- |
| A value only — both branches use symbols every configuration has | `build::kShipping` (or `kWithClientCode` · `kWithServerCode`) from `Core/Foundation/BuildInfo.h`, read with `if` / `if constexpr`. The dead branch is dropped; what it calls needs no definition there | `RenderDiagnostics::get`, `GameSelection::select`, `ConfigManager::initializeSubsystem` (disk vs baked JSON) |
| A few lines, and one branch needs a header, type or module the other configuration does not build | `#if defined( SW_SHIPPING )` at that spot, in the one file (`if constexpr` cannot help: a discarded branch outside a template is still name-checked) | Logger sink selection, the dev-command and automation registration macros (`DevCommandRegistry.h` · `AutomationWatch.h`) |
| A whole feature the configuration does not have (automation runner, DevTools host, module reload, server debug console, editor module) | The feature registers itself (`SW_MODULE_SUBSYSTEM` publishing an interface) from its own folder, and CMake excludes that folder. The host only looks the interface up; when it is absent and the run asked for it (`-scenario`, `-EnableEditor` …), startup fails with the reason | Unreal: a Developer/Editor module the shipping target does not link |

Rules:

- **Platform chains** end in `#else` + `#error "Unsupported platform"` and name the platform in every branch (`SW_PLATFORM_WINDOWS` · `_LINUX` · `_ANDROID` · `_WEB`, or `SW_PLATFORM_POSIX` for the three non-Windows). Never `#if Windows #else` or `!defined( SW_PLATFORM_* )`: a new platform must fail to compile, not run the wrong branch (Unreal `PLATFORM_*`). Lint `platform-chains`, exemptions in `Config/Rules/Lint/PlatformChains.json`.
  - CMake: a chain whose first condition is only platform tests (`WIN32`, `UNIX`, `CMAKE_SYSTEM_NAME STREQUAL`, `SW_PLATFORM_NAME STREQUAL`, `CMAKE_HOST_*`) ends in `else()` + `message(FATAL_ERROR …)`; `if(NOT WIN32)` is only a guard that returns or stops.
  - Scripts read the host OS only from `Scripts/common/host_platform.py` (`is_windows_host`, `kHost`, `executableName`).
  - Modules run on every platform unless the manifest's `_listExcludedPlatform` says otherwise.
- **No file-name switches** (`*DevCommands.cpp`, `*Reload.cpp`, `*Shipping.cpp`). Self-registering dev code stays in the feature's `.cpp`; the registration macro does not register in Shipping and holds the handler in a `[[maybe_unused]]` constant. Code that needs headers Shipping does not build lives in a `Dev/` folder listed in `Config/Rules/Layers/DevFolders.json` (`SW_DEV_ONLY_SOURCE_PATTERNS`; an unlisted `Dev/` stops configure). The only other dev-only pattern is `Tests/`.
- **No stub implementation files** (`<Name>Shipping.cpp` / `<Name>Off.cpp` with empty bodies): the host would call a feature that does not exist. The feature attaches to the host, not the other way.
- No file pair for a difference of a value or a few lines (first two rows). Do not split one class across files to shorten them — extract the second job into its own class.
- **Never re-derive the configuration** from other macros (`#if !defined( _DEBUG )`). `SW_SHIPPING` and `SW_TARGET_NAME` come from `cmake/Engine/BuildLayout.cmake`; `BuildInfo.h` mirrors them. Client/server has no macro: read `build::kWithClientCode` · `kWithServerCode`.
- **No alias macros** for a configuration; dev-only code writes `#if !defined( SW_SHIPPING )` itself. A folder CMake excludes says so where the sources are excluded (`Source/Engine/CMakeLists.txt`).
- **Client / server split by folder name**: a Server target builds no `Client/` (`SW_CLIENT_ONLY_SOURCE_PATTERNS`, gate `serverbuild`), a Client target builds no `Server/` (`SW_SERVER_ONLY_SOURCE_PATTERNS`, gate `clientbuild`; both `[gatefolder]` in `TargetLayers.toml`). Shared code reaches the other side only through a published seam (`IGameClientPartFactory`, `IOnlineLocalServerFactory`) and errors when it is absent. A shared type does not live in a folder named after one side (`Engine/Config/ServerConfig.h`).
- **Test and dev hooks stop at Release.** Shipping holds no automation runner or registrations, no test global variables (`SW_TEST_GLOBAL_VARIABLE` folds to a constant) and no dev counters on runtime classes — dev features observe through instrumentation (`SW_PROFILE_COUNT`). `py -3 -m Scripts shipping-check` fails on a test variable name left in a shipping binary.
- Editor-only functions are a separate rule (`AGENTS.md`, Source/Editor row); this file is dev vs shipping behaviour of runtime code.
