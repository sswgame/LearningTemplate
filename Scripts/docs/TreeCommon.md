# Scripts 파일 목록 — common · setup · generate

[목차](../README.md)에서 나눈 문서. 규칙은 [이름 · 자리 규칙](Layout.md). 아래 트리는 `Scripts/` 전체 트리를 폴더별로 끊은 첫 조각이다.

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
  │     ├── CodeText.py               # C++ · HLSL 글 읽기(게이트 공용) — 주석 · 리터럴 가리기(blankComments · blankMatch), 줄 번호(lineOf),
  │     │                             #   include 줄 훑기(iterIncludes), 층 폴더(firstFolderAfter), include → 저장소 파일(IncludeResolver)
  │     ├── ConfigCatalog.py          # 설정 파일 목록(층 · 읽는 곳 · 언제 · 배포본) — docs/Config 생성기와 CheckConfigReference 가 같이 읽는다
  │     ├── ConfigReference.py        # 설정 참조 문서(docs/Config)를 코드에서 만든다
  │     ├── AppRun.py                 # App 한 판 — 출력 모으기 · 못 도는 백엔드 판정 · 프로파일 표 읽기 · 밖에서 메모리 · 핸들 재기(qa/ 셋이 쓴다)
  │     ├── AssetValidation.py        # 에셋 검증 규칙 — 규칙 표(Config/Editor/AssetValidationRules.json)의 `check` 이름이 고르는 연산자들
  │     ├── EditorIconFont.py         # 에디터 아이콘 99 개의 그리기 함수 + 표준 라이브러리만으로 TrueType 폰트 · C++ 헤더 쓰기
  │     ├── MarkdownSections.py       # 마크다운을 제목 절로 가르는 유일한 구현 — plans --section · doc 이 쓴다
  │     ├── Plans.py                  # 계획 문서(docs/plans)의 머리 줄 · 후보 절 읽기 — 목록 명령(dev/ListPlans.py)이 쓴다
  │     ├── StatusBoard.py            # 진행 상황 표 만들기 · 표지 사이 바꾸기 — 명령(dev/RunStatusBoard.py)과 게이트(CheckStatusBoard)가 같이 쓴다
  │     ├── ReflectionAnnotationKeys.py # 편집기용 리플렉션 어노테이션 키 헤더 내용(AnnotationMeta.txt · 단위 표에서) — 생성기와 게이트가 같이 쓴다
  │     ├── ImageMetrics.py           # 스크린샷 비교 — PPM · PNG 읽기/쓰기, 축소, 배경을 뺀 지표 · 잡음 바닥에서 정한 허용 오차
  │     ├── XmlAssetMerge.py          # XML 에셋 의미 비교 · 3-way 병합(엔티티 id · 컴포넌트 · 속성 단위), 엔진 저장기와 같은 서식으로 쓰기
  │     ├── AssetPipeline.py          # 쿠커의 기본 출력 폴더 찾기 — 가장 최근에 구성된 build/*/Bin/<subDir>
  │     ├── CookContract.py           # 쿠킹 표(`Config/Engine/CookContract.json`)를 읽은 결과 — 헤더 생성기 · 쿠커 · 게이트가 같은 객체를 쓴다
  │     ├── RuleData.py               # 린트 규칙 데이터(`lint/rules/*.toml`) 읽기 · 스키마 검사 · 파이썬 3.10 용 TOML 부분 집합 읽기([Lint.md](Lint.md) "규칙 데이터")
  │     └── PackFormat.py             # `.pack` 바이너리 계약(Config/Engine/PackFormat.json)을 읽은 결과
  │
  ├── setup/                          # [환경 구성] 외부 도구를 찾고, 없으면 받아 설치한다
  │     ├── SetupEnvironment.py       # 통합 환경 설정 (ToolLocator 기반) → toolchain_config.json
  │     ├── SetupLlvm.py              # LLVM/libclang 부트스트랩 (kLlvmToolSpec 적용)
  │     ├── SetupVcpkg.py             # vcpkg 부트스트랩 (kVcpkgToolSpec 적용)
  │     ├── SetupLinuxDevEnvironment.py # Linux · WSL 홈 디렉터리 설정
  │     ├── HostTools.py              # MSVC, WinSDK, DXC, system include 탐색 (라이브러리 — SetupEnvironment 가 쓴다)
  │     ├── InstallGitHooks.py        # Git pre-commit 훅(린트) · pre-push 훅(main 으로 가는 트리의 전체 빌드 증거) 설치
  │     └── AddDefenderExclusions.py  # Windows Defender 빌드 폴더 예외 등록
  │
  ├── generate/                       # [생성] 원본(JSON · Constants.py · 폴더 목록)에서 파일을 만들어 낸다 — 대부분 구성 · 빌드가 부르고,
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
  │     ├── GenerateEngineAbiStamp.py # Core · Engine 헤더 지문 → 핫 리로드 ABI 스탬프 헤더
  │     ├── GenerateEditorIcons.py    # 에디터 아이콘 폰트 · 글리프 상수 헤더(그림은 common/EditorIconFont.py) — 손으로 돌리고 결과를 커밋한다
  │     ├── GenerateReflectionAnnotationKeys.py # 편집기용 어노테이션 키 헤더(ReflectionAnnotationKeys.h) — 손으로 돌리고 결과를 커밋한다
  │     ├── GenerateSpriteTextures.py # 엔진 스프라이트 텍스처(DDS) · 클립 — 손으로 돌리고 결과를 커밋한다
  │     └── GenerateDocs.py           # Doxygen 레퍼런스 생성
```
