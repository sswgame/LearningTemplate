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
  common/               # 도메인별 공통 모듈 패키지
    Constants.py        # 프로젝트 불변 경로, 파일명, JSON 스키마 키 (SSOT)
    Paths.py            # 프로젝트 루트, 경로 정규화, 플랫폼 판별
    Config.py           # JSON 설정 파일 읽기/쓰기/병합
    Search.py           # 파일 및 디렉터리 검색, vcpkg 판별, 소스 수집
    Archive.py          # 네트워크 다운로드, SHA256 검증, 안전한 압축 해제
    Host.py             # Git 및 clang-format 실행 등 호스트 도구
  setup/                # 개발 및 빌드 환경 구성/부트스트랩
    SetupEnvironment.py / SetupLlvm.py / SetupVcpkg.py / SetupLinuxDevEnvironment.py
    HostTools.py / InstallGitHooks.py / GenerateCMakeConstants.py
  generate/             # 데이터 쿠킹 및 코드/문서 생성
    CookPrefabs.py / CookScenes.py / BakeShippingHostDefaults.py / GenerateDocs.py
  lint/                 # 정적 검사 및 포맷팅
    CheckCodeConventions.py / CheckEngineLayers.py / CheckIncludeOrder.py
    CheckSourceGlob.py / RunClangFormat.py / FormatModified.py / PreCommitLint.py
```

진입 부트스트랩 (모든 하위 스크립트 공통):

```python
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from common import getProjectRoot  # 또는 from common.Paths import getProjectRoot
```

## 설정

| 파일 | 용도 |
|------|------|
| `Config/Environment/search_paths.json` | 탐색 후보·URL (없으면 defaults에서 복사) |
| `Config/Environment/toolchain_config.json` | 해석된 절대경로 캐시 |
| `Config/Environment/parser_config.json` | ReflectionParser 인자 |

## 수동 실행

```bash
# Windows PowerShell에서는 `py -3`, macOS/Linux에서는 `python3`를 사용합니다.
py -3 Scripts/setup/SetupEnvironment.py
py -3 Scripts/lint/CheckEngineLayers.py
py -3 Scripts/lint/CheckSourceGlob.py --root . --build build/Ninja-Debug --active-game Demo
py -3 Scripts/lint/RunClangFormat.py          # in-place (clang-format 없으면 Tools/LLVM에 자동 설치)
py -3 Scripts/lint/RunClangFormat.py --check  # CI dry-run
py -3 Scripts/generate/CookPrefabs.py
py -3 Scripts/generate/CookScenes.py
py -3 Scripts/generate/BakeShippingHostDefaults.py build/generated/sw/config/ShippingHostDefaults.h
```

`SetupLlvm` / `SetupEnvironment` 는 최소 LLVM 키트에 `clang-format` 을 포함·보완합니다 (기존 키트에 없으면 캐시된 LLVM tar에서 bin만 추출).
