# cmake/ (빌드 시스템)

CMake는 빌드 그래프·플래그·의존성만 담당합니다. 컴파일러/Ninja/vcpkg 탐색은 `Scripts/setup/`에 위임합니다.

## 디렉터리 구조 및 역할 (4-Layer Architecture)

```
cmake/
├── Config/                      [1계층: 전역 설정 및 프로젝트 옵션]
│   ├── BuildOptions.cmake       — SW_* 빌드 기능 옵션 및 프로젝트 메타데이터
│   ├── LoadConfigConstants.cmake — Constants.py 의 상수를 SW_* 변수로 (상수만 필요한 곳은 이것만 include)
│   ├── JsonValue.cmake          — JSON 값 읽기 둘뿐: 필수 sw_requireJsonValue · 선택 sw_readOptionalJsonValue
│   ├── GenerateConfigConstants.cmake — configure 생성기를 한 프로세스로(GenerateConfigureFiles.py) · ConfigConstants.h
│   └── ConfigConstants.h.in     — C++ 헤더 템플릿
│
├── Environment/                 [2계층: 개발 환경 및 툴체인 주입 (project() 이전)]
│   ├── DetectToolchain.cmake    — toolchain_config.json 파싱 & LLVM/Ninja 바인딩
│   ├── FindLlvmBin.cmake        — vcpkg 포트 툴체인의 LLVM bin
│   ├── ToolchainBinaries.cmake  — 아카이버를 "지금 쓰는 컴파일러 옆" 에서 고정 (LTO 비트코드를 읽어야 한다)
│   ├── FindWindowsTools.cmake   — lib.exe / mt.exe 탐색 및 clang-cl 아카이버 재바인딩
│   ├── WindowsToolSearch.cmake  — MSVC lib.exe · SDK mt.exe 폴더 탐색
│   ├── HostPath.cmake           — PATH 앞에 붙이기(sw_prependEnvPath) · Git for Windows 기본 경로
│   ├── FindLibclang.cmake       — ReflectionParser 가 링크하는 libclang 찾기(sw_findLibclang)
│   └── PythonUtils.cmake        — Python 인터프리터 탐색 및 스크립트 실행 헬퍼
│
├── Modules/                     [3계층: 컴파일러/플랫폼/아키텍처 INTERFACE 플래그]
│   ├── LoadCompileFlags.cmake   — 플래그 모듈 일괄 인클루더
│   ├── Architecture/ · BuildType/ · Compiler/ · Options/(CppStandard · Sanitizer · UnityBuild) · Platform/(SW_PLATFORM_* 정의 — macOS 는 지원하지 않는다)
│   ├── Toolchain/Vcpkg/         — vcpkg 에게 건네는 triplet · 포트 툴체인 · 포트 컴파일 규칙
│   └── Toolchain/VcpkgTsan/     — TSan 구성(CI-Debug-TSAN) 전용 triplet
│
└── Engine/                      [4계층: 엔진 빌드 파이프라인 및 타겟 헬퍼 (project() 이후)]
    ├── BuildLayout.cmake         — 출력 경로 · sw_global_options · IPO · 런타임 복사 큐 · 꺼진 모듈 산출물 지우기
    │                               (include 되는 순간 실행된다 — 타깃 규칙 파일보다 먼저여야 한다. 아래 파일은 함수만 정의한다)
    ├── UnbuiltSources.cmake      — 이 구성이 일부러 짓지 않는 소스 목록
    ├── TargetCompileRules.cmake  — 타깃 하나의 컴파일 규칙: 내보내기 매크로 · PCH · 결정성 TU
    ├── EngineAbiStamp.cmake      — 핫 리로드 ABI 스탬프(Core · Engine 헤더 지문)
    ├── ModuleRegistry.cmake      — 동적 모듈 레지스트리
    ├── ModuleTargets.cmake       — 모듈 라이브러리 팩토리(sw_addModuleLibrary)와 RHI 백엔드 · 키트 · 게임 모듈
    ├── ExecutableTargets.cmake   — 실행 파일의 링크 · 배치(통째 링크 · App/Server 의존 · 리눅스 Shipping 심볼 분리)
    ├── StageModuleRuntimeDlls.cmake — `cmake -P` 스크립트: 모듈의 서드파티 DLL 을 모아 `Bin` 에 한 벌
    ├── TestTargets.cmake         — 테스트 타깃 · CTest 등록
    ├── ModuleManifest.cmake      — 모듈 매니페스트(`<모듈>.module.json`) 해석(`cmake -P` 로도 include 된다)
    ├── ModuleActivation.cmake    — 꺼진 모듈 건너뛰기 · 꺼진 키트를 쓰는 테스트 소스 빼기
    ├── ThirdPartyLibs.cmake      — 서드파티 붙이기: SYSTEM include, vcpkg CONFIG 패키지, 헤더 전용 포트, Engine 압축 코덱
    ├── AssetAndToolTargets.cmake— 에셋 쿠킹, Doxygen 문서, 린트 타겟 및 CTest 등록 헬퍼
    ├── ReflectionCodeGen.cmake  — ReflectionParser 코드 생성 파이프라인 (sw_addReflectionStep)
    ├── CheckReflectionCandidates.cmake — 구성 뒤 반사 매크로가 생긴 헤더를 빌드 때 잡아 다시 구성하라고 세운다
    ├── RuntimeDependencies.cmake— vcpkg 경로 조회, Vulkan 레이어·mimalloc 런타임 DLL 복사(DXC 는 `ThirdParty/dxc/CMakeLists.txt` 의 `sw_copyDxcDlls`)
    └── RhiBackends.cmake        — RHI 백엔드 표의 CMake 쪽(표는 CookContract.json)
```

