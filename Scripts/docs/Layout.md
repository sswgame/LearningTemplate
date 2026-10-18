# Scripts 이름 · 자리 규칙

[목차](../README.md)에서 나눈 문서. 폴더별 파일 목록은 [공용 · 환경 · 생성](TreeCommon.md) · [린트](TreeLint.md) · [개발 · QA · 에셋](TreeDev.md).

## Naming

| 대상 | 규칙 | 예 |
|------|------|-----|
| 공개 함수 | `camelCase` | `runProcess`, `getProjectRoot` |
| 비공개 | `camelCaseInternal` | `exeNameInternal` |
| JSON 키 | `snake_case` | `"llvm_path"` |
| 파일명 | `PascalCase.py` | `SetupEnvironment.py` |

## Layout

**폴더가 곧 성격이다.** 새 스크립트는 "무엇을 하는가" 로 자리를 고른다 — 도구를 찾아 설치하면 `setup/`, 파일을 만들어 내면 `generate/`,
검사 · 수정이면 `lint/` 의 네 폴더 중 하나, 사람이 가끔 돌리는 실험 도구면 `dev/`, 빌드된 App 을 돌려 그림 · 시간 · 메모리를 기준과 비교하거나 에셋을
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
| `dev/` | 동사로 시작(`Run*` · `Compare*` · `Make*` · `Move*` · `Remove*` · `Configure*` · `Sample*` · `List*` …) | 사람이 가끔 — 테스트 데이터를 만드는 것은 `Make*`(`generate/` 와 겹치지 않게) |
| `qa/` | 명사(`GoldenImages` · `Soak` …) | App 을 돌려 비교하기 |

`gate/CheckScriptLayout.py` 가 이 표를 지킨다(린트 폴더는 기반 클래스까지). 진입점은 모두 `main(argv)` 로 인자를 받는다(`CheckScriptEntryPoints`).
