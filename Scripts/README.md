# Scripts (파이썬 도구)

CMake는 빌드만 담당하고, 도구 탐색·설정·보조 생성 및 코드 품질 검사는 여기서 처리한다.

## Naming

| 대상 | 규칙 | 예 |
|------|------|-----|
| 공개 함수 | `camelCase` | `setupEnvironment`, `getProjectRoot` |
| 비공개 | `camelCaseInternal` | `exeNameInternal` |
| JSON 키 | `snake_case` | `"llvm_path"` |
| 파일명 | `PascalCase.py` | `SetupEnvironment.py` |

## Layout

**폴더가 곧 성격이다.** 새 스크립트는 "무엇을 하는가" 로 자리를 고른다 — 도구를 찾아 설치하면 `setup/`, 파일을 만들어 내면 `generate/`,
검사 · 수정이면 `lint/` 의 네 폴더 중 하나, 사람이 가끔 돌리는 실험 도구면 `dev/`, 빌드된 App 을 돌려 그림 · 시간 · 메모리를 기준과 견주거나 에셋을
규칙으로 훑으면 `qa/`, 사람과 git 이 부르는 에셋 비교 · 병합이면 `asset/`. 모두 `common/` 만 import 하고 서로는 부르지 않는다
(`common` 은 위층을 부르지 않는다).

**파일 이름의 앞머리가 폴더를 말한다** — 이름만 보고 무엇을 하는지(실패하는가 · 고쳐 쓰는가 · 찍기만 하는가) 안다.

| 폴더 | 파일 이름 | 무엇 |
| --- | --- | --- |
| `lint/gate/` | `Check*` | `LintGate` |
| `lint/selftest/` | `Check*AreAlive` · `Check*SelfTest` · `Check*`(린트를 보는 것) | `LintGate` |
| `lint/fixer/` | `Format*` | `LintFixer`(조율자는 `kFixerSkipReason`) |
| `lint/report/` | `Run*` | `LintReport` |
| `generate/` | `Generate*` · `Cook*` | 빌드 · configure 가 부르는 생성기(`runGenerator`) |
| `setup/` | `Setup*` · `Install*` · `Add*` | 외부 도구 찾기 · 설치(`__main__` 이 없는 파일은 라이브러리 — `HostTools`) |
| `dev/` | 동사로 시작(`Run*` · `Compare*` · `Make*` · `Configure*` · `Sample*` …) | 사람이 가끔 — 시험 데이터를 만드는 것은 `Make*`(`generate/` 와 겹치지 않게) |
| `qa/` | 명사(`GoldenImages` · `Soak` …) | App 을 돌려 견주기 |

`gate/CheckScriptLayout.py` 가 이 표를 지킨다(린트 폴더는 기반 클래스까지). 진입점은 모두 `main(argv)` 로 인자를 받는다(`CheckScriptEntryPoints`).

