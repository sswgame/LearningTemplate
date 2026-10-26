# Scripts 린트 — 게이트 추가 · 규칙 데이터 · 커밋 훅

[목차](../README.md)에서 나눈 문서.

## 게이트 추가

`lint/gate/Check<무엇>.py` 에 `LintGate` 하위 클래스 하나를 두고 `main = XxxGate.run` 으로 내보낸다 — CMake 타깃 · CTest · 커밋 훅 · 자기 테스트는
폴더를 훑어 알아서 집는다(틀은 `lint/LintGate.py` 머리말). 게이트가 쓰는 것은 두 곳에서만 온다:

| 필요한 것 | 자리 |
| --- | --- |
| 파일마다 따로 보는 C++ · 파이썬 · CMake · 셰이더 검사 | **소스 게이트** — `LintGate.sourceScanRoots` · `sourceSuffixes`(집합 하나) 또는 `sourceScanSets`(`SourceScanSet` 목록 — C++ 와 CMake, 키트 폴더와 게임 폴더처럼 집합마다 규칙이 다르면 `SourceFile.setName` 으로 가른다) + `checkSourceFile`(+ 필요하면 `finishSourceScan`, 요약 문구만이면 `describeSourceScan`). 걷기 · 읽기 · 가리기(`SourceFile.masked` · `noComment`)를 함께 도는 게이트와 나눠 쓰고, CTest · CI 는 `CheckSourceFiles` 하나로 돈다. 원본의 줄 끝 · BOM 은 `SourceFile.rawText` · `lineEnding` · `hasBom` · `restore()`, 고친 글은 `FileFindings.fixedText`(픽서는 `FixPass(inspect=…)` 로 같은 판정 함수를 변환으로 쓴다). 파일 사이를 대 보는 게이트는 파일마다 `FileFindings.value` 로 모아 `finishSourceScan` 에서 합치고, 저장소 루트의 파일 하나는 `SourceScanSet.relativeFiles`, 파일 사이의 관계(import 그래프)를 알아야 하면 `bAlwaysWhole` + `selectedRelatives` |
| `--files` · 대상 파일 고르기 · 읽기(소스 게이트가 아닐 때) | `LintGate.addFilesArgument` · `selectTargetFiles` · `readFiles(mustContain=…)` — 파일 이름만 보는 검사(`CheckResourceCasing`), 파일 하나를 이름으로 읽는 검사, 규칙 데이터가 파일 집합을 정하는 검사(`CheckDataDrivenStructure`), 한 파일이 역할 여럿인 검사(`CheckNameReferences`)만 여기 남는다 |
| 파일 전체에 대는 "있기나 한가" 정규식 | `common.CodeText.findFast` — 머리 lookbehind · `\b` 를 뗀 식으로 먼저 거른다(그런 식은 `re` 가 접두 훑기를 못 써 39 MB 에 1 s) |
| 예외(이유 · 낡음 검사 포함) | `lint/rules/<게이트>.toml` 의 `[exemption]` → `LintGate.mapExemption` · `seeExemption` · `useExemption` |
| 게이트가 읽는 목록 · 표(티어 · 어휘 · 허용 파일) | `lint/rules/<게이트>.toml` 의 다른 키 → 클래스 `ruleSchema` + 모듈 수준 `LintGate.readRules` |
| 주석 · 리터럴 가리기, 줄 번호 | `common.CodeText.blankComments` · `blankCommentsAndLiterals` · `blankMatch` · `lineOf` |
| include 줄 · 층 폴더 · include 가 가리키는 파일 | `common.CodeText.iterIncludes` · `firstFolderAfter` · `IncludeResolver` |
| 파일마다 무거운 정규식 | `common.Parallel.flatMapInProcesses`(스레드는 GIL 에 막힌다) |

예: `CheckCoreNetworkLayers` 는 `checkSourceFile` 에서 `iterIncludes` · `firstFolderAfter` 로 층 표 하나를 대조하고,
`CheckModuleTargets` 는 파일마다 include 만 읽어 오고 끝맺음에서 `IncludeResolver` 로 소속 모듈로 푼다. 자기 테스트 조각(`selfTestCases`)이 없으면 `CheckLintsAreAlive` 가 실패한다.
조각은 임시 트리에 **바이트 그대로** 쓰인다(`\r\n` · BOM 이 든 조각이 실제 파일과 같다). 픽서도 조각 셋(`badSample` · `fixedSample` · `goodSample`)을 CRLF · BOM 형식으로 다시 돌려 글자까지 견준다(`CheckFixersAreAlive`).

## 픽서

