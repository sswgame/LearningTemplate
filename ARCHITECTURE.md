# Architecture

SW Engine 타겟 그래프, Dev/Shipping 레이아웃, Runtime C-ABI, RHI·리플렉션 파이프라인을 짧게 정리합니다. 네이밍 규칙은 [README.md](README.md)를 참고하세요.

## Target graph

```text
App (exe)
 ├─ Core          (SHARED Dev / STATIC Shipping)
 ├─ RuntimeAPI    (INTERFACE — EditorAPI / GameAPI / handles)
 ├─ EditorModule  (MODULE, Dev only)  ← fillEditorAPI
 └─ SWGame        (MODULE Dev / STATIC Shipping)  ← fillGameAPI

RHI_DX11 / RHI_DX12 / RHI_GL / RHI_Vulkan  (MODULE when SW_RHI_AS_MODULES; forced OFF in Shipping)
  └─ loaded by Core at runtime via createRHIDevice (DX Windows-only; GL/VK all platforms)

Tools/ReflectionParser  → generates *.gen.cpp for SWGame (and tests)
Test/*                  → links Core (+ RuntimeAPI for module smoke)
```

Include 정책 (`cmake/internal/Targets.cmake`):

- 기본 타겟은 `Source/` 를 **PRIVATE** 로만 씀 (자기 TU 컴파일용).
- **Core** 가 `Source/` · `Resource/` 를 **PUBLIC** 으로 export → 링크한 쪽에서 `Core/...` include.
- **RuntimeAPI** 는 INTERFACE 로 `Source/` 를 제공하고 Core 에 의존.

## Binary output (per preset)

`sw_output_directory` = `CMAKE_BINARY_DIR` (preset별). 런타임/모듈은 `${CMAKE_BINARY_DIR}/Bin` 에 모입니다.

예: `build/Ninja-Debug/Bin`, `build/Ninja-Shipping/Bin` — 프리셋끼리 `build/Bin` 을 덮어쓰지 않음.
MODULE DLL(`sw_set_module_bin_output`)도 같은 preset `Bin` 에 두어 LiveReload가 exe 옆에서 동작합니다.

## Development vs Shipping

| | Development (`SW_SHIPPING_BUILD=OFF`) | Shipping (`SW_SHIPPING_BUILD=ON`) |
|--|--|--|
| Core | SHARED DLL | STATIC |
| EditorModule | MODULE DLL + 핫리로드 | 빌드 안 함 |
| SWGame | MODULE DLL + 핫리로드 | STATIC → App에 링크 |
| `SW_RHI_AS_MODULES` | ON (기본) — RHI_* MODULE | **FORCE OFF** — all RHI backends linked into Core |
| C++ define | (없음) | `SW_SHIPPING` |

빌드 타입(Debug/Release)과 Shipping 여부는 **독립**입니다.

- `Ninja-Debug` / `Ninja-Release`: Dev 레이아웃 (핫리로드 가능). Release여도 MODULE.
- `Ninja-Shipping`: Release + `SW_SHIPPING_BUILD=ON` + `SW_RHI_AS_MODULES=OFF` + `SW_ENABLE_TESTING=ON`.
- `CI-Debug`: CI용 Dev Debug (시스템 컴파일러 + vcpkg toolchain `-D`).

## Runtime C-ABI

App은 Editor/Game 구현 클래스를 직접 링크하지 않고 함수 테이블만 사용합니다.

- `Source/Runtime/EditorAPI.h` — `fillEditorAPI`
- `Source/Runtime/GameAPI.h` — `fillGameAPI`
- Dev: `LiveReloadManager` 가 exe 옆 MODULE 을 섀도 복사 후 `GetProcAddress`/`dlsym`
  - **Swap semantics:** 새 섀도 DLL을 먼저 로드한 뒤에만 이전 핸들을 `FreeLibrary`. 실패 시 기존 모듈 유지.
  - 원본 DLL mtime이 올라가면 **~300ms debounce** 후 자동 리로드 큐잉 (복사 중 리로드 스톰 방지).
- Shipping: `fillGameAPI` 정적 호출, Editor 없음
- `EditorUIContext`: Dev 전용 **공유 Core 포인터** (Material*/IRHIDevice* 등). C ABI/POD가 아님 — 같은 프로세스 MODULE 전제. 핸들 API는 `RuntimeHandles.h`.

## RHI backends

- `IRHIDevice` + `RHIBackendRegistry` 는 Core. 헤더는 Core에 두고 device `.cpp` 만 MODULE로 분리.
- `SW_RHI_AS_MODULES=ON`(Dev 기본): `RHI_DX11` / `RHI_DX12` (Windows) + `RHI_GL` / `RHI_Vulkan` MODULE을 Core가 지연 로드 (`createRHIDevice`).
  - `unloadModules` 는 factory를 먼저 비운 뒤 `FreeLibrary`/`dlclose`. **MODULE 백엔드 device는 먼저 destroy** (`RHI::shutdown` 경로).
