"""
시나리오 실행의 벽시계 시한 — 정본은 C++ 헤더 `AutomationTimeLimits.h` 한 곳이고, 파이썬은 여기서 읽는다(두 벌을 갖지 않는다).

`common/ScenarioArguments.py` 가 `AutomationScenario.h` 에서 인자의 글을 읽는 것과 같은 방식이다. 헤더나 상수 줄을 읽지 못하면 폴백 없이 `sys.exit` 한다.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

from .Paths import getProjectRoot

#: 시한 상수의 정본(C++ 엔진 헤더).
kTimeLimitsHeaderRelPath = "Source/Engine/Scenario/AutomationTimeLimits.h"
_kIntegerConstantRe = re.compile(r"static\s+constexpr\s+uint32\s+(k\w+)\s*=\s*(\d+)\s*;")


def readProcessKillSeconds(repositoryRoot: Path) -> int:
    """`AutomationTimeLimits::kProcessKillSeconds` — 테스트 하네스가 시나리오 프로세스를 죽이는 시한(초). 못 읽으면 `sys.exit`."""
    headerPath = repositoryRoot / kTimeLimitsHeaderRelPath
    mapConstant = dict(_kIntegerConstantRe.findall(headerPath.read_text(encoding="utf-8")))
    if "kProcessKillSeconds" not in mapConstant:
        sys.exit(f"{headerPath}: `static constexpr uint32 kProcessKillSeconds = <정수>;` 줄을 찾지 못했다 — 줄 모양이 바뀌었나")
    return int(mapConstant["kProcessKillSeconds"])


kProcessKillSeconds = readProcessKillSeconds(getProjectRoot())
