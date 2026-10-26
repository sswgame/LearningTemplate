# Scripts 구조 — import 루트 하나 · 3층 · 층 게이트 (2026-10-21)

> 상태: 측정대기 · 선행: 없음 · 영역: project · 옛번호: 없음 · 진행: 표

**남은 확인(통합에서 확인):** 매니페스트 id `debug-full-build`, `ctest-lint`, `ctest-python`(6절 — configure 는 미리 한다). 확인이 끝나면 통합 담당이 이 문서를 닫는다.

`Scripts/` 의 파이썬 도구는 폴더가 곧 역할이고 이름 · 진입점 · 공용 도우미 · 파이썬 모양을 게이트가 지킨다. 빠진 것은 **import 경로의 형식**과 **의존 방향의 형식**이었다. 설계 검토(2026-10-21, 기준 main `dd2661d34`)의 권고안 B — 폴더를 크게 옮기지 않고 "단일 import 루트 + 3층 + 층 게이트" — 를 따른다. 명령 파일 경로는 그대로라 CMake · CI · 훅의 호출 경로는 바뀌지 않는다(`docs/09_Decisions.md` 5-2 "스크립트 이름과 도움말 규칙").

## 1. 근본 원인

1. **파이썬 import 는 파일 경로가 아니라 sys.path 순서로 이름을 정한다.** 파일마다 편한 루트를 더한 것이 쌓여 같은 파일이 두 모듈 이름으로 로드됐다(`gate.CheckEngineLayers is CheckEngineLayers` 가 `False`) — 모듈 상태 · 예외 클래스 · `mock.patch.object` 대상이 갈라질 수 있다.
2. **층 규칙이 산문에만 있었다.** "모두 `common/` 만 import 한다"(`Scripts/docs/Layout.md`)를 보는 게이트가 없어 역방향 · 옆방향 의존과 비공개 이름 사용이 생겼다.
3. **명령 폴더 안에 라이브러리가 숨었다**(다른 명령이 import 하는 `dev/MakeWorktree` · `setup/HostTools` · `lint/AcronymRegistry` 등).
4. **`common` 파사드가 18개 모듈을 `import *` 로 다시 내보냈다**(공개 이름 261개, 클래스 `BuildTree` 가 모듈 `common.BuildTree` 를 가림).
5. **손으로 쓴 파일 목록 문서가 낡았다.**

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
2. sys.path 를 바꾸는 것은 진입점의 한 줄뿐이다(`Scripts` 를 가리킨다). 라이브러리는 건드리지 않고 테스트도 `Scripts` 한 줄이다.
3. 층: 0 `common`, 1 `lint`(틀), 2 명령, 3 `__main__`. 아래층은 위층을 import 하지 않고 층 2 명령끼리는 import 하지 않는다(조립 명령은 규칙 데이터의 예외에 이유와 함께).
4. 층 2 파일을 다른 파일이 import 해야 한다면 그 코드를 층 0 이나 1 로 옮긴다.

`common` 을 core 와 domain 으로 나누는 것은 하지 않는다(역방향 import 가 생기거나 모듈 수가 크게 늘면 `common` 안 티어를 같은 TOML 에 더한다).

## 3. 대안과 기각 이유

- **최소 정리(숨은 라이브러리 꺼내기 + 층 게이트만)**: 두 이름 로드가 남고 게이트가 맨 이름을 파일로 되짚어야 해 판정이 모호하다.
- **전면 패키지화(`Scripts.*`, CMake · CI 호출을 `-m` 으로)**: "CMake · 훅 · CI 는 경로로 부른다"(2026-10-06) 결정을 바꿔야 하고 QA CTest 가 `PYTHONPATH` 에 기대게 되어 되돌리기 어렵다.
- **명령 플러그인 레지스트리(언리얼 UAT 방식)**: 명령 목록이 이미 `_kSubcommand` 한 곳이라 이득이 작고 `--help` 마다 명령 모듈 33개 import 비용(323 ms)을 낸다.
- **외부 도구 import-linter**: 외부 린트 도구 대신 AST 게이트를 쓰기로 한 결정(2026-10-11, 5-2 "Python 모양 규칙")에 따라 같은 구조의 AST 게이트(`CheckScriptLayers`)를 둔다.

