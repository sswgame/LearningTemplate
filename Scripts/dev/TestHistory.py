#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/dev/TestHistory.py

시나리오 보고 JSON(`AutomationReportWriter` — `name` · `result` · `backend` · `game` · `startedAtUnixMs` · `wallMilliseconds`)을 sqlite 에 쌓고,
테스트마다 흔들림 지수와 걸린 시간 p95 추세를 낸다. 격리 표(`Config/Rules/Test/Quarantine.json`)는 사유 · 만료가 필수이고 지난 만료는 오류다.

**왜 따로 쌓는가.** 한 번의 실행 보고는 "졌다" 만 말한다. 같은 테스트가 실행마다 이겼다 졌다 하는지(흔들림), 점점 느려지는지(p95 추세)는
여러 묶음 검증을 모아야 보인다 — 언리얼 Gauntlet · Horde 의 테스트 이력, 크로미움 Flakiness Dashboard 와 같은 자리를 저장소 안 sqlite 하나로 둔다.
흔들림 지수 = 연속 두 실행의 결과가 바뀐 횟수 / (실행 수 - 1). 0 은 늘 같음, 1 은 매번 바뀜.

  py -3 Scripts/dev/TestHistory.py ingest <보고 폴더…> [--commit <sha>]     # 기본 DB: Saved/TestHistory/history.sqlite
  py -3 Scripts/dev/TestHistory.py report [--min-runs 3] [--top 30]
  py -3 Scripts/dev/TestHistory.py quarantine [--ctest-exclude]              # 표를 검사한다(만료 · 빠진 칸은 실패), --ctest-exclude 는 ctest -E 정규식을 찍는다
  py -3 Scripts/dev/TestHistory.py --self-check
