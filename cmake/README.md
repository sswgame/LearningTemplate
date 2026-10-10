# cmake/ (빌드 시스템)

CMake는 빌드 그래프·플래그·의존성만 담당합니다. 컴파일러/Ninja/vcpkg 탐색은 `Scripts/setup/`에 위임합니다.

## 디렉터리 구조 및 역할 (4-Layer Architecture)

```
cmake/
├── Config/                      [1계층: 전역 설정 및 프로젝트 옵션]
│   ├── BuildOptions.cmake       — SW_* 빌드 기능 옵션 및 프로젝트 메타데이터 정의
│   ├── LoadConfigConstants.cmake — Constants.py 의 상수를 SW_* 변수로 (상수만 필요한 곳은 이것만 include)
│   ├── GenerateConfigConstants.cmake — configure 생성기 다섯을 한 프로세스로(GenerateConfigureFiles.py) · 상수 읽기 · 게임 프리셋 경로 · ConfigConstants.h (한 번)
│   └── ConfigConstants.h.in     — C++ 헤더 템플릿
│
├── Environment/                 [2계층: 개발 환경 및 툴체인 주입 (project() 이전)]
│   ├── DetectToolchain.cmake    — toolchain_config.json 파싱 & LLVM/Ninja 바인딩
│   ├── FindLlvmBin.cmake        — clang-cl / clang 이 있는 LLVM bin 찾기 (vcpkg 포트 툴체인도 쓴다)
│   ├── ToolchainBinaries.cmake  — 아카이버를 "지금 쓰는 컴파일러 옆" 에서 고정 (LTO 비트코드를 읽어야 한다)
│   ├── FindWindowsTools.cmake   — lib.exe / mt.exe 탐색 및 clang-cl 아카이버 재바인딩
│   ├── WindowsToolSearch.cmake  — MSVC lib.exe · SDK mt.exe 폴더 탐색 (본 프로젝트와 vcpkg 포트 툴체인이 함께 쓴다)
│   ├── HostPath.cmake           — PATH 앞에 붙이기(sw_prependEnvPath) · Git for Windows 기본 경로
│   ├── FindLibclang.cmake       — ReflectionParser 가 링크하는 libclang 찾기(환경 변수 → toolchain → 배포판 LLVM, sw_findLibclang)
│   └── PythonUtils.cmake        — Python 인터프리터 탐색(한 곳) 및 스크립트 실행 헬퍼
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
    ├── BuildLayout.cmake         — 산출물이 어디 놓이나: 출력 경로 · sw_global_options · IPO · 런타임 복사 큐,
    │                               `Bin` 의 옛 자리 · 꺼진 모듈 산출물 지우기(sw_removeStaleBinaryOutputs)
    │                               (include 되는 순간 실행된다 — 타깃 규칙 파일보다 먼저여야 한다. 아래 파일은 함수만 정의한다)
    ├── UnbuiltSources.cmake      — 이 구성이 일부러 짓지 않는 소스 목록(sw_declare* · sw_exclude* · 플랫폼 폴더 규칙 → CheckSourceGlob)
    ├── TargetCompileRules.cmake  — 타깃 하나의 컴파일 규칙: 내보내기 매크로 짝 · PCH · 결정성 TU(sw_markDeterministicSources)
    ├── DelayLoad.cmake           — Windows 지연 로드를 정하는 유일한 자리(sw_addDelayloadHook · sw_addDelayloadSystemDlls — CheckDelayLoadSites)
    ├── EngineAbiStamp.cmake      — 핫 리로드 ABI 도장(Core · Engine 헤더 지문을 Engine 과 Dev 모듈이 같이 박는다)
    ├── ModuleRegistry.cmake      — 동적 모듈 레지스트리: 등록(ABI 도장 · 서버 전용 표식) · 종류별 조회 · 빌드 순서 · 빠진 등록 대조
    ├── ModuleTargets.cmake       — 모듈 라이브러리 팩토리(sw_addModuleLibrary)와 RHI 백엔드 · 키트 · 게임 모듈, 모듈 출력 폴더 · 서드파티 DLL 모으기
    ├── ExecutableTargets.cmake   — 실행 파일의 링크 · 배치: 통째 링크(sw_linkWholeArchive) · App/Server 의존 · 프로세스 매니페스트 · 리눅스 Shipping 심볼 분리
    ├── StageModuleRuntimeDlls.cmake — `cmake -P` 스크립트: 모듈의 서드파티 DLL 을 모듈마다 스테이지에서 applocal 로 모아 `Bin` 에 한 벌(sw_stageModuleRuntimeDlls)
    ├── TestTargets.cmake         — 시험 타깃 · CTest 등록(실행 파일 · 샤드 · 새니타이저 보정 · 스크립트 시험)
    ├── ModuleManifest.cmake      — 모듈 매니페스트(`<모듈>.module.json`) 해석: 켜짐 · 플랫폼 · 구성 · 의존 · 순환 · 적재 순서, Dev 는 `Bin/Modules/` 에 복사
    │                               (`cmake -P` 로도 include 된다 — PythonTest_TestKitBuildOrder)
    ├── ModuleActivation.cmake    — 해석 결과를 묻는 쪽: 꺼진 모듈 건너뛰기(sw_skipInactiveModule) · 꺼진 키트를 쓰는 시험 소스 빼기
    ├── ThirdPartyLibs.cmake      — 서드파티를 어떻게 붙이나: SYSTEM include, vcpkg CONFIG 패키지(못 찾으면 구성 실패), 헤더 전용 포트, Engine 압축 코덱
    ├── AssetAndToolTargets.cmake— 에셋 쿠킹, Doxygen 문서, 린트 타겟 및 CTest 등록 헬퍼
    ├── ReflectionCodeGen.cmake  — ReflectionParser 코드 생성 파이프라인 (sw_addReflectionStep)
    ├── RuntimeDependencies.cmake— vcpkg 경로 조회, Vulkan 레이어·mimalloc 런타임 DLL 복사
    │                               (DXC 복사는 `ThirdParty/dxc/CMakeLists.txt` 의 `sw_copyDxcDlls` —
    │                                DXC 탐색 로직이 거기 있어 같이 둔다)
    └── RhiBackends.cmake        — RHI 백엔드 표의 CMake 쪽(장치 소스 · 이름 · 별칭 · 배포 백엔드 확인 — 표는 CookContract.json)
```

