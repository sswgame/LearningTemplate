# cmake/ (빌드 시스템)

CMake는 빌드 그래프·플래그·의존성만 담당한다. 컴파일러/Ninja/vcpkg 탐색은 `Scripts/setup/`에 위임한다.

## 네이밍

| 종류 | 규칙 | 예 |
|------|------|-----|
| function / macro | `sw_camelCase` | `sw_addLibrary`, `sw_configurePch` |
| 프로젝트 변수 · INTERFACE 타겟 | `sw_snake_case` | `sw_flag_libraries`, `sw_public_source_includes` |
| option / C++ 매크로 | `SW_UPPER_SNAKE_CASE` | `SW_ENABLE_PCH`, `SW_EXPORTS` |
| 함수 내부 로컬 | `camelCase` (앞에 `_` 없음) | `kitType`, `libType`, `pchPath` |

| 경로 | 역할 |
|------|------|
| `UserConfig.cmake` | `SW_*` 옵션, `sw_*` 변수, 네이밍 규칙 |
| `VcpkgGate.cmake` | `SW_USE_VCPKG` → `Modules/Toolchain/Vcpkg` |
| `LoadFlagModules.cmake` | Compiler / Platform / Architecture / BuildType / Options |
| `Modules/` | 플래그 INTERFACE · vcpkg overlay triplet |
| `internal/Targets.cmake` | **타겟 헬퍼 단일 진입** (출력/설치/Shipping/PCH/delay-load/GF kit/ThirdParty 래퍼) |
| `internal/SetupEnvironment.cmake` | Python 환경 주입 + sccache (`SW_USE_SCCACHE`) |
| `internal/VcpkgRuntime.cmake` | vcpkg include/bin · DLL 복사 |
| `internal/Reflection.cmake` | ReflectionParser codegen |
| `internal/AuxTargets.cmake` | CookPrefabs / CheckEngineLayers / GenerateDocs |
| `internal/Python.cmake` | `sw_executePythonScript` |
| `internal/ClangClWindowsTools.cmake` | clang-cl용 archive 도구 재바인딩 |

## 주요 헬퍼 (`Targets.cmake`)

| 함수 | 용도 |
|------|------|
| `sw_addLibrary` / `sw_addExecutable` | 소스 GLOB + 공통 속성 (`LOG_TAG` / `PCH` / MODULE→Bin 자동) |
| `sw_addGfKit` | GameFramework 키트 SHARED/STATIC + delay-load |
| `sw_addDelayloadHook` | Windows delay-load |
| `sw_addVcpkgConfigLib` / `sw_addVcpkgStaticLib` | ThirdParty find_package 래퍼 |
| `sw_discoverProjects` | ThirdParty 직속 하위 `add_subdirectory` |

## 링크 가방

| 타겟 / 변수 | 누가 | 내용 |
|-------------|------|------|
| `sw_flag_libraries` | 모든 `sw_addLibrary` / `sw_addExecutable` PRIVATE | compiler/platform/… |
| `sw_graphics_libs` | Engine PUBLIC | GPU |
| `sw_third_party_includes` | Core/Engine PUBLIC | ThirdParty (imgui 제외) |
| `sw_public_source_includes` | Engine/Core/Kits PUBLIC | `Source/` + `Resource/` |

## Preset / vcpkg

- Toolchain: `Tools/vcpkg/.../vcpkg.cmake` (또는 해석된 `vcpkg_root`)
- Shipping: `SW_RHI_AS_MODULES`는 `Targets.cmake`에서 CACHE FORCE OFF
- 루트 `CMakeLists.txt` 순서: `project()` 전 옵션/vcpkg → 도구 재바인딩 → 헬퍼 → Source(Core) → Tools → Engine/Editor/App → (옵션) GF/Games → Test
- 매니페스트 skip: `.sw_vcpkg_manifest_sha` == `vcpkg.json` 해시