## 네이밍 컨벤션

| 종류 | 규칙 | 예시 |
|------|------|------|
| function / macro | `sw_camelCase` | `sw_addRhiBackendModule`, `sw_addGameFrameworkKit`, `sw_registerScriptTest` |
| 프로젝트 변수 · INTERFACE 타겟 | `sw_snake_case` | `sw_flag_libraries`, `sw_public_source_includes` |
| option / C++ 매크로 | `SW_UPPER_SNAKE_CASE` | `SW_ENABLE_PCH`, `SW_EXPORTS`, `SW_MODULE_EXPORTS` |
| 함수 내부 로컬 | `camelCase` (앞에 `_` 없음) | `kitType`, `libType`, `targetName` |

## 주요 헬퍼 함수

함수 전체는 `cmake/Engine/*.cmake` 의 `function()` 정의가 정본입니다. 이름만으로 모르는 것만 적습니다.

| 함수 | 용도 |
|------|------|
| `sw_addModuleLibrary` | 엔진 모듈 라이브러리의 기본값(종류 · Bin 출력 · 내보내기 · 등록 · PCH · 유니티 · 리플렉션) 한 자리. 키트는 `sw_addGameFrameworkKit`, RHI 는 `sw_addRhiBackendModule` |
| `sw_registerDynamicModule` / `sw_getDynamicModules` | 동적 모듈 레지스트리. **모듈 이름을 적는 곳은 타겟을 만드는 자리 하나뿐이다**(`sw_registerDynamicModule( <타깃> rhi|kit|game|gameframework|editor )`) — App·EngineTest·SmokeTest 는 종류(`KINDS rhi`)로 묻는다 |
| `sw_excludeUnbuiltSources` / `sw_declareUnbuiltSources` | 이 구성이 **일부러 짓지 않는** 소스를 빼는 자리에서 적는다(`generated/sw/config/UnbuiltSources.txt` → `CheckSourceGlob`). 다른 타겟으로 옮겨 짓는 것에는 쓰지 않는다 |
| `sw_resolveModuleManifests` | 모듈 매니페스트(`Source/**/<모듈>.module.json`, 고른 게임의 `SWGame.module.json` 이 켜기/끄기 표)를 읽고 해석한다. 없는 의존 · 꺼진 의존 · 낮은 버전 · 순환 · 모르는 이름이면 구성이 선다. Dev 는 매니페스트와 적재 순서(`ResolvedModules.txt`)를 `Bin/Modules/` 에 두고 App 이 같은 규칙(`ModuleCatalog`)으로 다시 해석한다 |
| `sw_skipInactiveModule` | 꺼진 모듈의 폴더를 "짓지 않는다" 로 적고 건너뛴다(모듈을 만드는 함수의 첫 줄) |
| `sw_registerScriptTest` | 파이썬 스크립트 하나를 CTest 항목 하나로(PythonTest · QA · 린트가 같은 속성 철자). 린트 목록은 `Scripts/lint/gate/` · `selftest/` 폴더이고 `GenerateLintTargets.py` 가 만든 `LintTargets.cmake` 가 이것을 부른다 |
| `sw_addReflectionStep` | ReflectionParser 코드 생성 스텝 연결. 자동 훑기는 구성 때 굳으므로 매크로가 없던 헤더에 `REFLECT(` · `ENUM(` 을 더하면 빌드가 다시 구성하라고 세운다 |
| `sw_addSharedPchOwner` · `sw_useSharedPch` · `sw_setTargetDefsHeader` | PCH 하나를 여러 타깃이 나눠 쓴다 — 정의 없는 소유자가 짓고 타깃 고유 정의는 PCH 뒤 강제 포함 헤더로 |
| `sw_addTestExecutable` | 테스트 실행 파일 하나 — 소스는 그 폴더를 훑는다(`SOURCES` 를 주면 그것, 엔진 밖 소스는 `EXTRA_SOURCES`) |

