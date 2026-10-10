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
| `dev/` | 동사로 시작(`Run*` · `Compare*` · `Make*` · `Move*` · `Remove*` · `Configure*` · `Sample*` · `List*` …) | 사람이 가끔 — 시험 데이터를 만드는 것은 `Make*`(`generate/` 와 겹치지 않게) |
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
  │     ├── EditorIconFont.py         # 에디터 아이콘 99 개의 그리기 함수 + 표준 라이브러리만으로 TrueType 폰트 · C++ 헤더 쓰기
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
  │     ├── GenerateConfigureFiles.py # configure 가 부르는 하나 — 위 · 아래 생성기 다섯을 한 프로세스로
  │     ├── GenerateToolchainCMake.py # toolchain_config.json → CMake set() 목록
  │     ├── GenerateLintTargets.py    # lint/gate · selftest 폴더 → CMake 린트 타깃 · 테스트
  │     ├── GenerateEngineAbiStamp.py # Core · Engine 헤더 지문 → 핫 리로드 ABI 도장 헤더
  │     ├── GenerateEditorIcons.py    # 에디터 아이콘 폰트 · 글리프 상수 헤더(그림은 common/EditorIconFont.py) — 손으로 돌리고 결과를 커밋한다
  │     ├── GenerateSpriteTextures.py # 엔진 스프라이트 텍스처(DDS) · 클립 — 손으로 돌리고 결과를 커밋한다
  │     └── GenerateDocs.py           # Doxygen 레퍼런스 생성
  │
  ├── lint/                           # [정적 검사 및 코드 스타일] — 폴더가 곧 성격이다
  │     ├── LintGate.py · LintFixer.py · LintReport.py # 게이트 · 픽서 · 보고서 하나 = 클래스 하나 (껍데기는 기반이 든다)
  │     ├── LintCatalog.py            # gate/ · selftest/ 를 훑어 "무엇이 있고 어떻게 돌리는가" (CMake · 훅이 읽는다)
  │     ├── AcronymRegistry.py        # 약어 등록부 — 약어 · 줄임말 · 제품 이름 · 남의 이름 표와 철자 판정 (게이트 · 코드모드가 같이 읽는다)
  │     ├── PreCommitLint.py          # Git Staged 대상 사전 커밋 종합 검사 (넷을 조율하므로 여기 남는다)
  │     │                             #   병합 커밋은 어느 부모와도 내용이 다른 파일만 파일 단위로 본다 (아래 "커밋 훅과 병합 커밋")
  │     ├── gate/                     # 위반이 있으면 **실패한다** — 빌드와 커밋을 막는 건 이 폴더뿐
  │     │     ├── CheckCodeConventions.py     # C++ 엔진 코딩 컨벤션 (줄 단위 규칙 하나 = 클래스 하나)
  │     │     ├── CheckFunctionVocabulary.py  # 함수 이름 어휘 (한 개념 한 동사 · 약어는 단어)
  │     │     ├── CheckAcronymSpelling.py     # 약어 철자 (등록부 kEnforced · --enforce 약어만 막고 나머지는 숫자로만 — 고치기는 fixer/FormatAcronymSpelling.py)
  │     │     ├── CheckIncludeOrder.py        # 인클루드 순서·중복 (검사만 — 고치기는 fixer/FormatIncludeOrder.py)
  │     │     ├── CheckEngineLayers.py        # 아키텍처 레이어 침범
  │     │     ├── CheckCoreLayers.py          # Core 폴더 티어(Common → … → LogSink), 아래 층의 pch 로그 사용
  │     │     ├── CheckCoreNetworkLayers.py   # Core/Network 폴더 층(뿌리 ← Transport · Security ← Connection ← Message ← Replication)
  │     │     ├── CheckEngineServiceBinding.py # 엔진 서비스 표와 바인딩 호스트 대조
  │     │     ├── CheckNullableServiceUse.py  # nullptr 가능 서비스 조회를 확인 없이 역참조
  │     │     ├── CheckControlBoundary.py     # 입력을 읽는 파일은 플레이어 조종자 · 플레이어 뷰 · 명령 디렉터만(폰은 의도만)
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
  │     │     ├── CheckDocPaths.py            # 문서(.md)의 링크 · 앵커 · 저장소 경로가 실재하는지, 모든 README 가 문서 지도에 있는지
  │     │     ├── CheckPythonConventions.py   # 파이썬 명명 규칙
  │     │     ├── CheckScriptEntryPoints.py   # 진입점이 모듈 수준에서 common 을 import 하는지(콘솔 UTF-8) · main(argv) 로 인자를 받는지
  │     │     ├── CheckScriptCommonHelpers.py # common 의 한 자리(runProcess · BuildTree · writeGeneratedFile · 콘솔)를 비켜 가는 호출
  │     │     ├── CheckScriptLayout.py        # 폴더마다 파일 이름 앞머리 · 린트 기반 클래스(아래 Layout 표)
  │     │     ├── CheckHeaderSelfContained.py # staged 헤더가 혼자 서는지 — 빌드 폴더가 있을 때만, CTest 린트에는 안 든다(ctestSkipReason)
  │     │     ├── CheckPythonMinimumVersion.py # CI 의 파이썬에서도 파싱되는지
  │     │     ├── CheckTextFilesAreText.py    # 텍스트 파일의 널 바이트
  │     │     └── CheckExecutableBits.py      # `#!` 스크립트(오버레이 포트의 configure 포함)는 git 모드 100755 — Windows 에서 만든 파일은 실행 비트가 없다
  │     ├── fixer/                    # 파일을 실제로 고쳐 쓴다 (게이트가 아니다)
  │     │     ├── FormatBranchBraces.py       # if 계열 중괄호 (`--check` 면 검사만)
  │     │     ├── FormatAcronymSpelling.py    # 약어 철자 코드모드 (`--report` 사전 실행 · `--apply-files` 파일 · 데이터 · `--rename-folders` 표)
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
  │     │     ├── RunIncludeCost.py           # 헤더별 누적 파싱 시간 상위 (전 TU `-ftime-trace`, PCH 없이 구문 검사)
  │     │     ├── RunPaddingReport.py         # 레코드별 패딩 · 필드 재배치로 줄일 수 있는 크기 (libclang, `--preset` · `--define SW_SHIPPING`)
  │     │     ├── RunForwardDeclarationCandidates.py # 전방 선언으로 바꿀 수 있는 include (`--apply` 는 고쳐 쓴다)
  │     │     ├── RunDuplicateCode.py         # 복사돼 있는 코드 블록 (C++ · `--language py` · `--language cmake`)
  │     │     ├── RunEngineLayerGraph.py      # Engine 폴더 간 include 그래프 · 강결합 묶음
  │     │     ├── RunCoreLayerGraph.py        # Core 폴더 간 include 그래프 · 묶음 · 거꾸로 가는 include(파일 단위)
  │     │     ├── RunFolderFileCount.py       # 너무 큰 평면 폴더 · 파일 하나짜리 폴더
  │     │     ├── RunRepeatedConstants.py     # 같은 뜻이 여러 곳에 따로 적힌 상수 · 리터럴
  │     │     ├── RunBuildScriptInventory.py  # 빌드 스크립트 재고 — 죽은 CMake 함수 · 큰 CMake 파일 · 손 목록 · common 을 비켜 간 파이썬 호출
  │     │     └── RunDocStyle.py              # 문서마다 읽기 어려운 문장 모양과 조어 수 (기준은 docs/10_WritingDocs.md, `--files` 로 다시 쓰기 전후 비교)
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
  │     ├── MakeNoiseTexture.py       # 엔진 잡음 텍스처 원본(engine/textures_raw/perlin.png — 이음매 없는 Perlin fBm)
  │     ├── MakeBrandImages.py        # 에디터 스플래시 원본과 앱 아이콘(app.ico)을 같은 SW 모노그램에서 그린다
  │     ├── MakeWorktree.py           # 작업 단위용 git 워크트리 + main 과 나눠 쓰는 도구 · vcpkg 폴더 링크(docs/11_Workflow.md)
  │     ├── RemoveWorktree.py         # 워크트리 지우기 — 나눠 쓰는 링크를 먼저 끊는다
  │     ├── ListCiJobs.py             # GitHub Actions 실행 · 잡 · 실패 주석을 공개 API 로(로그인 없이)
  │     ├── ListOutdatedDeps.py       # vcpkg 의존성의 지금 판 · 레지스트리 최신 판(오버레이 포함, docs/09 5-2)
  │     ├── MoveEditorState.py        # 체크아웃마다 한 번: 옛 자리(Config/Editor · 팩 gv 프리셋)의 에디터 로컬 상태를 Saved/Editor 로
  │     ├── MoveEngineFolders.py      # Engine 폴더 재배치의 이동 표(git mv + 경로 치환)와 티어 표 맞추기(--sync-tier) — 다시 돌릴 수 있다
  │     ├── RunTests.py               # 스위트 · 케이스 이름으로 테스트 실행 — 시험까지 지은 뒤(`all` · `AllTests`) 그 케이스가 사는 실행 파일을 `Bin` 에서
  │     ├── RunBuildBaseline.py       # 빌드 시간 기준선(풀 · 헤더 수정 · .cpp 수정 · 워크트리 콜드, 각 3 회 중앙값, 머리에 PC · CPU)
  │     ├── SampleStacks.py           # 살아 있는 프로세스의 스레드 스택을 여러 번 떠 함수별로(DbgHelp) — 프로파일러가 닿지 않는 곳
  │     └── StoreSymbols.py           # 빌드의 PDB · .debug 를 심볼 저장소 배치(GUID+age · .build-id)로
  │
  ├── qa/                             # [QA] 빌드된 App 을 돌려 기준과 견주거나(hostgpu · soak · perf CTest 와 사람이 부른다) 에셋을 규칙으로 훑는다
  │     ├── ValidateAssets.py         # 에셋 검증 표(파일 · 규칙 · 심각도 · 메시지) — 게이트와 같은 코드, 경고까지 · JSON 출력
  │     ├── GoldenImages.py           # 시험 게임 자동 플레이 × 네 백엔드 캡처를 Test/QA/Golden 기준과 지표로 비교(`--record` 로 기준을 뜬다)
  │     ├── Soak.py                   # 자동 플레이 장시간 실행 — 메모리 · 핸들 증가 기울기, 프레임 p50 · p99
  │     └── PerfRegression.py         # Release 프레임 p50 · p99 를 이 기계의 기준(Test/QA/Perf)과 비교
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
검사가 원래 못 보므로 **병합 뒤 `ctest -L lint`(CI 도 같다)가 트리 전체로 다시 본다.** 이 줄이기는 `selftest/CheckMergeCommitScope.py` 가 지킨다.

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