## 네이밍 컨벤션

| 종류 | 규칙 | 예시 |
|------|------|------|
| function / macro | `sw_camelCase` | `sw_addRhiBackendModule`, `sw_addGameFrameworkKit`, `sw_registerScriptTest` |
| 프로젝트 변수 · INTERFACE 타겟 | `sw_snake_case` | `sw_flag_libraries`, `sw_public_source_includes` |
| option / C++ 매크로 | `SW_UPPER_SNAKE_CASE` | `SW_ENABLE_PCH`, `SW_EXPORTS`, `SW_MODULE_EXPORTS` |
| 함수 내부 로컬 | `camelCase` (앞에 `_` 없음) | `kitType`, `libType`, `targetName` |

## 주요 헬퍼 함수

| 함수 | 용도 |
|------|------|
| `sw_configurePch` | `SW_ENABLE_PCH`가 ON일 때만 `target_precompile_headers`를 적용 (`TargetCompileRules.cmake`) |
| `sw_configureDllExports` | 내보내기 매크로 짝(ENGINE · GF · MODULE) |
| `sw_prependEnvPath` | configure 프로세스의 PATH 앞에 폴더를 붙인다(호스트 구분자, 이미 있으면 그대로 — `HostPath.cmake`) |
| `sw_queueRuntimeCopy` / `sw_emitRuntimeCopies` | 런타임 DLL 복사를 모아 두었다가 타겟당 POST_BUILD 한 번으로 방출 (`BuildLayout.cmake`) |
| `sw_deployRuntimeDependencies` | 실행 파일 옆 런타임 DLL(DXC · Debug 검증 레이어 · Tracy)을 구성에 맞게 골라 복사 (`BuildLayout.cmake`) |
| `sw_addDynamicModuleDependencies` | 레지스트리의 동적 모듈(종류로 고름)이 그 타깃보다 먼저 지어지게 한다 — App · 시험이 이름을 적지 않는다 |
| `sw_configureAppDependencies` | App 타겟의 RHI 모듈, SWGame 딜레이로드/정적링크, CookAssets 의존성 자동 구성 |
| `sw_addRhiBackendModule` | RHI 그래픽스 백엔드(`RHI_DX11` 등) MODULE 타겟 정의 및 공통 속성 바인딩 |
| `sw_registerDynamicModule` / `sw_getDynamicModules` | 동적 모듈 레지스트리. **모듈 이름을 적는 곳은 타겟을 만드는 자리 하나뿐이다** — App·EngineTest·SmokeTest 는 목록을 묻는다 (`KINDS rhi` 처럼 종류로 고른다) |
| `sw_excludeUnbuiltSources` / `sw_declareUnbuiltSources` | 이 구성이 **일부러 짓지 않는** 소스(배포의 에디터 · 핫 리로드 · 고르지 않은 RHI 백엔드)를 빼는 자리에서 적는다. 구성 끝에 `sw_writeUnbuiltSourceList` 가 빌드 트리(`generated/sw/config/UnbuiltSources.txt`)에 쓰고 `CheckSourceGlob` 이 읽는다 — 게이트가 빼기 규칙을 따로 들지 않는다. 다른 타겟으로 옮겨 짓는 것에는 쓰지 않는다 |
| `sw_resolveModuleManifests` / `sw_readModuleManifest` | 모듈 매니페스트를 모두 읽고 해석한다(`Source/**/<모듈>.module.json`, 고른 게임의 `SWGame.module.json` 이 켜기/끄기 표). 없는 의존 · 꺼진 의존 · 낮은 버전 · 순환 · 모르는 이름이면 구성이 선다. Dev 는 매니페스트와 적재 순서(`ResolvedModules.txt`)를 `Bin/Modules/` 에 두고 App 이 같은 규칙(`ModuleCatalog`)으로 다시 해석한다 |
| `sw_isModuleActive` / `sw_skipInactiveModule` | 모듈이 켜져 있나 · 꺼졌으면 그 폴더를 "짓지 않는다" 로 적고 건너뛴다(모듈을 만드는 함수의 첫 줄). 매니페스트가 없는 동적 모듈은 `sw_registerDynamicModule` 에서 구성이 선다 |
| `sw_filterPlatformSources` | 플랫폼 폴더 규칙(`Windows/` · `Linux/` · `Posix/`)으로 소스 목록을 거르고 고르지 않은 것을 "짓지 않는 소스" 로 적는다(Core) |
| `sw_excludeSourcesOfInactiveKits` | 꺼진 키트의 헤더를 include 하는 소스를 목록에서 뺀다(EngineTest — 키트를 끄면 그 시험도 짓지 않는다) |
| `sw_addModuleLibrary` | 엔진 모듈 라이브러리의 기본값(종류 · Bin 출력 · 내보내기 · 등록 · PCH · 유니티 · 리플렉션 · 지연 로드) 한 자리 — 팩토리 셋 · GameFramework · 에디터가 이것을 부른다 |
| `sw_addGameFrameworkKit` | GameFramework 장르 키트(`GF_Overworld` 등) 라이브러리 정의 및 리플렉션/딜레이로드 자동화 |
| `sw_registerScriptTest` | 파이썬 스크립트 하나를 CTest 항목 하나로(PythonTest · QA · 린트가 같은 속성 철자). 린트는 **목록이 여기 없다** — `Scripts/lint/gate/` · `selftest/` 폴더가 목록이고, `GenerateLintTargets.py` 가 만든 `LintTargets.cmake` 의 등록 함수가 이것을 부른다 |
| `sw_addReflectionStep` | ReflectionParser 코드 생성 스텝 자동 연결 |
| `sw_addTestExecutable` | 시험 실행 파일 하나 — 소스는 그 폴더를 훑고(`SOURCES` 를 주면 그것), 같이 컴파일할 엔진 밖 소스는 `EXTRA_SOURCES` (`TestTargets.cmake`) |
| `sw_findEngineCompressionLibraries` | Engine 의 압축 코덱(zlib · LZ4 · Zstd) 링크 항목 — 찾기 · 진단을 한 자리에 (`ThirdPartyLibs.cmake`) |

