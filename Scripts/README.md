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
검사 · 수정이면 `lint/` 의 네 폴더 중 하나, 사람이 가끔 돌리는 실험 도구면 `dev/`. 모두 `common/` 만 import 하고 서로는 부르지 않는다
(`common` 이 위층을 부르면 안 된다 — 2026-09-30 에 하나 있던 것을 걷었다).

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
  │     ├── AppBinary.py              # 빌드된 App 을 찾고 헤드리스로 셰이더를 굽는 자리
  │     ├── AssetPipeline.py          # 멀티스레드 에셋 쿠킹 & 원자적 바이너리 변경 감지(writeBinaryIfChanged)
  │     └── PackFormat.py             # `.pack` 바이너리 계약(Config/Engine/PackFormat.json)을 읽은 결과
  │
  ├── setup/                          # [환경 구성] 외부 도구를 찾고, 없으면 받아 설치한다
  │     ├── SetupEnvironment.py       # 통합 환경 설정 (ToolLocator 기반) → toolchain_config.json
  │     ├── SetupLlvm.py              # LLVM/libclang 부트스트랩 (kLlvmToolSpec 적용)
  │     ├── SetupVcpkg.py             # vcpkg 부트스트랩 (kVcpkgToolSpec 적용)
  │     ├── SetupLinuxDevEnvironment.py # Linux · WSL 홈 디렉터리 설정
  │     ├── HostTools.py              # MSVC, WinSDK, DXC, system include 탐색
  │     ├── InstallGitHooks.py        # Git pre-commit 훅 설치
  │     └── AddDefenderExclusions.py  # Windows Defender 빌드 폴더 예외 등록
  │
  ├── generate/                       # [생성] 정본(JSON · Constants.py · 폴더 목록)에서 파일을 만들어 낸다 — CMake 가 부른다
  │     ├── CookAssets.py             # ★ Prefab, Scene, Resource Pack을 일괄/선택 쿠킹하는 단일 통합 쿠커
  │     ├── BakeShippingHostDefaults.py # 런타임 JSON → Shipping 용 C++ 헤더
  │     ├── GeneratePackFormat.py     # PackFormat.json → C++ 헤더
  │     ├── GenerateCMakeConstants.py # Constants.py → CMake set() 목록
  │     ├── GenerateToolchainCMake.py # toolchain_config.json → CMake set() 목록
  │     ├── GenerateLintTargets.py    # lint/gate · selftest 폴더 → CMake 린트 타깃 · 테스트
  │     ├── GenerateEngineAbiStamp.py # Core · Engine 헤더 지문 → 핫 리로드 ABI 도장 헤더
  │     └── GenerateDocs.py           # Doxygen 레퍼런스 생성
  │
  ├── lint/                           # [정적 검사 및 코드 스타일] — 폴더가 곧 성격이다
  │     ├── LintGate.py · LintFixer.py # 게이트 하나 = 클래스 하나, 픽서 하나 = 클래스 하나 (껍데기는 기반이 든다)
  │     ├── LintCatalog.py            # gate/ · selftest/ 를 훑어 "무엇이 있고 어떻게 돌리는가" (CMake · 훅이 읽는다)
  │     ├── PreCommitLint.py          # Git Staged 대상 사전 커밋 종합 검사 (넷을 조율하므로 여기 남는다)
  │     ├── gate/                     # 위반이 있으면 **실패한다** — 빌드와 커밋을 막는 건 이 폴더뿐
  │     │     ├── CheckCodeConventions.py     # C++ 엔진 코딩 컨벤션 (줄 단위 규칙 하나 = 클래스 하나)
  │     │     ├── CheckFunctionVocabulary.py  # 함수 이름 어휘 (한 개념 한 동사 · 약어는 단어)
  │     │     ├── CheckIncludeOrder.py        # 인클루드 순서·중복 (기본 검사, `--fix` 로 수정)
  │     │     ├── CheckEngineLayers.py        # 아키텍처 레이어 침범
  │     │     ├── CheckEngineServiceBinding.py # 엔진 서비스 표와 바인딩 호스트 대조
  │     │     ├── CheckNullableServiceUse.py  # nullptr 가능 서비스 조회를 확인 없이 역참조
  │     │     ├── CheckLogViewArgument.py     # 로그 인자의 string_view::data()
  │     │     ├── CheckGlobalVariableKinds.py # 전역 변수 정의와 extern 참조의 종류 일치
  │     │     ├── CheckRenderOwnership.py     # 렌더 스냅샷 소유 규칙
  │     │     ├── CheckTestSuites.py          # 스위트 명명 · 한 파일 한 스위트 · CI 경계 표식
  │     │     ├── CheckSourceGlob.py          # CMake GLOB 소스 누락 + RHI 백엔드 목록
  │     │     ├── CheckDataFileReferences.py  # 아무도 include 하지 않는 죽은 데이터 파일
  │     │     ├── CheckResourceCasing.py      # 리소스 소문자 명명
  │     │     ├── CheckCmakeConventions.py    # CMake 명명 규칙
  │     │     ├── CheckCmakeReadme.py         # cmake/README.md 가 가리키는 파일 · 함수가 실재하는지
  │     │     ├── CheckPythonConventions.py   # 파이썬 명명 규칙
  │     │     ├── CheckPythonMinimumVersion.py # CI 의 파이썬에서도 파싱되는지
  │     │     └── CheckTextFilesAreText.py    # 텍스트 파일의 널 바이트
  │     ├── fixer/                    # 파일을 실제로 고쳐 쓴다 (게이트가 아니다)
  │     │     ├── FormatBranchBraces.py       # if 계열 중괄호 (`--check` 면 검사만)
  │     │     ├── FormatForwardDeclarations.py
  │     │     ├── RunClangFormat.py           # clang-format 적용 (`py -3 -m Scripts format`)
  │     │     └── FormatModified.py           # 작업 트리 변경분에 위 셋 + 인클루드 순서
  │     ├── report/                   # 찍어 줄 뿐, 언제나 0 으로 끝난다
  │     │     ├── RunBuildWarnings.py         # 트리에 남아 있는 컴파일러 경고
  │     │     ├── RunClangTidy.py
  │     │     ├── RunHeaderSelfContained.py   # 혼자 서지 못하는 헤더
  │     │     ├── RunForwardDeclarationCandidates.py # 전방 선언으로 바꿀 수 있는 include (`--apply` 는 고쳐 쓴다)
  │     │     ├── RunDuplicateCode.py         # 복사돼 있는 코드 블록 (C++ · `--language py` · `--language cmake`)
  │     │     └── RunEngineLayerGraph.py      # Engine 폴더 간 include 그래프 · 강결합 묶음
  │     └── selftest/                 # 코드가 아니라 **린트** 를 본다
  │           ├── CheckLintsAreAlive.py       # gate/ 를 훑어 각 게이트가 아직 무는지 확인
  │           ├── CheckFixersAreAlive.py      # fixer/ 가 아직 고치는지, 고치면 안 되는 것은 안 고치는지
  │           └── CheckCodeConventionsSelfTest.py # 규칙 30종이 아직 무는지 확인
  │
  ├── dev/                            # [개발 실험] 사람이 가끔 손으로 돌린다 — 빌드 · CI 가 부르지 않는다
  │     ├── BackendSmoke.py           # 네 백엔드로 같은 씬을 그려 SceneColor 를 비교
  │     └── GenerateStressScene.py    # 로드 경로를 재기 위한 큰 씬
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
py -3 -m Scripts format               # C++ 코드 clang-format 자동 포맷팅 (RunClangFormat)
py -3 -m Scripts lint                 # Staged 파일 대상 사전 커밋 린트 검사 (PreCommitLint)
py -3 -m Scripts docs                 # Doxygen API 레퍼런스 문서 생성 (GenerateDocs)
```

## 개별 스크립트 실행

```bash
py -3 Scripts/setup/SetupEnvironment.py
py -3 Scripts/generate/CookAssets.py --all
py -3 Scripts/lint/gate/CheckEngineLayers.py
py -3 Scripts/lint/fixer/RunClangFormat.py
py -3 Scripts/generate/BakeShippingHostDefaults.py build/generated/sw/config/ShippingHostDefaults.h
```

`SetupLlvm` / `SetupEnvironment` 는 최소 LLVM 키트에 `clang-format` 을 포함·보완합니다 (기존 키트에 없으면 캐시된 LLVM tar에서 bin만 추출).
