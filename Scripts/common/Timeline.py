"""
검증 시각표(`timeline.log`) — 사람이 시각으로 "그때 무슨 항목 · 어느 App 실행이었나" 를 맞추게 하는 한 줄짜리 기록.

`verify-batch` 가 항목의 시작 · 끝 줄을 쓰고, 항목이 띄우는 하위 스크립트의 App 실행(`AppRun.runApp` — perf-compare · soak · perf · load · golden 이
모두 이 한 곳으로 App 을 띄운다)이 환경 변수로 받은 같은 파일에 시작 · 끝 줄을 더한다. 환경 변수가 없으면(verify-batch 밖, 예: 단독 `soak` · `perf`) `build/timeline/<시각>.log` 에 같은 모양으로 남긴다.
셰이더 쿠킹 · 검증(`AppBinary.runHeadlessTask`)과 백엔드 스모크도 같은 줄을 남긴다 — 사용자가 본 화면을 pid · 인자와 맞추려면 App 을 띄운 모든 길에 있어야 한다.

줄 모양: `HH:MM:SS <시작|끝|앱시작|앱끝> <항목 id> <내용>`. 여러 스레드 · 프로세스가 한 줄씩 덧붙이므로 줄 하나를 한 번에 쓴다.
"""

from __future__ import annotations

import datetime
import os
import threading
from pathlib import Path

from .Paths import getProjectRoot

#: verify-batch 가 하위 프로세스에 넘기는 시각표 경로 · 지금 도는 항목 id.
kTimelineEnvName = "SW_TIMELINE_PATH"
kTimelineItemEnvName = "SW_TIMELINE_ITEM"
#: App 명령줄에서 시각표에 싣는 스위치의 앞부분(그 밖의 인자는 줄을 길게 할 뿐이다).
kListSummaryPrefix: tuple[str, ...] = ("game", "scenario", "gv_")
#: 단독 실행(환경 변수 없음)의 시각표 폴더 — 저장소 루트 기준. `build/` 는 git 이 무시한다.
kStandaloneRelDir = "build/timeline"
_kLock = threading.Lock()
_standalonePath: Path | None = None


def formatClock(moment: datetime.datetime | None = None) -> str:
    return (moment or datetime.datetime.now()).strftime("%H:%M:%S")


def appendTimelineLine(path: Path, kind: str, itemId: str, detail: str) -> None:
    """`HH:MM:SS <kind> <itemId> <detail>` 한 줄을 덧붙인다."""
    line = f"{formatClock()} {kind} {itemId or '-'} {detail}".rstrip() + "\n"
    with _kLock, path.open("a", encoding="utf-8") as file:
        file.write(line)


def findStandaloneTimelinePath() -> Path:
    """verify-batch 밖(환경 변수 없음)에서 도는 이 프로세스의 시각표 — 첫 호출 시각으로 이름을 정해 프로세스 동안 같은 파일이다."""
    global _standalonePath
    with _kLock:
        if _standalonePath is None:
            _standalonePath = getProjectRoot() / kStandaloneRelDir / (datetime.datetime.now().strftime("%Y%m%d-%H%M%S") + ".log")
            _standalonePath.parent.mkdir(parents=True, exist_ok=True)
        return _standalonePath


def appendTimelineLineFromEnv(kind: str, detail: str) -> None:
    """환경 변수의 시각표(verify-batch 가 넘긴다)에 덧붙인다. 변수가 없으면 단독 실행이므로 `build/timeline/<시각>.log` 에 같은 모양으로 남긴다."""
    path = os.environ.get(kTimelineEnvName)
    appendTimelineLine(Path(path) if path else findStandaloneTimelinePath(), kind, os.environ.get(kTimelineItemEnvName, ""), detail)


def appendAppStartLine(pid: int, appPath: Path, summary: str) -> None:
    """App 프로세스를 띄운 줄 — pid · `<빌드 폴더>/<실행 파일>` · 인자 요약."""
    appendTimelineLineFromEnv("앱시작", f"pid={pid} {Path(appPath).parent.parent.name}/{Path(appPath).name} {summary}")


def appendAppEndLine(pid: int, exitCode: int | None, seconds: float, bTimedOut: bool = False) -> None:
    appendTimelineLineFromEnv("앱끝", f"pid={pid} 종료 {exitCode}{' 시간초과' if bTimedOut else ''} {seconds:.0f}초")


def summarizeAppArguments(listArgument: list[str], setBackendSwitch: frozenset[str]) -> str:
    """App 명령줄에서 게임 · 시나리오 · 백엔드 · `-gv_*`(`-gv_bench*` 포함) 인자만 골라 한 줄로 줄인다."""
    return " ".join(argument for argument in listArgument
                    if argument in setBackendSwitch or argument.lstrip("-").startswith(kListSummaryPrefix))
