> 질문: 개발 · 배포(· 플랫폼 · 타깃) 차이를 코드에서 어떻게 가르는가?
> 독자: 구성마다 다르게 동작해야 하는 코드를 쓰는 사람.
> 결론: 차이의 크기로 고른다 — 값뿐이면 `build::` 상수, 갈래 하나가 다른 구성에 없는 헤더 · 모듈을 쓰면 그 자리 `#if`, 기능 전체가 없으면 CMake 가 구현 파일을 고른다. 몇 줄짜리 차이로 파일 짝을 만들지 않는다.

## Build configuration switches

Pick the smallest form that fits the difference. The goal is the same in all three: one decision point, and code that is compiled in every configuration whenever possible so the configuration you did not build cannot rot.

| The difference is ... | Use | Example |
| --- | --- | --- |
| A value only — both branches use symbols every configuration has | `build::kShipping` (or `kWithClientCode` · `kWithServerCode`) from `Core/Foundation/BuildInfo.h`, read with plain `if` or `if constexpr`. Both branches compile everywhere; the dead one is dropped, and functions it calls need no definition in that configuration | GPU diagnostics gates (`build::kShipping == false && gv_gpuAssert`), `GameSelection::select`, `ConfigManager::initializeSubsystem` (disk vs baked JSON) |
| A few lines, and one branch needs a header, type or module the other configuration does not build | `#if defined( SW_SHIPPING )` at that spot, in the one file. `if constexpr` cannot help: a discarded branch outside a template is still parsed and name-checked | Logger sink selection, `DevCommandsEnabled.h` |
| A whole feature the configuration does not have (DevTools host, module reload, server debug console, editor module) | CMake excludes the feature's files and `sw_pickVariantSource` picks a `<Name>Shipping.cpp` that implements the same declaration as an empty or refusing body. The caller stays branch-free | `HostDevTools`, `ModuleReloadDriver`, `ServerDebugControl`, `EditorModuleManagerHeadless` |

Rules:

- Do not create a `<Name>Shipping.cpp` pair for a difference of a value or a few lines — two files that differ by `return true;` / `return false;` make the reader open both to learn one bit. Use the first row.
- A `<Name>Shipping.cpp` exists only because the dev implementation links code the configuration does not build. If the dev body would compile in shipping, merge it back with `build::kShipping`.
- Never re-derive the configuration from other macros (`#if !defined( _DEBUG )` and so on). `SW_SHIPPING`, `SW_WITH_CLIENT_CODE`, `SW_WITH_SERVER_CODE` come from `cmake/Engine/BuildLayout.cmake`; `BuildInfo.h` mirrors them as constants.
- Engine and Core still hold no editor-only function (`AGENTS.md`, Source/Editor row) — this file is about dev vs shipping behaviour of runtime code, not about `WITH_EDITOR` regions.
