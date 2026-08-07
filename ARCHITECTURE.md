# Architecture

SW Engine 타겟 그래프, Dev/Shipping 레이아웃, Runtime C-ABI, RHI·리플렉션 파이프라인을 짧게 정리합니다. 네이밍 규칙은 [README.md](README.md)를 참고하세요.

## Target graph

```text
App (exe)
 ├─ Core          (SHARED Dev / STATIC Shipping)
 ├─ RuntimeAPI    (INTERFACE — EditorAPI / GameAPI / handles)
 ├─ EditorModule  (MODULE, Dev only)  ← fillEditorAPI
 └─ SWGame        (MODULE Dev / STATIC Shipping)  ← fillGameAPI

RHI_DX11 / RHI_DX12  (MODULE, optional SW_RHI_AS_MODULES)
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
| C++ define | (없음) | `SW_SHIPPING` |

빌드 타입(Debug/Release)과 Shipping 여부는 **독립**입니다.

- `Ninja-Debug` / `Ninja-Release`: Dev 레이아웃 (핫리로드 가능). Release여도 MODULE.
- `Ninja-Shipping`: Release + `SW_SHIPPING_BUILD=ON`.
- `CI-Debug`: CI용 Dev Debug (시스템 컴파일러 + vcpkg toolchain `-D`).

## Runtime C-ABI

App은 Editor/Game 구현 클래스를 직접 링크하지 않고 함수 테이블만 사용합니다.

- `Source/Runtime/EditorAPI.h` — `fillEditorAPI`
- `Source/Runtime/GameAPI.h` — `fillGameAPI`
- Dev: `LiveReloadManager` 가 exe 옆 MODULE 을 섀도 복사 후 `GetProcAddress`/`dlsym`
- Shipping: `fillGameAPI` 정적 호출, Editor 없음

## RHI backends

- `IRHIDevice` + `RHIBackendRegistry` 는 Core.
- `SW_RHI_AS_MODULES=ON`(기본): DX11/DX12 는 `RHI_DX11` / `RHI_DX12` MODULE, Core가 지연 로드.
- Vulkan / OpenGL 디바이스는 현재 Core에 포함 (추후 MODULE 통일 후보).

## Reflection

1. `Tools/ReflectionParser` (libclang) 가 `REFLECT`/`PROPERTY` 헤더를 파싱.
2. `cmake/internal/Reflection.cmake` 의 `sw_add_reflection_step` 이 생성물(`build/generated/<Target>/`)을 해당 타겟에 추가.
3. 설정은 `Config/parser_config.json` (`-std=c++17` 등).

## Tests

| 타겟 | 역할 |
|--|--|
| CoreUtilityTest | Utility / Object / 일부 Graphics 단위 테스트 |
| ReflectionTest | 리플렉션 생성·직렬화 |
| CoreSmokeTest | Core 링크 스모크 |
| ModuleSmokeTest | Dev에서 MODULE `fillGameAPI` / `fillEditorAPI` 로드 스모크 |

## Local & CI

로컬:

```bash
cmake --preset Ninja-Debug
cmake --build --preset Ninja-Debug
ctest --test-dir build/Ninja-Debug --output-on-failure
```

CI는 `CI-Debug` preset + 동일 vcpkg installed dir 정책을 사용합니다 (`.github/workflows/ci.yml`).