```
Scripts/
  ├── common/                         # [공용 계층] 다른 폴더는 여기만 import 한다
  │     ├── Constants.py              # 경로, 파일명, JSON 키 단일 진실 공급원 (SSOT)
  │     ├── Paths.py                  # 경로 정규화, OS별 확장자/쉘 명령어 공통화
  │     ├── Config.py                 # JSON 설정 읽기/쓰기/병합 및 엄격 검증
  │     ├── Search.py                 # 파일 · 디렉터리 탐색, 검색 루트 템플릿 확장
  │     ├── ToolLocator.py            # 선언적 ToolSpec 기반 5단계 도구 탐색 프레임워크
  │     ├── Archive.py                # 다운로드 캐시, 해시 검증, 안전한 압축 해제
  │     ├── ClangFormat.py            # clang-format 찾기 · 고정 버전 확인 · 설치 (포맷하는 쪽과 설치하는 쪽이 같은 규칙)
  │     ├── Host.py                   # Git 연동 및 clang-format 배치 실행
  │     ├── Parallel.py               # 동시 처리 한 자리 — 워커 수 정책과 map/flatMap (스레드인 이유가 적혀 있다)
  │     ├── TranslationUnits.py       # 컴파일 DB 를 읽어 TU 를 골라 하나씩 돌리는 자리 (clang-tidy · 경고 스윕)
  │     ├── HeaderSelfContained.py    # 헤더 하나를 혼자 컴파일해 보는 자리 (자립 보고서 · 커밋 훅 게이트 · 전방 선언 후보 공용)
  │     ├── AppBinary.py              # 빌드된 App 을 찾고 헤드리스로 셰이더를 쿠킹하는 자리
  │     ├── BuildTree.py              # 빌드 폴더 하나(build/<프리셋>) — --preset · --build-dir 고르기 · 컴파일 DB · CMakeCache · 짓지 않는 소스
  │     ├── Process.py                # 자식 프로세스 한 창구(runProcess — UTF-8 디코딩, 못 띄움 · 시간 초과를 칸으로)
  │     ├── GeneratedFile.py          # 생성 파일 쓰기(바뀌었을 때만) · CMake 값 · 생성기 진입점(runGenerator)
  │     ├── CodeText.py               # C++ · HLSL 글에서 주석 · 리터럴을 같은 길이 공백으로 가리기(게이트 공용)
  │     ├── ConfigCatalog.py          # 설정 파일 목록(층 · 읽는 곳 · 언제 · 배포본) — docs/Config 생성기와 CheckConfigReference 가 같이 읽는다
  │     ├── ConfigReference.py        # 설정 참조 문서(docs/Config)를 코드에서 만든다
  │     ├── AppRun.py                 # App 한 판 — 출력 모으기 · 못 도는 백엔드 판정 · 프로파일 표 읽기 · 밖에서 메모리 · 핸들 재기(qa/ 셋이 쓴다)
  │     ├── AssetValidation.py        # 에셋 검증 규칙 — 규칙 표(Config/Editor/AssetValidationRules.json)의 `check` 이름이 고르는 연산자들
  │     ├── ImageMetrics.py           # 스크린샷 비교 — PPM · PNG 읽기/쓰기, 축소, 배경을 뺀 지표 · 잡음 바닥에서 정한 허용 오차
  │     ├── XmlAssetMerge.py          # XML 에셋 의미 비교 · 3-way 병합(엔티티 id · 컴포넌트 · 속성 단위), 엔진 저장기와 같은 서식으로 쓰기
  │     ├── AssetPipeline.py          # 쿠커의 기본 출력 폴더 찾기 — 가장 최근에 구성된 build/*/Bin/<subDir>
  │     ├── CookContract.py           # 쿠킹 표(`Config/Engine/CookContract.json`)를 읽은 결과 — 헤더 생성기 · 쿠커 · 게이트가 같은 객체를 쓴다
  │     └── PackFormat.py             # `.pack` 바이너리 계약(Config/Engine/PackFormat.json)을 읽은 결과
  │
  ├── setup/                          # [환경 구성] 외부 도구를 찾고, 없으면 받아 설치한다
  │     ├── SetupEnvironment.py       # 통합 환경 설정 (ToolLocator 기반) → toolchain_config.json
  │     ├── SetupLlvm.py              # LLVM/libclang 부트스트랩 (kLlvmToolSpec 적용)
  │     ├── SetupVcpkg.py             # vcpkg 부트스트랩 (kVcpkgToolSpec 적용)
  │     ├── SetupLinuxDevEnvironment.py # Linux · WSL 홈 디렉터리 설정
  │     ├── HostTools.py              # MSVC, WinSDK, DXC, system include 탐색 (라이브러리 — SetupEnvironment 가 쓴다)
  │     ├── InstallGitHooks.py        # Git pre-commit 훅 설치
  │     └── AddDefenderExclusions.py  # Windows Defender 빌드 폴더 예외 등록
  │
  ├── generate/                       # [생성] 정본(JSON · Constants.py · 폴더 목록)에서 파일을 만들어 낸다 — 대부분 구성 · 빌드가 부르고,
  │                                   #        결과를 커밋하는 것(GenerateSpriteTextures)은 손으로 돌린다
  │     ├── CookAssets.py             # ★ Prefab, Scene, Resource Pack을 일괄/선택 쿠킹하는 단일 통합 쿠커
  │     ├── GenerateShippingHostDefaults.py # 런타임 JSON → Shipping 용 C++ 헤더
  │     ├── GeneratePackFormat.py     # PackFormat.json → C++ 헤더
  │     ├── GenerateCookContract.py   # CookContract.json → C++ X-매크로 헤더(RHI 백엔드 표 · 쿡 접미사 표)
  │     ├── GenerateConfigReference.py # 설정 참조 문서(docs/Config) — 결과를 커밋한다, 낡음은 CheckConfigReference 가 본다
  │     ├── GenerateThirdPartyNotices.py # 배포물의 서드파티 고지(vcpkg 매니페스트가 끌어오는 포트 전부)
  │     ├── GenerateCMakeConstants.py # Constants.py → CMake set() 목록
  │     ├── GenerateToolchainCMake.py # toolchain_config.json → CMake set() 목록
  │     ├── GenerateLintTargets.py    # lint/gate · selftest 폴더 → CMake 린트 타깃 · 테스트
  │     ├── GenerateEngineAbiStamp.py # Core · Engine 헤더 지문 → 핫 리로드 ABI 도장 헤더
  │     ├── GenerateSpriteTextures.py # 엔진 스프라이트 텍스처(DDS) · 클립 — 손으로 돌리고 결과를 커밋한다
  │     └── GenerateDocs.py           # Doxygen 레퍼런스 생성
  │
  ├── lint/                           # [정적 검사 및 코드 스타일] — 폴더가 곧 성격이다
  │     ├── LintGate.py · LintFixer.py · LintReport.py # 게이트 · 픽서 · 보고서 하나 = 클래스 하나 (껍데기는 기반이 든다)
  │     ├── LintCatalog.py            # gate/ · selftest/ 를 훑어 "무엇이 있고 어떻게 돌리는가" (CMake · 훅이 읽는다)
  │     ├── PreCommitLint.py          # Git Staged 대상 사전 커밋 종합 검사 (넷을 조율하므로 여기 남는다)
  │     │                             #   병합 커밋은 어느 부모와도 내용이 다른 파일만 파일 단위로 본다 (아래 "커밋 훅과 병합 커밋")
  │     ├── gate/                     # 위반이 있으면 **실패한다** — 빌드와 커밋을 막는 건 이 폴더뿐
  │     │     ├── CheckCodeConventions.py     # C++ 엔진 코딩 컨벤션 (줄 단위 규칙 하나 = 클래스 하나)
  │     │     ├── CheckFunctionVocabulary.py  # 함수 이름 어휘 (한 개념 한 동사 · 약어는 단어)
  │     │     ├── CheckIncludeOrder.py        # 인클루드 순서·중복 (검사만 — 고치기는 fixer/FormatIncludeOrder.py)
  │     │     ├── CheckEngineLayers.py        # 아키텍처 레이어 침범
  │     │     ├── CheckCoreNetworkLayers.py   # Core/Network 폴더 층(뿌리 ← Transport · Security ← Connection ← Message ← Replication)
  │     │     ├── CheckEngineServiceBinding.py # 엔진 서비스 표와 바인딩 호스트 대조
  │     │     ├── CheckNullableServiceUse.py  # nullptr 가능 서비스 조회를 확인 없이 역참조
  │     │     ├── CheckLogViewArgument.py     # 로그 인자의 string_view::data()
  │     │     ├── CheckGlobalVariableKinds.py # 전역 변수 정의와 extern 참조의 종류 일치
  │     │     ├── CheckRenderOwnership.py     # 렌더 스냅샷 소유 규칙
  │     │     ├── CheckTestSuites.py          # 스위트 명명 · 한 파일 한 스위트 · CI 경계 표식 · CoreTest 는 엔진을 직접 쓰지 않음
  │     │     ├── CheckFallibleNodiscard.py   # 실패를 bool 로 알리는 함수 선언의 `[[nodiscard]]`
  │     │     ├── CheckSourceGlob.py          # CMake GLOB 소스 누락 (짓지 않는 소스는 CMake 가 적은 UnbuiltSources.txt 로만 안다)
  │     │     ├── CheckDataFileReferences.py  # 아무도 include 하지 않는 죽은 데이터 파일
  │     │     ├── CheckDelayLoadSites.py      # /DELAYLOAD 는 ModuleTargets.cmake 의 두 함수로만(지연 로드 첫 호출이 첫 float 인자를 망가뜨린다)
  │     │     ├── CheckResourceCasing.py      # 리소스 소문자 명명
  │     │     ├── CheckTextureFolders.py      # 런타임 textures/ 에는 DDS 만, 원본 이미지는 textures_raw/ 에만
  │     │     ├── CheckClockReads.py          # std::chrono 시계가 아니라 MonotonicClock · Stopwatch · Deadline
  │     │     ├── CheckConfigReference.py     # docs/Config 가 코드와 같은지 · 설정 파일이 모두 목록에 있는지
  │     │     ├── CheckDuplicateTypeNames.py  # 같은 이름의 타입 정의(ODR)
  │     │     ├── CheckEngineRootFiles.py     # Source/Engine 루트에는 기동 · 종료 배선 파일만
  │     │     ├── CheckGameFrameworkLayers.py # GameFramework 기반 폴더 층 · 키트 의존 방향
  │     │     ├── CheckGamePresets.py         # 게임마다 Config/Game/<게임>.json 이 있고 실제 팩 · 시작 씬을 가리키는지
  │     │     ├── CheckKitNamespaces.py       # 키트를 섞을 때 부딪히는 이름 공간 · 소유
  │     │     ├── CheckModuleTargets.py       # 모듈 대상(Client · Server)의 이름 · 의존 · include 방향
  │     │     ├── CheckNamespaceBlocks.py     # 한 namespace 블록에 정의 하나 (고치기는 fixer/FormatNamespaceBlocks.py)
  │     │     ├── CheckStdFilesystemIsolation.py # std::filesystem 은 Source/Core/File/Std 안에서만
  │     │     ├── CheckThirdPartyIsolation.py # 감싼 서드파티의 헤더 · 링크가 백엔드 폴더 밖으로 새지 않는지
  │     │     ├── CheckWellKnownConstants.py  # 잘 알려진 상수(π · √2 · 중력 · 해시 상수)를 집 밖에서 리터럴로
  │     │     ├── CheckWin32WideCalls.py      # Win32 API 는 W 판 이름으로
  │     │     ├── CheckAssetRules.py          # 에셋 검증 규칙의 오류 심각도(이름 · 텍스처 · 메시 예산 · 참조 · 머티리얼 · 컴포넌트 · id · guid · 팩 규칙)
  │     │     ├── CheckTargetMacros.py        # 플랫폼 · 아키텍처 · 컴파일러를 SW_* 매크로로만 묻기 (컴파일러 내장 매크로 금지)
  │     │     ├── CheckX11Isolation.py        # X11 헤더는 X11 구현 .cpp 에서만 · 뒤에 매크로 지우기 · 서드파티와 한 TU 에 두지 않기
  │     │     ├── CheckCookContract.py        # 쿠커가 쿠킹 표대로 고르는지
  │     │     ├── CheckShaderConventions.py   # HLSL 명명 규칙(AGENTS.md 의 HLSL 절)
  │     │     ├── CheckCmakeConventions.py    # CMake 명명 규칙
  │     │     ├── CheckCmakeReadme.py         # cmake/README.md 가 가리키는 파일 · 함수가 실재하는지
  │     │     ├── CheckPythonConventions.py   # 파이썬 명명 규칙
  │     │     ├── CheckScriptEntryPoints.py   # 진입점이 모듈 수준에서 common 을 import 하는지(콘솔 UTF-8) · main(argv) 로 인자를 받는지
  │     │     ├── CheckScriptCommonHelpers.py # common 의 한 자리(runProcess · BuildTree · writeGeneratedFile · 콘솔)를 비켜 가는 호출
  │     │     ├── CheckScriptLayout.py        # 폴더마다 파일 이름 앞머리 · 린트 기반 클래스(아래 Layout 표)
  │     │     ├── CheckHeaderSelfContained.py # staged 헤더가 혼자 서는지 — 빌드 폴더가 있을 때만, CTest 린트에는 안 든다(ctestSkipReason)
  │     │     ├── CheckPythonMinimumVersion.py # CI 의 파이썬에서도 파싱되는지
  │     │     └── CheckTextFilesAreText.py    # 텍스트 파일의 널 바이트
  │     ├── fixer/                    # 파일을 실제로 고쳐 쓴다 (게이트가 아니다)
  │     │     ├── FormatBranchBraces.py       # if 계열 중괄호 (`--check` 면 검사만)
  │     │     ├── FormatCmakeIndent.py        # CMake 줄머리 공백 들여쓰기 → 탭 (문자열 안 · vcpkg 툴체인 영역은 그대로)
  │     │     ├── FormatForwardDeclarations.py
  │     │     ├── FormatIncludeOrder.py       # include 순서 · 중복 (규칙은 gate/CheckIncludeOrder.py)
  │     │     ├── FormatNamespaceBlocks.py    # 정의마다 namespace 블록 (규칙은 gate/CheckNamespaceBlocks.py)
  │     │     ├── FormatClangFormat.py        # clang-format 적용 (`py -3 -m Scripts format`)
  │     │     └── FormatModified.py           # 작업 트리 변경분에 위 픽서들 + clang-format
  │     ├── report/                   # 찍어 줄 뿐, 0 으로 끝난다 (`RunBuildWarnings.py --fail-on` 을 명시했을 때만 예외)
  │     │                             #   보고서 = `LintReport` 하위 클래스(`main = XxxReport.run`) — --root · --preset/--build-dir · --jobs · --filter · --out 은 기반이
  │     │     ├── RunBuildWarnings.py         # 트리에 남아 있는 컴파일러 경고 (`--fail-on error` 를 명시하면 CI 가 막는 데 쓴다,
  │     │     │                               #   `--define SW_ENABLE_DEADLOCK_DETECTION` 처럼 어느 프리셋도 켜지 않는 옵션이 아직 컴파일되는지도 묻는다)
  │     │     ├── RunClangTidy.py
  │     │     ├── RunHeaderSelfContained.py   # 혼자 서지 못하는 헤더 (CI header-self-contained 가 매일 --fail-on-violation 으로)
  │     │     ├── RunPaddingReport.py         # 레코드별 패딩 · 필드 재배치로 줄일 수 있는 크기 (libclang, `--preset` · `--define SW_SHIPPING`)
  │     │     ├── RunForwardDeclarationCandidates.py # 전방 선언으로 바꿀 수 있는 include (`--apply` 는 고쳐 쓴다)
  │     │     ├── RunDuplicateCode.py         # 복사돼 있는 코드 블록 (C++ · `--language py` · `--language cmake`)
  │     │     ├── RunEngineLayerGraph.py      # Engine 폴더 간 include 그래프 · 강결합 묶음
  │     │     ├── RunFolderFileCount.py       # 너무 큰 평면 폴더 · 파일 하나짜리 폴더
  │     │     ├── RunRepeatedConstants.py     # 같은 뜻이 여러 곳에 따로 적힌 상수 · 리터럴
  │     │     └── RunBuildScriptInventory.py  # 빌드 스크립트 재고 — 죽은 CMake 함수 · 큰 CMake 파일 · 손 목록 · common 을 비켜 간 파이썬 호출
  │     └── selftest/                 # 코드가 아니라 **린트** 를 본다
  │           ├── CheckLintsAreAlive.py       # gate/ 를 훑어 각 게이트가 아직 무는지 확인
  │           ├── CheckFixersAreAlive.py      # fixer/ 가 아직 고치는지, 고치면 안 되는 것은 안 고치는지
  │           ├── CheckCodeConventionsSelfTest.py # CheckCodeConventions 의 규칙마다 아직 무는지 확인
  │           ├── CheckMergeCommitScope.py    # 병합 커밋에서 훅이 새 내용 파일을 빠뜨리지 않고 줄이는지 (임시 git 저장소)
  │           └── CheckReportsRun.py          # report/ 의 보고서가 모두 LintReport 이고 --help 로 뜨는지
  │
  ├── dev/                            # [개발 실험] 사람이 가끔 손으로 돌린다 — 빌드 · CI 가 부르지 않는다
  │     ├── RunBackendSmoke.py        # 네 백엔드로 같은 씬을 그려 SceneColor 를 비교
  │     ├── CiFailureReport.py        # CI 실패(시험 · 구성 · 크래시 스택)를 GitHub 주석으로 — ci.yml 이 부른다
  │     ├── ConfigureSnapshot.py      # CMake 구성 결과 스냅숏 · 비교(리팩터 전후) · 구성 시간 요약
  │     ├── MakeStressScene.py        # 로드 경로를 재기 위한 큰 씬(사람이 시험 데이터를 만든다 — `Make*`, 빌드가 만드는 것은 generate/)
  │     ├── MakeTerrainShowcase.py    # 지형 쇼케이스의 절차 생성 원본(heightfields_raw · textures_raw)
  │     ├── RunTests.py               # 스위트 · 케이스 이름으로 테스트 실행 — 그 케이스가 사는 실행 파일을 `Bin` 에서
  │     ├── SampleStacks.py           # 살아 있는 프로세스의 스레드 스택을 여러 번 떠 함수별로(DbgHelp) — 프로파일러가 닿지 않는 곳
  │     └── StoreSymbols.py           # 빌드의 PDB · .debug 를 심볼 저장소 배치(GUID+age · .build-id)로
  │
  ├── qa/                             # [QA] 빌드된 App 을 돌려 기준과 견주거나(hostgpu · soak · perf CTest 와 사람이 부른다) 에셋을 규칙으로 훑는다
  │     ├── ValidateAssets.py         # 에셋 검증 표(파일 · 규칙 · 심각도 · 메시지) — 게이트와 같은 코드, 경고까지 · JSON 출력
  │     ├── GoldenImages.py           # 시험 게임 자동 플레이 × 네 백엔드 캡처를 Test/Qa/Golden 기준과 지표로 비교(`--record` 로 기준을 뜬다)
  │     ├── Soak.py                   # 자동 플레이 장시간 실행 — 메모리 · 핸들 증가 기울기, 프레임 p50 · p99
  │     └── PerfRegression.py         # Release 프레임 p50 · p99 를 이 기계의 기준(Test/Qa/Perf)과 비교
  │
  ├── asset/                          # [에셋 도구] 사람과 git 이 부르는 에셋 비교 · 병합
  │     └── AssetMerge.py             # XML 에셋 의미 diff · 3-way merge, git 병합 · 비교 드라이버(아래 절)
  │
  └── __main__.py                     # ★ 통합 CLI 오케스트레이터 (`py -3 -m Scripts <cmd>`)
```