## 고친 뒤 구성이 같은지

CMake 리팩터는 컴파일러가 잡지 않는다 — 정의 하나 · 링크 순서 · 출력 폴더가 달라져도 configure 는 통과한다. 고치기 전과 뒤의 구성 결과를 견준다(`Scripts/dev/ConfigureSnapshot.py` — File API 답 · 생성 파일 해시 · ctest 목록):

```powershell
py -3 Scripts/dev/ConfigureSnapshot.py prepare --preset Ninja-Debug             # 질의를 둔다(한 번)
cmake --preset Ninja-Debug ; py -3 Scripts/dev/ConfigureSnapshot.py take --preset Ninja-Debug --out before.json
# ... CMake 를 고친다 ...
cmake --preset Ninja-Debug ; py -3 Scripts/dev/ConfigureSnapshot.py take --preset Ninja-Debug --out after.json
py -3 Scripts/dev/ConfigureSnapshot.py diff before.json after.json              # 다르면 1 · 다른 줄만 찍는다
```

Dev 와 Shipping · Server 는 다른 갈래를 타므로 고친 갈래의 프리셋마다 뜹니다(실행 파일 테스트의 command 는 그 exe 가 지어졌는지에 따라 비거나 차므로 차이로 읽지 않는다). 구성 시간은 `cmake --preset … --profiling-format=google-trace --profiling-output=t.json` 뒤 `ConfigureSnapshot.py profile t.json --top 25`.

## 함정 · 계약

게이트가 막는 것(팩토리 밖 SHARED/MODULE `CheckCmakeConventions`, `/DELAYLOAD` `CheckNoDelayLoad`, 소스 목록 대조 `CheckSourceGlob`, 서드파티 격리 `CheckThirdPartyIsolation`, 미등록 동적 모듈 `sw_verifyDynamicModuleRegistry`, 파이썬 공용 도우미 우회 `CheckScriptCommonHelpers`)은 여기 적지 않습니다.

- **Dev 산출물 자리**: 모듈 DLL(GF_* · RHI_* · EditorModule · SWGame)은 `Bin/Modules`, Engine · GameFramework · 서드파티 DLL 은 `Bin`, Windows PDB 는 `Bin/Symbols`, 함께 짓는 게임 팩(`SW_GAMES`)은 `Bin/Games/<게임>/SWGame` · `Bin/Symbols/Games/<게임>/`, 테스트 실행 파일은 `TestBin` 이다.
  런타임은 `FileUtil::getBinaryDirectory`(TestBin → Bin) · `ModuleImageUtil::findModuleLibraryPath`(Modules → Bin)로 찾는다. 테스트가 키트를 링크하면 그 DLL 을 TestBin 에 복사한다(OS 로더가 `Bin/Modules` 를 보지 않는다). 옛 자리 산출물과 꺼진 모듈의 DLL 은 configure 가 지운다(`sw_removeStaleBinaryOutputs`).
- **서드파티 DLL 은 `Bin` 에 한 벌이다.** vcpkg 의 `add_library` 래퍼는 타깃 옆에 의존 DLL 을 복사하는 POST_BUILD 를 다는데, `Bin/Modules` 에 가는 모듈은 `sw_addModuleLibrary` 가 그 순간만 끄고 `sw_stageModuleRuntimeDlls` 가 모듈마다 `build/<프리셋>/ModuleRuntimeStage/<모듈>` 에서 `vcpkg z-applocal` 로 모아 `Bin` 으로 옮긴다(병렬 링크가 같은 폴더를 다투지 않는다).
  모듈 로드는 모듈 폴더를 먼저 보므로 `Bin/Modules` 에 사본이 남으면 그것이 쓰인다(configure 가 모듈 이름 꼴이 아닌 DLL · PDB 와 `_temp_` 섀도 복사본을 지운다). `TestBin` 은 첫 빌드에 `sqlite3` · `libpq` · `libssl` · `libcrypto` 가 없고 다시 지으면 생긴다(테스트는 `Bin` 에서 찾아 어느 쪽이든 돈다 — 산출물 목록은 같은 단계끼리 견준다).
