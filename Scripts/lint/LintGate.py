#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
게이트 하나 = 클래스 하나.

`gate/` 의 린트들은 하는 일이 제각각이지만 **껍데기는 늘 같다**: UTF-8 출력을 켜고, `--root` 를
받고, 저장소 루트를 정하고, 위반 목록을 찍고, 있으면 1 · 없으면 0 을 돌려준다. 게이트마다 이것을 적으면
각자 조금씩 달라진다 — `--root` 기본값이 저장소가 아니라 `Scripts/` 를 가리키거나(CMake · 훅은 늘 `--root` 를
줘서 손으로 돌릴 때만 드러난다), `main()` 이 인자 없이 `parse_args()` 를 불러 프로그램에서 부를 수 없거나,
위반 줄이 stdout · stderr 로 갈린다.

그래서 껍데기를 여기 한 번만 적는다. 게이트가 쓰는 것은 **`scan()` 하나**다.

새 게이트를 넣는 법:

```python
class CheckSomethingGate(LintGate):
    description = "무엇을 검사하는가"
    preCommitPattern = ("Source/*",)            # 이 파일이 staged 됐을 때만 훅에서 돈다
    preCommitFileArgument = "--files"           # 훅이 staged 부분집합을 --files 로 넘긴다
    selfTestCases = [{"name": "...", "expectedViolation": "Source/Probe.h:1: ...", "files": {"Source/Probe.h": "..."}}]

    def addArguments(self, parser):
        self.addFilesArgument(parser)

    def scan(self, repositoryRoot, args):
        # 파일 고르기: --files 와 전체 훑기가 같은 규칙, 빌드 산출물 · 내려받은 외부 도구로는 내려가지 않는다
        listPath = self.selectTargetFiles(repositoryRoot, args.files, listScanRoot=("Source",), suffixes=(".h", ".cpp"))
        # 읽기: 동시에, 찾는 표식이 없는 파일은 거른다(비싼 정규식 앞에서)
        listViolation = [f"{path}: ..." for path, text in readTextFiles(listPath, mustContain="SOMETHING") if ...]
        return GateResult(listViolation=listViolation, summary=f"{len(listPath)} files scanned")

main = CheckSomethingGate.run
```

**파일마다 따로 보는 C++ · 셰이더 게이트는 `scan()` 대신 소스 게이트로 쓴다** — 트리 걷기 · 읽기 · 주석 가리기를 게이트마다 되풀이하지
않고 함께 도는 게이트들과 나눠 쓴다(`SourceScan.py`, 아래 "소스 게이트"):

```python
class CheckSomethingGate(LintGate):
    sourceScanRoots = kLintTargetRelDirs          # 이것을 채우면 소스 게이트다 — CTest · CI 는 `RunSourceGates.py` 한 프로세스로 돌린다
    sourceSuffixes = (".h", ".cpp")

    def checkSourceFile(self, source, args):      # source.text · source.masked · source.listLine · source.relative
        return FileFindings(listViolation=[f"{source.relative}: ..." for ... in ...])
```

파일 집합이 둘 이상(C++ 와 CMake, 키트 폴더와 게임 폴더)이면 `sourceScanRoots` 대신 `sourceScanSets = ( SourceScanSet( "cpp", roots, suffixes ), SourceScanSet( "cmake", … ) )`
를 적고 `source.setName` 으로 가른다. 원본의 줄 끝 · BOM 을 지켜야 하면 `source.rawText` · `lineEnding` · `hasBom` · `restore()`, 고칠 수 있으면 `FileFindings.fixedText`.
C++ 가 아닌 파일(파이썬 · CMake · 셰이더)도 같다. 루트의 파일 하나는 `SourceScanSet( "top", (), relativeFiles=( "CMakeLists.txt", ) )`, 파일 사이의 관계를 알아야 해서
`--files` 가 있어도 전체를 읽어야 하면 `bAlwaysWhole=True`(보고할 파일은 `selectedRelatives`). 파일마다 모은 것을 대 보는 게이트는 `FileFindings.value` 에 담고
`finishSourceScan` 에서 합친다. 요약 문구만 바꾸려면 `describeSourceScan` 만 재정의한다.

파일 사이의 관계를 보거나 파일마다가 아닌 일을 하는 게이트는 `scan()` 을 쓴다. 그런 게이트가 파일마다 파이썬 정규식을 오래 돌린다면
파일별 일을 `common.Parallel.flatMapInProcesses` 로 나눈다(`CheckCodeConventions`) — 스레드는 GIL 에 막힌다. 파일 읽기가 대부분이면
`mapConcurrent` · `readTextFiles` 로 충분하다(`common/Parallel.py` 머리말).

**검사하지 못한 것을 통과로 내지 않는다.** 게이트의 결과는 통과 · 건너뜀 · 불성립 · 위반이고 종료 코드부터 다르다(`lint/GateVerdict.py` 표).
조건이 안 맞아 아무것도 보지 못했을 때 `GateResult(summary="SKIPPED …")` 나 `listNote` 만 달고 `OK` 를 내지 말고 둘 중 하나를 낸다.

- `GateResult.unavailable("분류 — 상세")` : 필수 입력(빌드 폴더 · 컴파일 DB · 도구)이 없다. 통합에서는 실패, 훅에서만 허용된다. 환경 탓이든 `--root` 탓이든 같다.
- `GateResult.skipped("분류 — 상세")` : `--files` 로 좁힌 훑기에 이 게이트가 볼 것이 없다(병합 커밋 · 대상 아닌 파일). **전체 훑기에서는 쓰지 못한다.**
- 대상 파일을 `selectTargetFiles`(또는 `noteSelection` · `GateResult.checkedCount`)로 세는 게이트는 0 개를 보고 위반이 없을 때 기반이 알아서 위 둘로 바꾼다.
  "0개를 검사했고 위반 없음" 이 `OK` 로 보이는 일이 없다. 소스 게이트는 본 파일 수가 자동으로 `checkedCount` 다.
- 불성립 · 건너뜀을 내는 경로도 `selfTestCases` 에 조각(`expectedExitCode`)으로 든다 — `CheckLintsAreAlive` 가 그 경로가 살아 있는지 본다.

