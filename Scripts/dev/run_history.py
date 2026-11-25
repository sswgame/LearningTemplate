"""
시나리오 · 테스트 실행 이력(sqlite) — 보고 쌓기, 흔들림 · p95 추세 · 느려짐 · 단계 시간 · 시간표, 격리 표. 명령은 `test_history.py` 다.
"""

from __future__ import annotations

import datetime
import json
import re
import sqlite3
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Sequence



DEFAULT_DATABASE = Path("Saved") / "TestHistory" / "history.sqlite"
QUARANTINE_PATH = Path("Config") / "Rules" / "Test" / "Quarantine.json"
VERIFY_CONFIG_PATH = Path("Config") / "Dev" / "Verify.json"
RUN_KINDS = ("verify", "manual", "rerun")
STAGE_RESULTS = ("passed", "failed", "skipped")
SLOW_MIN_SAMPLES = 3
SCENARIO_COSTS_PATH = Path("Config") / "Dev" / "ScenarioCosts.json"
# 건너뜀(환경이 없어 돌지 않았다)은 실패가 아니다 — 흔들림 · 실패 수에서 통과 쪽에 둔다(단위 테스트 보고의 `skipped` 와 같은 뜻).
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
    runKind TEXT NOT NULL DEFAULT '',
    PRIMARY KEY (name, backend, startedAtUnixMs)
);
CREATE TABLE IF NOT EXISTS stage (
    name TEXT NOT NULL,
    commitSha TEXT NOT NULL,
    result TEXT NOT NULL,
    seconds REAL NOT NULL,
    startedAtUnixMs INTEGER NOT NULL,
    PRIMARY KEY (name, startedAtUnixMs)
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
    prepare_schema(connection)
    return connection


def prepare_schema(connection: sqlite3.Connection) -> None:
    """표를 만들고, 실행 종류 칸이 없던 옛 DB 에 칸을 더한다(옛 줄은 빈 종류 — 흔들림 판정에 들지 않는다)."""
    connection.executescript(SCHEMA)
    if "runKind" not in {row[1] for row in connection.execute("PRAGMA table_info(run)")}:
        connection.execute("ALTER TABLE run ADD COLUMN runKind TEXT NOT NULL DEFAULT ''")
        connection.commit()


def read_verify_config(project_root: Path, key: str) -> float:
    """`Config/Dev/Verify.json` 의 수 — 없으면 멈춘다(폴백 없음)."""
    value = json.loads((project_root / VERIFY_CONFIG_PATH).read_text(encoding="utf-8")).get(key)
    if not isinstance(value, (int, float)) or isinstance(value, bool):
        raise SystemExit(f"[TestHistory] {VERIFY_CONFIG_PATH} 의 {key} 가 수가 아니다")
    return float(value)


def ingest(connection: sqlite3.Connection, list_report: Iterable[dict], commit_sha: str, run_kind: str = "manual", since_unix_ms: int | None = None) -> int:
    """
    보고를 쌓는다. 같은 (이름, 백엔드, 시작 시각)은 한 번만 — 같은 폴더를 두 번 넣어도 늘지 않는다. 새로 넣은 수를 돌려준다.
    `since_unix_ms` 가 있으면 그 앞에 시작한 보고는 이 실행의 것이 아니다 — sha 없이 `manual` 로 넣는다.
    """
    before = connection.total_changes
    rows = []
    for report in list_report:
        started = int(report["startedAtUnixMs"])
        is_this_run = since_unix_ms is None or started >= since_unix_ms
        rows.append((report["name"], report.get("backend", ""), report.get("game", ""), report["result"], int(report.get("exitCode", 0)),
                     int(report.get("wallMilliseconds", 0)), started, commit_sha if is_this_run else "", run_kind if is_this_run else "manual"))
    connection.executemany("INSERT OR IGNORE INTO run (name, backend, game, result, exitCode, wallMilliseconds, startedAtUnixMs, commitSha, runKind) "
                           "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)", rows)
    connection.commit()
    return connection.total_changes - before


def find_slow_runs(connection: sqlite3.Connection, list_report: Iterable[dict], slow_factor: float) -> list[str]:
    """보고 가운데 통과했는데 같은 (이름, 백엔드)의 앞선 통과 실행 p95 의 `slow_factor` 배를 넘은 것. ingest 전에 부른다(자기 자신을 표본에 넣지 않게)."""
    list_warning: list[str] = []
    for report in list_report:
        if report["result"] != "Passed":
            continue
        previous = [float(row[0]) for row in connection.execute(
            "SELECT wallMilliseconds FROM run WHERE name = ? AND backend = ? AND result = 'Passed' AND startedAtUnixMs < ?",
            (report["name"], report.get("backend", ""), int(report["startedAtUnixMs"])))]
        if len(previous) < SLOW_MIN_SAMPLES:
            continue
        p95 = compute_percentile(previous, 0.95)
        wall = float(report.get("wallMilliseconds", 0))
        if p95 > 0 and wall > slow_factor * p95:
            list_warning.append(f"{report['name']} [{report.get('backend', '')}] {wall:.0f} ms — 지난 p95 {p95:.0f} ms 의 {wall / p95:.1f} 배")
    return list_warning


def record_stage(connection: sqlite3.Connection, name: str, commit_sha: str, result: str, seconds: float, now_unix_ms: int, slow_factor: float) -> str | None:
    """단계 시간을 쌓고, 통과한 단계가 지난 통과 시간 p95 의 `slow_factor` 배를 넘으면 경고 글을 돌려준다."""
    previous = [float(row[0]) for row in connection.execute("SELECT seconds FROM stage WHERE name = ? AND result = 'passed'", (name,))]
    connection.execute("INSERT OR REPLACE INTO stage VALUES (?, ?, ?, ?, ?)", (name, commit_sha, result, seconds, now_unix_ms))
    connection.commit()
    if result != "passed" or len(previous) < SLOW_MIN_SAMPLES:
        return None
    p95 = compute_percentile(previous, 0.95)
    return f"단계 {name} {seconds:.0f} 초 — 지난 p95 {p95:.0f} 초의 {seconds / p95:.1f} 배" if p95 > 0 and seconds > slow_factor * p95 else None


def find_flaky(connection: sqlite3.Connection, commit_window: int, set_excluded: set[str]) -> tuple[list[tuple[str, str, str]], int]:
    """
    최근 검증 커밋 `commit_window` 개에서 통과와 실패가 함께 있는 (이름, 백엔드, 커밋) 목록과, 그 창의 (이름, 백엔드, 커밋) 묶음 수.
    `set_excluded`(격리 표의 이름)는 목록에서 뺀다 — 묶음 수에는 남긴다(분모는 그 창에서 돈 것 전부).
    """
    list_commit = [row[0] for row in connection.execute(
        "SELECT commitSha FROM run WHERE runKind = 'verify' AND commitSha != '' GROUP BY commitSha ORDER BY MAX(startedAtUnixMs) DESC LIMIT ?", (commit_window,))]
    groups: dict[tuple[str, str, str], set[bool]] = {}
    for name, backend, sha, result in connection.execute(
            f"SELECT name, backend, commitSha, result FROM run WHERE runKind = 'verify' AND commitSha IN ({','.join('?' * len(list_commit))})", list_commit):
        groups.setdefault((name, backend, sha), set()).add(result in PASSED_RESULTS)
    return sorted(key for key, outcomes in groups.items() if len(outcomes) == 2 and key[0] not in set_excluded), len(groups)


def decide_flaky(list_flaky: Sequence[tuple[str, str, str]], group_count: int, rate_limit: float, commit_window: int) -> tuple[bool, str]:
    """(통과인가, 끝 줄 `FLAKY=<수> …`)."""
    rate = len(list_flaky) / group_count if group_count else 0.0
    passed = rate <= rate_limit
    return passed, f"FLAKY={len(list_flaky)} rate={rate:.2%} limit={rate_limit:.2%} groups={group_count} commits<={commit_window}{'' if passed else ' - 격리 표에 적거나 고친다'}"


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


def compute_scenario_costs(connection: sqlite3.Connection) -> dict[str, float]:
    """시나리오 → 초. 통과한 실행만(진 실행은 시간 제한에서 잘린다), 백엔드마다 p50 을 내 더한다 — 체인은 한 시나리오를 백엔드마다 차례로 돈다."""
    rows = connection.execute("SELECT name, backend, wallMilliseconds FROM run WHERE result = 'Passed'").fetchall()
    groups: dict[str, dict[str, list[float]]] = {}
    for name, backend, wall in rows:
        groups.setdefault(name, {}).setdefault(backend, []).append(float(wall))
    return {name: round(sum(compute_percentile(list_wall, 0.5) for list_wall in by_backend.values()) / 1000.0, 1)
            for name, by_backend in sorted(groups.items())}


def find_scenario_cost(map_cost: dict[str, float], name: str) -> float:
    """표의 초. 표에 없는 시나리오(새로 더했다)는 표의 중앙값이다. 표가 비면 나눌 근거가 없어 오류다."""
    if name in map_cost:
        return map_cost[name]
    if not map_cost:
        raise ValueError(f"{SCENARIO_COSTS_PATH} 가 비었다 — `test_history.py costs --write` 로 먼저 쓴다")
    return compute_percentile(list(map_cost.values()), 0.5)


def make_scenario_costs_text(map_cost: dict[str, float]) -> str:
    return json.dumps({"_comment": "test_history.py costs --write 가 쓴다 — 시나리오 → 초(통과한 실행의 백엔드별 p50 합). 사람이 커밋할 때만 바뀐다",
                       "scenarioSeconds": map_cost}, ensure_ascii=False, indent=2) + "\n"


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