- **vcpkg 설치 폴더 · 스탬프는 워크트리 모두가 나눠 쓴다**(`build/vcpkg_installed` junction). 게이트(`Vcpkg.cmake` 5 절)는 스탬프가 다르면 `vcpkg install --dry-run` 계획을 보고 지을 것이 없으면 skip, 지우게 되면 멈추고 경고(스탬프도 덮지 않는다), 빠진 포트가 있을 때만 install 한다.
- **ASan (Windows)** — SmokeTest 만 `report_globals=0`(DLL 을 내려도 전역 등록이 안 지워진다), `detect_odr_violation=0`, `/MD` 강제, ASan 런타임 DLL 은 `Bin` 과 `BuildTools` 양쪽(`cmake/Modules/Options/Sanitizer.cmake`). Windows 의 memcpy 는 겹쳐도 맞게 옮겨 겹친 복사 버그가 안 보인다 — 리눅스 ASan 이 잡는다.
- **TSan** 은 `SW_SANITIZER_KIND=thread` · `CI-Debug-TSAN`(GNU/Clang 전용, ASan 과 동시 불가). 크래시 자식 테스트는 ASan · TSan 에서 건너뛴다. 원자 연산으로 스스로 동기화하는 서드파티(Jolt · Box2D)는 트리플릿 `x64-linux-tsan`(`cmake/Modules/Toolchain/VcpkgTsan/`)이 계측해 짓는다(안 하면 거짓 경쟁).
  트리플릿 파일은 ABI 해시에 들어 고치면 포트를 다 다시 짓고 기본 CI 캐시 키가 보는 `Toolchain/Vcpkg/**` 밖에 둔다. 포트 컴파일러는 프리셋의 `CC=clang` 이 정한다.
- **유니티 빌드는 `CI-*` 프리셋에만 켜져 있다.** `Ninja-*` 가 초록이어도 익명 네임스페이스 충돌이 없다는 뜻이 아니다 — 헬퍼 · 상수는 `XxxInternal` 구조체로 감싼다. 유니티 제외 목록은 만들지 않고 부딪치는 이름을 고친다. 서드파티 매크로 정리는 "내가 정의한 것만" 지운다(`#undef TRUE` 가 `windows.h` 의 정의까지 지우면 같은 묶음 뒤의 파일이 PCH 끈 빌드에서 깨진다).
- **LTO** — clang `-flto` obj 는 MSVC `lib.exe` 가 못 읽는다(LNK1107). 아카이버는 "지금 컴파일러 옆" 을 먼저 보고(리눅스 `/usr/bin` 에는 llvm-ar 이 없어 LTO 가 조용히 꺼진다) `check_ipo_supported` 는 거짓 NO 를 내서 직접 판정한다(`cmake/Environment/ToolchainBinaries.cmake`). `SW_ENABLE_LTO` 하나가 Release · Shipping.
- **configure 의 PATH 는 `sw_prependEnvPath` 로만 고친다.** project() 전에는 호스트 판정 `CMAKE_HOST_WIN32` 로 고른다.
- **configure 의 파이썬은 `GenerateConfigureFiles.py` 한 프로세스**(생성기 목록은 그 파일 머리말) — 새 configure 생성기는 각자 `sw_executePythonScript` 를 더하지 말고 거기에 한 줄.
- **빌드 타깃별 제외 에셋은 쿠킹 표 한 줄이다**(`Config/Engine/CookContract.json` 의 `target_excluded_asset_kinds`, 어느 파일이 그 묶음인지는 `AssetTypeDef::_excludeGroup`). 팩 쿠커(`PackCooker`)와 같은 호스트의 런타임(`ResourcePaths::setHostTarget`)이 같은 표 · 같은 맞춤 함수(`ResourcePaths::isExcludedForTarget`)를 쓴다 — 한쪽만 고치면 서버가 "파일 없음" 을 쏟거나 팩에 쓸모없는 바이트가 남는다.
- **서드파티 고지는 빌드가 만든다**(`ThirdPartyNotices` 타깃 → `Bin/THIRD_PARTY_NOTICES.txt`, vcpkg 포트의 `share/<포트>/copyright`). vcpkg 밖에서 들인 코드는 저절로 들어가지 않으니 그 고지를 같이 넣는다.
- **enum switch 는 LLVM 방식**: 모든 열거자를 다루면 `default:` 없음(`-Werror=switch` · `-Werror=covered-switch-default`), 일부만 다루면 `default:`. 파일별 `#pragma` 로 switch 경고를 바꾸지 않는다. 외부 헤더는 `SYSTEM` include 여야 이 규칙이 그 안에 걸리지 않는다.
- **RHI 백엔드 표(이름 · 별칭 · 셰이더 폴더 · 포맷 · 빌드 칸)와 쿡 접미사 표의 원본은 `Config/Engine/CookContract.json`** — `GenerateCookContract.py` 가 C++ X-macro(`sw/config/CookContract.gen.h`)를, CMake 는 `generated/sw/config/CookContract.cmake`(`RhiBackends.cmake`)로, Python 은 `Scripts/common/CookContract.py` 로 읽는다. 목록을 따로 적지 말 것.
  `SW_SHIPPING_RHI_BACKEND` 는 표의 이름만 받는다(별칭 · 소문자는 구성 실패 — 옛 빌드 폴더는 캐시 값을 고칠 것).
