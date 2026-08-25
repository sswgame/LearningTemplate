# cmake/ (빌드 시스템)

CMake는 빌드 그래프·플래그·의존성만 담당합니다. 컴파일러/Ninja/vcpkg 탐색은 `Scripts/setup/`에 위임합니다.

## 디렉터리 구조 및 역할 (4-Layer Architecture)

```
cmake/
├── Config/                      [1계층: 전역 설정 및 프로젝트 옵션]
│   ├── BuildOptions.cmake       — SW_* 빌드 기능 옵션 및 프로젝트 메타데이터 정의
│   ├── GenerateConfigConstants.cmake — ConfigConstants.h 및 Shipping 호스트 기본값 자동 생성
│   └── ConfigConstants.h.in     — C++ 헤더 템플릿
│
├── Environment/                 [2계층: 개발 환경 및 툴체인 주입 (project() 이전)]
│   ├── DetectToolchain.cmake    — toolchain_config.json 파싱 & LLVM/Ninja 바인딩
│   ├── VcpkgIntegration.cmake   — vcpkg 매니페스트 및 오버레이 게이트
│   ├── FindWindowsTools.cmake   — lib.exe / mt.exe 탐색 및 clang-cl 아카이버 재바인딩
│   └── PythonUtils.cmake        — Python 인터프리터 탐색 및 스크립트 실행 헬퍼
│
├── Modules/                     [3계층: 컴파일러/플랫폼/아키텍처 INTERFACE 플래그]
│   ├── LoadCompileFlags.cmake   — 플래그 모듈 일괄 인클루더
│   ├── Architecture/            — X64.cmake, ARM64.cmake
│   ├── BuildType/               — Debug.cmake, Release.cmake
│   ├── Compiler/                — Clang.cmake, MSVC.cmake, GCC.cmake
│   ├── Options/                 — CppStandard.cmake, Sanitizer.cmake, UnityBuild.cmake
│   ├── Platform/                — Windows.cmake, Linux.cmake, MacOS.cmake
│   └── Toolchain/               — Vcpkg triplet 및 LLVM 바이너리 탐색
│
└── Engine/                      [4계층: 엔진 빌드 파이프라인 및 타겟 헬퍼 (project() 이후)]
    ├── ModuleBuildRules.cmake   — 타겟 생성 헬퍼 (RHI, Kits, DllExports, OutputDir, AppDeps 등)
    ├── AssetAndToolTargets.cmake— 에셋 쿠킹, Doxygen 문서, 린트 타겟 및 CTest 등록 헬퍼
    ├── ReflectionCodeGen.cmake  — ReflectionParser 코드 생성 파이프라인 (sw_addReflectionStep)
    ├── RuntimeDependencies.cmake— DXC, Vulkan 레이어, mimalloc 런타임 DLL 복사
    └── RhiBackendSources.cmake  — RHI 백엔드 소스 파일 목록
```

## 네이밍 컨벤션

| 종류 | 규칙 | 예시 |
|------|------|------|
| function / macro | `sw_camelCase` | `sw_addRhiBackendModule`, `sw_addGameFrameworkKit`, `sw_registerLintTests` |
| 프로젝트 변수 · INTERFACE 타겟 | `sw_snake_case` | `sw_flag_libraries`, `sw_public_source_includes` |
| option / C++ 매크로 | `SW_UPPER_SNAKE_CASE` | `SW_ENABLE_PCH`, `SW_EXPORTS`, `SW_MODULE_EXPORTS` |
| 함수 내부 로컬 | `camelCase` (앞에 `_` 없음) | `kitType`, `libType`, `targetName` |

## 주요 헬퍼 함수 (`ModuleBuildRules.cmake` & `AssetAndToolTargets.cmake`)

| 함수 | 용도 |
|------|------|
| `sw_configureAppDependencies` | App 타겟의 RHI 모듈, SWGame 딜레이로드/정적링크, CookAssets 의존성 자동 구성 |
| `sw_addRhiBackendModule` | RHI 그래픽스 백엔드(`RHI_DX11` 등) MODULE 타겟 정의 및 공통 속성 바인딩 |
| `sw_addGameFrameworkKit` | GameFramework 장르 키트(`GF_Overworld` 등) 라이브러리 정의 및 리플렉션/딜레이로드 자동화 |
| `sw_registerLintTests` | CTest 린트 테스트(`CheckEngineLayers`, `CheckIncludeOrder`, `CheckSourceGlob`) 일괄 등록 |
| `sw_addReflectionStep` | ReflectionParser 코드 생성 스텝 자동 연결 |