- Shipping: `SW_RHI_AS_MODULES` FORCE OFF → 모든 백엔드 device가 Core에 정적 링크.
- Editor는 concrete device 타입에 링크하지 않음 — `getNativeTextureName` / `queryVulkanImGuiNative` 가상 API 사용.
- Caps: OpenGL/Vulkan `_bBindless = false` (`supportsBindless()` false). `_bOffscreenRT=true`. `_bEditorSupported` / `_bImGuiHooks` false.
- **RHIReleaseQueue** (`frameLatency` deferred destroy): wired on **DX12 / OpenGL / DX11** — `destroyBuffer`/`destroyTexture` enqueue; `endFrame` → `tickFrame()`; `waitIdle`/`shutdown` → `flushAll()`. **Vulkan not wired** (deferred).

### Vulkan — deferred / future

명시적으로 **아직 하지 않은** 항목 (추후 후보):

- 에디터 핫스왑 / ImGui multi-viewport hooks (`_bEditorSupported` / `_bImGuiHooks`)
- 본격 bindless descriptor 경로
- RHIReleaseQueue 연동한 deferred destroy

### RenderGraph

- `compile()`: 리소스 producer→consumer Kahn 위상 정렬 + 사이클 감지.
- `execute()`: 컴파일된 순서로 패스 콜백 실행. 입력/출력에 대해 **논리** Read/Write 전이를 카운트합니다 (RHI barrier API 없음 — GPU barrier 미연동).

### Command lists / compute root constants

- **Immediate-mode backends** (DX11 / OpenGL, and current DX12 device path): draw/dispatch run on the live context; `executeCommandList` is effectively a no-op (recorded `IRHICommandList` is not a deferred submission queue yet).
- **Compute root constants**: D3D12 maps to `SetComputeRoot32BitConstants` (API capped by D3D12 root signature limits, typically ≤64 DWORDs per root parameter). DX11/GL/VK stubs may ignore or partially implement — do not assume cross-backend parity.

## Reflection

1. `Tools/ReflectionParser` (libclang) 가 `REFLECT`/`PROPERTY` 헤더를 파싱.
2. `cmake/internal/Reflection.cmake` 의 `sw_add_reflection_step` 이 생성물(`build/generated/<Target>/`)을 해당 타겟에 추가.
3. **Core Object 베이스**(`GameObject` / `Component` / `SceneComponent`)는 `REFLECT` 하지 않음 — 엔진 고정 타입.
   - 이름/활성 등은 `ObjectStateSerializer` 가 직접 처리.
   - `getTypeInfo()` 훅은 파생 타입(게임 모듈)용으로 남겨 둠.
   - `Component::setCachedTypeInfo` / `_cachedTypeInfo`: 팩토리 생성 시 `TypeRegistry`에서 찾아 캐시.
   - **TypeInfo rebind on MODULE reload:** `ComponentManager::rebindAllCachedTypeInfo` / `GameObjectManager::rebindAllCachedTypeInfo` refresh `_cachedTypeInfo` from `TypeRegistry` after hot-reload (called from SWGame / EditorModule reload paths). `clearCachedTypeInfo` drops stale pointers first.
4. 게임/테스트 타입만 codegen. Component 파생이면 `ComponentFactoryRegistrar` 도 emit.
5. **모듈별 factory head:** `SW_DECLARE_MODULE_REGISTRAR_HEAD` / `SW_COMPONENT_FACTORY_MODULE_HEAD()` —
   App / SWGame / Editor / Test 가 각자 head를 두어 MODULE CRT 정적 초기 시 registrar 체인이 섞이지 않음 (`ModuleRegistrarHeads.h`).
6. `ComponentManager::unregisterFactoriesByModule` — 모듈 언로드 시 해당 모듈이 등록한 팩토리만 제거.
7. 설정은 `Config/parser_config.json` (`-std=c++17` 등).

## Resource paths

`ResourceUtil` 은 루트/폴더 경로만 유지하고 **파일 경로 캐시는 하지 않습니다** (`clearCache()` 는 no-op). 해석은 호출 시마다 루트 목록을 순회합니다.

## Editor Hierarchy / Inspector

- **Hierarchy** (`OutlinerPanel`): root GameObjects (parent==null) with nested child GOs when present; under each GO, components (SceneComponent tree kept separate). Selection in `EditorSelection`.
  - Context menu: Unparent / Parent to Selected (cycle-safe), Create Child, Create under selected from toolbar.