## 함정 · 계약

- **키트 커밋은 저장소가 고정한 clang-format(`Tools/LLVM/bin/clang-format`, 20)으로.** 시스템의 18 은 멤버 포인터(`float32 Foo::*_pMember`) 줄을 다르게 맞춰 린트가 막는다.
- **clang-format 은 고정 바이너리 `Tools/LLVM/bin/clang-format.exe`(20.1.8)** 로만 센다(`clang_format_version` 키로 LLVM 과 따로 고정, `Scripts/common/ClangFormat.py`).
  PATH 의 것으로 세면 틀린다(정답은 0 개). `--dry-run` 에 파일 여럿을 한꺼번에 주면 보고가 조용히 잘린다 — 파일마다 한 번씩 센다:

  ```bash
  CF=Tools/LLVM/bin/clang-format.exe
  find Source Test Tools/ReflectionParser \( -name '*.cpp' -o -name '*.h' -o -name '*.inl' \) -print0 \
    | xargs -0 -P 8 -n 1 -I{} sh -c "\"$CF\" --dry-run --ferror-limit=0 \"{}\" 2>&1 | grep -q warning: && echo {}"
  ```
- **clang-tidy 는 버전마다 다른 숫자를 낸다.** `RunClangTidy.py` 가 실행 파일 경로와 버전을 머리에 찍고 `--clang-tidy <경로>` 로 고정한다(VS 의 `VC/Tools/Llvm/x64/bin` 도 찾는다,
  PCH 는 `/Y-`). 남은 `bugprone-throwing-static-initialization`(전역 변수 등록자 · 설정 싱글턴 ~30 건)은 고치지 않는다 — [결정 기록](../docs/09_Decisions.md) 2절 참고. 숫자가 늘면 **종류**를 먼저 본다(전역
  변수 개수를 따라간다). 새 지적은 고치거나 `NOLINTNEXTLINE` + 이유(진단이 붙는 줄 바로 위). 끈 검사의 근거는 `.clang-tidy` 에 있다. `performance-enum-size`(열거형 폭에
  ABI · 직렬화가 달렸다)와 `performance-no-int-to-ptr`(Win32 API)는 기각했다.
