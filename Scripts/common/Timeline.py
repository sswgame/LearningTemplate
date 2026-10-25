"""
검증 시각표(`timeline.log`) — 사람이 시각으로 "그때 무슨 항목 · 어느 App 실행이었나" 를 맞추게 하는 한 줄짜리 기록.

`verify-batch` 가 항목의 시작 · 끝 줄을 쓰고, 항목이 띄우는 하위 스크립트의 App 실행(`AppRun.runApp` — perf-compare · soak · perf · load · golden 이
모두 이 한 곳으로 App 을 띄운다)이 환경 변수로 받은 같은 파일에 시작 · 끝 줄을 더한다. 환경 변수가 없으면(verify-batch 밖에서 돌 때) 아무것도 쓰지 않는다.

줄 모양: `HH:MM:SS <시작|끝|앱시작|앱끝> <항목 id> <내용>`. 여러 스레드 · 프로세스가 한 줄씩 덧붙이므로 줄 하나를 한 번에 쓴다.
"""

from __future__ import annotations

import datetime
import os
import threading
from pathlib import Path

#: verify-batch 가 하위 프로세스에 넘기는 시각표 경로 · 지금 도는 항목 id.
kTimelineEnvName = "SW_TIMELINE_PATH"
kTimelineItemEnvName = "SW_TIMELINE_ITEM"
#: App 명령줄에서 시각표에 싣는 스위치의 앞부분(그 밖의 인자는 줄을 길게 할 뿐이다).
kListSummaryPrefix: tuple[str, ...] = ("game", "scenario", "gv_")
_kLock = threading.Lock()


def formatClock(moment: datetime.datetime | None = None) -> str:
    return (moment or datetime.datetime.now()).strftime("%H:%M:%S")


def appendTimelineLine(path: Path, kind: str, itemId: str, detail: str) -> None:
    """`HH:MM:SS <kind> <itemId> <detail>` 한 줄을 덧붙인다."""
    line = f"{formatClock()} {kind} {itemId or '-'} {detail}".rstrip() + "\n"
    with _kLock, path.open("a", encoding="utf-8") as file:
        file.write(line)


def appendTimelineLineFromEnv(kind: str, detail: str) -> None:
    """환경 변수의 시각표에 덧붙인다. 변수가 없으면 verify-batch 밖이므로 쓰지 않는다."""
    path = os.environ.get(kTimelineEnvName)
    if path:
        appendTimelineLine(Path(path), kind, os.environ.get(kTimelineItemEnvName, ""), detail)


def summarizeAppArguments(listArgument: list[str], setBackendSwitch: frozenset[str]) -> str:
    """App 명령줄에서 게임 · 시나리오 · 백엔드 · `-gv_*`(`-gv_bench*` 포함) 인자만 골라 한 줄로 줄인다."""
    return " ".join(argument for argument in listArgument
                    if argument in setBackendSwitch or argument.lstrip("-").startswith(kListSummaryPrefix))