- **손으로 적은 소스 목록은 `Test/EditorTest/CMakeLists.txt` 하나다**(Editor 소스 중 ImGui 없는 것). Core 는 폴더 GLOB + 플랫폼 폴더 규칙(`sw_filterPlatformSources`), RHI 백엔드 장치 소스는 백엔드 폴더 GLOB(`sw_getRhiBackendSources`)이다. 파일을 옮기면 경로를 문자열로 적은 곳은 컴파일러가 안 잡는다.
- **생성 상수는 `Scripts/common/Constants.py` 의 `k*` 전부를 기계 변환한다**(`kDirSourceEngine` → `SW_DIR_SOURCE_ENGINE`). `configure_file` 은 빈 값을 조용히 넣는다. `toolchain_config.json` 은 CMake 가 파싱하지 않는다(`GenerateToolchainCMake.py` → `SW_TOOLCHAIN_<KEY>`) — 예외는 vcpkg 가 부르는 `FindLlvmBin.cmake` · `VcpkgPortsToolchain.cmake` 둘이고 거기서 `CMAKE_SOURCE_DIR` 은 vcpkg scripts 폴더다.
  상수만 필요한 CMake 는 `LoadConfigConstants.cmake` 를 **파일 스코프에서** include 한다(함수 안에서 처음 include 하면 상수가 그 함수에만 생긴다).
- **필수 도구(doxygen — `SW_BUILD_DOCS=ON` 일 때 · LLD · 리눅스 X11/xcb/GL/Vulkan · Windows 아카이버/mt)와 `sw_addVcpkgPackage` 는 못 찾으면 구성을 세운다**(경고만 남기고 링크에서 서지 않는다). 설정 파일이 없는 헤더 전용 포트는 `sw_addVcpkgHeaderOnly`. vcpkg 업스트림 결함은 `ThirdParty/<pkg>/vcpkg-port/` 오버레이로 고친다.
- **Core 는 OBJECT 둘이다**(`Core_low_objects` · `Core_objects`) — 티어 표(`Scripts/lint/rules/CheckCoreLayers.toml`)에서 `Log` 보다 아래인 폴더의 `.cpp` 는 낮은 쪽이고 `Source/Core/CMakeLists.txt` 가 표를 직접 읽어 가른다(표에 없는 폴더는 구성이 선다). 낮은 쪽은 `Logger.h` · File · String 이 없는 `pchLow.h` 로 컴파일되고 `SW_LOG_TAG` 가 없다.
  OBJECT 라이브러리는 링크를 전파하지 않으므로 외부 라이브러리는 `Core` 와 `Engine` 양쪽에 단다. `Core` STATIC 의 `SW_CORE_OBJECTS` 속성이 OBJECT 목록이고 Engine 이 그것을 읽는다.
