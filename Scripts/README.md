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

```
Scripts/
  ├── common/                         # [Core 재사용 계층]
  │     ├── Constants.py              # 경로, 파일명, JSON 키 단일 진실 공급원 (SSOT)
  │     ├── Paths.py                  # 경로 정규화, OS별 확장자/쉘 명령어 공통화
  │     ├── Config.py                 # JSON 설정 읽기/쓰기/병합 및 엄격 검증
  │     ├── ToolLocator.py            # 선언적 ToolSpec 기반 5단계 도구 탐색 프레임워크
  │     ├── AssetPipeline.py          # 멀티스레드 에셋 쿠킹 & 원자적 바이너리 변경 감지(writeBinaryIfChanged)
  │     ├── Archive.py                # 다운로드 캐시, 해시 검증, 안전한 압축 해제
  │     └── Host.py                   # Git 연동 및 clang-format 배치 실행
  │
  ├── generate/                       # [빌드/에셋 생성 계층]
  │     ├── CookAssets.py             # ★ Prefab, Scene, Resource Pack을 일괄/선택 쿠킹하는 단일 통합 쿠커
  │     ├── BakeShippingHostDefaults.py # 런타임 JSON -> C++ 헤더 베이킹
  │     └── GenerateDocs.py           # Doxygen 레퍼런스 생성
  │
  ├── setup/                          # [환경 구성 계층]
  │     ├── SetupEnvironment.py       # 통합 환경 설정 (ToolLocator 기반)
  │     ├── SetupLlvm.py              # LLVM/libclang 부트스트랩 (kLlvmToolSpec 적용)
  │     ├── SetupVcpkg.py             # vcpkg 부트스트랩 (kVcpkgToolSpec 적용)
  │     ├── HostTools.py              # MSVC, WinSDK, Ninja, Sccache 탐색
  │     ├── GenerateCMakeConstants.py # CMake 상수 자동 생성
  │     ├── InstallGitHooks.py        # Git pre-commit 훅 설치
  │     └── AddDefenderExclusions.py  # Windows Defender 빌드 폴더 예외 등록
  │
  ├── lint/                           # [정적 검사 및 코드 스타일 계층]
  │     ├── PreCommitLint.py          # Git Staged 대상 사전 커밋 종합 검사
  │     ├── RunClangFormat.py         # clang-format 자동 포맷팅
  │     ├── CheckCodeConventions.py   # C++ 엔진 코딩 컨벤션 정적 검사
  │     ├── CheckEngineLayers.py      # 아키텍처 레이어 침범 검사
  │     ├── CheckIncludeOrder.py      # 인클루드 순서 및 중복 검사
  │     ├── CheckResourceCasing.py    # 리소스 소문자 명명 강제 검사
  │     └── CheckSourceGlob.py        # CMake GLOB 소스 누락 검사
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
py -3 Scripts/lint/CheckEngineLayers.py
py -3 Scripts/lint/RunClangFormat.py
py -3 Scripts/generate/BakeShippingHostDefaults.py build/generated/sw/config/ShippingHostDefaults.h
```

`SetupLlvm` / `SetupEnvironment` 는 최소 LLVM 키트에 `clang-format` 을 포함·보완합니다 (기존 키트에 없으면 캐시된 LLVM tar에서 bin만 추출).