"""

from __future__ import annotations

import argparse
import datetime
import json
import re
import sqlite3
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.ExitCode import ExitCode  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402

kDefaultDatabase = Path("Saved") / "TestHistory" / "history.sqlite"
kQuarantinePath = Path("Config") / "Rules" / "Test" / "Quarantine.json"
kPassedResults = frozenset({"Passed", "Skipped"})
kSchema = """
CREATE TABLE IF NOT EXISTS run (
    name TEXT NOT NULL,
    backend TEXT NOT NULL,
    game TEXT NOT NULL,
    result TEXT NOT NULL,
    exitCode INTEGER NOT NULL,
    wallMilliseconds INTEGER NOT NULL,
    startedAtUnixMs INTEGER NOT NULL,
    commitSha TEXT NOT NULL,
    PRIMARY KEY (name, backend, startedAtUnixMs)
);
"""


@dataclass(frozen=True)
class TestStats:
    name: str
    backend: str
    runs: int
    failures: int
    flakiness: float
    p95Recent: float
    p95Previous: float

    def getTrend(self) -> float:
        """최근 절반의 p95 / 앞 절반의 p95. 앞이 0 이면 1(비교할 것이 없다)."""
        return self.p95Recent / self.p95Previous if self.p95Previous > 0 else 1.0


def readReports(listRoot: Iterable[Path]) -> list[dict]:
    """폴더 아래 보고 JSON(이름 · 결과 · 시작 시각이 있는 것)만 모은다. 다른 JSON 은 건너뛴다(같은 폴더에 측정 파일이 있다)."""
    listReport: list[dict] = []
    for root in listRoot:
        for path in sorted(root.rglob("*.json")):
            try:
                document = json.loads(path.read_text(encoding="utf-8"))
            except (OSError, json.JSONDecodeError):
                continue
            if isinstance(document, dict) and {"name", "result", "startedAtUnixMs"} <= document.keys():
                listReport.append(document)
    return listReport


def openDatabase(path: Path) -> sqlite3.Connection:
    path.parent.mkdir(parents=True, exist_ok=True)
    connection = sqlite3.connect(path)
    connection.executescript(kSchema)
    return connection


def ingest(connection: sqlite3.Connection, listReport: Iterable[dict], commitSha: str) -> int:
    """보고를 쌓는다. 같은 (이름, 백엔드, 시작 시각)은 한 번만 — 같은 폴더를 두 번 넣어도 늘지 않는다. 새로 넣은 수를 돌려준다."""
    before = connection.total_changes
    connection.executemany(
        "INSERT OR IGNORE INTO run VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
        [(report["name"], report.get("backend", ""), report.get("game", ""), report["result"], int(report.get("exitCode", 0)),
          int(report.get("wallMilliseconds", 0)), int(report["startedAtUnixMs"]), commitSha) for report in listReport])
    connection.commit()
    return connection.total_changes - before


def computePercentile(listValue: Sequence[float], ratio: float) -> float:
    """가장 가까운 순위 백분위(표본이 적어도 실제 값 하나를 고른다). 비면 0."""
    if not listValue:
        return 0.0
    ordered = sorted(listValue)
    index = max(0, min(len(ordered) - 1, int(round(ratio * len(ordered) + 0.5)) - 1))
    return float(ordered[index])


def computeStats(connection: sqlite3.Connection, minRuns: int) -> list[TestStats]:
    listStats: list[TestStats] = []
    rows = connection.execute("SELECT name, backend, result, wallMilliseconds FROM run ORDER BY name, backend, startedAtUnixMs").fetchall()
    groups: dict[tuple[str, str], list[tuple[str, int]]] = {}
    for name, backend, result, wall in rows:
        groups.setdefault((name, backend), []).append((result, wall))
    for (name, backend), listRun in groups.items():
        if len(listRun) < minRuns:
            continue
        listPassed = [result in kPassedResults for result, _ in listRun]
        flips = sum(1 for previous, current in zip(listPassed, listPassed[1:]) if previous != current)
        half = len(listRun) // 2
        listWall = [float(wall) for _, wall in listRun]
        listStats.append(TestStats(name, backend, len(listRun), listPassed.count(False), flips / (len(listRun) - 1),
                                   computePercentile(listWall[half:], 0.95), computePercentile(listWall[:half], 0.95)))
    return sorted(listStats, key=lambda stats: (-stats.flakiness, -stats.getTrend(), stats.name))


@dataclass(frozen=True)
class QuarantineEntry:
    test: str
    reason: str
    expires: datetime.date


def readQuarantine(projectRoot: Path, today: datetime.date) -> tuple[list[QuarantineEntry], list[str]]:
    """격리 표를 읽고 (유효한 항목, 문제)를 돌려준다. 칸이 빠졌거나 날짜가 틀렸거나 만료가 지났으면 문제다(지난 격리는 풀든 사유를 새로 적든 사람이 정한다)."""
    path = projectRoot / kQuarantinePath
    try:
        table = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exception:
        return [], [f"{kQuarantinePath}: 읽지 못했다 ({exception})"]
    listEntry: list[QuarantineEntry] = []
    listProblem: list[str] = []
    for index, item in enumerate(table.get("_quarantined", [])):
        test, reason, expires = item.get("test", ""), item.get("reason", ""), item.get("expires", "")
        if not test or not reason or not expires:
            listProblem.append(f"{kQuarantinePath}: _quarantined[{index}] 에 test · reason · expires 가 모두 있어야 한다")
            continue
        try:
            expiry = datetime.date.fromisoformat(expires)
        except ValueError:
            listProblem.append(f"{kQuarantinePath}: `{test}` 의 expires '{expires}' 는 YYYY-MM-DD 가 아니다")
            continue
        if expiry < today:
            listProblem.append(f"{kQuarantinePath}: `{test}` 의 격리가 {expires} 에 끝났다 — 고쳐서 빼거나 사유와 새 만료를 적는다")
            continue
        listEntry.append(QuarantineEntry(test, reason, expiry))
    return listEntry, listProblem


def makeCtestExclude(listEntry: Sequence[QuarantineEntry]) -> str:
    """ctest `-E` 정규식(테스트 이름 그대로 — 정규식 글자는 이스케이프). 비면 빈 글."""
    return "|".join(f"^{re.escape(entry.test)}$" for entry in listEntry)


def runSelfCheck() -> None:
    connection = sqlite3.connect(":memory:")
    connection.executescript(kSchema)
    listReport = [{"name": "a", "result": result, "startedAtUnixMs": index, "wallMilliseconds": wall, "backend": "dx12"}
                  for index, (result, wall) in enumerate([("Passed", 100), ("Failed", 100), ("Passed", 300), ("Passed", 300)])]
    assert ingest(connection, listReport, "abc") == 4 and ingest(connection, listReport, "abc") == 0, "같은 보고는 한 번만"
    [stats] = computeStats(connection, 3)
    assert stats.failures == 1 and abs(stats.flakiness - 2 / 3) < 1e-9, "흔들림 = 바뀐 횟수 / (실행 - 1)"
    assert stats.getTrend() == 3.0, "최근 절반 p95 / 앞 절반 p95"
    assert computePercentile([1, 2, 3, 4, 5, 6, 7, 8, 9, 10], 0.95) == 10.0
    assert makeCtestExclude([QuarantineEntry("Scenario.a.b", "r", datetime.date(2030, 1, 1))]) == r"^Scenario\.a\.b$"


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="시나리오 보고 이력(sqlite) · 흔들림 · p95 추세 · 격리 표")
    parser.add_argument("--self-check", action="store_true")
    parser.add_argument("--database", type=Path, default=None, help=f"기본 {kDefaultDatabase}")
    sub = parser.add_subparsers(dest="command")
    ingestParser = sub.add_parser("ingest")
    ingestParser.add_argument("roots", nargs="+", type=Path)
    ingestParser.add_argument("--commit", default="", help="보고를 낸 커밋(비우면 빈 글)")
    reportParser = sub.add_parser("report")
    reportParser.add_argument("--min-runs", type=int, default=3)
    reportParser.add_argument("--top", type=int, default=30)
    quarantineParser = sub.add_parser("quarantine")
    quarantineParser.add_argument("--ctest-exclude", action="store_true")
    arguments = parser.parse_args(argv)
    if arguments.self_check:
        runSelfCheck()
        print("[TestHistory] self-check OK")
        return ExitCode.Passed
    projectRoot = getProjectRoot()
    if arguments.command == "quarantine":
        listEntry, listProblem = readQuarantine(projectRoot, datetime.date.today())
        for problem in listProblem:
            print(f"  - {problem}")
        if arguments.ctest_exclude and not listProblem:
            print(makeCtestExclude(listEntry))
        return ExitCode.Violation if listProblem else ExitCode.Passed
    if arguments.command not in ("ingest", "report"):
        parser.error("ingest · report · quarantine 중 하나")
    connection = openDatabase(arguments.database or projectRoot / kDefaultDatabase)
    if arguments.command == "ingest":
        added = ingest(connection, readReports(arguments.roots), arguments.commit)
        print(f"[TestHistory] {added} 보고를 더했다")
        return ExitCode.Passed
    for stats in computeStats(connection, arguments.min_runs)[: arguments.top]:
        print(f"  {stats.name} [{stats.backend}] 실행 {stats.runs} · 실패 {stats.failures} · 흔들림 {stats.flakiness:.2f} · p95 {stats.p95Recent:.0f} ms (추세 x{stats.getTrend():.2f})")
    return ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
