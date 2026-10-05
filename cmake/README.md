# cmake/ (빌드 시스템)

CMake는 빌드 그래프·플래그·의존성만 담당합니다. 컴파일러/Ninja/vcpkg 탐색은 `Scripts/setup/`에 위임합니다.

## 디렉터리 구조 및 역할 (4-Layer Architecture)

```
cmake/
├── Config/                      [1계층: 전역 설정 및 프로젝트 옵션]
│   ├── BuildOptions.cmake       — SW_* 빌드 기능 옵션 및 프로젝트 메타데이터 정의
│   ├── LoadConfigConstants.cmake — Constants.py 의 상수를 SW_* 변수로 (상수만 필요한 곳은 이것만 include)
│   ├── GenerateConfigConstants.cmake — 위 상수로 ConfigConstants.h · PackFormat.gen.h · CookContract.gen.h · Shipping 호스트 기본값 생성 (한 번)
│   └── ConfigConstants.h.in     — C++ 헤더 템플릿
│
├── Environment/                 [2계층: 개발 환경 및 툴체인 주입 (project() 이전)]
│   ├── DetectToolchain.cmake    — toolchain_config.json 파싱 & LLVM/Ninja 바인딩
│   ├── VcpkgIntegration.cmake   — vcpkg 매니페스트 및 오버레이 게이트
│   ├── FindLlvmBin.cmake        — clang-cl / clang 이 있는 LLVM bin 찾기 (vcpkg 포트 툴체인도 쓴다)
│   ├── ToolchainBinaries.cmake  — 아카이버를 "지금 쓰는 컴파일러 옆" 에서 고정 (LTO 비트코드를 읽어야 한다)
│   ├── FindWindowsTools.cmake   — lib.exe / mt.exe 탐색 및 clang-cl 아카이버 재바인딩
│   ├── WindowsToolSearch.cmake  — MSVC lib.exe · SDK mt.exe 폴더 탐색 (본 프로젝트와 vcpkg 포트 툴체인이 함께 쓴다)
│   └── PythonUtils.cmake        — Python 인터프리터 탐색 및 스크립트 실행 헬퍼
│
├── Modules/                     [3계층: 컴파일러/플랫폼/아키텍처 INTERFACE 플래그]
│   ├── LoadCompileFlags.cmake   — 플래그 모듈 일괄 인클루더
│   ├── Architecture/            — DetectArchitecture.cmake(컴파일러가 겨냥하는 아키텍처 판정), X64.cmake, ARM64.cmake
│   ├── BuildType/               — Debug.cmake, Release.cmake
│   ├── Compiler/                — Clang.cmake, MSVC.cmake, GCC.cmake (SW_COMPILER_* 정의, `-Werror=switch` · `-Werror=unused-result` 등 경고 정책)
│   ├── Options/                 — CppStandard.cmake, Sanitizer.cmake, TsanSuppressions.txt(TSan 억제 — 비어 있는 것이 정상), UnityBuild.cmake
│   ├── Platform/                — Windows.cmake, Linux.cmake (SW_PLATFORM_* 정의, macOS 는 지원하지 않는다 — 코드는 컴파일러 내장 매크로 대신 이것을 묻는다)
│   ├── Toolchain/Vcpkg/         — vcpkg 에게 건네는 파일: triplet · 포트 툴체인 · 포트 컴파일 규칙
│   └── Toolchain/VcpkgTsan/     — TSan 구성(CI-Debug-TSAN) 전용 triplet: Jolt · Box2D 를 -fsanitize=thread 로 (기본 CI 캐시 키 밖에 두려고 폴더를 가른다)
│
└── Engine/                      [4계층: 엔진 빌드 파이프라인 및 타겟 헬퍼 (project() 이후)]
    ├── BuildLayout.cmake         — 산출물이 어디 놓이나: 출력 경로 · sw_global_options · IPO · 런타임 복사 큐
    │                               (include 되는 순간 실행된다 — TargetRules 보다 먼저여야 한다)
    ├── TargetRules.cmake         — 타겟을 어떻게 만드나: DLL export, RHI·키트·게임·테스트 팩토리, delay-load
    ├── ModuleManifest.cmake      — 모듈 매니페스트(`<모듈>.module.json`) 해석: 켜짐 · 플랫폼 · 구성 · 의존 · 순환, 꺼진 모듈은 짓지 않고 `Bin/Modules/` 에 복사
    ├── ThirdPartyLibs.cmake      — 서드파티를 어떻게 붙이나: SYSTEM include, vcpkg CONFIG, STATIC 폴백
    ├── AssetAndToolTargets.cmake— 에셋 쿠킹, Doxygen 문서, 린트 타겟 및 CTest 등록 헬퍼
    ├── ReflectionCodeGen.cmake  — ReflectionParser 코드 생성 파이프라인 (sw_addReflectionStep)
    ├── RuntimeDependencies.cmake— vcpkg 경로 조회, Vulkan 레이어·mimalloc 런타임 DLL 복사
    │                               (DXC 복사는 `ThirdParty/dxc/CMakeLists.txt` 의 `sw_copyDxcDlls` —
    │                                DXC 탐색 로직이 거기 있어 같이 둔다)
    └── RhiBackendSources.cmake  — RHI 백엔드 소스 파일 목록
```