## 통합 CLI 인터페이스 (`python -m Scripts`)

모든 도구는 프로젝트 루트에서 파이썬 모듈 인터페이스를 통해 일관되게 실행할 수 있습니다:

```bash
# Windows: py -3 -m Scripts <command>  /  POSIX: python3 -m Scripts <command>
py -3 -m Scripts setup                # 개발 환경 및 도구체인 탐색/설정 (SetupEnvironment)
py -3 -m Scripts cook --all           # 프리팹, 씬, 리소스 팩 일괄 쿠킹 (CookAssets)
py -3 -m Scripts vcpkg                # vcpkg 탐색 및 부트스트랩 (SetupVcpkg)
py -3 -m Scripts llvm                 # LLVM/Clang 탐색 및 설정 (SetupLlvm)
py -3 -m Scripts format               # C++ 코드 clang-format 자동 포맷팅 (FormatClangFormat) — 파일을 고르면 `--files a.cpp b.h`(게이트와 같은 철자)
py -3 -m Scripts lint                 # Staged 파일 대상 사전 커밋 린트 검사 (PreCommitLint)
py -3 -m Scripts gate                 # lint/gate/ 의 이름 목록 — 폴더 명령 넷은 이름 표가 아니라 폴더를 훑는다
py -3 -m Scripts gate CheckEngineLayers --files Source/Engine/Scene/Scene.h   # 게이트 하나
py -3 -m Scripts fix FormatIncludeOrder --files Source/Engine/Scene/Scene.cpp # 픽서 하나(lint/fixer/)
py -3 -m Scripts report RunBuildScriptInventory   # 보고서 하나(lint/report/)
py -3 -m Scripts selftest CheckLintsAreAlive      # 셀프테스트 하나(lint/selftest/)
py -3 -m Scripts docs                 # Doxygen API 레퍼런스 문서 생성 (GenerateDocs)
py -3 -m Scripts test SceneTest.*     # 스위트 · 케이스 이름으로 테스트 실행 (RunTests)
py -3 -m Scripts validate-assets      # 에셋 검증 표 (ValidateAssets) — `--severity error` 는 게이트와 같은 판정
py -3 -m Scripts asset-merge diff a.scene.xml b.scene.xml   # XML 에셋 의미 비교 (AssetMerge)
py -3 -m Scripts golden --app build/Ninja-Debug-NileCity/Bin/App.exe   # 골든 이미지 (GoldenImages)
py -3 -m Scripts soak --app <App> --minutes 10                # 장시간 실행 (Soak)
py -3 -m Scripts perf --app build/Ninja-Release/Bin/App.exe   # 성능 회귀 (PerfRegression)
py -3 -m Scripts symbols --preset Ninja-Shipping --store <저장소>   # 심볼 저장소 배치 (StoreSymbols)
py -3 -m Scripts stacks <pid> --samples 200                   # 스택 샘플러 (SampleStacks, Windows)
```