## 고친 뒤 구성이 같은지

CMake 리팩터는 컴파일러가 잡지 않는다 — 정의 하나 · 링크 순서 · 출력 폴더가 달라져도 configure 는 통과한다. 고치기 전과 뒤의 구성 결과를 견준다
(`Scripts/dev/ConfigureSnapshot.py` — File API 답 · 생성 파일 해시 · ctest 목록):

```powershell
py -3 Scripts/dev/ConfigureSnapshot.py prepare --preset Ninja-Debug             # 질의를 둔다(한 번)
cmake --preset Ninja-Debug ; py -3 Scripts/dev/ConfigureSnapshot.py take --preset Ninja-Debug --out before.json
# ... CMake 를 고친다 ...
cmake --preset Ninja-Debug ; py -3 Scripts/dev/ConfigureSnapshot.py take --preset Ninja-Debug --out after.json
py -3 Scripts/dev/ConfigureSnapshot.py diff before.json after.json              # 다르면 1 · 다른 줄만 찍는다
```

Dev 와 Shipping · Server 는 다른 갈래를 탄다 — 고친 갈래의 프리셋마다 뜬다. 구성 시간은 `cmake --preset … --profiling-format=google-trace --profiling-output=t.json`
뒤 `ConfigureSnapshot.py profile t.json --top 25`.
죽은 CMake 함수 · common 을 비켜 간 파이썬 호출은 `RunBuildScriptInventory.py`(보고서)가 센다.

