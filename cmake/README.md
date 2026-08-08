# cmake/

빌드 그래프·플래그·의존성·RPATH를 담당합니다. 호스트 파일/경로 탐색은 [`Scripts/`](../Scripts/README.md)에 두고 여기서 호출만 합니다.

| 경로 | 역할 |
|------|------|
| `UserConfig.cmake` | `SW_*` 옵션 |
| `VcpkgGate.cmake` | `SW_USE_VCPKG` → `Modules/Toolchain/Vcpkg` (≠ `CMAKE_TOOLCHAIN_FILE`) |
| `LoadFlagModules.cmake` | Compiler / Platform / Architecture / BuildType / Options (명시 include) |
| `Modules/Platform/` | `sw_platform_*` (OS) → `sw_flag_libraries`; GPU → `sw_graphics_libs` |
| `Modules/Toolchain/` | vcpkg 게이트 세부·overlay triplet |
| `internal/Python.cmake` | `sw_execute_python_script` |
| `internal/SetupEnvironment.cmake` | `Scripts/setup/SetupEnvironment.py` |
| `internal/Discover.cmake` | ThirdParty 직속 하위만 |
| `internal/Targets.cmake` | `sw_add_*` (`TYPE` / `NO_INSTALL` 등) |
| `internal/VcpkgHostFixes.cmake` | Linux Vulkan loader fix (Scripts) |
| `internal/VcpkgManifestHash.cmake` | `vcpkg.json` (+ config) SHA256 |
| `internal/VcpkgManifestStamp.cmake` | manifest 해시 스탬프 (install skip용) |
| `internal/VcpkgRuntime.cmake` | `sw_copy_vcpkg_*` / path helpers (`Targets` 이후) |
| `internal/ThirdPartyWarnings.cmake` | SYSTEM include / 경고 억제 헬퍼 |
| `internal/AuxTargets.cmake` | AutoChangelog / GenerateDocs |
| `internal/BuildConfig.cmake` | Shipping 레이아웃 정책 (`SW_SHIPPING_BUILD`) |
| `internal/Output.cmake` | flat `Bin` / `Lib` (Ninja single-config + LiveReload) |

루트 `CMakeLists.txt`가 `sw_third_party_includes`를 만들고 `ThirdParty/`를 `add_subdirectory` 합니다.

## 링크 가방

| 변수 / 타겟 | 누가 링크 | 내용 |
|-------------|-----------|------|
| `sw_flag_libraries` | 모든 `sw_add_*` (PRIVATE) | compiler/platform/arch/buildtype + `sw_toolchain_vcpkg` |
| `sw_graphics_libs` | **Core PUBLIC만** (소비자에 전파) | d3d / OpenGL / Vulkan 등 GPU |
| `sw_third_party_includes` | Core PUBLIC | ThirdParty INTERFACE 집결 |

## Preset / vcpkg 계약

- **Preset = `Tools/vcpkg` 필수**: `CMAKE_TOOLCHAIN_FILE`은 `${sourceDir}/Tools/vcpkg/scripts/buildsystems/vcpkg.cmake`로 고정합니다.
- **`FindVcpkg.py`**: 루트 탐색·bootstrap·`sw_vcpkg_root` 설정용. 해석 결과가 `Tools/vcpkg`와 다르면 configure 시 WARNING.
- Hidden base: `base-vcpkg` → `base-windows` / `base-wsl`; 파생 preset은 build type + 기본과 다른 `SW_*`만 둡니다.
- Shipping 시 `SW_RHI_AS_MODULES`는 `BuildConfig.cmake`가 **non-cache**로 끕니다.

## Install

- 기본: `sw_add_*` → `install` (App/Core/Editor/SWGame/RHI_* + Resource)
- `NO_INSTALL`: Test 전 타겟, ReflectionParser

## Config 보완

- **캐시 절대경로**: `Config/engine_config.json` (Scripts가 기록, CMake/`FindLlvmBin`가 소비)
- **탐색 후보·URL**: `Config/search_paths.json`
- **vcpkg install skip**: installed 트리 + `.sw_vcpkg_manifest_sha` == `vcpkg.json` 해시 → `VCPKG_MANIFEST_MODE=OFF`
- 강제 재설치: `-DSW_VCPKG_FORCE_INSTALL=ON` 또는 `build/vcpkg_installed` 삭제
- vcpkg 자동 clone: `-DSW_VCPKG_AUTO_BOOTSTRAP=ON` 또는 `FindVcpkg.py --install`
