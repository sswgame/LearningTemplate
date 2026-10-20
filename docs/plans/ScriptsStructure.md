# Scripts 구조 — import 루트 하나 · 3층 · 층 게이트 (2026-10-21)

> 상태: 구현중 · 선행: 없음 · 영역: project · 옛번호: 없음 · 진행: 표

`Scripts/` 의 파이썬 도구는 폴더가 곧 역할이고 이름 · 진입점 · 공용 도우미 · 파이썬 모양을 게이트가 지킨다. C++ 쪽과 비교해 빠진 것은 **import 경로의 형식**과 **의존 방향의 형식** 두 가지다.
설계 검토(2026-10-21, 기준 main `dd2661d34`)의 권고안 B 를 따른다. 폴더를 크게 옮기지 않고 "단일 import 루트 + 3층 + 층 게이트"로 고친다. 명령 파일 경로는 그대로라 CMake · CI · 훅이 부르는 경로는 바뀌지 않는다(`docs/09_Decisions.md` 5-2 "스크립트 이름과 도움말 규칙").

## 1. 근본 원인

1. **파이썬 import 는 파일 경로가 아니라 sys.path 순서로 이름을 정한다.** 파일마다 편한 루트(Scripts · Scripts/lint · lint/gate · lint/report · lint/fixer · dev · setup)를 더한 것이 쌓여, 같은 파일이 두 모듈 이름으로 로드됐다(`gate.CheckEngineLayers is CheckEngineLayers` 가 `False`). 모듈 수준 상태 · 예외 클래스 · `mock.patch.object` 대상이 갈라질 수 있는 구조다.
2. **층 규칙이 산문에만 있었다.** `Scripts/docs/Layout.md` 의 "모두 `common/` 만 import 한다"를 보는 게이트가 없어 역방향(common → lint) · 옆방향(lint → setup, dev → dev) 의존과 다른 모듈의 비공개 이름 사용이 생겼다.
3. **명령 폴더 안에 라이브러리가 숨었다.** `dev/MakeWorktree` · `RunSharedLock`(일부), `RunLogDigest` · `ListChangedTests`(전부), `setup/HostTools`, `lint/AcronymRegistry` 가 다른 명령에 import 됐다.
4. **`common` 파사드가 18개 모듈을 `import *` 로 다시 내보냈다.** 공개 이름 261개, 표준 모듈과 `...Internal` 이름이 새고, 클래스 `BuildTree` 가 모듈 `common.BuildTree` 를 가렸다. 어느 모듈에 기대는지 grep 으로 보이지 않는다.
5. **손으로 쓴 파일 목록 문서가 낡았다.** `Scripts/docs/Tree*.md` 에서 17개 파일이 빠졌다.

## 2. 목표 구조

```
Scripts/
  __main__.py            층 3. 얇은 진입점. 이름 있는 명령 목록(_kSubcommand)이 원본
  common/                층 0. 공용 라이브러리. 위층을 import 하지 않는다
  lint/__init__.py       층 1. 린트 틀: LintGate, LintFixer, LintReport, LintCatalog, SourceScan, LayerRules, StagedScope, conventions/
  lint/gate, fixer, report, selftest/, lint 루트 명령       층 2. 명령
  setup/, generate/, dev/, qa/, asset/                     층 2. 명령(라이브러리 없음). 모두 __init__.py 를 가진 패키지
```

규칙:
1. import 루트는 `Scripts/` 하나다. 모든 모듈은 `Scripts/` 기준 점 이름 하나로만 import 한다(`common.Process`, `lint.LintGate`, `lint.gate.CheckEngineLayers`). 맨 이름과 `Scripts.` 로 시작하는 이름은 쓰지 않는다.
2. sys.path 를 바꾸는 것은 진입점의 한 줄뿐이다(`Scripts` 를 가리킨다). 라이브러리는 sys.path 를 건드리지 않는다. 테스트도 `Scripts` 한 줄이다.
3. 층: 0 `common`, 1 `lint`(틀), 2 명령, 3 `__main__`. 아래층은 위층을 import 하지 않고, 층 2 명령끼리는 import 하지 않는다(조립 명령은 규칙 데이터의 예외에 이유와 함께).
4. 층 2 파일을 다른 파일이 import 해야 한다면 그 코드를 층 0 이나 1 로 옮긴다.

`common` 을 core 와 domain 으로 나누는 것은 하지 않는다. `common` 안에서 역방향 import 가 생기거나 모듈 수가 크게 늘면 그때 `common` 안 티어를 같은 TOML 에 더한다.

## 3. 대안과 기각 이유