## 함정 · 계약

- **Dev 산출물 자리**: 모듈 DLL(GF_* · RHI_* · EditorModule · SWGame)은 `Bin/Modules`, Engine · GameFramework · 서드파티 DLL 은 `Bin`, Windows PDB 는 `Bin/Symbols`,
  시험 실행 파일은 `TestBin` 이다. 런타임은 `FileUtil::getBinaryDirectory`(TestBin → Bin) · `ModuleImageUtil::findModuleLibraryPath`(Modules → Bin)로 찾는다.
  시험이 키트를 링크하면 그 DLL 을 TestBin 에 복사한다 — 키트는 자료를 내보내 lld 가 지연 로드를 거절한다(`cannot delay-load ... due to import of data`).
  옛 자리 산출물과 꺼진 모듈의 DLL 은 configure 가 지운다(`sw_removeStaleBinaryOutputs`, 잠긴 파일은 경고).
- **서드파티 DLL 은 `Bin` 에 한 벌이다.** vcpkg 의 `add_library` 래퍼는 타깃을 만드는 순간 `VCPKG_APPLOCAL_DEPS` 를 읽어 의존 DLL 을 타깃 **옆에** 복사하는 POST_BUILD 를 단다.
  `Bin/Modules` 에 가는 모듈은 `sw_addModuleLibrary` 가 그 순간만 끄고, `sw_stageModuleRuntimeDlls` 가 모듈마다 `build/<프리셋>/ModuleRuntimeStage/<모듈>` 에 하드 링크를 두고
  `vcpkg z-applocal` 을 돌려 복사된 것만 `Bin` 으로 옮긴다(병렬 링크가 같은 폴더를 다투지 않는다). 모듈 로드(`LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | DEFAULT_DIRS`)는 모듈 폴더를 먼저 보므로
  `Bin/Modules` 에 사본이 남으면 그것이 쓰인다 — configure 가 `Bin/Modules` 의 모듈 이름 꼴이 아닌 DLL · PDB 와 남은 섀도 복사본(`_temp_`)을 지운다.
- **vcpkg 설치 폴더 · 스탬프는 워크트리 모두가 나눠 쓴다**(`build/vcpkg_installed` junction). 해시만 보고 설치하던 때는 옛 vcpkg.json 의 워크트리가
  configure(빌드 중 GLOB 로 도는 재구성 포함)하면 다른 워크트리가 쓰는 포트를 지웠다(recast · tracy). 지금 게이트(`Vcpkg.cmake` 5 절)는 스탬프가 다르면
  `vcpkg install --dry-run` 계획을 보고 — 지을 것이 없으면 skip, 지우게 되면 멈추고 경고(스탬프도 덮지 않는다), 빠진 포트가 있을 때만 install. 재현:
  `git show <옛 커밋>:vcpkg.json > vcpkg.json` → `cmake --preset Ninja-Debug` → "Install skipped … would remove" 경고 · 포트가 남는지 · `VCPKG_MANIFEST_INSTALL=OFF`
  → vcpkg.json 되돌림. 스탬프가 다를 때만 dry-run 이 돈다(약 20 초).
- **ASan (Windows)** — SmokeTest 만 `report_globals=0`(DLL 을 내려도 전역 등록이 안 지워진다), `detect_odr_violation=0`(1 이면 1800 s+), `/MD` 강제, ASan 런타임 DLL 은
  `Bin` 과 `BuildTools` 양쪽(`cmake/Modules/Options/Sanitizer.cmake`). Windows 의 memcpy 는 겹쳐도 맞게 옮겨 겹친 복사 버그가 안 보인다 — 리눅스 ASan 이 잡는다.