- **게이트는 고치지 않는다** — include 순서 · namespace 블록의 고치기는 `fixer/FormatIncludeOrder` · `FormatNamespaceBlocks`(규칙은 게이트 파일 한 자리). CMake 들여쓰기는
  `FormatCmakeIndent`(탭, 문자열 안 · vcpkg 툴체인 영역 제외) — 다른 묶음을 받은 뒤 공백 충돌이 나면 `--all` 을 다시 돌린다.
- **설정 참조 문서(`docs/Config/`)는 생성물이다** — `GenerateConfigReference.py` 가 PROPERTY · `ConfigKeyDoc` · 전역 변수 · `ArgumentList.xxx` ·
  CMake 옵션 · `*.settings.xml` 에서 만들고 `CheckConfigReference` 가 낡음 · 목록 밖 설정 파일 · 빈 설명을 막는다. 파서는 선언 모양을 읽으므로 칸 대조는
  `ConfigReferenceTest.FieldsMatchReflection` 이 리플렉션으로 한다. 새 설정 파일 = `ConfigCatalog.py` 한 줄 + 시험 표 한 줄. PROPERTY · gv · 명령줄 ·
  `SW_*` 옵션을 더하는 커밋은 같은 커밋에서 생성기를 다시 돌린다(커밋 훅이 알린다).
