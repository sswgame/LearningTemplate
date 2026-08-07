# Naming Conventions

이 문서는 프로젝트의 **CMake / Code(C++) / Scripts(Python)** 네이밍 규칙을 정리합니다.

---

## CMake

| 대상 | 규칙 | 예 |
|------|------|-----|
| Feature option (`-D`) | `SW_*` + `option()` | `SW_ENABLE_PCH`, `SW_USE_VCPKG`, `SW_BUILD_GAME` |
| 함수 / 매크로 | `sw_snake_case` | `sw_add_library`, `sw_copy_dxc_dlls`, `sw_emit_runtime_copies` |
| 메타·경로·리스트 변수 | `sw_snake_case` | `sw_output_directory`, `sw_vcpkg_root`, `sw_flag_libraries` |
| 함수 내부 로컬 | `_snake_case` | `_dxc_dll`, `_opt_name` |
| INTERFACE 타겟 | `sw_<영역>_<이름>` | `sw_compiler_clang`, `sw_third_party_includes` |
| 타겟 프로퍼티 / compile def | `SW_SCREAMING` | `SW_RUNTIME_COPY_FILES`, `SW_PLATFORM_WINDOWS` |
| 제품 타겟 이름 | PascalCase / 제품명 | `App`, `Core`, `EditorModule`, `RHI_DX12` |
| 업스트림 | 그대로 | `CMAKE_*`, `VCPKG_*`, `DIRECTX_DXC_TOOL` |

---

## Code (C++)

| 대상 | 규칙 | 예 |
|------|------|-----|
| 네임스페이스 | `sw` (+ 하위) | `sw`, `sw::editor`, `sw::constant` |
| 클래스 / 구조체 / enum 타입 | `PascalCase` | `ImGuiEditor`, `TaskManager`, `RHIBackend` |
| 인터페이스 | `I` + `PascalCase` | `IEditor`, `IRHIDevice`, `IWindow` |
| 멤버 함수 / free 함수 | `camelCase` | `initialize()`, `getRootFolderPath()` |
| 멤버 변수 | `_camelCase` | `_bInitialized`, `_gameRenderTarget` |
| bool 멤버 | `_b` + `Pascal` | `_bEnableEditor`, `_bAutoScroll` |
| 지역 변수 | `camelCase` | `consolasPath`, `deltaTime` |
| 상수 | `k` + `PascalCase` | `kMaxPathSize`, `kFontSize` |
| **전역 변수** (`SW_GLOBAL_VARIABLE_*` / `extern`) | **`gv_` + `PascalCase`** | **`gv_RHIBackend`, `gv_EnableVSync`** |
| **파일·함수 static / 익명 네임스페이스 변수** | **`s_` + `PascalCase` 또는 `camelCase`** | **`s_activeWindow`, `s_deviceExtensions`, `s_instance`** |
| **public static 멤버 변수** | **`s_` + …** | **`s_typeKey`** |
| **private / protected static 멤버 변수** | **`_s_` + …** | **`_s_nextObjectId`, `_s_bInitialize`, `_s_engineFolderPath`** |
| 타입 별칭 | 소문자 고정폭 | `int32`, `uint64`, `float32`, `utf8` |
| 매크로 / compile def | `SW_SCREAMING` | `SW_API`, `SW_LOG_INFO`, `SW_ASSERT`, `SW_DEBUG` |
| 핸들 / RHI 타입 | `RHI` + `Pascal` / `Handle` | `RHITextureHandle`, `RHIBackend` |
| 모듈 export | `SW_` / 제품명 | `SW_MODULE_API`, `fillEditorAPI` |

> `SW_GLOBAL_VARIABLE_*` 매크로로 등록하는 변수도 심볼·등록 이름 모두 `gv_*` 를 사용합니다. 파일/함수 범위 `static` 이나 익명 네임스페이스 안의 데이터는 `gv_*` 가 아니라 `s_*` 입니다.
> private static 멤버는 일반 private 멤버의 `_` 접두사와 static의 `s_` 를 합쳐 `_s_*` 로 씁니다.
> **예외 (Math):** `float2`/`float3`/`float4`/`float4x4`/`quaternion`/`double3`/`MathUtil` 의 public static 상수(`Zero`, `Identity`, `UnitX`, `Pi` 등)는 `s_` 없이 PascalCase 그대로 둡니다.

---

## Scripts (Python)

| 대상 | 규칙 | 예 |
|------|------|-----|
| 공개 함수 | `PascalCase` | `SetupEnvironment`, `FindDxcDlls`, `GenerateChangelog` |
| 비공개 헬퍼 | `_snake_case` | `_find_first_existing_file`, `_get_or_find` |
| 모듈 / 파일명 | `PascalCase.py` | `SetupEnvironment.py`, `ConfigHelper.py` |
| 로컬 변수 | `snake_case` | `llvm_path`, `project_root` |
| `engine_config.json` 키 | `snake_case` | `dxc_dll_path`, `vcpkg_root`, `ninja_path` |
| CLI 진입점 | `if __name__ == "__main__":` → 공개 함수 호출 | `GenerateDocs()` |

---

## 한 줄 요약

| 레이어 | 핵심 |
|--------|------|
| CMake | 플래그 `SW_*`, 헬퍼 `sw_*` |
| Code | 타입 Pascal, 멤버 `_camel`, **전역 `gv_*`**, **static `s_*` / private static `_s_*`**, 매크로 `SW_*` |
| Scripts | 공개 PascalCase, 비공개 `_snake`, config 키 `snake_case` |