- **TSan** 은 `SW_SANITIZER_KIND=thread` · `CI-Debug-TSAN`(GNU/Clang 전용, ASan 과 동시 불가). 크래시 자식 시험은 ASan · TSan 에서 건너뛴다. 새 CI 검사는 매트릭스에
  `reportOnly: true` 로 들여 보고를 추린 뒤 막는 잡으로 바꾼다.
  원자 연산으로 스스로 동기화하는 서드파티(Jolt · Box2D)는 트리플릿 `x64-linux-tsan`(`cmake/Modules/Toolchain/VcpkgTsan/`)이 계측해 짓는다 — 계측 안 된 정적 라이브러리는 동기화가
  안 보이는데 헤더 인라인 함수는 링커가 우리 TU 의 계측된 사본을 골라 거짓 경쟁 수백 건이 났다. 트리플릿 파일은 ABI 해시에 들어 고치면 포트를 다 다시 짓고(WSL 약 35 분),
  기본 CI 캐시 키가 보는 `Toolchain/Vcpkg/**` 밖에 둔다. 포트 컴파일러는 프리셋의 `CC=clang` 이 정한다(빠지면 vcpkg 가 GCC 로 짓는다).
- **유니티 빌드는 `CI-*` 프리셋에만 켜져 있다.** `Ninja-*` 가 초록이어도 익명 네임스페이스 충돌이 없다는 뜻이 아니다 — 헬퍼 · 상수는 `XxxInternal` 구조체로 감싼다.
  유니티 제외 목록(옛 skipUnitySources 함수)은 다시 만들지 않는다 — 부딪치는 이름을 고친다.
- **LTO 함정** — clang `-flto` obj 는 MSVC `lib.exe` 가 못 읽는다(LNK1107). 아카이버는 "지금 컴파일러 옆" 을 먼저 본다(리눅스 `/usr/bin` 에는 llvm-ar 이 없어 LTO 가 조용히 꺼진다).
  CMake 는 IPO 아카이브 명령을 `project()` 때 정해 두고, `check_ipo_supported` 는 거짓 NO 를 내서 직접 판정한다(`cmake/Environment/ToolchainBinaries.cmake`). `SW_ENABLE_LTO` 하나가 Release · Shipping.
- **configure 의 PATH 는 `sw_prependEnvPath` 로만 고친다**(`cmake/Environment/HostPath.cmake` — 호스트 구분자, 이미 있으면 그대로). project() 전에는
  호스트 판정 `CMAKE_HOST_WIN32` 로 고른다(이 PC 의 --fresh 구성에서는 `WIN32` 도 그때 이미 1 이었다 — 문서가 보장하는 것은 호스트 변수다).
- **빌드 스크립트의 자리**: 모듈 라이브러리는 `sw_addModuleLibrary` 하나(`cmake/Engine/ModuleTargets.cmake` — 팩토리 밖 SHARED/MODULE 은
  `CheckCmakeConventions` 가 막는다), 지연 로드는 `DelayLoad.cmake` 하나(`CheckDelayLoadSites` 가 그 파일 이름을 든다 — 옮기면 게이트도 고친다), 키트는 매니페스트 의존 위상 순서(`PythonTest_TestKitBuildOrder`), RHI 백엔드의 빌드 칸은 `CookContract.json` 의 `rhi_backends`,
  vcpkg 라이브러리는 `sw_addVcpkgPackage`(REQUIRED) · `sw_addVcpkgHeaderOnly`. 파이썬은 프로세스 `runProcess` · 빌드 폴더 `BuildTree` · 생성 파일 `writeGeneratedFile` ·
  보고서 `LintReport` 가 한 자리이고 `CheckScriptCommonHelpers` 가 비켜 가는 호출을, `CheckScriptLayout` 이 폴더 → 이름 앞머리 → 기반 클래스 표를 지킨다.
- **configure 의 파이썬은 `GenerateConfigureFiles.py` 한 프로세스**(생성기 다섯) — 새 configure 생성기는 각자 `sw_executePythonScript` 를 더하지 말고 거기에 한 줄.
  재는 법: `cmake --preset <p> --profiling-format=google-trace --profiling-output=t.json` → `ConfigureSnapshot.py profile t.json`(CMake 는 B/E 짝으로 적는다).
  리팩터 전후 configure 동일성은 `ConfigureSnapshot.py take/diff`(실행 파일 시험의 command 는 그 exe 가 지어졌는지에 따라 비거나 찬다 — 차이로 읽지 말 것).
