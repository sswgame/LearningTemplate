# BuildConfigSwitch

> 결론: 값뿐이면 `build::` 상수, 갈래 하나가 다른 구성에 없는 헤더 · 모듈을 쓰면 그 자리 `#if`, 기능 전체가 없으면 CMake 가 폴더를 뺀다. 파일 짝을 만들지 않는다.

## Build configuration switches

Pick the smallest form that fits; keep code compiled in every configuration so the one you did not build cannot rot.

| The difference is ... | Use | Example |
| --- | --- | --- |
| A value only — both branches use symbols every configuration has | `build::kShipping` (or `kWithClientCode` · `kWithServerCode`, `kStaticModules`) from `Core/Foundation/BuildInfo.h`, with `if` / `if constexpr`. What the dead branch calls needs no definition | `RenderDiagnostics::get`, `ConfigManager::initializeSubsystem` |
| A few lines, and one branch needs a header, type or module the other configuration does not build | `#if defined( SW_SHIPPING )` (or `SW_WITH_DYNAMIC_MODULES`) at that spot, in one file (`if constexpr` outside a template still name-checks) | Logger sink selection, `DevCommandRegistry.h` · `AutomationWatch.h` registration macros |
| A whole feature the configuration does not have (automation runner, DevTools host, module reload, server debug console, editor module) | The feature registers itself (`SW_MODULE_SUBSYSTEM` publishing an interface) from its own folder; CMake excludes the folder. The host looks the interface up and skips it when absent; when the run asked for it (`-scenario`, `-EnableEditor`), startup fails with the reason | Unreal Developer/Editor module |

Rules:

- **Platform chains** end in `#else` + `#error "Unsupported platform"` and name the platform in every branch (`SW_PLATFORM_POSIX` for the three non-Windows). Never `#if Windows #else` or `!defined( SW_PLATFORM_* )`. Lint `platform-chains` enforces (C++ and CMake; exemptions `Config/Rules/Lint/PlatformChains.json`). Scripts read the host OS only from `Scripts/common/host_platform.py`.
- Platform folders are chosen by CMake (`sw_filter_platform_sources`); no whole-file `#if` in a platform-folder `.cpp`.
- **No file-name switches** (`*DevCommands.cpp`, `*Shipping.cpp`). Self-registering dev code stays in the feature's `.cpp` (the macro does not register in Shipping). Code needing headers Shipping does not build goes in a `Dev/` folder listed in `Config/Rules/Layers/DevFolders.json` (configure enforces).
- **No stub implementation files** (`<Name>Shipping.cpp` / `<Name>Off.cpp`): the feature attaches to the host, not the other way.
- No file pair for a value or a few lines. Do not split one class across files to shorten them — extract the second job into its own class.
- **Never re-derive the configuration** from other macros (`#if !defined( _DEBUG )`); no alias macros. `SW_SHIPPING` · `SW_TARGET_NAME` come from `cmake/Modules/Build/BuildLayout.cmake`; the old client/server macros are poisoned (`TargetMacroCheck.h`).
- **Client / server split by folder name** (`Client/` · `Server/`, gates in [Includes](Includes.md)). Shared code reaches the other side only through a published seam and errors when absent. A shared type does not live in a folder named after one side.
- **Costly diagnostics are compiled into every non-Shipping build and chosen at startup**: `-rhi-validation`, `-rhi-dred`, `-rhi-breadcrumbs`, `-deadlock-detection` (defaults in `Config/Engine/EngineConfig.json` `_rhi`, read once through `resolveRHIDiagnosticSwitch`; never a runtime dev variable). Cheap checks sit under `SW_DO_CHECKS`. A diagnostic too costly for Release reads `build::kDebug` with `if constexpr` — verification builds no Debug, so `#if defined( SW_DEBUG )` would rot; it stays only for the debug CRT or headers the reflection parser reads.
- **Test and dev hooks stop at Release**: no automation, test global variables or dev counters in Shipping. Per-frame numbers go through `SW_PROFILE_SCOPE` · `SW_PROFILE_COUNT`; a count only tests read is a `DevCounter` field (`Core/Concurrency/DevCounter.h`, getter returns `const DevCounter&`). `py -3 -m Scripts shipping-check` (run once in `preflight`) fails on dev names in a shipping binary.
- Dev-only behaviour (plain ports, dev certificates, fake providers) is asked through a registered dev substitute (`Core/Network/DevOnlineExtension.h`), not the configuration name.
- Reflection editor metadata lives in `ReflectionEditorMetadata.h` tables built only in dev units (`<이름>.gen.meta.cpp`); runtime metadata in `ReflectionMetadata.h` — struct layout is the same in every configuration.
- Editor-only functions: `AGENTS.md` Source/Editor row.
