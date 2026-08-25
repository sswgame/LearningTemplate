# Scripts (파이썬 도구)

CMake는 빌드만 담당하고, 도구 탐색·설정·보조 생성은 여기서 처리한다.

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
  ConfigHelper.py       # 공통: 루트/toolchain_config/search_paths
  setup/                # 환경 구성 (CMake configure가 SetupEnvironment 호출)
    SetupEnvironment.py
    SetupLlvm.py / SetupNinja.py / SetupVcpkg.py
    HostTools.py / UpdateParserConfig.py / SetupLinuxDevEnvironment.py
  generate/             # AuxTargets: GenerateDocs, CookPrefabs
  lint/                 # CheckEngineLayers, CheckSourceGlob, RunClangFormat
```

진입 부트스트랩 (모든 하위 스크립트 공통)::

```python
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from ConfigHelper import getProjectRoot
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
```

`SetupLlvm` / `SetupEnvironment` 는 최소 LLVM 키트에 `clang-format` 을 포함·보완합니다 (기존 키트에 없으면 캐시된 LLVM tar에서 bin만 추출).