- **린트 CTest 는 프리셋이 4 개씩 동시에 돌린다**(`Ninja-Debug-lint` 의 `execution.jobs`) — 린트끼리 공유하는 출력이 없어야 한다: 셀프테스트는 `mkdtemp`,
  게이트는 읽기만. 새 린트가 저장소 안에 파일을 쓰면 이 전제가 깨진다. 8 · 16 은 4 와 같거나 느리다(`CheckCodeConventions` · `CheckLintsAreAlive` 가 스스로 여럿을 쓴다).
- **커밋 훅은 트리 전체 게이트(`preCommitFileArgument = ""`)를 하위 프로세스로 먼저 띄운다**(`GateRunPlan.bBackground`) — 파일 하나 커밋 9.6 → 4.3 s(부하 중).
  파일 단위 게이트는 이 프로세스에서(기동 0.1~0.3 s 가 게이트보다 비싸다). 훅의 바닥 시간은 가장 긴 트리 전체 게이트다 — 새 게이트는 가능하면 `--files` 를 받게 짓는다.
- **린트 시간은 `RunLintSuite` 가 잰다**(CI 린트 잡 · 손으로 `py -3 -m Scripts lint-suite`): CTest 와 같은 목록 · 인자를 빌드 폴더 없이 돌리고, CTest TIMEOUT 의 절반을
  넘긴 린트와 훅 표본(staged 1 · 10)이 `kHookBudgetSeconds` 를 넘으면 경고, 기록은 CI 아티팩트 `lint-timing`. 새 린트의 `timeoutSeconds` 는 이 PC 시간의 3~4 배로 —
  절반 경고가 먼저 울리게.
