# Scripts 파일 목록 — lint

[목차](../README.md)에서 나눈 문서. 규칙은 [이름 · 자리 규칙](Layout.md). 아래 트리는 `Scripts/` 전체 트리의 `lint/` 조각이다.

```
  ├── lint/                           # [정적 검사 및 코드 스타일] — 폴더가 곧 성격이다
  │     ├── LintGate.py · LintFixer.py · LintReport.py # 게이트 · 픽서 · 보고서 하나 = 클래스 하나 (껍데기는 기반이 든다)
  │     ├── LintCatalog.py            # gate/ · selftest/ 를 훑어 "무엇이 있고 어떻게 돌리는가" (CMake · 훅이 읽는다)
  │     ├── SourceScan.py             # 소스 게이트의 공유 훑기 — 트리 한 번 걷기 · 파일 한 번 읽기 · 주석 한 번 가리기, 파일을 코어 수만큼 프로세스로
  │     ├── RunSourceGates.py         # 소스 게이트 전부를 한 프로세스로 (CTest · CI 린트 잡의 실행 단위 `CheckSourceFiles`)
  │     ├── AcronymRegistry.py        # 약어 레지스트리 — 약어 · 줄임말 · 제품 이름 · 남의 이름 표와 철자 판정 (게이트 · 코드모드가 같이 읽는다)
  │     ├── PreCommitLint.py          # Git Staged 대상 사전 커밋 종합 검사 (넷을 조율하므로 여기 남는다)
  │     │                             #   병합 커밋은 어느 부모와도 내용이 다른 파일만 파일 단위로 본다 ([Lint.md](Lint.md) "커밋 훅과 병합 커밋")
  │     ├── RunLintSuite.py           # 린트 전체를 빌드 폴더 없이 돌려 린트마다 · 커밋 훅 시간을 측정한다 (CI 린트 잡)
  │     ├── conventions/              # CheckCodeConventions 의 규칙 묶음(라이브러리 — 묶음 지도는 __init__.py). LintCatalog 는 훑지 않는다
  │     ├── rules/                    # 규칙 데이터 — `<게이트 · 레지스트리 이름>.toml`(예외 표 `[exemption]` · 티어 · 어휘 · 허용 목록). 코드는 없다
  │     ├── gate/                     # 위반이 있으면 **실패한다** — 빌드와 커밋을 막는 건 이 폴더뿐
  │     │     ├── CheckCodeConventions.py     # C++ 엔진 코딩 컨벤션 — 진입점 · 파일 고르기 · 보고(규칙은 lint/conventions/, 맨이름 `out` 도 여기)
  │     │     ├── CheckLoopBraces.py          # 본문에 중괄호가 없는 for · while
  │     │     ├── CheckConditionParts.py      # if · while 조건이 && · || 로 세 부분 이상이면 이름 붙이기 (훅은 추가한 줄만 · 전체는 rules 기준선 0)
  │     │     ├── CheckEditorStyleLiterals.py # 에디터 코드의 숫자 리터럴 색 · 스타일 값(IM_COL32 · ImVec4 · PushStyleColor/Var)은 테마 토큰으로 (훅은 추가한 줄만 · 전체는 rules 기준선 이하)
  │     │     ├── CheckDiscardReason.py       # 실패 가능 함수 결과를 (void) 로 버리는 자리의 이유 한 줄
  │     │     ├── CheckSuppressionReasons.py  # NOLINT · diagnostic ignored · CMake 경고 끄기의 검사 이름과 이유
  │     │     ├── CheckProductNames.py        # 제품 이름 식별자는 드라이버 · 제공자 · 백엔드 · 플랫폼 폴더 안에서만
  │     │     ├── CheckEditorIcons.py         # 에디터 아이콘 폰트 · 글리프 헤더가 그림 정의와 같은지
  │     │     ├── CheckReflectionAnnotationKeys.py # 편집기용 어노테이션 키 헤더가 AnnotationMeta.txt · 단위 표와 같은지
  │     │     ├── CheckResourceCredits.py     # Resource 원본 자산이 도메인 credits.md 에 있고 라이선스가 CC0 1.0 · 이 저장소인지
  │     │     ├── CheckFunctionVocabulary.py  # 함수 이름 어휘 (한 개념 한 동사 · 대문자 묶음은 레지스트리 약어)
  │     │     ├── CheckAcronymSpelling.py     # 약어 철자 (레지스트리 kEnforced · --enforce 약어만 막고 나머지는 숫자로만 — 고치기는 fixer/FormatAcronymSpelling.py)
  │     │     ├── CheckIncludeOrder.py        # 인클루드 순서·중복 (검사만 — 고치기는 fixer/FormatIncludeOrder.py)
  │     │     ├── CheckEngineLayers.py        # 아키텍처 레이어 침범
  │     │     ├── CheckCoreLayers.py          # Core 폴더 티어(Common → … → LogSink), 아래 층의 pch 로그 사용
  │     │     ├── CheckEditorLayers.py        # Editor 폴더 층(Common → Workspace → Commands → GUI → Viewport · Popups → Panels → SelfTest → 셸)
  │     │     ├── CheckCoreNetworkLayers.py   # Core/Network 폴더 층(뿌리 ← Transport · Security ← Connection ← Message ← Replication)
  │     │     ├── CheckNullableServiceUse.py  # nullptr 가능 서비스 조회를 확인 없이 역참조
  │     │     ├── CheckServiceAccess.py       # 서비스 접근은 getSubsystem / findSubsystem 한 어휘 · 헤더에 새 전역 단일체(getInstance) 금지
  │     │     ├── CheckControlBoundary.py     # 입력을 읽는 파일은 플레이어 조종자 · 플레이어 뷰 · 명령 디렉터만(폰은 의도만)
  │     │     ├── CheckLogViewArgument.py     # 로그 인자의 string_view::data()
  │     │     ├── CheckGlobalVariableKinds.py # 전역 변수 정의와 extern 참조의 종류 일치
  │     │     ├── CheckNameReferences.py      # 실행 인자 · 시나리오의 -gv_ 이름과 코드가 부르는 입력 액션 · 오디오 이벤트 · 시나리오 단계 · 탐침 · 개발 명령 이름이 정의돼 있는지
  │     │     ├── CheckRenderOwnership.py     # 렌더 스냅샷 소유 규칙
  │     │     ├── CheckTestSuites.py          # 스위트 명명 · 한 파일 한 스위트 · CI 경계 표식 · CoreTest 는 엔진을 직접 쓰지 않음
  │     │     ├── CheckFallibleNodiscard.py   # 실패를 bool 로 알리는 함수 선언의 `[[nodiscard]]`
  │     │     ├── CheckSourceGlob.py          # CMake GLOB 소스 누락 (짓지 않는 소스는 CMake 가 적은 UnbuiltSources.txt 로만 안다)
  │     │     ├── CheckDataFileReferences.py  # 아무도 include 하지 않는 죽은 데이터 파일
  │     │     ├── CheckNoDelayLoad.py         # /DELAYLOAD · delayimp 금지(lld 20 지연 로드 썽크가 첫 호출의 float 인자를 망가뜨린다)
  │     │     ├── CheckPostTickThisBinding.py # 틱 뒤 실행에 this 메서드 델리게이트 금지(핸들로 다시 찾는 executeOrDeferPostTickOnComponent 로)
  │     │     ├── CheckResourceCasing.py      # 리소스 소문자 명명
  │     │     ├── CheckTextureFolders.py      # 런타임 textures/ 에는 DDS 만, 원본 이미지는 textures_raw/ 에만
  │     │     ├── CheckRuntimeResourceDiskAccess.py # GameFramework · Games 는 리소스를 디스크로 훑거나 풀지 않고 ResourceIO 를 쓴다
  │     │     ├── CheckClockReads.py          # std::chrono 시계가 아니라 MonotonicClock · Stopwatch · Deadline
  │     │     ├── CheckStdThread.py           # Source 의 스레드는 std::thread 가 아니라 sw::thread(합류 대기 관찰 · 로더 락 안 join 단언)
  │     │     ├── CheckExportMacros.py        # GameFramework 헤더의 내보내기 매크로(Base · 키트 · 서버 키트)와 순수 인터페이스에 매크로 금지
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
  │     │     ├── CheckAssetRules.py          # 에셋 검증 규칙의 오류 심각도(이름 · 텍스처 · 메시 예산 · 참조 · 머티리얼 · 컴포넌트 · id · guid · 팩 규칙 · 컴포넌트 이름 참조)
  │     │     ├── CheckTargetMacros.py        # 플랫폼 · 아키텍처 · 컴파일러를 SW_* 매크로로만 묻기 (컴파일러 내장 매크로 금지)
  │     │     ├── CheckX11Isolation.py        # X11 헤더는 X11 구현 .cpp 에서만 · 뒤에 매크로 지우기 · 서드파티와 한 TU 에 두지 않기
  │     │     ├── CheckCookContract.py        # 쿠커가 쿠킹 표대로 고르는지
  │     │     ├── CheckShaderConventions.py   # HLSL 명명 규칙(docs/agents/NamingScripts.md 의 HLSL 절)
  │     │     ├── CheckShaderStamps.py        # 커밋된 셰이더 바이너리가 지금 소스에서 나온 것인지 쿠킹 표의 모든 백엔드에서(빌드 없이 cook.stamp 로)
  │     │     ├── CheckCmakeConventions.py    # CMake 명명 규칙
  │     │     ├── CheckCmakeReadme.py         # cmake/README.md 가 가리키는 파일 · 함수가 실재하는지
  │     │     ├── CheckDocPaths.py            # 문서(.md)의 링크 · 앵커 · 저장소 경로가 실재하는지, 모든 README 가 문서 지도에 있는지
  │     │     ├── CheckDataDrivenStructure.py # 코드의 에셋 경로 · 조용한 폴백 · 버린 읽기 결과가 기준선(rules 의 [baseline])을 넘지 않는지 — --summary 가 진척 표
  │     │     ├── CheckStatusBoard.py         # README 의 진행 상황 표가 계획 문서 · 로드맵 · 후보 문서에서 만든 결과와 같은지
  │     │     ├── CheckPythonConventions.py   # 파이썬 이름 · 모양 규칙
  │     │     ├── CheckScriptEntryPoints.py   # 진입점이 모듈 수준에서 common 을 import 하는지(콘솔 UTF-8) · main(argv) 로 인자를 받는지
  │     │     ├── CheckScriptCommonHelpers.py # common 의 한 자리(runProcess · BuildTree · writeGeneratedFile · 콘솔)를 비켜 가는 호출
  │     │     ├── CheckScriptLayout.py        # 폴더마다 파일 이름 앞머리 · 린트 기반 클래스([Layout.md](Layout.md) Layout 표)
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
  │     │     ├── RunEngineLayerGraph.py      # Engine 폴더 간 include 그래프 · 강결합 묶음 · 티어 (`--apply` 는 티어 표 둘을 고쳐 쓴다)
  │     │     ├── RunCoreLayerGraph.py        # Core 폴더 간 include 그래프 · 묶음 · 거꾸로 가는 include(파일 단위)
  │     │     ├── RunFolderFileCount.py       # 너무 큰 평면 폴더 · 파일 하나짜리 폴더
  │     │     ├── RunFunctionSize.py          # 함수마다 본문 줄 · 중첩 · return · 람다 줄 (`--group-depth` 는 영역별 수, 게이트 아님)
  │     │     ├── RunRepeatedConstants.py     # 같은 뜻이 여러 곳에 따로 적힌 상수 · 리터럴
  │     │     ├── RunBuildScriptInventory.py  # 빌드 스크립트 재고 — 죽은 CMake 함수 · 큰 CMake 파일 · 손 목록 · common 을 비켜 간 파이썬 호출
  │     │     ├── RunDiscardReasons.py        # (void) 로 버린 실패 가능 결과의 목록과 이유
  │     │     └── RunDocStyle.py              # 문서마다 읽기 어려운 문장 모양과 조어 수 (기준은 docs/10_WritingDocs.md, `--files` 로 다시 쓰기 전후 비교)
  │     └── selftest/                 # 코드가 아니라 **린트** 를 본다
  │           ├── CheckLintsAreAlive.py       # gate/ 를 훑어 각 게이트가 아직 무는지 확인
  │           ├── CheckFixersAreAlive.py      # fixer/ 가 아직 고치는지, 고치면 안 되는 것은 안 고치는지
  │           ├── CheckCodeConventionsSelfTest.py # CheckCodeConventions 의 규칙마다 아직 무는지 확인
  │           ├── CheckServiceAccessSelfTest.py   # CheckServiceAccess 의 모양마다 잡히는지 · 허용 모양이 안 잡히는지
  │           ├── CheckMergeCommitScope.py    # 병합 커밋에서 훅이 새 내용 파일을 빠뜨리지 않고 줄이는지 (임시 git 저장소)
  │           ├── CheckExemptionTables.py     # 게이트의 예외가 rules/<게이트>.toml 한 자리에만 있는지 · rules/ 파일이 3.10 에서도 같게 읽히는지
  │           ├── CheckLintScanRoots.py       # 게이트가 대상 뿌리를 손으로 조립하지 않는지(kLintTargetRelDirs 하나)
  │           └── CheckReportsRun.py          # report/ 의 보고서가 모두 LintReport 이고 --help 로 뜨는지
```