- **Inspector**: selection Name/Active, read-only Parent name (+ Unparent), SceneComponent transform, reflected `PROPERTY` widgets (containers show size only), `FUNCTION()` Invoke with simple arg editors (`int32`/`int64`/`float`/`bool`/`string`); Engine/Material section remains collapsible.
- **Play→Stop selection**: `EditorSelection` remaps GO by cached name and component by stable key (`componentName|typeName` + `#` + occurrence), matching `ObjectStateSerializer` SceneTransforms keys.

## FUNCTION() codegen

ReflectionParser emits `FunctionInfo` + `TaskArgs` invokers for `FUNCTION()` methods (return value packed in `TaskValue`). Param type spellings are normalized (`int`→`int32`, `long long`→`int64`, etc.). Prefer `TaskArgs( int32{ n }, … )` / `args.add( int32{ n } )` over braced-init `TaskArgs{ n }` (initializer_list typing surprises).

## ObjectDiffSerializer

`serializeDiff` currently writes property **name hashes only** (no value payloads). `deserializeDiff` therefore **fails honestly** (`SW_LOG_ERROR` + `false`) until a real delta format exists.

## Scene tick

`GameObjectManager::tickParallel`:

1. `flushSceneTransforms` — SceneComponent 월드 캐시를 루트→자식 순으로 확정
2. 병렬 `GameObject::tick` — 이 구간 `getWorld*` 는 캐시만 읽음 (`beginParallelTransformReadOnly`)
3. 다시 flush — tick 중 바꾼 로컬 포즈를 렌더/다음 프레임용으로 반영

병렬 tick 중 `attach`/`detach` 및 계층 구조 변경은 하지 마세요.

**GameObject parent hierarchy (basic):** `attachToParent` / `detachFromParent` / `getParent` / `getChildren`; `isActiveInHierarchy` propagates from parent. Independent of SceneComponent attach. Parallel transform read-only blocks attach/detach (same guard as SceneComponent).

**ObjectStateSerializer:** `ParentGO` stores the parent GameObject by **stable name** (empty = root). Load creates/restores GOs first; `rebindSceneHierarchy` second pass re-attaches both SceneComponent parents and GameObject parents (used by Play/Stop snapshot restore). `SceneTransforms` keyed by **stable component keys** (component name or typeName + occurrence index on that GO), not flat list index. Parent attach can reference cross-GO parents as `ownerName/stableKey`. Count mismatches log loudly.

## Tests

| 타겟 | 역할 | Labels |
|--|--|--|
| CoreUtilityTest | Utility / Object / 일부 Graphics 단위 테스트 | `unit;core` |
| CoreUtilityTest_NoGPU | GPU/window/shader 스위트 제외 (`--test_filter`); MaterialLoadAndSave 유지 | `unit;core;nogpu` |
| ReflectionTest | 리플렉션 생성·직렬화 (GPU 불필요) | `unit;reflection;nogpu` |
| CoreSmokeTest | Core 링크 스모크 (GPU 불필요) | `unit;core;nogpu` |
| ModuleSmokeTest | Dev: MODULE `fill*API` 로드 / Shipping: 정적 `fillGameAPI` (GPU 불필요) | `unit;module;nogpu` |

`ctest -L nogpu` 는 ReflectionTest / CoreSmokeTest / ModuleSmokeTest / CoreUtilityTest_NoGPU 를 돌립니다.

### Soft-SKIP quarantine

Some cases call `SW_TEST_SKIP` so the suite stays green when an environment cannot run them (discoverable via `--test_list` / `[SKIPPED]` summary):

- **TestRHI** full pipeline — needs stable GPU/RHI env
- **TestMaterial** color/instance/reflection — historically hang on TaskManager / DXC without timeout
- **TestShader** — compiler DLL unavailable
- Fixed / un-SKIP'd when flake was assert/size: **WindowTest.PlatformFactoryAndLifecycle** (DPI-tolerant size), **Utility_GlobalVariable.CommandLineIntegration** (partial CLI map + `getArgument` assert)

## Local & CI

로컬:

```bash
cmake --preset Ninja-Debug
cmake --build --preset Ninja-Debug
ctest --test-dir build/Ninja-Debug --output-on-failure
# binaries: build/Ninja-Debug/Bin
```

clangd / VS Code compile_commands: `build/Ninja-Debug` (`.clangd`, `.vscode/settings.json`).

CI (`.github/workflows/ci.yml`):

- Windows/Linux Dev: `CI-Debug` preset
- Linux: `ctest -L nogpu` (ReflectionTest / CoreSmokeTest / ModuleSmokeTest / CoreUtilityTest_NoGPU)
- Windows Dev: full `ctest` (GPU 실패 시 TestRHI는 SKIP)
- Windows Shipping: `Ninja-Shipping` → `App` + `CoreUtilityTest`, then `ctest -L nogpu`
- lint-format: `find … \( -name "*.cpp" -o … \)` 로 확장자 OR 묶음