- **파이썬 도구의 단위 시험은 `Test/PythonTest/Test*.py`** — 파일을 놓으면 CTest 항목(`PythonTest_<이름>`, `nogpu`)이다. Blender 애드온처럼 바깥 모듈(bpy)을
  쓰는 것은 그 import 를 한 파일에 가두고 나머지를 시험한다(`TestBlenderExporter` 가 빈 패키지 모듈을 세워 읽는다).
- **주석 정리에서 마커(예전 · 날짜 · 백로그)로만 뽑으면 과거형 경위("~를 각자 들고 있었습니다")가 영역마다 ~10 % 남는다** — `(었|았|였)(는데|다|습니다)` 로 한 번 더 훑는다.
  빌드가 도는 동안 헤더를 고치면 PCH 크기 불일치("modified since the precompiled header")로 빌드가 진다 — 편집과 빌드를 겹치지 말 것.
- **`git mv` 로 옮긴 시험 파일은 pre-commit 의 `CheckIncludeOrder` · `CheckTestSuites` 가 "변경 없음" 으로 건너뛴다** — 옮긴 뒤에는 `ctest -L lint` 로 확인할 것.
- **같은 클래스가 `#if` / `#else` 로 헤더에 두 번 있으면 `CheckCodeConventions` 의 헤더 기본값 검사가 그 클래스를 건너뛴다** — D3D11 · D3D12 비Windows 스텁을 지우자
  숨어 있던 위반 9 건이 드러났다. 다른 플랫폼 스텁이 있는 헤더도 같은 사각일 수 있다.
- **패딩은 `RunPaddingReport.py`(libclang + 컴파일 DB 플래그) 로 본다** — clang-cl(MS ABI)은 `-Wpadded` 를 내지 않고 `-fdump-record-layouts` 는 필드 위치를 안 준다.
  libclang 에는 `-resource-dir` 를 직접 줘야 한다(안 주면 MSVC `offsetof` 가 상수식이 아니어서 constexpr 표가 오류로 무너진다). 줄인 타입의 회귀는 "크기 ≤ 필드 합을
  정렬로 올린 값" static_assert 로 막는다(DrawCandidate · SpriteAnimatorComponent).
- **생성자 초기화는 `Style/ConstructorInitializesEveryField` 가 막는다** — 기본값 없는 스칼라 · 포인터 · 열거형 · atomic · 비트필드만 대상(컨테이너 · 문자열은 스스로 초기화).
  MSVC STL 은 atomic 을 값 초기화해 Windows 시험만으로는 빠뜨림이 안 드러난다.
- **픽서는 UTF-8 로 못 읽는 파일을 고쳐 쓰지 않는다**(`errors="ignore"` 로 읽고 다시 쓰면 그 바이트가 사라진다 — `PythonTest_TestLintFixer`).
  대상 파일 고르기는 `common.resolveFileArguments` 한 규칙 — 게이트 · 픽서 모두 `--files`(위치 인자 없음), 게이트의 파일 읽기 창구는 `LintGate.readFiles`.
- **린트의 제외 폴더 비교는 저장소 아래 경로의 폴더 이름으로** — 절대 경로 부분 문자열로 비교하면 경로에 "build" 가 든 워크트리에서 파일을 하나도 안 본다
  (실제로 그랬다). 짓지 않는 소스는 CMake 가 `sw_declareUnbuiltDirectory` 로 적고 `CheckSourceGlob` 은 그 목록만 본다.
