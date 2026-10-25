"""
시나리오 실행 한 번의 결과 읽기 — 종료 코드 표(C++ 정본)와 보고 JSON(`-scenario-report`).

- 종료 코드 표의 정본은 C++ `AutomationExitCode.h` 의 `case AutomationOutcome::X: return N;` 줄이다. 숫자를 스크립트에 다시 쓰지 않고 그 줄을 읽는다
  (`dev/RunScenarioLogs.py` · `qa/ScenarioRun.py` 가 같이 쓴다). 줄 모양이 바뀌면 오류로 멈춘다(폴백 없음).
- 보고 JSON 은 `AutomationReportWriter` 가 쓴다. 이 모듈은 사람에게 보일 칸(결과 · 이유 · `failures[]` · `unconfirmed[]` · `unhandledLogs[]`)을 읽는다.
"""

from __future__ import annotations

import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path

#: 종료 코드 표의 정본(C++ `getAutomationExitCode`).
kExitCodeHeaderRelPath = "Source/Engine/Automation/AutomationExitCode.h"
_kExitCodeCaseRe = re.compile(r"case\s+AutomationOutcome::(\w+)\s*:\s*return\s+(\d+)\s*;")
#: 표에 반드시 있어야 하는 결과 이름 — 하나라도 없으면 줄 모양이 바뀐 것이다.
kRequiredOutcomeNames: tuple[str, ...] = ("Passed", "Failed", "LoadError", "TimedOut", "Skipped", "Unconfirmed", "UnhandledLog")
#: 결과 이름 → 사람이 읽는 뜻.
kOutcomeMeaning: dict[str, str] = {
    "Passed": "통과",
    "Failed": "실패(단언이 졌다)",
    "LoadError": "읽기 오류(시나리오 · 기동 인자가 틀렸다)",
    "TimedOut": "시나리오 시간 초과",
    "Skipped": "시나리오가 건너뜀",
    "Unconfirmed": "미확정(어림값 문턱 — 잰 값으로 확정해야 한다)",
    "UnhandledLog": "처리하지 않은 Warning/Error 가 남음",
}


#: 보고 JSON 에서 이 모듈이 꼭 읽는 칸(`AutomationReportWriter`).
_kRequiredReportKeys: tuple[str, ...] = ("name", "result", "exitCode", "failures", "unconfirmed", "unhandledLogs")


def readOutcomeExitCodes(headerPath: Path) -> dict[str, int]:
    """`AutomationExitCode.h` 에서 결과 이름 → 종료 코드를 읽는다. 표를 못 읽으면 오류로 멈춘다(폴백 없음)."""
    mapCode = {name: int(number) for name, number in _kExitCodeCaseRe.findall(headerPath.read_text(encoding="utf-8"))}
    for name in kRequiredOutcomeNames:
        if name not in mapCode:
            sys.exit(f"{headerPath}: AutomationOutcome::{name} 의 종료 코드 줄을 찾지 못했다 — case 줄 모양이 바뀌었나")
    return mapCode


def describeOutcomeExitCode(mapCode: dict[str, int], exitCode: int) -> str:
    """종료 코드의 뜻. 표에 없는 값이면 `표에 없는 종료 코드` 라고 말한다."""
    for name, code in mapCode.items():
        if code == exitCode and name in kOutcomeMeaning:
            return kOutcomeMeaning[name]
    return "표에 없는 종료 코드(App 이 시나리오 결과 전에 끝났다)"


@dataclass(frozen=True)
class ScenarioRunReport:
    """보고 JSON 한 편에서 사람에게 보일 부분."""

    name: str
    result: str
    exitCode: int
    frames: int
    reason: str
    listFailure: tuple[str, ...]
    listUnconfirmed: tuple[str, ...]
    listUnhandled: tuple[str, ...]


def readRunReport(path: Path) -> ScenarioRunReport:
    """보고 파일을 읽는다. 없거나 모양이 다르면 `ValueError`(이유가 문장에 든다) — 부르는 쪽이 "보고 없음" 으로 보인다."""
    try:
        data = json.loads(path.read_text(encoding="utf-8-sig"))
    except OSError as error:
        raise ValueError(f"보고를 읽지 못했다: {error}") from error
    except ValueError as error:
        raise ValueError(f"보고 JSON 이 깨졌다: {error}") from error
    if not isinstance(data, dict) or any(key not in data for key in _kRequiredReportKeys):
        raise ValueError(f"시나리오 보고 모양이 아니다(필요한 칸: {', '.join(_kRequiredReportKeys)})")
    return ScenarioRunReport(name=str(data["name"]), result=str(data["result"]), exitCode=int(data["exitCode"]),
                             frames=int(data.get("frames", 0)), reason=str(data.get("reason", "")),
                             listFailure=tuple(str(item) for item in data["failures"]),
                             listUnconfirmed=tuple(str(item) for item in data["unconfirmed"]),
                             listUnhandled=tuple(str(item) for item in data["unhandledLogs"]))