## 커밋 훅과 병합 커밋

`PreCommitLint.py` 는 staged 파일만 본다. 병합 커밋(`MERGE_HEAD` 가 있다)에서는 병합으로 바뀐 파일이 전부 staged 로 잡히지만,
그 대부분은 한쪽 부모와 바이트가 같고 그 부모 커밋을 만들 때 훅이 이미 검사했다. 그래서 파일 단위 검사(`--files` · 위치 인자를 받는
게이트, 픽서, clang-format)에는 **staged 내용이 어느 부모의 같은 경로 blob 과도 다른 파일** — 충돌 해결 · 자동 병합으로 내용이 새로 생긴
파일 — 만 넘긴다. 트리 전체 게이트(파일 인자 없음)와 셰이더 쿠킹 검증은 그대로 돈다. 부모 둘에서 따로 온 파일끼리의 관계는 파일 단위
검사가 원래 못 보므로 **병합 뒤 `ctest -L lint`(CI 도 같다)가 트리 전체로 다시 본다.** 최근 병합 여덟 개에서 파일 단위 대상은
staged 67 → 4, 73 → 18, 759 → 0, 442 → 40 개였다. 이 줄이기는 `selftest/CheckMergeCommitScope.py` 가 지킨다.

## 개별 스크립트 실행