픽서는 `lint/fixer/Format<무엇>.py` 에 `LintFixer` 하위 클래스를 두고 `listPass` 에 `FixPass` 를 적는다. 판정과 고침은 **한 함수**다 —
`lint/<무엇>Rules.py` 에 파일 하나(`SourceFile`)를 받아 위반(`listViolation`)과 고친 완성 글(`fixedText`, 원본의 줄 끝 · BOM 으로 되돌린 것)을 함께 돌려주는
`inspectSource` 를 두고, 짝 게이트가 있으면 게이트의 `checkSourceFile` 이 같은 함수를 부르고(`CheckLoopBraces` ↔ `LoopBraceRules.inspectSource`), 픽서는
`FixPass(inspect=…)` 로 부른다. 줄 끝 · BOM · 디코드를 규칙마다 다시 짓지 않는다(규칙은 줄 끝이 `\n` 인 `SourceFile.text` 만 본다).

## 규칙 데이터 — `lint/rules/` 와 `RuleData`

게이트가 빼 주는 자리(예외)와 게이트 · 레지스트리가 쓰는 긴 목록은 코드가 아니라 데이터다. `lint/rules/<이름>.toml` 하나에 두고,
`common/RuleData.py` 가 읽고 모양을 검사한다. 이름은 게이트 이름(`CheckCoreLayers`) 또는 읽는 모듈 이름(`AcronymRegistry`)이다.

```toml
# 게이트 이름의 파일 — 맨 키(배열)가 먼저, 표가 뒤(TOML 은 표 뒤에 맨 키를 둘 수 없다)
pawn_side = ["Source/GameFramework/Base/Actor/Control/Pawn*"]

[exemption]                       # 예외 — 키의 뜻(경로 · fnmatch · `종류:이름`)은 게이트가 정한다, 이유 칸은 비울 수 없다
"Source/Core/Math/VectorMath.cpp" = "영 벡터 단언 — Math 가 Log 아래라 링크 간선이 거꾸로 간다"
```

- **예외만 있는 게이트는 파일만 둔다.** `LintGate` 가 클래스를 만들 때 `rules/<게이트 이름>.toml` 의 `[exemption]` 으로 `mapExemption` 을
  채운다. 파일이 없으면 빈 표다 — 새 게이트는 여전히 파일 하나 떨구기다.
- **목록도 읽는 게이트**는 클래스에 `ruleSchema`(키 → `kKindReason` · `kKindText` · `kKindInteger` · `kKindTextList`)를 적고, 모듈 수준에서
  `LintGate.readRules( 이름, 스키마, requiredKeys=… )` 로 읽어 지금까지의 이름(`_kEngineTier` …)에 담는다. 스키마에 없는 키 · 빈 이유 ·
  종류가 다른 값 · 같은 값 두 번 · 빠진 필수 키는 `RuleDataError` 다(게이트는 종료 2).
- **스크립트 상수에 적은 저장소 경로**(`"Source/…"` · `"Test/…"` · `"Resource/…"` 로 시작하는 `k…` 상수)는 `selftest/CheckScriptRepoPaths` 가 실제 파일 · 폴더
  (`/` 끝은 폴더, `.` · `_` · `-` 끝은 파일 이름 앞머리, `*` 는 글롭)를 가리키는지 본다. 경로가 문자열이라 형식으로 막지 못하는 틈이다 — 파일을 옮기면 상수가
  조용히 낡는다. 예외 앞머리(`CheckEditorStyleLiterals` 의 테마 구현 파일)는 상수가 아니라 `[exemption]` 에 둔다.
- 옮기지 않은 표와 그 까닭(정규식이 든 표, 여러 칸짜리 레코드, 언어가 정한 낱말)은 [결정 기록](../../docs/09_Decisions.md) 5-2 에 있다.

## 커밋 훅과 병합 커밋

`PreCommitLint.py` 는 staged 파일만 본다. 병합 커밋(`MERGE_HEAD` 가 있다)에서는 병합으로 바뀐 파일이 전부 staged 로 잡히지만,
그 대부분은 한쪽 부모와 바이트가 같고 그 부모 커밋을 만들 때 훅이 이미 검사했다. 그래서 파일 단위 검사(`--files` · 위치 인자를 받는
게이트, 픽서, clang-format)에는 **staged 내용이 어느 부모의 같은 경로 blob 과도 다른 파일** — 충돌 해결 · 자동 병합으로 내용이 새로 생긴
파일 — 만 넘긴다. 트리 전체 게이트(파일 인자 없음)와 셰이더 쿠킹 검증은 그대로 돈다. 부모 둘에서 따로 온 파일끼리의 관계는 파일 단위
검사가 원래 못 보므로 **병합 뒤 `ctest -L lint`(CI 도 같다)가 트리 전체로 다시 본다.** 이 줄이기는 `selftest/CheckMergeCommitScope.py` 가 지킨다.