- **쿠킹** — `CookAssets` 는 App 에 의존하고 Shipping 에서는 `all` 에 든다. App 경로는 CMake 가 `--app $<TARGET_FILE:App>` 로 넘긴다(빌드 폴더를 뒤지면 다른 프리셋의 낡은 App
  을 집는다). 산출물은 `build/<preset>/Cooked/`. 소스 트리에 남은 옛 `.bin` 은 경고와 함께 팩에서 빠진다 — 지운다. 팩 코덱은 `Config/Engine/PackConfig.json`(LZ4 · Zstd 는 pip
  선택 의존성, 없으면 그 자리에서 멈춘다 — zlib 으로 조용히 물러나면 안 된다), 팩 계약 SSOT 는 `Config/Engine/PackFormat.json`(고치면 configure) · 파이썬 `PackFormatSpec`
  (`PackStruct.pack()` 은 필드 **이름으로만**).
- **경고를 재기 전에 그 프리셋을 한 번 빌드한다**(생성 헤더 `FlagOps.gen.h` 가 없으면 `-fsyntax-only` 가 가짜 오류 32 건). 파일을 옮긴 뒤 옛 compile DB 면 "no such file".
  주석만 바뀐 TU 는 sccache 가 캐시를 재생해 `-Wdocumentation` 이 안 보인다 — `RunBuildWarnings.py --preset Ninja-Debug`. 리눅스 전용 경고는 Windows 의 `RunBuildWarnings` 가 못 본다
  (WSL-Debug 로그). 한 플랫폼에서만 읽는 필드는 `[[maybe_unused]]`.
- **include 를 지울 때는 단독 컴파일로 확인한다**(PCH 가 가린다). `.xxx` X-매크로 include 는 빼도 컴파일되지만 함수 본문이 빈다 — 기계로 지우지 말 것. 헤더 안 `= default`
  소멸자가 `unique_ptr<T>` 멤버를 파괴하면 전방 선언으로는 안 선다.
- **전방 선언 후보의 이득은 `ninja -t deps` 로 전후를 센다**(그 헤더에 의존하는 오브젝트 수). `RunForwardDeclarationCandidates` 후보 40 건 중 실제로 준 것은 13 건이었다 —
  짝 `.cpp` 하나뿐인 후보는 include 가 그 `.cpp` 로 옮겨 갈 뿐이라 0, 값으로 거쳐 받던 헤더 · 인라인 멤버 접근 · 인라인 생성자의 `unique_ptr` 소멸자는 깨진다.
  강제 include `FlagOps.gen.h` 의 `*.gen.h` 는 `Core/Common/BitFlagTrait.h`(`<type_traits>` 만)만 든다 — 여기에 무엇을 더하면 모든 TU 의 누락이 가려진다.
- **헤더 자립은 정기 실행 + 커밋 훅이 나눠 막는다** — CI `header-self-contained.yml`(매일 · 수동)이 `RunHeaderSelfContained --fail-on-violation` 으로 트리 전체를,
  게이트 `CheckHeaderSelfContained` 가 커밋 훅에서 빌드 폴더가 있을 때 staged 헤더만 본다(`ctestSkipReason` 으로 CTest 린트에서 빠진다 — 전 트리 3~10 분). 판정은
  `common/HeaderSelfContained.py` 한 자리. 유니티 빌드(CI 프리셋)의 컴파일 DB 는 TU 가 빌드 폴더 안이라 `CMakeFiles` 앞을 소스 경로로 옮겨 씨앗 TU 를 고른다(안 그러면
  모든 헤더가 첫 TU 의 플래그를 받는다). 구성만 한 폴더(`FlagOps.gen.h` 자리 표시자)는 검사 불가로 친다 — 가짜 오류 수십 건.