## 4. 단위 (모두 끝남)

| 단계 | 내용 | 상태 |
| --- | --- | --- |
| L1a | `dev/MakeWorktree` · `RunSharedLock` · `RunLogDigest` · `ListChangedTests` 의 라이브러리 부분을 `common/` 으로 | 끝남 |
| L1b | `setup/HostTools.py` · `lint/AcronymRegistry.py` 를 `common/` 으로, `EditorIconFont` 의 함수 안 sys.path 조작 삭제 | 끝남 |
| L1c | 새 `lint/LayerRules.py`(Engine · Core 티어를 게이트와 보고서가 같이 씀, `RunCoreLayerGraph` 의 `None` 폴백 삭제), 새 `lint/StagedScope.py` | 끝남 |
| L2a | `dev` · `generate` · `qa` · `asset` 패키지화, 진입점 sys.path 를 `Scripts` 한 줄로 | 끝남 |
| L2b | `lint/__init__.py`, 모든 importer 를 점 이름으로, `LintCatalog` 모듈 이름에 `lint.` 접두, 라이브러리의 sys.path 줄 삭제 | 끝남 |
| L2c | `Test/PythonTest/TestScriptsImportIdentity.py` — 같은 파일이 두 이름으로 로드되지 않는지 본다 | 끝남 |
| L3 | `lint/gate/CheckScriptLayers.py` + `lint/rules/CheckScriptLayers.toml`, sys.path 규칙은 `CheckScriptEntryPoints` 에 | 끝남 |
| L4 | `common/__init__.py` 의 `import *` 삭제, `from common.<모듈> import X` 로 | 끝남 |
| L5 | `py -3 -m Scripts --list` 가 생성하는 목록. `Scripts/docs/Tree*.md` 와 `Commands.md` 의 손으로 쓴 목록 삭제 | 끝남 |
| L6 | `common/ExitCode.py` 의 `IntEnum` 을 `LintGate.run` · `AppRun` · `ListCiJobs` 가 사용 | 끝남 |

## 5. 측정 결과 (2026-10-23 `wt/scriptsverify`, 다른 작업자 빌드와 겹친 PC라 방향만 믿는다)

- `import common` 은 L4 전 1391 · 386 ms 에서 후 191 · 75 ms(10회 중앙값, 두 번). 전에는 `import common` 한 번이 `AppBinary` · `BuildTree` · `Host` · `Parallel` · `subprocess` 까지 끌어왔다. 방향은 후가 빠르고 배수는 믿지 않는다.
- `lint-suite`: L2b 전 39~49 s(첫 회 콜드 제외), 후 32~34 s. 나빠졌다는 신호는 없다.
- `ctest -L lint -j 2` 벽시계와 `cmake --preset` configure 시간은 재지 않았다(6절).

## 6. 남은 확인

2026-10-23 파이썬만 돌려 `lint-suite` 45개와 `Test/PythonTest/Test*.py` 49개가 전부 통과했다(`CheckScriptLayers` · `TestScriptsImportIdentity` 포함). 남은 것: `cmake --preset Ninja-Debug` configure 를 이 브랜치에서 돌리지 않았다(`GenerateConfigureFiles.py` 를 임시 폴더로 돌린 결과가 main 빌드 폴더의 생성물과 같았고 `LintTargets.cmake` 는 새 게이트 두 블록만 달랐다). 병합 뒤 configure 와 `ctest -L lint -j 2` · `-L python -j 2` 를 한 번 돈다.
층 게이트의 CookAssets 간선 예외는 PK4 가 쿠커를 지울 때 사라졌다. 게이트와 짝 명령의 간선(픽서 셋 · 보고서 하나)은 판정을 `lint/LoopBraceRules` · `IncludeOrderRules` · `NamespaceBlockRules` · `DiscardReasonRules`(층 1)로 옮겨 예외 네 줄을 지웠다. 셀프테스트 둘이 자기 게이트를 부르는 예외는 게이트 내부를 검사하는 일이라 남긴다.
