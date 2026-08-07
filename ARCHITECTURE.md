# Architecture

SW Engine 타겟 그래프, Dev/Shipping 레이아웃, Runtime C-ABI, RHI·리플렉션 파이프라인을 짧게 정리합니다. 네이밍 규칙은 [README.md](README.md)를 참고하세요.

## Target graph

```text
App (exe)
 ├─ Core          (SHARED Dev / STATIC Shipping)
 ├─ RuntimeAPI    (INTERFACE — EditorAPI / GameAPI / handles)
 ├─ EditorModule  (MODULE, Dev only)  ← fillEditorAPI
 └─ SWGame        (MODULE Dev / STATIC Shipping)  ← fillGameAPI

RHI_DX11 / RHI_DX12  (MODULE, optional SW_RHI_AS_MODULES; forced OFF in Shipping)
  └─ loaded by Core at runtime via createRHIDevice

Tools/ReflectionParser  → generates *.gen.cpp for SWGame (and tests)
Test/*                  → links Core (+ RuntimeAPI for module smoke)
```

Include 정책 (`cmake/internal/Targets.cmake`):

- 기본 타겟은 `Source/` 를 **PRIVATE** 로만 씀 (자기 TU 컴파일용).
- **Core** 가 `Source/` · `Resource/` 를 **PUBLIC** 으로 export → 링크한 쪽에서 `Core/...` include.
- **RuntimeAPI** 는 INTERFACE 로 `Source/` 를 제공하고 Core 에 의존.

## Development vs Shipping

| | Development (`SW_SHIPPING_BUILD=OFF`) | Shipping (`SW_SHIPPING_BUILD=ON`) |
|--|--|--|
| Core | SHARED DLL | STATIC |
| EditorModule | MODULE DLL + 핫리로드 | 빌드 안 함 |
| SWGame | MODULE DLL + 핫리로드 | STATIC → App에 링크 |
| `SW_RHI_AS_MODULES` | ON (기본) — DX MODULE | **FORCE OFF** — DX linked into Core |
| C++ define | (없음) | `SW_SHIPPING` |

빌드 타입(Debug/Release)과 Shipping 여부는 **독립**입니다.

- `Ninja-Debug` / `Ninja-Release`: Dev 레이아웃 (핫리로드 가능). Release여도 MODULE.
- `Ninja-Shipping`: Release + `SW_SHIPPING_BUILD=ON` + `SW_RHI_AS_MODULES=OFF`.
- `CI-Debug`: CI용 Dev Debug (시스템 컴파일러 + vcpkg toolchain `-D`).

## Runtime C-ABI

App은 Editor/Game 구현 클래스를 직접 링크하지 않고 함수 테이블만 사용합니다.

- `Source/Runtime/EditorAPI.h` — `fillEditorAPI`
- `Source/Runtime/GameAPI.h` — `fillGameAPI`
- Dev: `LiveReloadManager` 가 exe 옆 MODULE 을 섀도 복사 후 `GetProcAddress`/`dlsym`
  - **Swap semantics:** 새 섀도 DLL을 먼저 로드한 뒤에만 이전 핸들을 `FreeLibrary`. 실패 시 기존 모듈 유지. 원본 DLL mtime이 올라가면 자동 리로드 큐잉.
- Shipping: `fillGameAPI` 정적 호출, Editor 없음

## RHI backends

- `IRHIDevice` + `RHIBackendRegistry` 는 Core.
- `SW_RHI_AS_MODULES=ON`(Dev 기본): DX11/DX12 는 `RHI_DX11` / `RHI_DX12` MODULE, Core가 지연 로드. 로드된 HMODULE은 registry가 추적·unload.
- **Vulkan / OpenGL 디바이스는 Core에 정적 링크 (의도적).** DX만 optional MODULE. VK/GL MODULE 분리는 추후 후보.
- VK/GL: 기본 `createTexture2D` 지원(`_bOffscreenRT=true`). `_bEditorSupported` / `_bImGuiHooks` 는 false — 에디터 핫스왑/멀티뷰포트 비활성.

## Reflection

1. `Tools/ReflectionParser` (libclang) 가 `REFLECT`/`PROPERTY` 헤더를 파싱.
2. `cmake/internal/Reflection.cmake` 의 `sw_add_reflection_step` 이 생성물(`build/generated/<Target>/`)을 해당 타겟에 추가.
3. **Core Object 베이스**(`GameObject` / `Component` / `SceneComponent`)는 `REFLECT` 하지 않음 — 엔진 고정 타입.
   - 이름/활성 등은 `ObjectStateSerializer` 가 직접 처리.
   - `getTypeInfo()` 훅은 파생 타입(게임 모듈)용으로 남겨 둠.
4. 게임/테스트 타입만 codegen. Component 파생이면 `ComponentFactoryRegistrar` 도 emit.
5. 설정은 `Config/parser_config.json` (`-std=c++17` 등).

## Scene tick

`GameObjectManager::tickParallel`:

1. `flushSceneTransforms` — SceneComponent 월드 캐시를 루트→자식 순으로 확정
2. 병렬 `GameObject::tick` — 이 구간 `getWorld*` 는 캐시만 읽음 (`beginParallelTransformReadOnly`)
3. 다시 flush — tick 중 바꾼 로컬 포즈를 렌더/다음 프레임용으로 반영

병렬 tick 중 `attach`/`detach` 및 계층 구조 변경은 하지 마세요.

## Tests

| 타겟 | 역할 |
|--|--|
| CoreUtilityTest | Utility / Object / 일부 Graphics 단위 테스트 |
| CoreUtilityTest_NoGPU | GPU/window 스위트 제외 (`LABELS nogpu`) |
| ReflectionTest | 리플렉션 생성·직렬화 |
| CoreSmokeTest | Core 링크 스모크 |
| ModuleSmokeTest | Dev에서 MODULE `fillGameAPI` / `fillEditorAPI` 로드 스모크 (Shipping 스킵) |

## Local & CI

로컬:

```bash
cmake --preset Ninja-Debug
cmake --build --preset Ninja-Debug
ctest --test-dir build/Ninja-Debug --output-on-failure
```

clangd / VS Code compile_commands: `build/Ninja-Debug` (`.clangd`, `.vscode/settings.json`).

CI (`.github/workflows/ci.yml`):

- Windows/Linux Dev: `CI-Debug` preset
- Linux: `ctest -L nogpu`
- Windows Shipping: `Ninja-Shipping` → build `App`