- **서드파티 격리의 링크 주인은 규칙마다다**(`CheckThirdPartyIsolation` 의 `cmakeLinkOwner`) — 엔진 백엔드는 `Source/Engine/CMakeLists.txt`, 키트 안 드라이버(SQLite)는 그 키트의 CMakeLists 가 링크한다. 어느 드라이버가 실행 파일에 드는지는 빌드 타깃(`_listTarget`)이 정한다 — 드라이버 목록 옵션을 따로 두지 않는다.
- **빌드 타깃별 제외 에셋은 쿠킹 표 한 줄이다**(`Config/Engine/CookContract.json` 의 `asset_kinds` · `target_excluded_asset_kinds`). 쿠커(`--build-target`)가 팩에서 빼고,
  같은 호스트의 런타임(`ResourceUtil::setHostTarget`)은 그 종류를 읽지 않는다 — 한쪽만 고치면 서버가 "파일 없음" 을 쏟거나 팩에 쓸모없는 바이트가 남는다.
  종류를 더하면 `CheckCookContract` · `TestServerPackExclusion` · `ResourceHostTargetTest` 가 같이 본다.
- **서드파티 고지는 빌드가 만든다** — `ThirdPartyNotices` 타깃(`Scripts/generate/GenerateThirdPartyNotices.py`)이 `Bin/THIRD_PARTY_NOTICES.txt` 에
  매니페스트가 끌어오는 vcpkg 포트의 `share/<포트>/copyright` 를 모은다(설치 트리를 워크트리끼리 나눠 써 트리 전체가 아니라 `vcpkg/status` 의 의존 닫힘).
  vcpkg 밖에서 들인 코드(저장소에 복사한 헤더 등)는 여기 저절로 들어가지 않는다 — 그런 것을 들이면 그 고지를 같이 넣는다.
- **스크립트 시험은 `sw_registerScriptTest`** — 파이썬 단위 시험 · QA · 린트가 같은 속성 철자. 시험 실행 파일 폴더는 `Test/` 아래 CMakeLists 가 있으면 저절로 들어간다.
- **enum switch 는 LLVM 방식**: 모든 열거자를 다루면 `default:` 없음(`-Werror=switch` 가 빠진 case 를, `-Werror=covered-switch-default` 가 다 다룬 switch 의
  default 를 잡는다), 일부만 다루면 `default:`(`-Wno-switch-enum` · `-Wno-switch-default`). 파일별 `#pragma` 로 switch 경고를 바꾸지 않는다. 외부 헤더는 `SYSTEM`
  include 여야 이 규칙이 그 안에 걸리지 않는다(ReflectionParser 의 nlohmann-json 이 일반 `-I` 였다).
- **RHI 백엔드 표(이름 · 별칭 · 셰이더 폴더 · 포맷)와 쿡 접미사 표의 정본은 `Config/Engine/CookContract.json`** — `GenerateCookContract.py` 가 C++ X-macro
  (`sw/config/CookContract.gen.h`)를 만들고 Python 은 `Scripts/common/CookContract.py` 로 읽는다. `CheckCookContract` 게이트가 쿠커 함수를 표와 대조한다.
  두 언어에 목록을 따로 적지 말 것(`PackFormat.json` 과 같은 모양). 백엔드의 빌드 칸(모듈 · 장치 소스 폴더 · 그래픽 라이브러리 · 배포 매크로)도 여기 —
  CMake 는 `generated/sw/config/CookContract.cmake` 로 읽는다(`cmake/Engine/RhiBackends.cmake`). `SW_SHIPPING_RHI_BACKEND` 는 표의 이름만 받고(별칭 · 소문자는 구성 실패 —
  옛 빌드 폴더는 캐시 값을 고칠 것), 그 플랫폼에 없는 백엔드면 구성이 선다(`PythonTest_TestRhiBackendTable`).
- **소스 손 목록은 `Test/EditorTest/CMakeLists.txt` 하나다** — Editor 소스 중 ImGui 없는 것을 고른다(`CheckTestSuites`). Core 는 폴더 GLOB + 플랫폼 폴더 규칙
  (`sw_filterPlatformSources` — `Windows/` · `Linux/` · `Posix/`), RHI 백엔드 장치 소스는 백엔드 폴더 GLOB 이다(`sw_getRhiBackendSources`). `CheckSourceGlob` 이 디스크와 대조한다. 구성이 일부러 짓지 않는 소스는
  `sw_excludeUnbuiltSources` · `sw_declareUnbuiltSources` 로 적는다(`<빌드>/generated/sw/config/UnbuiltSources.txt`). 파일을 옮기면 경로를 문자열로 적은 곳은 컴파일러가 안 잡는다.