- **`CheckCodeConventions` 알아 둘 것** — 명명 판정은 `kMapContainerVocabulary` × `kMapNamingSubject` 표 하나. `Style/BitfieldBoolean` · `Naming/DuplicateInternalHelper` ·
  `Style/HeaderMemberInitializer` 는 전체 스캔에서만 돈다. `Naming/OutParameter` 는 `out` 이 든 지역 변수(`arrOutput`)를 오탐한다. 게이트는 파일을 동시에 훑으니 규칙
  객체에 상태를 들지 말 것. 자기 시험 조각은 그 검사가 **통과하는** 바탕(`_kCleanFixture`) 위에 위반 하나만 얹는다.
  `Style/ConstructorOrder` 는 헤더에서 읽은 멤버 순서와 비교하므로, 멤버 선언을 못 읽으면(예전엔 `Widget* const* _ppWidget`) 순서가 맞아도 위반으로 건다 —
  멤버 정규식 `_kClassMemberRe` 를 넓히면 `_kWholeScanCleanCase` 의 `RangeTable` 조각으로 확인한다.
  교차 파일 검사(`Style/BitfieldBoolean` · `Naming/Duplicate*` · `Style/HeaderMemberInitializer` · `Style/ConstructorInitializesEveryField`)는 파일별 몫을 워커가
  `CrossFileFacts` 로 모으고 부모는 합치기만 한다 — 새 교차 파일 검사도 그 모양으로(부모에서 파일을 다시 읽으면 그것이 게이트 시간의 2/3 였다).
- **린트 정규식에 `(식별자+ … \s*)+` 모양을 쓰지 말 것** — 빈 구분자로 식별자를 몇 조각으로든 나눌 수 있어 맞지 않는 줄에서 역추적이 지수로 는다(한 줄 7 초,
  커밋 훅이 부하에서 수십 분). 식별자 뒤에 `(?![A-Za-z0-9_:])` 를 붙인다. 느린 게이트는 파일별 시간부터 정렬해 볼 것 — 평균이 아니라 몇 파일이 지배한다.
  줄 규칙의 `"글자" in line and 정규식` 앞 검사는 그 정규식이 반드시 품는 글자다 — 정규식을 바꾸면 같이 본다.
  글자마다 도는 파이썬 루프는 트리 전체에서 초 단위다(`CheckNamespaceBlocks` 의 가리기 — 정규식 `sub` 로 바꾸자 직렬 CPU 8.8 → 2.1 s) — 덩어리는 정규식으로 찾고,
  루프는 반응하는 글자만(`findall(r"[{};]")`) 돈다. 덩어리 글자 집합 + 꼬리 패턴 정규식(`[A-Za-z0-9_./\-]+\.ext`)은 `(?<![집합])` 로 덩어리 첫 글자에서만
  시작하게 — 없으면 모든 자리에서 끝까지 먹고 되돌아온다(`AssetValidation` 의 참조 토큰, 꼬리 거르기와 함께 2.8 → 1.3 s).
- **`CheckIncludeOrder` 는 첫 `#if` 를 경계로 삼는다.** include 가 전부 `#if` 안인 파일(`DelayLoadNotifyHook.cpp` · `PlatformOsHeaders.h` · `X11MacroUndef.h`)과 새 플랫폼 전용
  `.cpp` 는 손으로 순서를 지킨다(Core → Engine).
- **`CheckFunctionVocabulary` 는 헤더 선언만 본다**(호출부를 보면 `vk*KHR` 를 잡는다). 대문자 규칙은 "셋 이상은 어디서든, 둘은 이름 끝에서". `hashed_string` 은 리터럴에서만
  암묵 변환, `string_view` 판과 `const hashed_string&` 판을 함께 두면 NamePair 위반, `setX` 의 게터는 `getX`(BareGetter), `calculate` · `calc` · `init*` 축약 금지(`initRHI` 예외).
- **`FormatBranchBraces` 의 case 규칙** — `break;` 도 한 문장, 본문에 전처리기 지시문이 있으면 손대지 않는다. 플랫폼 전용 파일이나 `#if` 가 든 코드를 텍스트로 변환했으면 그
  플랫폼에서 빌드한다. clang-format `RemoveBracesLLVM` · `InsertBraces` 는 켜지 말 것(반복문까지 벗긴다).
- **일괄 이름 바꾸기는 파일 범위를 정한 규칙 표로**, 문자열 · 문자 리터럴(직렬화 키 · 로그 문구)은 건드리지 않는다. 새 이름 충돌은 `-Wshadow` 가 잡는다. `Win32Window.cpp` 는
  리눅스에서도 컴파일된다(스텁 구간) — 창 API 이름을 바꾸면 스텁 · X11 · Cocoa 까지.