## 네이밍 컨벤션

| 종류 | 규칙 | 예시 |
|------|------|------|
| function / macro | `sw_camelCase` | `sw_addRhiBackendModule`, `sw_addGameFrameworkKit`, `sw_registerLintTests` |
| 프로젝트 변수 · INTERFACE 타겟 | `sw_snake_case` | `sw_flag_libraries`, `sw_public_source_includes` |
| option / C++ 매크로 | `SW_UPPER_SNAKE_CASE` | `SW_ENABLE_PCH`, `SW_EXPORTS`, `SW_MODULE_EXPORTS` |
| 함수 내부 로컬 | `camelCase` (앞에 `_` 없음) | `kitType`, `libType`, `targetName` |

## 주요 헬퍼 함수

| 함수 | 용도 |
|------|------|
| `sw_configurePch` | `SW_ENABLE_PCH`가 ON일 때만 `target_precompile_headers`를 적용 (`BuildOptions.cmake`) |
| `sw_queueRuntimeCopy` / `sw_emitRuntimeCopies` | 런타임 DLL 복사를 모아 두었다가 타겟당 POST_BUILD 한 번으로 방출 (`BuildLayout.cmake`) |
| `sw_configureAppDependencies` | App 타겟의 RHI 모듈, SWGame 딜레이로드/정적링크, CookAssets 의존성 자동 구성 |
| `sw_addRhiBackendModule` | RHI 그래픽스 백엔드(`RHI_DX11` 등) MODULE 타겟 정의 및 공통 속성 바인딩 |
| `sw_registerDynamicModule` / `sw_getDynamicModules` | 동적 모듈 레지스트리. **모듈 이름을 적는 곳은 타겟을 만드는 자리 하나뿐이다** — App·EngineTest·SmokeTest 는 목록을 묻는다 (`KINDS rhi` 처럼 종류로 고른다) |
| `sw_excludeUnbuiltSources` / `sw_declareUnbuiltSources` | 이 구성이 **일부러 짓지 않는** 소스(배포의 에디터 · 핫 리로드 · 고르지 않은 RHI 백엔드)를 빼는 자리에서 적는다. 구성 끝에 `sw_writeUnbuiltSourceList` 가 빌드 트리(`generated/sw/config/UnbuiltSources.txt`)에 쓰고 `CheckSourceGlob` 이 읽는다 — 게이트가 빼기 규칙을 따로 들지 않는다. 다른 타겟으로 옮겨 짓는 것에는 쓰지 않는다 |
| `sw_resolveModuleManifests` / `sw_readModuleManifest` | 모듈 매니페스트를 모두 읽고 해석한다(`Source/**/<모듈>.module.json`, 고른 게임의 `SWGame.module.json` 이 켜기/끄기 표). 없는 의존 · 꺼진 의존 · 낮은 버전 · 순환 · 모르는 이름이면 구성이 선다. Dev 는 매니페스트와 적재 순서(`ResolvedModules.txt`)를 `Bin/Modules/` 에 두고 App 이 같은 규칙(`ModuleCatalog`)으로 다시 해석한다 |
| `sw_isModuleActive` / `sw_skipInactiveModule` | 모듈이 켜져 있나 · 꺼졌으면 그 폴더를 "짓지 않는다" 로 적고 건너뛴다(모듈을 만드는 함수의 첫 줄). 매니페스트가 없는 동적 모듈은 `sw_registerDynamicModule` 에서 구성이 선다 |
| `sw_excludeSourcesOfInactiveKits` | 꺼진 키트의 헤더를 include 하는 소스를 목록에서 뺀다(EngineTest — 키트를 끄면 그 시험도 짓지 않는다) |
| `sw_addGameFrameworkKit` | GameFramework 장르 키트(`GF_Overworld` 등) 라이브러리 정의 및 리플렉션/딜레이로드 자동화 |
| `sw_registerLintTests` | 린트 CTest 일괄 등록. **목록은 여기 없다** — `Scripts/lint/gate/` · `selftest/` 폴더가 목록이고, `GenerateLintTargets.py` 가 만든 `LintTargets.cmake` 를 부른다 |
| `sw_addReflectionStep` | ReflectionParser 코드 생성 스텝 자동 연결 |