- **모듈 라이브러리는 `sw_addModuleLibrary` 로 만든다**(기본값 한 자리 — 팩토리 셋 · GameFramework · 에디터).
  **동적 모듈은 타깃을 만드는 자리에서** `sw_registerDynamicModule( <타깃> rhi|kit|game|gameframework|editor )` 로 등록한다. `sw_verifyDynamicModuleRegistry` 가 루트부터 훑어
  미등록 MODULE 이면 FATAL_ERROR.
- **생성 상수는 `Scripts/common/Constants.py` 의 `k*` 전부를 기계 변환한다**(`kDirSourceEngine` → `SW_DIR_SOURCE_ENGINE`). 경로 조립은 소비 쪽(`ConfigConstants.h.in`)이 한다.
  `configure_file` 은 빈 값을 조용히 넣는다. `toolchain_config.json` 은 CMake 가 파싱하지 않는다(`GenerateToolchainCMake.py` → `SW_TOOLCHAIN_<KEY>`) — 예외는 vcpkg 가 부르는
  `FindLlvmBin.cmake` · `VcpkgPortsToolchain.cmake` 둘이고, 거기서 `CMAKE_SOURCE_DIR` 은 vcpkg scripts 폴더다. 상수만 필요한 CMake 는 `LoadConfigConstants.cmake` 를 **파일
  스코프에서** include 한다(함수 안에서 처음 include 하면 상수가 그 함수에만 생긴다).
- **`sw_addVcpkgPackage` 는 못 찾으면 구성을 세운다(REQUIRED)** — 설정 파일이 없는 헤더 전용 포트는 `sw_addVcpkgHeaderOnly`(PROBE 헤더 확인). 서드파티 폴더 목록은 없다(폴더 훑기).
  OBJECT 라이브러리(`Core_objects`)는 링크를 전파하지 않는다 — 외부 라이브러리는 `Core` 와 `Engine` 양쪽에. vcpkg 업스트림 결함은 `ThirdParty/<pkg>/vcpkg-port/` 오버레이로
  (`.gitattributes` 의 `*.patch -text`).
- **Shipping 정적 링크는 열거형만 든 `.gen.cpp` 를 버린다**(증상: `findEnum` 이 null, enum 2 개) — `sw_linkWholeArchive` 가 링커별 whole-archive 를 고른다.
- **PCH 안 헤더의 include 하나가 TU 수를 정한다** — `RHITypes.h`(PCH)가 쓰지도 않는 `RHIBackendType.h` 를 들어 1987 TU 가 그 헤더에 의존했다(끊은 뒤 약 474).
  끊을 때 직접 include 를 받는 것은 그 이름의 **값**(`RHIBackend::DirectX12`)을 쓰는 파일뿐이다 — 타입 이름만 쓰는 헤더는 불투명 선언(`enum class RHIBackend : uint32;`)으로
  선다. 거쳐 받던 파일은 글자 include 그래프에서 그 간선을 끊어 후보를 좁힌 뒤 `-fsyntax-only`(PCH 없이)로 확인한다.