- **Shipping 정적 링크는 열거형만 든 `.gen.cpp` 를 버린다**(증상: `findEnum` 이 null) — `sw_linkWholeArchive` 가 링커별 whole-archive 를 고른다. 통째 링크는 "링크한 라이브러리" 기준이어야 하고 "적은 `LIBS`" 기준이 아니다(Engine 을 `TestFramework` 로만 받는 `CoreTest` 는 Shipping 에서 등록기가 빠졌다).
- **PCH 를 나눠 쓸 때 clang-cl 은 PCH 헤더보다 앞에 강제 포함(`/FI`)한 파일을 건너뛴다**(CMake 는 PCH 플래그를 명령줄 맨 끝에 둔다). `REUSE_PRECOMPILE_HEADERS_FROM` 속성만 세우면 CMake 4.4 는 아무것도 붙이지 않는다(`target_precompile_headers( REUSE_FROM )` 이어야 한다). 확인은 `compile_commands.json` 에서 그 타깃 명령에 `/Yu` 가 있는지 본다. `sw_useSharedPch` 가 둘 다 피한다.
- **PCH 에는 자주 바뀌거나 쓰지 않는 헤더를 넣지 않는다.** `Component.h` · `SceneComponent.h` · `GameObject.h` · `Scene.h` · `SceneManager.h` · `RHITypes.h` 는 쓰는 .cpp 가 직접 include 하고(안 하면 "incomplete type", PCH 를 끈 CI 구성에서는 원래 그렇게 지어진다), D3D · DXGI · D3DCompiler · MF · XAudio2 헤더는 `EnginePlatformHeaders.h` 를 직접 include 한다.
  끊을 때 직접 include 를 받는 것은 이름의 **값**(`RHIBackend::DirectX12`)을 쓰는 파일뿐이고 타입 이름만 쓰는 헤더는 불투명 선언(`enum class RHIBackend : uint32;`)으로 선다. 어느 헤더가 모든 TU 에 걸리는지는 `ninja -t deps` 로 헤더별 TU 수를 센다([검증과 측정](../docs/08_Verification.md) "빌드 속도").
  `#if defined( SW_HAS_DXC_API )` 처럼 정의 여부로 읽는 매크로는 정의 헤더가 빠지면 **조용히** 꺼진다(`ShaderCompiler.cpp` 의 Windows `#error` 가 막는다).
- **SHARED 타깃의 `.lib` 는 내용이 바뀔 때만 갱신한다**(`sw_keepImportLibraryIfUnchanged`) — lld-link 는 내보내기가 같아도 `.lib` 를 다시 써서 링크하는 DLL 이 전부 다시 링크된다. 새 SHARED 타깃은 `sw_addModuleLibrary` 를 거치면 저절로 적용된다.
- **Release · Shipping 도 서명 · 심볼을 만든다**(`SW_RELEASE_DEBUG_INFO`, 기본 `lines` = `-gline-tables-only`, `full` = `/Z7`) — 링크 `/DEBUG:FULL` + `/OPT:REF` · `/OPT:ICF`(`/DEBUG` 가 끄므로 다시) · `/PDBALTPATH:%_PDB%`, 리눅스 `--build-id=sha1`. Shipping PDB 는 배포 폴더 밖 `Symbols/`(테스트 실행 파일 PDB 는 `TestBin`), 배치는 `py -3 -m Scripts symbols`.
- **구성을 넘어 같은 비트가 필요한 TU 는 `sw_markDeterministicSources`** — 부동소수점 축약(FMA 합치기)을 끄고 유니티 묶음에서 뺀다(Release 의 `/arch:AVX2` 가 `a * b + c` 를 합쳐 파괴 해시가 Debug 와 갈렸다). 대상은 Engine `Destruction/` 의 시뮬레이션 TU · `GF_NetDestruction` · `GF_Fighting` · `GF_MechArena` · Core `Math/VectorMath.cpp` · `Math/DeterministicMathUtil.cpp` 이고 새 결정성 경로는 파일을 더하고 기준값 테스트를 붙인다. 표시한 TU 는 libm 삼각함수 대신 `DeterministicMathUtil` 을 쓴다. 롤백 계약(`saveState( vector<uint8>& )`)을 구현한 모듈이 표시를 빠뜨리면 configure 가 멈춘다(`sw_requireDeterministicMarkForRollback`).
- **`NOMINMAX` · `WIN32_LEAN_AND_MEAN` 은 `Platform/Windows.cmake` 의 컴파일 정의다** — 헤더 정의(`PlatformOsHeaders.h`)는 그보다 먼저 `windows.h` 를 읽은 TU 에 닿지 않는다. `sw_global_options` 를 링크하지 않는 타깃은 이 정의도 없다.
- **`cmake -P` 스크립트는 `cmake_minimum_required` 를 먼저 둔다** — 없으면 정책이 OLD 라 CMake 3.x(CI 러너)에서 `IN_LIST` 가 "Unknown arguments" 다(CMake 4.x 는 늘 통과해 로컬에서는 안 보인다). `-P` 로 include 되는 모듈(`ModuleManifest` · `RhiBackends`)은 그것이 없으면 구성을 세운다.
