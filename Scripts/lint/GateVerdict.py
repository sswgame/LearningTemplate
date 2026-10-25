"""
게이트 결과의 **종류** — 통과 · 건너뜀 · 불성립 · 실패 — 를 한 자리에서 정한다. 게이트(`LintGate`)가 찍고 실행기(`PreCommitLint` ·
`RunLintSuite` · `RunSourceGates`)가 읽는 약속이라 양쪽이 같은 모듈을 본다.

**왜 있나.** 게이트가 "빌드 폴더가 없다" · "훑을 파일이 없다" 로 아무것도 검사하지 않고 `OK` 를 내면 그 줄은 "검사했고 위반이 없다" 와 같아 보인다.
커밋 훅은 빌드 없는 작업자 워크트리에서 헤더 단독 검사를 사실상 하지 않았고, 아무도 몰랐다. 폴백이 조용히 통과로 보이는 것이 폴백이다 —
그래서 결과 종류를 **종료 코드부터** 가른다(`ExitCode`).

| 종류 | 종료 코드 | 뜻 | 통합(`lint-suite` · CTest) | 커밋 훅 |
| --- | --- | --- | --- | --- |
| 통과 | 0 | 검사했고 위반이 없다 | 통과 | 통과 |
| 건너뜀 | 77 | `--files` 로 좁힌 훑기에 이 게이트가 볼 것이 없다(정상) | 건너뜀 N · 사유별 | 건너뜀 N · 사유별 |
| 불성립 | 5 | 필수 입력(빌드 폴더 · 도구 · 훑을 파일)이 없어 검사를 하지 못했다 | **실패** | 빌드 없는 PC 를 막지 않도록 허용하되, 통과와 다른 줄로 보인다 |
| 위반 | 1 | 규칙을 어겼다 | 실패 | 실패 |
| 검사 오류 | 2 | 규칙 데이터 · 입력 파일이 깨져 검사가 서지 않는다 | 실패 | 실패 |

**사유는 한 줄로 찍는다**: `[이름] SKIPPED (분류 — 상세)` · `[이름] UNAVAILABLE (분류 — 상세)`. 실행기는 게이트의 마지막 줄에서 사유를 읽고
`분류`(첫 ` — ` 앞)로 묶어 센다. 사유의 문구를 흩뜨리지 않으려면 분류를 짧게 쓴다.
"""

from __future__ import annotations

import re
from collections import Counter

from common.ExitCode import ExitCode

kVerdictPassed = "passed"
kVerdictSkipped = "skipped"
kVerdictUnavailable = "unavailable"
kVerdictFailed = "failed"

#: 결과 줄의 표식. 게이트가 찍고(`formatOutcomeLine`) 실행기가 읽는다(`readOutcomeReason`).
kSkippedMarker = "SKIPPED"
kUnavailableMarker = "UNAVAILABLE"
_kOutcomeLineRe = re.compile(rf"^\[[^\]]+\] (?:{kSkippedMarker}|{kUnavailableMarker}) \((.*)\)\s*$", re.MULTILINE)

#: 사유의 분류와 상세를 가르는 구분자.
kReasonSeparator = " — "
kUnknownReason = "사유 미상"


def verdictOfExitCode(returnCode: int) -> str:
    """종료 코드 → 결과 종류. 모르는 값은 실패다(통과로 읽지 않는다)."""
    if returnCode == ExitCode.Passed:
        return kVerdictPassed
    if returnCode == ExitCode.Skipped:
        return kVerdictSkipped
    if returnCode == ExitCode.PrerequisiteMissing:
        return kVerdictUnavailable
    return kVerdictFailed


def formatOutcomeLine(name: str, bUnavailable: bool, reason: str) -> str:
    """게이트가 찍는 건너뜀 · 불성립 줄."""
    return f"[{name}] {kUnavailableMarker if bUnavailable else kSkippedMarker} ({reason})"


def readOutcomeReason(output: str) -> str:
    """게이트 출력에서 마지막 건너뜀 · 불성립 줄의 사유. 없으면 빈 글."""
    listMatch = _kOutcomeLineRe.findall(output)
    return listMatch[-1] if listMatch else ""


def categoryOfReason(reason: str) -> str:
    """사유의 분류 — 첫 ` — ` 앞. 사유가 비면 `사유 미상`."""
    return reason.split(kReasonSeparator, 1)[0].strip() or kUnknownReason


def formatReasonCounts(listReason: list[str]) -> str:
    """사유들 → `분류 N · 분류 M`(많은 순, 같으면 이름순). 비면 빈 글."""
    mapCount = Counter(categoryOfReason(reason) for reason in listReason)
    return " · ".join(f"{category} {count}" for category, count in sorted(mapCount.items(), key=lambda item: (-item[1], item[0])))


def mergeExitCodes(listCode: list[int]) -> int:
    """
    한 프로세스에서 게이트 여럿을 돌린 종료 코드들 → 하나. 검사 오류 > 위반 > 불성립 순이고, 그 밖은 0 이다
    (건너뜀은 `--files` 로 좁힌 훑기에서만 나오고, 여럿을 함께 도는 전체 훑기에는 없다).
    """
    for code in (ExitCode.CheckInvalid, ExitCode.Violation, ExitCode.PrerequisiteMissing):
        if code in listCode:
            return int(code)
    return int(ExitCode.Passed)
