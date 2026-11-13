#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/dev/test_history.py

시나리오 보고 JSON(`AutomationReportWriter` — `name` · `result` · `backend` · `game` · `startedAtUnixMs` · `wallMilliseconds`)을 sqlite 에 쌓고,
테스트마다 흔들림 지수와 걸린 시간 p95 추세를 낸다. 격리 표(`Config/Rules/Test/Quarantine.json`)는 사유 · 만료가 필수이고 지난 만료는 오류다.

**왜 따로 쌓는가.** 한 번의 실행 보고는 "졌다" 만 말한다. 같은 테스트가 실행마다 이겼다 졌다 하는지(흔들림), 점점 느려지는지(p95 추세)는
여러 묶음 검증을 모아야 보인다 — 언리얼 Gauntlet · Horde 의 테스트 이력, 크로미움 Flakiness Dashboard 와 같은 자리를 저장소 안 sqlite 하나로 둔다.
흔들림 지수 = 연속 두 실행의 결과가 바뀐 횟수 / (실행 수 - 1). 0 은 늘 같음, 1 은 매번 바뀜.

  py -3 Scripts/dev/test_history.py ingest <보고 폴더…> [--commit <sha>]     # 기본 DB: Saved/TestHistory/history.sqlite
  py -3 Scripts/dev/test_history.py report [--min-runs 3] [--top 30]
  py -3 Scripts/dev/test_history.py quarantine [--ctest-exclude]              # 표를 검사한다(만료 · 빠진 칸은 실패), --ctest-exclude 는 ctest -E 정규식을 찍는다
  py -3 Scripts/dev/test_history.py --self-check

묶음 검증(`Scripts/dev/VerifyBundle.sh`)이 ctest 마다 보고 폴더를 ingest 하고, ctest 에 격리 표를 `-E` 로 넘긴다.
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

from common.exit_code import ExitCode  # noqa: E402
from common.paths import get_project_root  # noqa: E402