- **D3D · DXGI · D3DCompiler · MF · XAudio2 헤더는 PCH 에 넣지 않는다**(`EngineMinimal.h` 는 OS 헤더만, 쓰는 파일이 `EnginePlatformHeaders.h` 를 직접) — 넣으면
  1,654 TU 가 `d3d12.h` 를 파싱했다(뺀 뒤 약 43). `#if defined( SW_HAS_DXC_API )` 처럼 정의 여부로 읽는 매크로는 정의 헤더가 빠지면 **조용히** 꺼진다(시험은 "컴파일러
  없음" 으로 건너뛴다) — `ShaderCompiler.cpp` 의 Windows `#error` 가 막는다.
- **Shipping 통째 링크는 "링크한 라이브러리" 기준이어야 한다, "적은 `LIBS`" 기준이 아니다** — `CoreTest` 는 Engine 을 `TestFramework` 로만 받아 Shipping 에서 등록기가
  빠졌고, 하네스 기동의 `[Error] Failed to deserialize config` 한 줄만 남긴 채 모든 시험이 기본값 설정으로 돌았다(종료 코드 0). 배포 구성의 하네스는 생성 JSON 이
  역직렬화되지 않으면 기동을 실패시킨다.
- **Release · Shipping 도 서명 · 심볼을 만든다**(`SW_RELEASE_DEBUG_INFO`, 기본 `lines` = `-gline-tables-only`, `full` = `/Z7`) — 링크 `/DEBUG:FULL` + `/OPT:REF` · `/OPT:ICF`
  (`/DEBUG` 가 끄므로 다시) · `/PDBALTPATH:%_PDB%`, 리눅스 `--build-id=sha1`. Shipping PDB 는 `Symbols/`(배포 폴더 밖, 시험 실행 파일 PDB 는 `TestBin`), 저장소 배치는
  `py -3 -m Scripts symbols`. PDB 이름만 적으므로 크래시 스택(DbgHelp)은 실행 파일 폴더를 검색 경로에 더한다(`WindowsCallStackCapture`). 측정(2026-10-06,
  Windows Shipping 전체 빌드, 기계 공유 중): 오브젝트 합 146 → 262 MB, `Bin` 29.6 MB 그대로(App.exe 크기 같음), `TestBin` 81 → 257 MB(시험 PDB), `Symbols` 31.5 MB,
  링크 App 26.7 → 30.5 s · EngineTest 68.6 → 79.9 s, 전체 빌드 벽시계는 기계 부하에 묻혀 차이 없음(7.6 · 6.8 분).
- **유니티 묶음에서 서드파티 매크로 정리는 "내가 정의한 것만" 지운다** — `AudioVorbisDecode.cpp` 가 stb_vorbis 뒤에 `#undef TRUE` · `FALSE` 를 하자 Windows
  `windows.h` 의 것이 지워져, 같은 묶음 뒤의 `XAudio2System.cpp`(d3d11.h)가 PCH 끈 빌드(CI Windows)에서 깨졌다. PCH 가 있으면 d3d 헤더가 먼저 들어와 가려진다.
- **구성을 넘어 같은 비트가 필요한 TU 는 `sw_markDeterministicSources`** — 부동소수점 축약(FMA 합치기)을 끄고 유니티 묶음에서 뺀다. Release 의
  `/arch:AVX2` 가 `a * b + c` 를 FMA 로 합쳐 파괴 해시가 Debug 와 갈렸다(파쇄 결과부터). 대상은 Engine `Destruction/` 의 시뮬레이션 TU 여덟 ·
  `GF_NetDestruction` · Core `Math/VectorMath.cpp`(거리 · 길이가 줄 밖 함수라 그쪽도 — 빼면 사건 넷째의 반경 피해에서 다시 갈린다). 새 결정성 경로는
  이 함수에 파일을 더하고 기준값 시험을 붙인다.
- **`TestBin` 의 서드파티 DLL 은 첫 빌드와 다시 빌드가 다르다** — vcpkg applocal(실행 파일 POST_BUILD 첫 단계)이 옆의 키트 DLL import 까지 따라가는데, 키트 DLL 은 그 뒤
  단계가 복사한다. 깨끗한 폴더의 첫 빌드에는 `sqlite3` · `libpq` · `libssl` · `libcrypto` 가 없고 다시 지으면 생긴다(시험은 작업 폴더 `Bin` 에서 찾아 어느 쪽이든 돈다).
  산출물 목록을 견줄 때는 같은 단계(둘 다 다시 빌드)끼리 견준다.
- **`NOMINMAX` · `WIN32_LEAN_AND_MEAN` 은 `Platform/Windows.cmake` 의 컴파일 정의다** — 헤더 정의(`PlatformOsHeaders.h`)는 그보다 먼저 `windows.h` 를 읽은 TU 에 닿지 않는다.
  그 헤더에서 OS 헤더를 빼는 일(백로그 1-9)은 이 정의에 기댄다. `sw_global_options` 를 링크하지 않는 타깃은 이 정의도 없다.
- **`cmake -P` 스크립트는 `cmake_minimum_required` 를 먼저 둔다** — 없으면 정책이 OLD 라 CMake 3.x(CI 러너)에서 `IN_LIST` 가 "Unknown arguments" 다.
  CMake 4.x(이 PC)는 3.5 이전 정책의 OLD 를 지워 늘 통과하므로 로컬에서는 안 보인다. `-P` 로 include 되는 모듈(`ModuleManifest` · `RhiBackends`)은 그것이 없으면 구성을 세운다.