```bash
py -3 Scripts/setup/SetupEnvironment.py
py -3 Scripts/generate/CookAssets.py --all
py -3 Scripts/lint/gate/CheckEngineLayers.py
py -3 Scripts/lint/fixer/FormatClangFormat.py
py -3 Scripts/generate/GenerateShippingHostDefaults.py build/Ninja-Shipping/generated/sw/config/ShippingHostDefaults.h  # 빌드가 읽는 자리(<빌드 폴더>/generated)
```

`SetupLlvm` / `SetupEnvironment` 는 최소 LLVM 키트에 `clang-format` 을 포함·보완합니다 (기존 키트에 없으면 캐시된 LLVM tar에서 bin만 추출).

## XML 에셋 병합 · 비교 드라이버 (git)

`Scripts/asset/AssetMerge.py` 는 씬 · 프리팹 · 머티리얼 · 카탈로그 XML 을 **엔티티 id · 컴포넌트(`_componentName`) · 속성** 단위로 비교하고
3-way 병합한다(유니티 Smart Merge 와 같은 자리). 줄 단위 병합이 충돌하는 두 갈래 — 같은 목록 끝에 서로 엔티티를 더한 것, 한 요소의 다른 속성을
고친 것 — 가 충돌 없이 합쳐진다. 같은 속성을 다르게 고치면 충돌이고, 결과 파일에 우리 쪽 값을 둔 채 `<!-- MERGE CONFLICT … -->` 주석을 남기고
1 로 끝난다(XML 은 그대로 읽힌다 — 줄 충돌 표식처럼 파일을 깨지 않는다). 출력은 엔진 저장기와 같은 서식이라 엔진이 다시 저장해도 줄이 바뀌지 않는다
(엔진이 쓴 파일은 읽고 다시 쓰면 바이트까지 같다 — `PythonTest_TestXmlAssetMerge`).

git 에 붙이는 것은 강제하지 않는다(PC 마다 한 번):

```bash
git config merge.swasset.name   "SW XML asset merge"
git config merge.swasset.driver "py -3 Scripts/asset/AssetMerge.py git-merge %O %A %B %P"
git config diff.swasset.command  "py -3 Scripts/asset/AssetMerge.py git-diff"
```

그리고 `.gitattributes`(또는 저장소에 남기지 않으려면 `.git/info/attributes`)에:

```
*.scene.xml   merge=swasset diff=swasset
*.prefab.xml  merge=swasset diff=swasset
*.material    merge=swasset diff=swasset
```

리눅스는 `py -3` 대신 `python3`. 병합기가 XML 로 읽지 못하면(2) git 은 그 파일을 충돌로 남긴다. `--prefer ours|theirs` 는 충돌을 그쪽으로 푼다
(`py -3 -m Scripts asset-merge merge base ours theirs --prefer theirs`).