DEFAULT_DATABASE = Path("Saved") / "TestHistory" / "history.sqlite"
QUARANTINE_PATH = Path("Config") / "Rules" / "Test" / "Quarantine.json"
PASSED_RESULTS = frozenset({"Passed", "Skipped"})
SCHEMA = """
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
    p95_recent: float
    p95_previous: float

    def get_trend(self) -> float:
        """최근 절반의 p95 / 앞 절반의 p95. 앞이 0 이면 1(비교할 것이 없다)."""
        return self.p95_recent / self.p95_previous if self.p95_previous > 0 else 1.0


def read_reports(list_root: Iterable[Path]) -> list[dict]:
    """폴더 아래 보고 JSON(이름 · 결과 · 시작 시각이 있는 것)만 모은다. 다른 JSON 은 건너뛴다(같은 폴더에 측정 파일이 있다)."""
    list_report: list[dict] = []
    for root in list_root:
        for path in sorted(root.rglob("*.json")):
            try:
                document = json.loads(path.read_text(encoding="utf-8"))
            except (OSError, json.JSONDecodeError):
                continue
            if isinstance(document, dict) and {"name", "result", "startedAtUnixMs"} <= document.keys():
                list_report.append(document)
    return list_report


def open_database(path: Path) -> sqlite3.Connection:
    path.parent.mkdir(parents=True, exist_ok=True)
    connection = sqlite3.connect(path)
    connection.executescript(SCHEMA)
    return connection


def ingest(connection: sqlite3.Connection, list_report: Iterable[dict], commit_sha: str) -> int:
    """보고를 쌓는다. 같은 (이름, 백엔드, 시작 시각)은 한 번만 — 같은 폴더를 두 번 넣어도 늘지 않는다. 새로 넣은 수를 돌려준다."""
    before = connection.total_changes
    connection.executemany(
        "INSERT OR IGNORE INTO run VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
        [(report["name"], report.get("backend", ""), report.get("game", ""), report["result"], int(report.get("exitCode", 0)),
          int(report.get("wallMilliseconds", 0)), int(report["startedAtUnixMs"]), commit_sha) for report in list_report])
    connection.commit()
    return connection.total_changes - before


def compute_percentile(list_value: Sequence[float], ratio: float) -> float:
    """가장 가까운 순위 백분위(표본이 적어도 실제 값 하나를 고른다). 비면 0."""
    if not list_value:
        return 0.0
    ordered = sorted(list_value)
    index = max(0, min(len(ordered) - 1, int(round(ratio * len(ordered) + 0.5)) - 1))
    return float(ordered[index])


def compute_stats(connection: sqlite3.Connection, min_runs: int) -> list[TestStats]:
    list_stats: list[TestStats] = []
    rows = connection.execute("SELECT name, backend, result, wallMilliseconds FROM run ORDER BY name, backend, startedAtUnixMs").fetchall()
    groups: dict[tuple[str, str], list[tuple[str, int]]] = {}
    for name, backend, result, wall in rows:
        groups.setdefault((name, backend), []).append((result, wall))
    for (name, backend), list_run in groups.items():
        if len(list_run) < min_runs:
            continue
        list_passed = [result in PASSED_RESULTS for result, _ in list_run]
        flips = sum(1 for previous, current in zip(list_passed, list_passed[1:]) if previous != current)
        half = len(list_run) // 2
        list_wall = [float(wall) for _, wall in list_run]
        list_stats.append(TestStats(name, backend, len(list_run), list_passed.count(False), flips / (len(list_run) - 1),
                                   compute_percentile(list_wall[half:], 0.95), compute_percentile(list_wall[:half], 0.95)))
    return sorted(list_stats, key=lambda stats: (-stats.flakiness, -stats.get_trend(), stats.name))


@dataclass(frozen=True)
class QuarantineEntry:
    test: str
    reason: str
    expires: datetime.date


def read_quarantine(project_root: Path, today: datetime.date) -> tuple[list[QuarantineEntry], list[str]]:
    """격리 표를 읽고 (유효한 항목, 문제)를 돌려준다. 칸이 빠졌거나 날짜가 틀렸거나 만료가 지났으면 문제다(지난 격리는 풀든 사유를 새로 적든 사람이 정한다)."""
    path = project_root / QUARANTINE_PATH
    try:
        table = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exception:
        return [], [f"{QUARANTINE_PATH}: 읽지 못했다 ({exception})"]
    list_entry: list[QuarantineEntry] = []
    list_problem: list[str] = []
    for index, item in enumerate(table.get("_quarantined", [])):
        test, reason, expires = item.get("test", ""), item.get("reason", ""), item.get("expires", "")
        if not test or not reason or not expires:
            list_problem.append(f"{QUARANTINE_PATH}: _quarantined[{index}] 에 test · reason · expires 가 모두 있어야 한다")
            continue
        try:
            expiry = datetime.date.fromisoformat(expires)
        except ValueError:
            list_problem.append(f"{QUARANTINE_PATH}: `{test}` 의 expires '{expires}' 는 YYYY-MM-DD 가 아니다")
            continue
        if expiry < today:
            list_problem.append(f"{QUARANTINE_PATH}: `{test}` 의 격리가 {expires} 에 끝났다 — 고쳐서 빼거나 사유와 새 만료를 적는다")
            continue
        list_entry.append(QuarantineEntry(test, reason, expiry))
    return list_entry, list_problem


def make_ctest_exclude(list_entry: Sequence[QuarantineEntry]) -> str:
    """ctest `-E` 정규식(테스트 이름 그대로 — 정규식 글자는 이스케이프). 비면 빈 글."""
    return "|".join(f"^{re.escape(entry.test)}$" for entry in list_entry)


def run_self_check() -> None:
    connection = sqlite3.connect(":memory:")
    connection.executescript(SCHEMA)
    list_report = [{"name": "a", "result": result, "startedAtUnixMs": index, "wallMilliseconds": wall, "backend": "dx12"}
                  for index, (result, wall) in enumerate([("Passed", 100), ("Failed", 100), ("Passed", 300), ("Passed", 300)])]
    assert ingest(connection, list_report, "abc") == 4 and ingest(connection, list_report, "abc") == 0, "같은 보고는 한 번만"
    [stats] = compute_stats(connection, 3)
    assert stats.failures == 1 and abs(stats.flakiness - 2 / 3) < 1e-9, "흔들림 = 바뀐 횟수 / (실행 - 1)"
    assert stats.get_trend() == 3.0, "최근 절반 p95 / 앞 절반 p95"
    assert compute_percentile([1, 2, 3, 4, 5, 6, 7, 8, 9, 10], 0.95) == 10.0
    assert make_ctest_exclude([QuarantineEntry("Scenario.a.b", "r", datetime.date(2030, 1, 1))]) == r"^Scenario\.a\.b$"


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="시나리오 보고 이력(sqlite) · 흔들림 · p95 추세 · 격리 표")
    parser.add_argument("--self-check", action="store_true")
    parser.add_argument("--database", type=Path, default=None, help=f"기본 {DEFAULT_DATABASE}")
    sub = parser.add_subparsers(dest="command")
    ingest_parser = sub.add_parser("ingest")
    ingest_parser.add_argument("roots", nargs="+", type=Path)
    ingest_parser.add_argument("--commit", default="", help="보고를 낸 커밋(비우면 빈 글)")
    report_parser = sub.add_parser("report")
    report_parser.add_argument("--min-runs", type=int, default=3)
    report_parser.add_argument("--top", type=int, default=30)
    quarantine_parser = sub.add_parser("quarantine")
    quarantine_parser.add_argument("--ctest-exclude", action="store_true")
    arguments = parser.parse_args(argv)
    if arguments.self_check:
        run_self_check()
        print("[TestHistory] self-check OK")
        return ExitCode.PASSED
    project_root = get_project_root()
    if arguments.command == "quarantine":
        list_entry, list_problem = read_quarantine(project_root, datetime.date.today())
        for problem in list_problem:
            print(f"  - {problem}")
        if arguments.ctest_exclude and not list_problem:
            print(make_ctest_exclude(list_entry))
        return ExitCode.VIOLATION if list_problem else ExitCode.PASSED
    if arguments.command not in ("ingest", "report"):
        parser.error("ingest · report · quarantine 중 하나")
    connection = open_database(arguments.database or project_root / DEFAULT_DATABASE)
    if arguments.command == "ingest":
        added = ingest(connection, read_reports(arguments.roots), arguments.commit)
        print(f"[TestHistory] {added} 보고를 더했다")
        return ExitCode.PASSED
    for stats in compute_stats(connection, arguments.min_runs)[: arguments.top]:
        print(f"  {stats.name} [{stats.backend}] 실행 {stats.runs} · 실패 {stats.failures} · 흔들림 {stats.flakiness:.2f} · p95 {stats.p95_recent:.0f} ms (추세 x{stats.get_trend():.2f})")
    return ExitCode.PASSED


if __name__ == "__main__":
    sys.exit(main())