- **최소 정리(숨은 라이브러리 꺼내기 + 층 게이트만)**: 두 이름 로드가 남고, 게이트가 맨 이름을 파일로 되짚어야 해서 판정이 모호하다.
- **전면 패키지화(저장소 루트 기준 `Scripts.*`, CMake · CI 호출을 `-m` 으로)**: sys.path 줄이 0 이 되지만 "CMake · 훅 · CI 는 경로로 부른다"(2026-10-06) 결정을 바꿔야 하고, QA CTest 가 `PYTHONPATH` 에 기대게 된다. 되돌리기 어렵다.
- **명령 플러그인 레지스트리(언리얼 UAT 의 `BuildCommand` 방식)**: 명령 목록이 이미 `_kSubcommand` 한 곳이라 이득이 작고, import 로 훑으면 `--help` 마다 명령 모듈 33개 import 비용(1회 측정 323 ms)을 낸다.
- **외부 도구 import-linter**: 외부 린트 도구 대신 AST 게이트를 쓰기로 한 결정(2026-10-11, `docs/09_Decisions.md` 5-2 "Python 모양 규칙")에 따라 같은 구조의 AST 게이트(`CheckScriptLayers`)를 둔다.

## 4. 단위

| 단계 | 내용 | 상태 |
| --- | --- | --- |
| L1a 워크트리 · 빌드 라이브러리 | `dev/MakeWorktree` · `RunSharedLock` 의 라이브러리 부분을 `common/Worktree.py` · `common/SharedLock.py` 로, `dev/RunLogDigest` · `ListChangedTests` 를 `common/LogDigest.py` · `common/ChangedTests.py` 로 | 끝남 |
| L1b HostTools · AcronymRegistry | `setup/HostTools.py` · `lint/AcronymRegistry.py` 를 `common/` 으로. `EditorIconFont` 의 함수 안 sys.path 조작을 지운다 | 끝남 |
| L1c 린트 명령끼리 | 새 `lint/LayerRules.py`(Engine · Core 티어를 게이트와 보고서가 같이 씀), `RunCoreLayerGraph.loadGateTiers` 의 `None` 폴백을 지운다. 새 `lint/StagedScope.py`(`selectFileScopedStaged` 공개) | 끝남 |
| L2a 린트 밖 폴더 패키지화 | `dev` · `generate` · `qa` · `asset` 에 `__init__.py`, 진입점 sys.path 를 `Scripts` 한 줄로, 테스트의 `Scripts/dev` · `Scripts/setup` 루트를 지운다 | 끝남 |
| L2b 린트 폴더 패키지화 | 한 커밋. `lint/__init__.py`, 모든 importer 를 `lint.LintGate` 등 점 이름으로, `LintCatalog` 의 모듈 이름에 `lint.` 접두, 라이브러리의 sys.path 줄 삭제 | 끝남 |
| L2c 이름 하나 테스트 | `Test/PythonTest/TestScriptsImportIdentity.py` — 모든 모듈을 점 이름으로 import 해 같은 파일이 두 이름으로 로드되지 않는지 본다 | 끝남 |
| L3 층 게이트 | `lint/gate/CheckScriptLayers.py` + `lint/rules/CheckScriptLayers.toml`. sys.path 규칙은 `CheckScriptEntryPoints` 에 | 끝남 |
| L4 `common` 파사드 제거 | `common/__init__.py` 의 `import *` 를 지우고 `from common import X` 를 `from common.<모듈> import X` 로 | 끝남 |
| L5 생성 목록 | `py -3 -m Scripts --list`. `Scripts/docs/Tree*.md` 와 `Commands.md` 의 손으로 쓴 목록을 지운다 | 끝남 |
| L6 종료 코드 형식 | `common/ExitCode.py` 의 `IntEnum` 을 `LintGate.run` · `AppRun` · `ListCiJobs` 가 쓴다 | 대기 |

진행 중 다른 작업과 겹치는 곳: StructureSimplification PK4 가 `generate/CookAssets.py` · `PackFormat.py` · `GeneratePackFormat.py` 를 지우므로 이 계획은 그 파일을 옮기지 않고 머리 줄 수정만 한다.

## 5. 측정으로 가릴 질문

1. `common` 파사드의 import 비용: L4 전후 `py -3 -X importtime -c "import common"` 과 `py -3 -c "import common.Constants"` 를 각 10회, 같은 날 `ctest -L lint -j 2` 벽시계와 `cmake --preset Ninja-Debug` configure 시간을 3회씩.
2. L2b 전후 `py -3 -m Scripts lint-suite` 총 시간(3회 중앙값). 달라질 구조적 이유는 없다.