`gate/` 에 놓으면 `CheckLintsAreAlive` 가 알아서 집어 가고(목록이 아니라 자리가 규칙이다),
증거(`selfTestCases`)를 들고 있지 않으면 그 자리에서 실패한다.
"""

from __future__ import annotations

import argparse
import fnmatch
import sys
from dataclasses import dataclass, field
from pathlib import Path
from types import ModuleType
from typing import Any, Callable, Iterable


from common.Constants import kNotOurDirNames  # noqa: E402
from common.Host import getMergeHeadRevisions, runGit  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402
from common.Search import collectRepositoryFiles, collectSourceFiles, readTextFiles, resolveFileArguments  # noqa: E402
from common.ExitCode import ExitCode  # noqa: E402
from common.RuleData import RuleDataError, kExemptionKey, kKindInteger, kKindReason, kRuleDataRelDir, readRuleFile  # noqa: E402
from lint.GateVerdict import formatOutcomeLine  # noqa: E402
from lint.SourceScan import FileFindings, LineScope, SourceFile, SourceScanResult, SourceScanSet, joinFindings, parseAddedLines, scanSourceGates  # noqa: E402


#: 음성 테스트 조각(`LintGate.selfTestCases`)이 가질 수 있는 키. `name` · `expectedViolation` 은 필수다.
kSelfTestCaseKeys = frozenset({"name", "expectedViolation", "unexpectedViolation", "files", "args", "repoFiles", "git", "expectedExitCode"})

#: 조각의 `expectedExitCode` 로 쓸 수 있는 값 — 위반(기본), 불성립, 건너뜀. 통과(0)는 "살아 있다" 의 증거가 아니라 쓰지 못한다.
kSelfTestExitCodes = frozenset({ExitCode.Violation, ExitCode.PrerequisiteMissing, ExitCode.Skipped})


def checkSelfTestCase(className: str, case: dict) -> None:
    """
    조각 정의 하나를 봅니다. 이름 · 노린 위반 문구가 없거나 모르는 키가 있으면 `TypeError` — 약한 조각(종료 코드만 맞는 조각)이 들어오지 못하게
    게이트 클래스를 만드는 자리에서 멈춘다.
    """
    caseName = case.get("name")
    if not isinstance(caseName, str) or not caseName.strip():
        raise TypeError(f"{className}: selfTestCases 조각에 name 이 없습니다")
    expected = case.get("expectedViolation")
    if not isinstance(expected, str) or not expected.strip():
        raise TypeError(f"{className}/{caseName}: selfTestCases 조각에 expectedViolation(출력에 있어야 하는 노린 위반 문구)이 없습니다")
    listUnknown = sorted(set(case) - kSelfTestCaseKeys)
    if listUnknown:
        raise TypeError(f"{className}/{caseName}: selfTestCases 조각의 모르는 키 {listUnknown} — 쓸 수 있는 키: {sorted(kSelfTestCaseKeys)}")
    if "expectedExitCode" in case and case["expectedExitCode"] not in kSelfTestExitCodes:
        raise TypeError(f"{className}/{caseName}: expectedExitCode 는 {sorted(int(code) for code in kSelfTestExitCodes)} 중 하나다(통과 0 은 증거가 아니다)")


class GateError(Exception):
    """
    검사가 **서지 않는다**(규칙 데이터 · 입력 파일이 깨졌다) — 위반(1)이 아니라 오류(2)로 끝냅니다.

    "위반이 없다" 와 "검사할 수 없었다" 는 다른 답이다. 후자를 0 으로 돌려주면 게이트가 조용히
    죽고, 1 로 돌려주면 코드를 고쳐도 통과하지 않는다. 필수 입력이 **없는** 것(빌드 폴더 · 도구 · 훑을 파일)은 오류가 아니라
    `GateResult.unavailable` 이다 — 훅에서는 허용되는 종류라 종료 코드를 따로 둔다.
    """


@dataclass
class GateResult:
    """
    게이트가 한 번 훑고 돌려주는 것.

    - `listViolation`: 있으면 실패(1). 한 줄에 하나씩 찍힌다.
    - `listNote`     : 찍기만 하고 **실패시키지 않는다**(경고·참고).
    - `summary`      : 위반이 없을 때 `OK (...)` 괄호 안에 들어갈 문구. 무엇을 얼마나 봤는지 적는다 —
                       "0건" 과 "아무것도 안 봤다" 를 구분해 주는 것이 이 한 줄이다.
    - `skipReason`   : **건너뜀(정상)** — `--files` 로 좁힌 훑기에 이 게이트가 볼 것이 없다. 종료 77. 전체 훑기에서는 쓰지 못한다(`endRun` 이 막는다).
    - `unavailableReason`: **불성립** — 필수 입력(빌드 폴더 · 도구 · 훑을 파일)이 없어 검사를 하지 못했다. 종료 5. 통합에서는 실패이고 훅에서만 허용된다.
                       사유는 `분류 — 상세` 꼴로 쓴다(실행기가 분류로 센다, `lint/GateVerdict.py`). 둘 다 `GateResult.skipped(…)` · `unavailable(…)` 로 만든다 —
                       `OK (SKIPPED …)` 처럼 요약 문구로 건너뜀을 흉내 내면 통과와 같은 줄로 보인다.
    - `checkedCount` : 이 게이트가 실제로 본 대상 수. 알면 채운다 — 비어 있고(`None`) `selectTargetFiles` 도 쓰지 않았다면 기반은 세지 않는다.
                       0 이면 기반이 건너뜀(`--files`) · 불성립(전체 훑기)으로 바꾼다("0개를 검사했고 위반 없음" 이 통과로 보이지 않게).
    """

    listViolation: list[str] = field(default_factory=list)
    listNote: list[str] = field(default_factory=list)
    summary: str = ""
    skipReason: str = ""
    unavailableReason: str = ""
    checkedCount: int | None = None

    def __post_init__(self) -> None:
        if self.skipReason and self.unavailableReason:
            raise ValueError("GateResult: 건너뜀과 불성립을 함께 낼 수 없다")
        if (self.skipReason or self.unavailableReason) and self.listViolation:
            raise ValueError("GateResult: 위반이 있는 결과는 건너뜀 · 불성립이 아니다 — 검사했다")

    @classmethod
    def skipped(cls, reason: str, listNote: list[str] | None = None) -> GateResult:
        """건너뜀(정상) — `--files` 로 좁힌 훑기에 볼 것이 없다. `reason` 은 `분류 — 상세`."""
        return cls(listNote=list(listNote or []), skipReason=reason)

    @classmethod
    def unavailable(cls, reason: str, listNote: list[str] | None = None) -> GateResult:
        """불성립 — 필수 입력이 없어 검사를 하지 못했다. `reason` 은 `분류 — 상세`."""
        return cls(listNote=list(listNote or []), unavailableReason=reason)


class LintGate:
    """
    게이트 린트 하나. 상속해서 `scan()` 만 쓰면 CLI·출력·종료 코드는 기반이 준다.

    - `name`             : 로그 태그 `[이름]`. 비우면 모듈 이름(= 파일 이름)을 쓴다.
    - `description`      : `--help` 한 줄.
    - `violationHeader`  : 실패 머리말의 낱말. 기본 "위반".
    - `hint`             : 위반 목록 뒤에 덧붙일 안내 한 줄(고치는 법).
    - `maxViolationShown`: 위반을 몇 개까지 찍을지. 0 이면 전부.
    - `maxNoteShown`     : 참고를 몇 개까지 찍을지.
    - `selfTestCases`    : 이 게이트가 **반드시 잡아야 하는** 조각. `CheckLintsAreAlive` 가 읽어 간다. 종료 1(위반)이 나와야 하고 종료 2 는 실패다.
                           키: `name` · `expectedViolation`(필수 — 게이트 출력에 들어 있어야 하는 노린 위반 문구) · `files`(임시 트리에 쓸 글) ·
                           `args` · `repoFiles`(저장소에서 그대로 복사할 입력 — 표 · 설정을 손으로 베끼면 형식이 바뀌는 날 조각이 종료 2 로
                           죽는다) · `git`(참이면 임시 트리를 git 저장소로 만든다). 모르는 키도 오류다.
                           `expectedViolation` 이 없으면 클래스를 만들 때 `TypeError` 로 멈춘다 — 종료 코드만 보면 노린 위반이 빠져도
                           같은 조각의 다른 위반(입력 부족 · 낡은 예외)으로 통과한다.
                           `expectedExitCode`(선택, 기본 1) — 불성립(5) · 건너뜀(77) 경로를 증명하는 조각이 쓴다. 그때 `expectedViolation` 은 출력의 `UNAVAILABLE (…)` ·
                           `SKIPPED (…)` 사유 문구다(통과 0 은 쓰지 못한다 — 아무것도 안 잡는 게이트와 구분되지 않는다).
                           `unexpectedViolation`(선택) — 출력에 **없어야 하는** 문구. 오탐 방지: 진짜 위반 하나와 잡히면 안 되는 모양을 한 조각에
                           함께 넣고, 잡히면 안 되는 쪽의 위반 문구를 여기 적는다.
    - `selfTestSkipReason`: 조각을 만들 수 없는 이유. 이유 없는 예외는 없다.
    - `mapExemption`     : 규칙에서 빼 주는 자리 → 이유. 이유 · 낡음 · 크기를 기반이 본다(아래 "예외 표").

    CMake 가 이 게이트를 타깃·CTest 로 등록할 때 묻는 것 셋도 여기 있다 — CMake 에 따로 적으면 파이썬과 어긋난다
    (`Scripts/lint/LintCatalog.py` 가 읽어 간다).

    - `buildComment`     : ninja 가 이 타깃을 만들 때 찍는 줄. **영어다** — 이 저장소의 빌드 출력은
                           전부 영어이고, Windows 콘솔 코드페이지에서 한글이 깨진 전례가 있다.
    - `timeoutSeconds`   : CTest TIMEOUT.
    - `listCtestArgument`: `--root` 말고 더 줄 인자. CMake 변수 참조를 그대로 적는다.
    - `ctestSkipReason`  : CTest 린트로 등록하지 않는 이유(트리 전체가 몇 분 걸리는 검사). 커밋 훅 · 직접 실행은 그대로다.
                           이유 없는 예외는 없다 — 그 검사를 트리 전체로 돌리는 다른 자리(CI 잡)를 적는다.
    """

    name: str = ""
    description: str = ""
    buildComment: str = ""
    timeoutSeconds: int = 30
    listCtestArgument: tuple[str, ...] = ()
    ctestSkipReason: str = ""
    violationHeader: str = "위반"
    noteHeader: str = "참고"
    hint: str = ""
    maxViolationShown: int = 0
    maxNoteShown: int = 20
    selfTestCases: list[dict] = []
    selfTestSkipReason: str = ""

    # --- 커밋 훅이 묻는 것 ---------------------------------------------------
    #
    # 훅도 폴더를 훑는다 — 게이트를 이름으로 import 하면 새 게이트가 빠지고, C++ 가 staged 됐을 때만 돌리면
    # **`.cmake` 나 `.py` 만 커밋할 때 아무 게이트도 돌지 않는다.** 훅이 알아야 하는 것은 게이트마다 다르므로 게이트가 든다 —
    # `LintCatalog` 가 CMake 등록 정보를 게이트에서 가져가는 것과 같은 방식이다.
    #
    # - `preCommitPattern`     : 이 패턴에 맞는 파일이 staged 되었을 때만 돈다 (`fnmatch`,
    #                            저장소 기준 POSIX 경로). **비우면 항상 돈다.**
    # - `preCommitFileArgument`: staged 부분집합을 넘기는 방법. `"--files"`(게이트 · 픽서 같은 철자) ·
    #                            `""`(전체를 훑는 게이트).
    # - `preCommitContentPattern`    : `(글롭, 정규식)` 줄. 글롭에 맞는 staged 파일의 **HEAD 내용 또는 staged 내용** 어느 쪽이든 정규식에 맞으면 돈다
    #                                  (지운 토큰도 잡는다). 이 줄이 있으면 `preCommitPattern` 은 "무조건 도는 파일" 이 되고 비워도 항상 돌지 않는다.
    #                                  게이트가 읽는 것이 파일 전체의 어떤 토큰일 때(`REFLECT` · `GLOBAL_VARIABLE`) — 좁힌 근거는 게이트 옆에 적는다.
    # - `preCommitChangedLinePattern`: 같은 모양인데 **바뀐 줄**(`git diff --cached -U0` 의 + · - 줄)만 본다. 위반이 그 줄 하나로 생기는 게이트용.
    # - `preCommitSkipReason`  : 훅에서 돌 수 없는 이유. 이유 없는 예외는 없다. **이것은 정상 건너뜀이다** — 훅 요약에 `건너뜀 N(사유 분류별)` 로 세어지고(`PreCommitLint`),
    #                            그 게이트는 통합(`lint-suite` · CTest)에서 그대로 돌아 트리 전체를 본다(그래서 `ctestSkipReason` 과 함께 들 수 없다). `분류 — 상세` 꼴로 쓴다.
    #                            훅에서 돌되 필수 입력이 없는 경우(빌드 폴더)는 이것이 아니라 `GateResult.unavailable` 이다 — 이쪽은 통합에서 실패한다.
    preCommitPattern: tuple[str, ...] = ()
    preCommitContentPattern: tuple[tuple[str, str], ...] = ()
    preCommitChangedLinePattern: tuple[tuple[str, str], ...] = ()
    preCommitFileArgument: str = ""
    preCommitSkipReason: str = ""

    # --- 예외 표 -------------------------------------------------------------
    #
    # - `mapExemption`: 이 게이트가 규칙에서 빼 주는 자리(경로 · fnmatch 패턴 · `종류:이름` — 키의 뜻은 게이트가 정한다) → 이유.
    #                   **표는 `Scripts/lint/rules/<게이트 이름>.toml` 의 `[exemption]` 에 둔다** — 클래스를 만들 때 기반이 읽어 채운다
    #                   (`common/RuleData.py`). 파일이 없으면 빈 표다(새 게이트는 파일 하나 떨구기 그대로). 게이트 파일에 `mapExemption` 을
    #                   적거나 모듈 수준 `_k…Allowed/Exempt…` 표를 두는 것은 `selftest/CheckExemptionTables` 가 막는다. 기반이 지키는 것:
    #                   ① 이유가 빈 줄이 있으면 검사가 서지 않는다(종료 2) ② **전체 훑기**(`--files` 없음)에서 대상은 봤는데 한 번도 적용되지
    #                   않은 줄, 그리고 실제 저장소(`.git` 이 있는 루트)에서 대상조차 못 본 줄은 "낡은 예외" 위반이다 ③ OK 줄에 표 크기를 찍는다.
    #                   게이트는 예외의 대상(그 파일 · 그 이름)을 훑을 때 `seeExemption( key )`, 예외로 위반을 넘길 때 `useExemption( key )` 를 부른다.
    #                   둘 다 클래스 메서드다 — 모듈 수준 도우미 함수도 `XxxGate.useExemption( key )` 로 부른다. 기록은 `main()` 이 비운다.
    # - `ruleSchema`  : 같은 데이터 파일에서 `[exemption]` 말고 게이트가 더 읽는 키 → 종류(`common.RuleData.kKind…`). 모르는 키는 오류라
    #                   여기 적지 않은 키가 파일에 있으면 검사가 서지 않는다. 게이트는 그 키를 모듈 수준에서 `LintGate.readRules( 이름, 스키마 )` 로 읽는다.
    mapExemption: dict[str, str] = {}
    ruleSchema: dict[str, str] = {}
    _ruleDataError: str = ""
    _setSeenExemption: set[str] = set()
    _setUsedExemption: set[str] = set()
    #: 이번 훑기에서 `noteSelection` 이 적은 대상 수들(`beginRun` 이 비운다). 소스 게이트는 쓰지 않는다 — 훑기가 파일 수를 `GateResult.checkedCount` 로 준다.
    _listSelectionCount: list[int] = []

    # --- 소스 게이트 -------------------------------------------------------------
    #
    # 파일마다 따로 보는 게이트는 `scan()` 을 쓰지 않고 아래를 채운다. 그러면 걷기 · 읽기 · 주석 가리기를 함께 도는 게이트들과 나눠 쓰고
    # (`SourceScan.scanSourceGates`), CTest · CI 린트 잡은 소스 게이트 전부를 `RunSourceGates.py` 한 프로세스로 돌린다(`LintCatalog`).
    # 혼자 돌릴 때(`main = XxxGate.run` · 커밋 훅 `--files`)도 같은 함수를 게이트 하나로 부른다.
    #
    # - `sourceScanSets`       : 파일 집합(`SourceScanSet`: 이름 · 뿌리 · 확장자/파일 이름 · 제외)의 목록. **채우면 소스 게이트다.** 파일은 어느 집합에서
    #                            왔는지(`SourceFile.setName`)와 함께 온다 — C++ 와 CMake, 키트 폴더와 게임 폴더처럼 파일 집합마다 규칙이 다른 게이트용.
    # - `sourceScanRoots`      : 집합이 하나일 때의 줄임 — 걸을 뿌리(저장소 기준). 확장자(`sourceSuffixes`)도 반드시 함께 적는다.
    #                            클래스를 만들 때 `sourceScanSets` 한 칸(이름 비움)으로 바뀌므로 읽는 쪽은 `sourceScanSets` 하나만 본다. 둘을 함께 적으면 TypeError.
    # - `sourceExcludedDirNames`: 줄임 형식에서 내려가지 않을 폴더 이름(기본 `kNotOurDirNames`)
    # - `prepareSourceScan`    : 훑기 전에 한 번 — 저장소 전체를 묻는 확인(집 파일이 있는가)이나 규칙 준비. 성립하지 않으면 `GateError`.
    # - `checkSourceFile`      : 파일 하나 → `FileFindings`(없으면 None). **자식 프로세스에서도 돈다** — 인스턴스에 상태를 쌓지 말고,
    #                            파일 사이에 모을 것은 `FileFindings.value` 로 돌려 `finishSourceScan` 에서 모은다.
    # - `finishSourceScan`     : 파일마다의 결과 → `GateResult`. 기본은 위반 · 참고를 파일 순서대로 잇고 요약은 `sourceSummary`(비우면 본 파일 수)다.
    sourceScanSets: tuple[SourceScanSet, ...] = ()
    sourceScanRoots: tuple[str, ...] = ()
    sourceSuffixes: tuple[str, ...] = ()
    sourceExcludedDirNames: frozenset[str] = kNotOurDirNames
    sourceSummary: str = ""

    def prepareSourceScan(self, repositoryRoot: Path, args: argparse.Namespace) -> None:
        """소스 게이트가 훑기 전에 한 번 확인할 것. 성립하지 않으면 `GateError`."""

    def checkSourceFile(self, source: SourceFile, args: argparse.Namespace) -> FileFindings | None:
        """소스 게이트가 파일 하나를 봅니다."""
        raise NotImplementedError

    def describeSourceScan(self, result: SourceScanResult) -> str:
        """위반이 없을 때 `OK (...)` 괄호 안의 문구 — 기본은 `sourceSummary`(없으면 본 파일 수). 파일 수 · 규칙 수를 적는 게이트가 재정의한다."""
        return self.sourceSummary or f"{result.fileCount} files"

    def finishSourceScan(self, repositoryRoot: Path, args: argparse.Namespace, result: SourceScanResult) -> GateResult:
        """파일마다의 결과를 게이트 결과로 모읍니다."""
        listViolation, listNote = joinFindings(result.listFindings)
        return GateResult(listViolation=listViolation, listNote=listNote, summary=self.describeSourceScan(result))

    @classmethod
    def isSourceGate(cls) -> bool:
        return bool(cls.sourceScanSets)

    @classmethod
    def seeExemption(cls, key: str) -> None:
        """예외 `key` 의 대상을 이번 훑기에서 보았다고 적습니다."""
        cls._setSeenExemption.add(key)

    @classmethod
    def useExemption(cls, key: str) -> str:
        """예외 `key` 로 위반 하나를 넘깁니다. 이유를 돌려줍니다(표에 없는 키면 KeyError — 게이트의 결함이다)."""
        cls._setSeenExemption.add(key)
        cls._setUsedExemption.add(key)
        return cls.mapExemption[key]

    @classmethod
    def findExemptionKey(cls, value: str) -> str | None:
        """키가 fnmatch 패턴인 표에서 `value` 에 맞는 첫 키를 돌려줍니다(없으면 None)."""
        for key in cls.mapExemption:
            if fnmatch.fnmatchcase(value, key):
                return key
        return None

    def __init_subclass__(cls, **kwargs: object) -> None:
        super().__init_subclass__(**kwargs)
        if "name" not in cls.__dict__:
            # 클래스 이름에서 `Gate` 를 뗀 것이 로그 태그다 — `CheckEngineLayersGate` -> `CheckEngineLayers`.
            # 모듈 이름을 쓰지 않는 이유: 스크립트로 직접 돌리면 `__main__` 이 된다. 부모의 이름을 물려받지 않는다 —
            # 중간 기반(`BaselineSourceGate`)을 상속한 게이트가 기반의 이름으로 규칙 파일을 읽게 된다.
            cls.name = cls.__name__.removesuffix("Gate")
        if any(key in cls.__dict__ for key in ("sourceScanRoots", "sourceSuffixes", "sourceExcludedDirNames")) and cls.sourceScanRoots:
            if "sourceScanSets" in cls.__dict__:
                raise TypeError(f"{cls.__name__}: sourceScanSets 와 sourceScanRoots 를 함께 적지 않는다 — 집합이 하나면 줄임만, 여럿이면 sourceScanSets 만")
            if not cls.sourceSuffixes:
                raise TypeError(f"{cls.__name__}: sourceScanRoots 를 채운 소스 게이트는 sourceSuffixes 도 적는다")
            cls.sourceScanSets = (SourceScanSet("", tuple(cls.sourceScanRoots), tuple(cls.sourceSuffixes), excludedDirNames=cls.sourceExcludedDirNames),)
        if cls.sourceScanSets:
            listSetName = [scanSet.name for scanSet in cls.sourceScanSets]
            if len(set(listSetName)) != len(listSetName):
                raise TypeError(f"{cls.__name__}: sourceScanSets 의 집합 이름이 겹칩니다 {listSetName} — 이름으로 파일의 집합을 가른다")
            if "scan" in cls.__dict__:
                raise TypeError(f"{cls.__name__}: 소스 게이트(sourceScanSets)는 scan() 이 아니라 checkSourceFile() 을 쓴다")
        if cls.preCommitSkipReason and cls.ctestSkipReason:
            raise TypeError(f"{cls.__name__}: preCommitSkipReason 과 ctestSkipReason 을 함께 들 수 없다 — 훅에서도 CTest 에서도 안 도는 게이트는 CI 잡 말고는 어디서도 안 돈다")
        for case in cls.__dict__.get("selfTestCases", []):
            checkSelfTestCase(cls.__name__, case)
        if "mapExemption" not in cls.__dict__:
            # 예외 표는 rules/<이름>.toml 에서 온다. 읽기 오류는 import 를 깨지 않고 main() 이 종료 2 로 알린다(CheckLintsAreAlive 도 본다).
            try:
                cls.mapExemption = cls.readRules(cls.name, cls.ruleSchema)[kExemptionKey]
                cls._ruleDataError = ""
            except RuleDataError as exception:
                cls.mapExemption = {}
                cls._ruleDataError = str(exception)

    @staticmethod
    def readRules(name: str, schema: dict[str, str] | None = None, *, requiredKeys: Iterable[str] = ()) -> dict:
        """
        `Scripts/lint/rules/<name>.toml` 을 읽습니다 — `[exemption]`(이유 표)에 `schema` 의 키를 더한 스키마로. 파일이 없으면
        `requiredKeys` 가 빌 때만 빈 데이터다. 모양이 틀리면 `RuleDataError`.
        """
        return readRuleFile(name, {kExemptionKey: kKindReason, **(schema or {})}, requiredKeys=requiredKeys,
                            bMissingFileIsEmpty=True)

    # --- 구현이 채우는 것 ------------------------------------------------------

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        """`--root` 말고 더 받을 인자가 있으면 여기서 더합니다."""

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        """
        저장소를 한 번 훑고 결과를 돌려줍니다. 검사가 성립하지 않으면 `GateError` 를 던집니다.
        소스 게이트는 이것을 쓰지 않는다 — 기반이 공유 훑기를 게이트 하나로 부른다.
        """
        if not self.isSourceGate():
            raise NotImplementedError
        self.prepareSourceScan(repositoryRoot, args)
        try:
            listResult = scanSourceGates(repositoryRoot, [(self, args)])
        except OSError as exception:
            raise GateError(str(exception)) from exception
        return self.finishSourceScanChecked(repositoryRoot, args, listResult[0])

    def finishSourceScanChecked(self, repositoryRoot: Path, args: argparse.Namespace, result: SourceScanResult) -> GateResult:
        """파일에서 검사가 서지 않았으면(`FileFindings.error`) `GateError`, 아니면 `finishSourceScan`. 본 파일 수를 `checkedCount` 로 단다."""
        listError = [findings.error for findings in result.listFindings if findings.error]
        if listError:
            raise GateError(" · ".join(listError))
        gateResult = self.finishSourceScan(repositoryRoot, args, result)
        if gateResult.checkedCount is None:
            gateResult.checkedCount = result.fileCount
        return gateResult

    # --- 기반이 주는 것 --------------------------------------------------------

    @classmethod
    def run(cls, argv: list[str] | None = None) -> int:
        """진입점. 각 게이트 모듈은 `main = XxxGate.run` 한 줄로 이것을 내보냅니다."""
        return cls().main(argv)

    def parseArguments(self, argv: list[str] | None) -> tuple[argparse.Namespace, Path]:
        """명령줄 → (인자, 저장소 루트)."""
        parser = argparse.ArgumentParser(description=self.description)
        parser.add_argument("--root", type=Path, default=None, help="저장소 루트")
        self.addArguments(parser)
        args = parser.parse_args(argv)
        return args, (args.root or getProjectRoot()).resolve()

    def beginRun(self) -> int:
        """훑기 전 — 예외 기록을 비우고 규칙 데이터 · 예외 표를 확인합니다. 검사가 서지 않으면 `CheckInvalid`, 아니면 `Passed`."""
        cls = type(self)
        cls._setSeenExemption = set()
        cls._setUsedExemption = set()
        LintGate._listSelectionCount = []
        if self._ruleDataError:
            print(f"[{self.name}] {self._ruleDataError}", file=sys.stderr)
            return ExitCode.CheckInvalid
        listNoReason = [key for key, reason in self.mapExemption.items() if not reason.strip()]
        if listNoReason:
            print(f"[{self.name}] 이유 없는 예외: {', '.join(listNoReason)} — 이유 없는 예외는 없다", file=sys.stderr)
            return ExitCode.CheckInvalid
        return ExitCode.Passed

    def main(self, argv: list[str] | None = None) -> int:
        args, repositoryRoot = self.parseArguments(argv)
        returnCode = self.beginRun()
        if returnCode != ExitCode.Passed:
            return returnCode
        try:
            result = self.scan(repositoryRoot, args)
        except (GateError, RuleDataError) as exception:
            print(f"[{self.name}] {exception}", file=sys.stderr)
            return ExitCode.CheckInvalid
        return self.endRun(repositoryRoot, args, result)

    def judgeOutcome(self, args: argparse.Namespace, result: GateResult) -> GateResult:
        """
        위반이 없는 결과가 **아무것도 보지 않았다면** 통과로 두지 않습니다 — `--files` 로 좁힌 훑기는 건너뜀, 전체 훑기는 불성립.
        게이트가 스스로 건너뜀 · 불성립을 냈으면 그대로 두되, 전체 훑기의 건너뜀은 막는다(전체 훑기에 볼 것이 없다는 것은 정상이 아니라 입력이 없는 것이다).
        """
        bFiles = bool(getattr(args, "files", None))
        if result.skipReason and not bFiles:
            return GateResult.unavailable(f"전체 훑기에 건너뜀은 없다 — {result.skipReason}", result.listNote)
        if result.skipReason or result.unavailableReason or result.listViolation:
            return result
        count = result.checkedCount if result.checkedCount is not None else (sum(LintGate._listSelectionCount) if LintGate._listSelectionCount else None)
        if count != 0:
            return result
        if bFiles:
            return GateResult.skipped("대상 파일 없음 — --files 의 파일 가운데 이 게이트가 보는 것이 없다", result.listNote)
        return GateResult.unavailable("훑을 파일 없음 — 대상 폴더에 파일이 하나도 없다(--root 가 저장소인지, 폴더를 옮기지 않았는지 본다)", result.listNote)

    def reportOutcome(self, result: GateResult) -> int:
        """건너뜀 · 불성립 결과를 찍고 종료 코드를 돌려줍니다 — 통과의 `OK` 줄과 다른 줄, 다른 종료 코드."""
        if result.listNote:
            print(f"[{self.name}] {self.noteHeader} {len(result.listNote)}건:")
            self.printListInternal(result.listNote, self.maxNoteShown)
        bUnavailable = bool(result.unavailableReason)
        print(formatOutcomeLine(self.name, bUnavailable, result.unavailableReason or result.skipReason), file=sys.stderr if bUnavailable else sys.stdout)
        return ExitCode.PrerequisiteMissing if bUnavailable else ExitCode.Skipped

    def endRun(self, repositoryRoot: Path, args: argparse.Namespace, result: GateResult) -> int:
        """훑은 뒤 — 건너뜀 · 불성립을 가르고, 낡은 예외를 더하고, 표 크기를 요약에 붙여 찍고, 종료 코드를 돌려줍니다."""
        result = self.judgeOutcome(args, result)
        if result.skipReason or result.unavailableReason:
            return self.reportOutcome(result)   # 아무것도 보지 않았으니 낡은 예외를 따지지 않는다
        if not getattr(args, "files", None):
            result.listViolation += self.findStaleExemptionsInternal(repositoryRoot)
        if self.mapExemption:
            result.summary = f"{result.summary} · 예외 {len(self.mapExemption)} 줄" if result.summary else f"예외 {len(self.mapExemption)} 줄"
        return self.report(result)

    def findStaleExemptionsInternal(self, repositoryRoot: Path) -> list[str]:
        """전체 훑기 뒤 — 대상은 봤는데 쓰지 않은 예외, 실제 저장소에서 대상을 못 본 예외."""
        bRealRepository = (repositoryRoot / ".git").exists()
        listStale: list[str] = []
        table = f"{kRuleDataRelDir}/{self.name}.toml 의 [{kExemptionKey}]"
        for key, reason in self.mapExemption.items():
            if key in self._setUsedExemption:
                continue
            if key in self._setSeenExemption:
                listStale.append(f"[낡은 예외] '{key}' 의 대상은 그대로인데 규칙을 어기지 않습니다 — {table} 에서 지웁니다({reason})")
            elif bRealRepository:
                listStale.append(f"[낡은 예외] '{key}' 의 대상이 없습니다(옮겼거나 지웠다) — {table} 을 고칩니다({reason})")
        return listStale

    def report(self, result: GateResult) -> int:
        """결과를 찍고 종료 코드를 정합니다. 출력 모양을 바꾸려는 게이트만 재정의합니다."""
        if result.listNote:
            print(f"[{self.name}] {self.noteHeader} {len(result.listNote)}건:")
            self.printListInternal(result.listNote, self.maxNoteShown)

        if result.listViolation:
            print(f"[{self.name}] {self.violationHeader} {len(result.listViolation)}건:", file=sys.stderr)
            self.printListInternal(result.listViolation, self.maxViolationShown)
            if self.hint:
                print(self.hint)
            return ExitCode.Violation

        print(f"[{self.name}] OK ({result.summary})" if result.summary else f"[{self.name}] OK")
        return ExitCode.Passed

    # --- 대상 파일 고르기 — `--files` 를 받는 게이트가 함께 쓴다 ---------------

    @staticmethod
    def addFilesArgument(parser: argparse.ArgumentParser, helpText: str = "검사할 파일 (생략 시 전체)") -> None:
        """커밋 훅이 staged 부분집합을 넘기는 `--files` 인자를 더합니다(`preCommitFileArgument = "--files"`)."""
        parser.add_argument("--files", nargs="*", default=None, help=helpText)

    @staticmethod
    def selectTargetFiles(repositoryRoot: Path,
                          listFileArgument: list[str] | None,
                          *,
                          listScanRoot: Iterable[str] = ("",),
                          suffixes: Iterable[str] = (),
                          fileNames: Iterable[str] = (),
                          excludedDirNames: Iterable[str] = kNotOurDirNames,
                          bAnySuffix: bool = False) -> list[Path]:
        """
        게이트가 볼 파일을 고릅니다. `--files` 가 있으면 그 파일 가운데서(`common.Search.resolveFileArguments`), 없으면 `listScanRoot` 를 걸어서
        (`common.Search.collectRepositoryFiles`) — **같은 규칙으로**.

        - 확장자가 `suffixes` 이거나 이름이 `fileNames` 인 파일만(`bAnySuffix` 면 모두).
        - `excludedDirNames` 의 폴더 아래는 뺀다(기본은 빌드 산출물 · 내려받은 외부 도구, `kNotOurDirNames`). 걸을 때는 그 폴더로 내려가지 않는다.
        - `listScanRoot`(저장소 기준, `""` 은 전체) 밖의 파일은 뺀다.
        - `--files` 의 상대 경로는 **저장소 루트 기준**으로 풀고, 거기 없으면 현재 폴더 기준으로 푼다. 저장소 밖 파일은 뺀다.

        게이트마다 이 일을 따로 하지 말 것 — 상대 경로를 푸는 기준과 제외 목록이 갈리고, `--files` 에 제외를 빠뜨리면
        **커밋 훅과 전체 검사가 서로 다른 파일을 본다.** 픽서도 같은 함수로 고른다(`LintFixer.selectFixerTargetFiles`).
        """
        listScanRoot = tuple(listScanRoot)
        if not listFileArgument:
            listPath = collectRepositoryFiles(repositoryRoot, listScanRoot, suffixes=suffixes, fileNames=fileNames,
                                              excludedDirNames=excludedDirNames, bAnySuffix=bAnySuffix)
        else:
            listPath = resolveFileArguments(repositoryRoot, listFileArgument, listScanRoot=listScanRoot, suffixes=suffixes, fileNames=fileNames,
                                            excludedDirNames=excludedDirNames, bAnySuffix=bAnySuffix)
        LintGate.noteSelection(len(listPath))
        return listPath

    @staticmethod
    def noteSelection(count: int) -> None:
        """
        이번 훑기가 대상으로 고른 파일 수를 적습니다(`selectTargetFiles` 가 부른다). 직접 고르는 게이트는 이것을 부른다 — 적힌 수가 전부 0 이고 위반도 없으면
        `endRun` 이 통과로 두지 않는다(`GateResult.checkedCount`).
        """
        LintGate._listSelectionCount.append(count)

    @staticmethod
    def isOwnRepositoryInternal(repositoryRoot: Path) -> bool:
        """`repositoryRoot` 가 이 스크립트가 든 저장소의 루트인가(자기 증거 조각의 임시 트리가 아닌가)."""
        return repositoryRoot.resolve() == getProjectRoot().resolve()

    @staticmethod
    def walkRepository(repositoryRoot: Path, listRoot: Iterable[str], *, what: str, bMustFind: bool,
                       accept: Callable[[Path], bool] | None = None, **walkOptions: Any) -> list[Path]:
        """
        게이트의 **보조 걷기**(전체 훑기 안에서 정의 · 데이터 · 시나리오를 모으는 걷기)가 쓰는 창구 — `common.Search.collectRepositoryFiles` 를 직접 부르지 않는다
        (`CheckGateFileWalk` 가 막는다). `bMustFind` 에 값을 꼭 적게 해서 "걷기가 0건을 모았는데 위반 없음 = 통과" 를 쓸 수 없게 한다:
        True 면 `accept` 를 거친 결과가 0건일 때 GateError(걷는 경로 · 접미사가 바뀐 것). False 는 0건이 정상인 걷기(선택 폴더)이고 이유를 호출 옆에 적는다.
        True 는 이 스크립트가 든 저장소를 훑을 때만 지킨다 — 자기 증거 조각(`selfTestCases`)의 임시 트리는 조각이 필요한 파일만 가져 걷기가 비는 것이 정상이다.
        """
        listPath = [path for path in collectRepositoryFiles(repositoryRoot, tuple(listRoot), **walkOptions) if accept is None or accept(path)]
        if bMustFind and not listPath and LintGate.isOwnRepositoryInternal(repositoryRoot):
            raise GateError(f"{what}: 걷기가 파일을 하나도 모으지 못했습니다 — 폴더 · 접미사가 바뀌었나요?")
        return listPath

    @staticmethod
    def walkSources(listRoot: Iterable[Path], extensions: set[str] | None = None, *, what: str, bMustFind: bool, **walkOptions: Any) -> list[Path]:
        """`walkRepository` 의 `common.Search.collectSourceFiles` 판 — 같은 뜻의 `bMustFind`."""
        listRoot = list(listRoot)
        listPath = collectSourceFiles(listRoot, extensions, **walkOptions)
        if bMustFind and not listPath and any(LintGate.isOwnRepositoryInternal(root.parent) for root in listRoot):
            raise GateError(f"{what}: 걷기가 파일을 하나도 모으지 못했습니다 — 폴더 · 확장자가 바뀌었나요?")
        return listPath

    @staticmethod
    def readFiles(listPath: Iterable[Path], *, encoding: str = "utf-8", errors: str = "replace",
                  mustContain: str | None = None) -> list[tuple[Path, str]]:
        """
        게이트가 파일 내용을 읽는 **창구** — 지금은 `common.Search.readTextFiles`(동시 읽기 · `mustContain` 앞 거르기)를 그대로 부른다.
        한 번 읽어 여러 게이트가 나눠 쓰기 · 내용 해시 캐시를 얹을 자리는 이 메서드와 `selectTargetFiles` 둘이다 — 게이트가 `open()` ·
        `read_text()` 를 직접 부르면 그 최적화를 비켜 간다.
        """
        try:
            return readTextFiles(listPath, encoding=encoding, errors=errors, mustContain=mustContain)
        except OSError as exception:
            raise GateError(str(exception)) from exception

    @staticmethod
    def printListInternal(lines: list[str], maxShown: int) -> None:
        shown = lines[:maxShown] if maxShown > 0 else lines
        for line in shown:
            print(f"  - {line}")
        if len(lines) > len(shown):
            print(f"  ... +{len(lines) - len(shown)} more")


@dataclass
class CountedFinding:
    """기준선 게이트(`BaselineSourceGate`)가 센 위반 하나. 줄은 1 부터, `lastLine` 은 여러 줄 구문의 끝(한 줄이면 `firstLine`)."""

    rule: str        #: `[baseline]` 의 키(`BaselineSourceGate.baselineRules` 가운데 하나)
    relative: str
    firstLine: int
    lastLine: int
    message: str     #: 위반 줄의 `경로:줄:` 뒤에 붙는 글
    text: str        #: 원문 첫 줄(앞뒤 공백을 뗀 것)


class BaselineSourceGate(LintGate):
    """
    기존 위반을 **기준선**으로 세는 소스 게이트 — 판정 단위(추가한 줄 · 기준선)를 기반이 든다.

    전에는 게이트 셋(`CheckConditionParts` · `CheckEditorStyleLiterals` · `CheckDataDrivenLiterals`)이 diff 해석 · 병합 커밋 판정 ·
    기준선 비교를 저마다 베꼈고, 병합 판정은 셋 다 `.git/MERGE_HEAD` 를 경로로 만들어 워크트리(`.git` 이 파일)에서 병합을 못 알아봤다.
    한 자리에 두면 그런 결함이 한 번에 고쳐지고 새 기준선 게이트가 같은 틀을 다시 짓지 않는다.

    모드 둘:
      - 커밋 훅(`--files`): 이번 커밋이 **추가한 줄**(`LineScope`)에 걸친 위반만 위반이다. 병합 커밋은 건너뛴다 — 상대 쪽 줄은 그 브랜치 커밋 때
        검사됐다. `--whole-files` 면 파일 전체를 본다. git 이 추가한 줄을 답하지 못하면 `GateError` 다 — 파일 전체로 바꿔 보지 않는다(폴백 없음).
      - 전체 훑기: `rules/<이름>.toml` 의 `[baseline]` 규칙마다 건수가 기준선을 **넘으면** 실패, 줄면 낮추라는 참고. `--list` 는 위반 전부를 찍는다.
        `[baseline]` 에는 `baselineRules` 의 키가 모두 있고 그 밖의 키는 없어야 한다(없는 키를 0 으로 읽으면 새 규칙이 처음부터 전부 위반이 된다).

    게이트가 채우는 것: `baselineRules` · `countedNoun`(요약의 낱말) · `baselineExceededHint`(기준선을 넘었을 때 고치는 법) ·
    `findCounted( source )`(파일 하나의 위반 — None 이면 그 파일은 세지 않아 요약의 파일 수에서도 빠진다. 예외 표는 여기서 쓴다).
    """

    baselineRules: tuple[str, ...] = ("total",)
    countedNoun: str = "findings"
    baselineExceededHint: str = ""
    preCommitFileArgument = "--files"
    ruleSchema = {"baseline": kKindInteger}
    maxNoteShown = 12

    def findCounted(self, source: SourceFile) -> list[CountedFinding] | None:
        """파일 하나의 위반(추가한 줄로 거르기 전). 자식 프로세스에서도 돈다."""
        raise NotImplementedError

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser)
        parser.add_argument("--whole-files", action="store_true", help="--files 에서 추가한 줄이 아니라 파일 전체를 본다")
        parser.add_argument("--baseline", type=int, default=None, help="전체 훑기의 기준선을 규칙 모두 이 값으로(기본은 rules 의 [baseline])")
        parser.add_argument("--list", action="store_true", help="전체 훑기에서 위반을 전부 찍는다")

    def prepareSourceScan(self, repositoryRoot: Path, args: argparse.Namespace) -> None:
        args.bMergeSkip = False
        args.lineScope = LineScope()
        if not args.files or args.whole_files:
            return
        try:
            args.bMergeSkip = bool(getMergeHeadRevisions(repositoryRoot))
        except RuntimeError as exception:
            raise GateError(str(exception)) from exception
        if args.bMergeSkip:
            return
        result = runGit(["diff", "--cached", "-U0", "--no-color", "--no-ext-diff", "--no-renames", "--diff-filter=ACM"], cwd=repositoryRoot)
        if result.returnCode != 0:
            raise GateError(f"git diff --cached 실패 — 추가한 줄을 알 수 없다(--whole-files 로 파일 전체를 본다): {result.stderr.strip()}")
        args.lineScope = parseAddedLines(result.stdout)

    def checkSourceFile(self, source: SourceFile, args: argparse.Namespace) -> FileFindings | None:
        if args.bMergeSkip:
            return None
        listFound = self.findCounted(source)
        if listFound is None:
            return None
        scope: LineScope = args.lineScope
        return FileFindings(value=[finding for finding in listFound if scope.touches(finding.relative, finding.firstLine, finding.lastLine)])

    def finishSourceScan(self, repositoryRoot: Path, args: argparse.Namespace, result: SourceScanResult) -> GateResult:
        if args.bMergeSkip:
            return GateResult.skipped("병합 커밋 — 상대 쪽 줄은 그 브랜치 커밋 때 검사됐다")
        fileCount = sum(1 for findings in result.listFindings if findings.value is not None)
        listAll: list[CountedFinding] = [finding for findings in result.listFindings for finding in (findings.value or ())]
        listDescribed = [f"{finding.relative}:{finding.firstLine}: {finding.message} | {finding.text}" for finding in listAll]
        summary = f"{fileCount} files, {len(listAll)} {self.countedNoun}"
        if args.files:
            return GateResult(listViolation=listDescribed, summary=summary)

        mapBaseline = self.readBaselineInternal(args)
        mapPerFile: dict[str, int] = {}
        for finding in listAll:
            mapPerFile[finding.relative] = mapPerFile.get(finding.relative, 0) + 1
        listNote: list[str] = []
        listViolation: list[str] = []
        for rule in self.baselineRules:
            count = sum(1 for finding in listAll if finding.rule == rule)
            baseline = mapBaseline[rule]
            listNote.append(f"{rule}: 기준선 {baseline} · 지금 {count}")
            if count < baseline:
                listNote.append(f"  줄었다 — rules/{self.name}.toml 의 [baseline] {rule} 를 {count} 로 낮춘다")
            if count > baseline:
                listViolation.append(f"{rule} {count}건이 기준선 {baseline} 을 넘었다 — {self.baselineExceededHint}(--list 로 위치 확인), 정말 줄었다면 기준선을 맞춘다")
        listTop = sorted(mapPerFile.items(), key=lambda item: (-item[1], item[0]))[:10]
        listNote += [f"파일 {len(mapPerFile)}개, 많은 순:"] + [f"{count:4d}  {relative}" for relative, count in listTop]
        if args.list:
            self.maxNoteShown = 0
            listNote += listDescribed
        return GateResult(listViolation=listViolation, listNote=listNote, summary=summary)

    def readBaselineInternal(self, args: argparse.Namespace) -> dict[str, int]:
        """규칙마다의 기준선. `--baseline` 이 있으면 모두 그 값, 아니면 `[baseline]` — 키가 빠지거나 남으면 `GateError`."""
        if args.baseline is not None:
            return {rule: args.baseline for rule in self.baselineRules}
        mapBaseline = self.readRules(self.name, self.ruleSchema).get("baseline", {})
        listMissing = [rule for rule in self.baselineRules if rule not in mapBaseline]
        listUnknown = sorted(set(mapBaseline) - set(self.baselineRules))
        if listMissing or listUnknown:
            raise GateError(f"rules/{self.name}.toml 의 [baseline] 이 규칙과 맞지 않는다 — 빠진 키 {listMissing} · 모르는 키 {listUnknown}"
                            f"(규칙: {list(self.baselineRules)})")
        return dict(mapBaseline)


def findGateClass(module: ModuleType) -> type[LintGate] | None:
    """모듈 안에 정의된 게이트 클래스를 찾습니다 (한 파일에 게이트 하나)."""
    for value in vars(module).values():
        if isinstance(value, type) and issubclass(value, LintGate) and value is not LintGate:
            if value.__module__ == module.__name__:
                return value
    return None