- **Scripts 규칙** — 폴더는 하는 일로(`generate/` 생성기 · `setup/` 외부 도구 · `lint/*`), 모두 `common` 만 import(`common` → `setup` 역방향 금지). `common` 은 무거운 표준
  모듈을 쓰는 함수 안에서 import 한다. 동시 처리는 `Scripts/common/Parallel.py` 한 자리 — 파일 읽기 · 하위 프로세스 대기는 스레드, 정규식이 무거우면 `flatMapInProcesses`
  (코어 수만큼의 덩어리). `App.exe` 찾기 · 실행은 `Scripts/common/AppBinary.py` 하나. 파일을 한 번 읽어 나눠 쓰는 캐시는 CRLF 를 LF 로 바꿔야 결과가 같다.
- **상수의 자리는 `AGENTS.md` "Constants" 절** — 한 TU 는 Internal, 모듈은 소유 타입, 계약은 계약 헤더(`RHITypes` · `bindingslots.hlsli` · `Defines`), 잘 알려진 값은 집 하나
  (`CheckWellKnownConstants`), 반복은 `RunRepeatedConstants.py` 로 본다. "X 와 같아야 한다" 주석이 달린 사본이 결함의 모양이다(루트 상수 16/64, 모프 배치 넷).
  게임플레이 튜닝 값은 컴포넌트 `PROPERTY` · 키트 설정 구조체 칸, 중력은 `PhysicsSystem::getConfiguredGravity` 하나.
- **커밋 훅은 staged 셰이더가 있으면 `App --cook-shaders` 결과(바이너리 · `cook.stamp`)를 자동으로 stage 한다** — 도우미처럼 바이너리를 커밋하지 않을 때는
  커밋 뒤 그 경로를 빼고 `--amend` 한다(셰이더 소스가 없는 amend 는 훅이 다시 굽지 않는다).
- **문서의 경로는 `CheckDocPaths` 가 본다** — 상대 링크 · 제목 앵커 · 백틱 안 저장소 경로 · 코드 블록의 `#include` · `py -3 Scripts/…`,
  그리고 모든 README · `docs/*.md` 가 `docs/02_DocumentMap.md` 에 있는지. 파일을 옮기거나 지우는 커밋은 .md 를 건드리지 않아 훅이 이 게이트를 돌리지 않으므로
  `ctest -L lint` 가 잡는다. CI 는 문서만 바뀐 push 에도 린트 잡을 돌리고 빌드 잡만 건너뛴다(`ci.yml` 린트 잡의 `changes` 단계).
  자리만 보이는 예시는 `<게임>` 처럼 꺾쇠로 쓰고(게이트가 경로로 읽지 않는다), 아직 없는 파일은 백로그에만 적는다(백로그는 링크만 본다).
- **`#!` 스크립트는 git 모드 100755 로 커밋한다**(`CheckExecutableBits`) — Windows 에서 만든 파일은 100644 로 들어가 리눅스에서만 `Permission denied` 가 난다
  (오버레이 포트 `openssl/unix/configure` 가 리눅스 CI 다섯 잡을 Configure 에서 세웠다). 새 스크립트는 `git update-index --chmod=+x <경로>`.
- **"헤더 혼자 빼도 선다" 는 "아무도 안 쓴다" 가 아니다** — `RunForwardDeclarationCandidates --verify-unused` 가 고른 120 건을 지우자 소비자 TU 에서 오류 2790 개가
  났다(거쳐 받던 `RHIBackend` · `IRHIResourceFactory`, NOMINMAX 가 `windows.h` 보다 먼저 오던 순서가 깨져 `std::max` 가 매크로에 먹힘). 보고는 "후보" 로만 쓰고, 지울 때는
  그 헤더를 include 하는 TU 전부를 다시 지어 본다.
