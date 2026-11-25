#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/dev/test_history.py

시나리오 보고 JSON(`AutomationReportWriter` — `name` · `result` · `backend` · `game` · `startedAtUnixMs` · `wallMilliseconds`)을 sqlite 에 쌓고,
테스트마다 흔들림 지수와 걸린 시간 p95 추세를 낸다. 격리 표(`Config/Rules/Test/Quarantine.json`)는 사유 · 만료가 필수이고 지난 만료는 오류다.

**왜 따로 쌓는가.** 한 번의 실행 보고는 "졌다" 만 말한다. 같은 테스트가 실행마다 이겼다 졌다 하는지(흔들림), 점점 느려지는지(p95 추세)는
여러 묶음 검증을 모아야 보인다 — 언리얼 Gauntlet · Horde 의 테스트 이력, 크로미움 Flakiness Dashboard 와 같은 자리를 저장소 안 sqlite 하나로 둔다.
흔들림 지수 = 연속 두 실행의 결과가 바뀐 횟수 / (실행 수 - 1). 0 은 늘 같음, 1 은 매번 바뀜.

  py -3 Scripts/dev/test_history.py ingest <보고 폴더…> [--commit <sha>] [--kind verify|manual|rerun] [--since <표지 파일>]   # DB: Saved/TestHistory/history.sqlite
  py -3 Scripts/dev/test_history.py flaky                                    # 최근 검증 커밋의 흔들림 — 끝 줄 FLAKY=<수>, 문턱을 넘으면 진다
  py -3 Scripts/dev/test_history.py stage <단계> --commit <sha> --result passed|failed|skipped --seconds <초>   # 묶음 검증 단계 시간(느려지면 경고)
  py -3 Scripts/dev/test_history.py report [--min-runs 3] [--top 30]
  py -3 Scripts/dev/test_history.py quarantine [--ctest-exclude]              # 표를 검사한다(만료 · 빠진 칸은 실패), --ctest-exclude 는 ctest -E 정규식을 찍는다
  py -3 Scripts/dev/test_history.py costs [--write]                           # 시나리오 → 초 시간표(통과한 실행의 백엔드별 p50 합). --write 는 Config/Dev/ScenarioCosts.json 을 쓴다
  py -3 Scripts/dev/test_history.py changed --preset <프리셋> [--base origin/main]  # 새로 들어온 · 바뀐 테스트 · 시나리오를 먼저 돈다(아래)
  py -3 Scripts/dev/test_history.py --self-check

묶음 검증(`Scripts/dev/preflight.sh`)이 ctest 마다 보고 폴더를 ingest 하고, ctest 에 격리 표를 `-E` 로 넘긴다.

**실행 종류(`runKind`).** 보고 폴더(`build/<프리셋>/Bin/Saved/Automation`)는 손으로 돌린 실행 · 다시 돌리기 도구(`scenario_bisect.py`)도 함께 쓴다 — 예전에는 다음 묶음
검증의 ingest 가 그 보고를 그때의 sha 로 넣어, 다른 코드로 돈 실행이 같은 커밋으로 적혔다(흔들림 상한값이 부풀었다). 그래서 ingest 는 `--since <표지 파일>`(단계가 시작할 때
만드는 `verify.ctest.<이름>.log.start`) 뒤에 시작한 보고에만 `--commit` 과 `--kind` 를 붙이고, 그 앞 보고는 sha 없이 `manual` 로 넣는다. 흔들림 판정(`flaky`)은 `verify` 만 본다.
실행기에 표지 인자를 주는 길도 있지만 그러면 손 실행도 같은 값을 줄 수 있다 — 시각 표지는 검증 스크립트만 만든다.

**흔들림 판정(`flaky`).** 최근 검증 커밋 `_flakyCommitWindow` 개에서 같은 (테스트, 백엔드, 커밋)에 통과와 실패가 함께 있는 묶음을 센다. 격리 표에 없는 것의 비율이
`_flakyRateLimit`(`Config/Dev/Verify.json`)를 넘으면 진다 — 흔들리는 테스트는 "다시 돌리니 통과" 로 넘기지 않고 격리 + 만료로 다룬다(Google 의 흔들림 격리, 자동 재시도는
대리물을 통과시키는 모양이라 하지 않는다). 격리 표의 `test` 는 ctest 이름이고 보고의 `name` 은 시나리오 이름이라, 흔들림 줄의 이름을 그대로 격리 표에 적어도 이 판정에서 빠진다.

**느려짐 경고.** ingest 는 새로 넣은 검증 실행이 같은 (테스트, 백엔드)의 지난 통과 실행 p95 의 `_slowFactor` 배를 넘으면 경고 줄을, `stage` 는 같은 단계의 지난 통과 시간 p95 와
견줘 경고 줄을 찍는다(판정을 바꾸지 않는다 — 시한은 실행기가 따로 끊는다). 표본이 `SLOW_MIN_SAMPLES` 보다 적으면 견주지 않는다.

**시간표(`costs`).** 러너를 나눌 때(docs/NEXT.md T3 샤드 균형)의 입력이다 — 언리얼 Horde · 크로미움 Swarming 처럼 지난 실행 시간으로 나눈다.
표는 `--write` 로 갱신하고 **사람이 커밋할 때만 바뀐다**(실행마다 다시 나누면 앞 단계가 가리던 결함이 실행마다 드러났다 숨는다). 표에 없는 시나리오는 표의 중앙값이다.

**`changed`.** push 는 검증한 커밋만 하므로 `origin/main` 에 있는 것은 돈 적이 있고, 그 뒤에 더해지거나 바뀐 것은 돈 적이 없다 — 상태 파일 없이
`git diff --name-status <base>...HEAD` 로 가른다(이력 DB 는 기계마다 달라 쓰지 않는다). 묶음 검증이 build 와 cpu 사이에 부른다(`preflight.sh` 의 `changed` 단계).
  (a) 바뀐 소스 · 테스트 파일의 기능 폴더(`…/Tests/…` 는 `Tests` 앞, 아니면 그 파일의 폴더 — 테스트가 없으면 한 단계 위)를 그 폴더를 싣는 단위 항목
      (`UnitTests` · `UnitTests.<게임>` · `UnitTests.OnlineKits` — ctest 의 명령을 `ctest --show-only=json-v1` 에서 읽는다)에 `--test-source=` 로 좁혀 돈다.
      에디터 쪽(`/Editor/` 소스 · 편집기 시나리오 · `Content/editor` 데이터)이나 리플렉션 선언(`REFLECT` 이 있는 헤더 — 메타 `Icon=` · 분류 · 표시 이름)이 바뀌었으면 에디터 단위 테스트(`UnitTests.Editor` — 창 · GPU 없는 `App --test-editor`)를 돈다. 단위라 시나리오보다 먼저다.
  (b) 더해지거나 바뀐 `*.scenario.xml`(골든이 바뀌면 그 시나리오)을 하나씩 제 프로세스로, 그 시나리오가 든 ctest 항목의 백엔드마다 돈다.
  돌 것이 있으면 먼저 프리셋을 증분 빌드한다 — HEAD 와 다른 옛 바이너리로 판정하지 않는다(지면 `CHANGED_FAILED`).
  둘 다 **없는 기준 파일만 쓴다**(`--record-missing-snapshots` · `-scenario-record-missing-golden`) — 쓴 케이스 · 시나리오는 `MissingBaseline` 으로 지고, 쓴 파일(추적 안 된
  `golden/` · `Snapshots/`)이 있으면 목록을 내고 멈춘다. 통합 담당이 보고 커밋한 뒤 `--from changed` 로 다시 돌려 기록 없이 통과해야 한다.
  출력은 표 하나(항목 · 새것/바뀐 것 · 실패 종류 · 까닭 · 그 파일을 더한 갈래)이고, 끝 줄은 `CHANGED_OK=<sha>` 또는 `CHANGED_FAILED` 다. 새 시나리오가 모든 백엔드에서
  같은 단언으로 지면 `전제`, 한 백엔드만 지면 `백엔드 <이름>` 으로 묶는다.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import datetime
import json
import re
import shutil
import sqlite3
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Iterable, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.exit_code import ExitCode  # noqa: E402
from common.git import run_git, run_git_or_raise  # noqa: E402
from common.paths import get_build_dir, get_project_root  # noqa: E402
from common.run_process import ProcessResult, build_preset_or_exit, run_process  # noqa: E402
from dev.scenario_reach import read_reach, select_reached  # noqa: E402
from net.ctest_items import read_items as read_net_items  # noqa: E402

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


# ---------------------------------------------------------------------------
# changed — origin/main 뒤에 들어온 테스트 · 시나리오를 먼저 돈다
# ---------------------------------------------------------------------------
CHANGED_OUTPUT = Path("Testing") / "Changed"   # 빌드 폴더 아래 이 단계의 로그 · 보고 — 돌 때마다 비운다
# 에디터 단위 테스트(창 · GPU 없는 `App --test-editor`) — 시나리오 읽기 검사 · 아이콘 · 테마 · 현지화 검사가 모두 여기다.
EDITOR_UNIT_TEST_ITEM = "UnitTests.Editor"
REFLECT_DECLARATION = re.compile(r"^\s*(?:REFLECT|PROPERTY|ENUM)\(", re.MULTILINE)
UNIT_ITEM_PATTERN = re.compile(r"^UnitTests(\.[A-Za-z0-9_]+)?$")
SOURCE_SUFFIXES = (".cpp", ".h", ".hpp", ".inl")
SCENARIO_SUFFIX = ".scenario.xml"
PAIR_SUFFIX = re.compile(r"\.[12]\.scenario\.xml$")   # 두 번 기동 쌍(`sequence/` · `statedir/`)은 ctest 가 cmake -P 로 돈다(`-DPAIR=<앞머리>`)
LAUNCH_VALUE = re.compile(r'variable="([^"]+)"[ \t]+value="([^"]*)"')   # Test/Scenario/CMakeLists.txt 의 sw_read_scenario_launch 와 같은 꼴
MERGE_SUBJECT = re.compile(r"^Merge (?:remote-tracking )?branch '(?:origin/)?([^']+)'")
SOURCE_PATH = re.compile(r"(?:^|/)(Source/.+)$")
# 시한은 ctest 항목의 TIMEOUT 속성이다(없으면 ctest 의 기본 1500 초). 단계가 여럿인 체인에서 꺼낸 시나리오 하나만 체인 시한(단계 초 × 단계 수 × 물결)을 나눠 쓸 수 없어
# — 단계 몫에는 기동 · 종료가 없다 — App 의 단일 시나리오 프로세스 시한(`AutomationTimeLimits::getChainKillSeconds( 1 )` 545 초) 위에 기동 · 셰이더 여유를 둔 값을 쓴다.
CTEST_DEFAULT_TIMEOUT_SECONDS = 1500
# 새 시나리오의 결정성 검사를 도는 백엔드 — 앞 단계에 달린 값은 게임 쪽에서 생겨 하나면 드러난다.
DETERMINISM_BACKEND = "dx12"
SINGLE_SCENARIO_TIMEOUT_SECONDS = 900
# 단위 실행의 기준 파일 없음(`SnapshotMissing`)은 시나리오와 같은 이름으로 적는다 — 표의 한 칸이 "기록했으니 보고 커밋할 것" 을 뜻한다.
UNIT_KIND_ALIAS = {"SnapshotMissing": "MissingBaseline"}


@dataclass(frozen=True)
class ChangedFile:
    status: str   # git 의 A(새것) · M(바뀐 것)
    path: str     # 저장소 기준, `/` 구분


@dataclass(frozen=True)
class CtestItem:
    name: str
    command: tuple[str, ...]
    working_directory: str
    timeout_seconds: int = CTEST_DEFAULT_TIMEOUT_SECONDS   # ctest TIMEOUT 속성


@dataclass
class ChangedRun:
    item: str                       # 표의 항목 이름
    files: list[ChangedFile]        # 이 실행을 고른 바뀐 파일
    command: list[str]
    working_directory: str
    output: Path                    # 단위: 보고 JSON, 시나리오: 보고 폴더
    timeout_seconds: int
    is_scenario: bool = False
    result: ProcessResult | None = field(default=None)


@dataclass(frozen=True)
class ChangedRow:
    item: str
    change: str     # 새것 · 바뀐 것
    kind: str       # 실패 종류(통과는 빈 글)
    reason: str
    branch: str


def parse_name_status(text: str) -> list[ChangedFile]:
    """`git diff --name-status --no-renames` — 지운 것은 돌 것이 없어 뺀다. 형식만 바뀐 것(T)은 바뀐 것이다."""
    list_file: list[ChangedFile] = []
    for line in text.splitlines():
        status, _, path = line.partition("\t")
        if status in ("A", "M", "T") and path:
            list_file.append(ChangedFile("A" if status == "A" else "M", path.strip().replace("\\", "/")))
    return list_file


def describe_change(list_file: Sequence[ChangedFile]) -> str:
    return "새것" if any(changed.status == "A" for changed in list_file) else "바뀐 것"


def find_feature_folder(path: str, list_test_file: Sequence[str]) -> str | None:
    """바뀐 파일의 기능 폴더(`/` 로 끝남) — `…/Tests/…` 는 `Tests` 앞, 아니면 그 파일의 폴더. 그 아래 테스트가 없으면 한 단계 위, 그래도 없으면 없음."""
    parts = path.split("/")
    parts = parts[:parts.index("Tests")] if "Tests" in parts else parts[:-1]
    # 한 단계 위까지만, `Source/<층>/<기능>/` 보다 넓게는 올라가지 않는다(층 전체는 전체 실행과 같다) — 먼 테스트는 cpu · gpu-release 에서 처음 돈다(D4 남는 위험).
    for depth in (len(parts), len(parts) - 1):
        if depth < 3:
            break
        folder = "/".join(parts[:depth]) + "/"
        if any(test.startswith(folder) for test in list_test_file):
            return folder
    return None


def select_unit_folders(list_changed: Sequence[ChangedFile], list_test_file: Sequence[str]) -> dict[str, list[ChangedFile]]:
    """기능 폴더 → 그 폴더를 고른 바뀐 파일. 에디터 폴더(`/Editor/`)는 뺀다 — 그 케이스는 `App --test` 에 등록되지 않고 `UnitTests.Editor` 가 돈다."""
    map_folder: dict[str, list[ChangedFile]] = {}
    for changed in list_changed:
        if not changed.path.startswith("Source/") or "/Editor/" in changed.path:
            continue
        if not (changed.path.endswith(SOURCE_SUFFIXES) or "/Snapshots/" in changed.path):
            continue
        folder = find_feature_folder(changed.path, list_test_file)
        if folder is not None:
            map_folder.setdefault(folder, []).append(changed)
    return map_folder


def find_golden_scenario(golden_path: str, list_scenario_file: Sequence[str]) -> str | None:
    """`<폴더>/golden/<시나리오>.<이름>.<백엔드>.ppm`(· 메타 `.golden.json`)의 시나리오(`<폴더>/<시나리오>.scenario.xml`). 이름이 가장 긴 것이 맞다."""
    folder, _, file_name = golden_path.rpartition("/golden/")
    list_match = [scenario for scenario in list_scenario_file
                  if scenario.rpartition("/")[0] == folder and file_name.startswith(scenario.rpartition("/")[2][: -len(SCENARIO_SUFFIX)] + ".")]
    return max(list_match, key=len) if list_match else None


def select_scenarios(list_changed: Sequence[ChangedFile], list_scenario_file: Sequence[str]) -> dict[str, list[ChangedFile]]:
    """시나리오(Content 기준 리소스 경로) → 그 시나리오를 고른 바뀐 파일(시나리오 파일 · 그 골든)."""
    map_scenario: dict[str, list[ChangedFile]] = {}
    for changed in list_changed:
        if not changed.path.startswith("Content/"):
            continue
        if changed.path.endswith(SCENARIO_SUFFIX):
            scenario: str | None = changed.path
        elif "/golden/" in changed.path:
            scenario = find_golden_scenario(changed.path, list_scenario_file)
        else:
            scenario = None
        if scenario is not None:
            map_scenario.setdefault(scenario[len("Content/"):], []).append(changed)
    return map_scenario


def has_reflected_type(path: str, read_text: Callable[[str], str]) -> bool:
    """리플렉션 형식을 선언하는 헤더 — 에디터 단위 테스트가 그 메타(아이콘 · 색 · 분류)를 데이터 표와 대조한다."""
    return path.startswith("Source/") and path.endswith(".h") and REFLECT_DECLARATION.search(read_text(path)) is not None


def select_editor_files(list_changed: Sequence[ChangedFile], read_text: Callable[[str], str]) -> list[ChangedFile]:
    """에디터 단위 테스트(`UnitTests.Editor`)를 돌게 하는 바뀐 파일 — 에디터 모듈 · 편집기 확장의 소스, 편집기 시나리오, 에디터 데이터, 리플렉션 헤더(어느 층이든)."""
    return [changed for changed in list_changed
            if (changed.path.startswith("Source/") and "/Editor/" in changed.path)
            or has_reflected_type(changed.path, read_text)
            or (changed.path.startswith("Content/") and "/automation/editor/" in changed.path and changed.path.endswith(SCENARIO_SUFFIX))
            or changed.path.startswith("Content/editor/")]


def parse_ctest_items(json_text: str) -> list[CtestItem]:
    """`ctest --show-only=json-v1` 의 항목(이름 · 명령 · 작업 폴더 · 시한). 명령이 없는 항목(꺼 둔 것)은 뺀다."""
    list_item: list[CtestItem] = []
    for test in json.loads(json_text).get("tests", []):
        if not test.get("command"):
            continue
        properties = {prop.get("name"): prop.get("value") for prop in test.get("properties", [])}
        timeout = int(float(properties.get("TIMEOUT", CTEST_DEFAULT_TIMEOUT_SECONDS)))
        list_item.append(CtestItem(test["name"], tuple(test["command"]), properties.get("WORKING_DIRECTORY", ""), timeout))
    return list_item


def matches_argument(token: str, key: str) -> bool:
    """`-key` · `--key` · `-key=값` · `--key=값`."""
    if not token.startswith("-"):
        return False
    bare = token.lstrip("-")
    return bare == key or bare.startswith(key + "=")


def get_argument(command: Sequence[str], key: str) -> str | None:
    for token in command:
        if matches_argument(token, key):
            return token.partition("=")[2]
    return None


def set_argument(command: Sequence[str], key: str, value: str | None, dash: str = "-") -> list[str]:
    """@p key 를 모두 빼고, 값이 있으면 `<dash>key=값` 을 끝에 둔다."""
    list_token = [token for token in command if not matches_argument(token, key)]
    if value is not None:
        list_token.append(f"{dash}{key}={value}")
    return list_token


def find_dash(command: Sequence[str], key: str) -> str:
    """그 실행 파일이 쓰는 앞머리 — `App --test` 는 `--`, 봇의 엔진 인자(`-test`)는 `-`."""
    return "--" if any(token == f"--{key}" or token.startswith(f"--{key}=") for token in command) else "-"


def make_unit_runs(list_item: Sequence[CtestItem], map_folder: dict[str, list[ChangedFile]], output: Path) -> list[ChangedRun]:
    """
    기능 폴더를 그 폴더를 싣는 단위 항목에 나눈다 — `--test-source` 가 있는 항목(게임 · 온라인 키트)은 제 폴더 아래 것만, 나머지는 `--test-source` 가 없는 항목
    (엔진 · 활성 게임)이 돈다. 거르개 항목(`UnitTests.OtherWorkingFolder`)은 뺀다. 몫으로 나눈 항목(`UnitTests.<i>`)은 첫 몫 하나에서 몫 인자를 빼고 돈다.
    """
    list_unit: list[CtestItem] = []
    for item in list_item:
        if not UNIT_ITEM_PATTERN.match(item.name) or item.name == EDITOR_UNIT_TEST_ITEM or get_argument(item.command, "test-filter") is not None:
            continue
        shard = get_argument(item.command, "test-shard")
        if shard is not None and not shard.startswith("0/"):
            continue
        list_unit.append(item)
    list_sourced = [(item, [folder for folder in (get_argument(item.command, "test-source") or "").split(",") if folder]) for item in list_unit]
    list_general = [item for item, list_owned in list_sourced if not list_owned]
    map_item_folder: dict[str, list[str]] = {}
    for folder in map_folder:
        list_owner = [item for item, list_owned in list_sourced if list_owned and any(folder.startswith(owned) for owned in list_owned)]
        for item in list_owner or list_general:
            map_item_folder.setdefault(item.name, []).append(folder)
    list_run: list[ChangedRun] = []
    for item in list_unit:
        list_folder = map_item_folder.get(item.name)
        if not list_folder:
            continue
        name = "UnitTests" if get_argument(item.command, "test-shard") is not None else item.name
        dash = find_dash(item.command, "test")
        command = set_argument(item.command, "test-source", ",".join(list_folder), dash)
        command = set_argument(command, "test-shard", None)
        command = set_argument(command, "test-report", str(output / f"{name}.json"), dash)
        command = set_argument(command, "log-file", str(output / f"{name}.log"))
        command.append(f"{dash}record-missing-snapshots")
        list_changed = [changed for folder in list_folder for changed in map_folder[folder]]
        list_run.append(ChangedRun(name, list_changed, command, item.working_directory, output / f"{name}.json", item.timeout_seconds))
    return list_run


def read_launch_arguments(scenario_file: Path) -> list[str]:
    """시나리오의 `<Launch variable value>` 를 명령줄 인자(`-이름=값`)로 — ctest 체인은 첫 단계의 것만 명령줄로 주므로 바꿔 끼운다."""
    list_argument: list[str] = []
    for line in scenario_file.read_text(encoding="utf-8").splitlines():
        if line.lstrip().startswith("<Launch "):
            match = LAUNCH_VALUE.search(line)
            if match:
                list_argument.append(f"-{match.group(1)}={match.group(2)}")
    return list_argument


def make_scenario_command(item: CtestItem, scenario: str, content_root: Path, output: Path) -> list[str]:
    """체인 항목의 명령을 시나리오 하나로 — 나누기 · 이어 가기를 빼 모든 백엔드에서 혼자 돌고, 첫 단계의 `<Launch>` 대신 이 시나리오의 것을 준다."""
    list_path = (get_argument(item.command, "scenario") or "").split(",")
    command = list(item.command)
    for argument in read_launch_arguments(content_root / list_path[0]):
        if argument in command:
            command.remove(argument)
    command = set_argument(command, "scenario", scenario)
    command = set_argument(command, "scenario-backend-split", None)
    command = set_argument(command, "scenario-backend-split-offset", None)
    command = set_argument(command, "scenario-keep-going", None)
    command = set_argument(command, "log-file", str(output / "parent.log"))
    # 셰이더 요청 기록은 체인 전체의 것이다 — 시나리오 하나로 덮으면 다음 검증의 데우기(preflight shader warm)가 그 몫만 컴파일한다.
    command = set_argument(command, "shader-request-log", None)
    command = set_argument(command, "scenario-report", str(output / (scenario.rpartition("/")[2][: -len(SCENARIO_SUFFIX)] + ".json")))
    command.append("-scenario-record-missing-golden")
    command += read_launch_arguments(content_root / scenario)
    return command


def read_manual_scenarios(project_root: Path) -> dict[str, str]:
    """ctest 가 띄울 수 없는 시나리오 → 까닭과 명령(`Config/Rules/Test/ManualScenarios.json`)."""
    table = json.loads((project_root / "Config" / "Rules" / "Test" / "ManualScenarios.json").read_text(encoding="utf-8"))
    return {entry["path"]: f"손으로 돈다: {entry['reason']} — `{entry['command']}`" for entry in table["_scenarios"]}


def make_scenario_runs(list_item: Sequence[CtestItem], map_scenario: dict[str, list[ChangedFile]], content_root: Path, build_dir: Path,
                       output: Path, find_branch: Callable[[str], str],
                       map_quarantine: dict[str, QuarantineEntry] | None = None, map_net: dict[str, str] | None = None,
                       map_manual: dict[str, str] | None = None) -> tuple[list[ChangedRun], list[ChangedRow]]:
    """
    시나리오마다 실행 하나. 두 번 기동 쌍 · 여러 프로세스 시나리오(@p map_net — 시나리오 → ctest 이름)는 ctest 항목 그대로(기록 없이) 돈다.
    항목이 띄울 수 없는 시나리오(@p map_manual — 시나리오 → 까닭과 명령)는 표에 그렇다고 적고, 어느 쪽도 아니면 실패다(아무도 돌리지 않는 시나리오다).
    """
    list_run: list[ChangedRun] = []
    list_row: list[ChangedRow] = []
    for scenario, list_changed in sorted(map_scenario.items()):
        folder = output / "scenario" / scenario.replace("/", ".")
        item = next((item for item in list_item if scenario in (get_argument(item.command, "scenario") or "").split(",")), None)
        quarantine = (map_quarantine or {}).get(item.name) if item is not None else None
        if quarantine is not None:
            # 격리한 ctest 의 시나리오는 묶음 검증이 빼고 돈다(gpu 단계의 `-E` 와 같은 표) — 표에 사유 · 만료를 남기고 돌리지 않는다.
            list_row.append(ChangedRow(scenario, describe_change(list_changed), "", f"격리 `{item.name}` — {quarantine.reason} (만료 {quarantine.expires.isoformat()})",
                                       find_branch(list_changed[0].path)))
            continue
        if item is not None:
            alone = len((get_argument(item.command, "scenario") or "").split(",")) == 1   # 시나리오 하나짜리 항목(부하 등)은 제 시한 그대로다
            list_run.append(ChangedRun(scenario, list_changed, make_scenario_command(item, scenario, content_root, folder), item.working_directory, folder,
                                       item.timeout_seconds if alone else SINGLE_SCENARIO_TIMEOUT_SECONDS, is_scenario=True))
            continue
        pair = PAIR_SUFFIX.sub("", scenario)
        pair_item = next((item for item in list_item if f"-DPAIR={pair}" in item.command), None) if pair != scenario else None
        if pair_item is not None:
            list_run.append(ChangedRun(scenario, list_changed, ["ctest", "-R", f"^{re.escape(pair_item.name)}$", "--output-on-failure"], str(build_dir),
                                       folder, pair_item.timeout_seconds))
            continue
        net_item = next((item for item in list_item if item.name == (map_net or {}).get(scenario)), None)
        if net_item is not None:
            if all(run.item != net_item.name for run in list_run):   # 한 항목이 여러 시나리오를 띄운다 — 한 번만 돈다
                list_run.append(ChangedRun(net_item.name, list_changed, ["ctest", "-R", f"^{re.escape(net_item.name)}$", "--output-on-failure"], str(build_dir),
                                           folder, net_item.timeout_seconds))
            continue
        manual = (map_manual or {}).get(scenario)
        list_row.append(ChangedRow(scenario, describe_change(list_changed), "" if manual else "NotRun", manual or "어느 ctest 항목도 띄우지 않는다 — 항목을 만들거나 Config/Rules/Test/ManualScenarios.json 에 까닭을 적는다",
                                   find_branch(list_changed[0].path)))
    return list_run, list_row


def make_editor_unit_run(list_item: Sequence[CtestItem], list_editor_file: list[ChangedFile], output: Path) -> ChangedRun | None:
    """에디터 쪽이 바뀌었으면 에디터 단위 테스트(`UnitTests.Editor`)를 통째로 도는 실행(13 초 안팎이라 좁히지 않는다)."""
    if not list_editor_file:
        return None
    item = next((item for item in list_item if item.name == EDITOR_UNIT_TEST_ITEM), None)
    if item is None:
        raise RuntimeError(f"ctest 에 {EDITOR_UNIT_TEST_ITEM} 이 없다 - 에디터 단위 테스트를 돌 수 없다")
    dash = find_dash(item.command, "test-editor")
    command = set_argument(list(item.command), "test-report", str(output / f"{item.name}.json"), dash)
    command = set_argument(command, "log-file", str(output / f"{item.name}.log"))
    command.append(f"{dash}record-missing-snapshots")
    return ChangedRun(item.name, list_editor_file, command, item.working_directory, output / f"{item.name}.json", item.timeout_seconds)


def get_last_result_line(text: str, prefix: str) -> str:
    list_line = [line.strip() for line in text.splitlines() if line.strip().startswith(prefix)]
    return list_line[-1] if list_line else ""


def make_one_line(text: str, limit: int = 200) -> str:
    line = text.strip().splitlines()[0] if text.strip() else ""
    return line if len(line) <= limit else line[: limit - 1] + "…"


def read_unit_rows(run: ChangedRun, map_changed: dict[str, ChangedFile], find_branch: Callable[[str], str]) -> list[ChangedRow]:
    """단위 실행 하나의 줄 — 진 케이스마다 한 줄(케이스 파일이 바뀐 파일이면 그 파일의 새것/바뀐 것 · 갈래), 보고가 없으면 끝 줄로 한 줄, 다 통과면 한 줄."""
    result = run.result or ProcessResult(return_code=-1, launched=False)
    first_branch = find_branch(run.files[0].path)
    try:
        report = json.loads(run.output.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        report = None
    list_row: list[ChangedRow] = []
    for case in (report or {}).get("cases", []):
        if case.get("passed") or case.get("skipped"):
            continue
        match = SOURCE_PATH.search(str(case.get("file", "")).replace("\\", "/"))
        changed = map_changed.get(match.group(1)) if match else None
        kind = UNIT_KIND_ALIAS.get(case.get("failureKind", ""), case.get("failureKind", "")) or "Assertion"
        list_row.append(ChangedRow(f"{run.item}: {case['name']}", "새것" if changed and changed.status == "A" else "바뀐 것", kind,
                                   make_one_line((case.get("failures") or [""])[0]), find_branch(changed.path) if changed else first_branch))
    if list_row:
        return list_row
    end_line = get_last_result_line(result.stdout, "TEST_RESULT=")
    if result.succeeded and report is not None:
        return [ChangedRow(run.item, describe_change(run.files), "", f"통과 — {report.get('runCount', 0)} 케이스", first_branch)]
    if result.timed_out:
        kind = "Timeout"
    elif end_line:
        kind = end_line.partition("=")[2].partition(" ")[0]
    else:
        kind = "Crash"
    reason = end_line or make_one_line(result.stderr) or f"종료 코드 {result.return_code}, 끝 줄 없음 — {run.output.with_suffix('.log')}"
    return [ChangedRow(run.item, describe_change(run.files), kind, reason, first_branch)]


def fold_scenario_failures(list_report: Sequence[dict]) -> list[tuple[str, str, str]]:
    """(종류, 까닭, 묶음) — 같은 (종류 · 단계)로 진 백엔드를 접는다. 돈 백엔드가 둘 이상이고 모두 같이 졌으면 `전제`(시나리오의 가정이 틀렸다), 하나만 졌으면 `백엔드 <이름>`."""
    list_ran = [report for report in list_report if report.get("result") != "Skipped"]
    groups: dict[tuple[str, str], list[dict]] = {}
    for report in list_ran:
        if report.get("result") in PASSED_RESULTS:
            continue
        failure = report.get("failure") or {}
        groups.setdefault((failure.get("kind") or report.get("result", ""), failure.get("step", "")), []).append(report)
    list_fold: list[tuple[str, str, str]] = []
    for (kind, _), group in groups.items():
        list_backend = sorted(str(report.get("backend", "")) for report in group)
        if len(list_ran) > 1 and len(group) == len(list_ran):
            label = "전제"
        elif len(list_ran) > 1 and len(group) == 1:
            label = f"백엔드 {list_backend[0]}"
        else:
            label = ",".join(list_backend)
        failure = group[0].get("failure") or {}
        list_fold.append((kind, make_one_line(failure.get("reason") or group[0].get("reason", "")), label))
    return list_fold


def read_scenario_rows(run: ChangedRun, find_branch: Callable[[str], str]) -> list[ChangedRow]:
    """시나리오 실행 하나의 줄 — 백엔드 자식 보고(`<이름>.<백엔드>.json`)를 접어 낸다. 보고가 없으면 끝 줄로 한 줄."""
    result = run.result or ProcessResult(return_code=-1, launched=False)
    change, branch = describe_change(run.files), find_branch(run.files[0].path)
    if not run.is_scenario:   # 두 번 기동 쌍 — ctest 의 성패만
        if result.succeeded:
            return [ChangedRow(run.item, change, "", "통과(ctest 쌍)", branch)]
        return [ChangedRow(run.item, change, "Assertion", "ctest 쌍이 졌다 — Testing/Logs 의 그 항목 로그", branch)]
    by_backend: dict[str, dict] = {}
    for report in sorted(read_reports([run.output]) if run.output.is_dir() else [], key=lambda report: report["startedAtUnixMs"]):
        by_backend[str(report.get("backend", ""))] = report   # 같은 백엔드는 마지막 것
    list_report = list(by_backend.values())
    if not list_report:
        end_line = get_last_result_line(result.stdout, "SCENARIO_RESULT=")
        kind = "Hang" if result.timed_out else "LoadError"
        return [ChangedRow(run.item, change, kind, end_line or f"보고가 없다(종료 코드 {result.return_code}) — {run.output / 'parent.log'}", branch)]
    return fold_scenario_rows(run.item, change, branch, by_backend)


def fold_scenario_rows(item: str, change: str, branch: str, by_backend: dict[str, dict]) -> list[ChangedRow]:
    """백엔드별 보고를 표 줄로 — 모든 백엔드가 건너뛰었으면 `NotRun`(새 · 바뀐 시나리오가 한 번도 돌지 않았다 — 통과로 적으면 거짓 통과다), 일부만이면 통과 줄에 건너뛴 백엔드를 적는다."""
    list_report = list(by_backend.values())
    list_skipped = sorted(backend for backend, report in by_backend.items() if report.get("result") == "Skipped")
    if len(list_skipped) == len(list_report):
        return [ChangedRow(item, change, "NotRun", f"모든 백엔드가 건너뛰었다 — {make_one_line(list_report[0].get('reason', ''))}", branch)]
    list_fold = fold_scenario_failures(list_report)
    if not list_fold:
        list_ran = sorted(backend for backend in by_backend if backend not in list_skipped)
        skipped_note = f" (건너뜀: {','.join(list_skipped)})" if list_skipped else ""
        return [ChangedRow(item, change, "", "통과 — " + ",".join(list_ran) + skipped_note, branch)]
    return [ChangedRow(item, change, kind, f"[{label}] {reason}", branch) for kind, reason, label in list_fold]


def parse_branch(subject: str) -> str:
    """first-parent 커밋 제목 → 갈래 이름. 병합이 아니면 직접 커밋이다."""
    match = MERGE_SUBJECT.match(subject)
    if match:
        return match.group(1)
    return f"(직접) {make_one_line(subject, 40)}" if subject else "?"


def make_find_branch(project_root: Path, base: str) -> Callable[[str], str]:
    """파일 → 그 파일을 마지막으로 더한 갈래(`<base>..HEAD` 의 first-parent 커밋 — 병합이면 그 갈래 이름)."""
    cache: dict[str, str] = {}

    def find(path: str) -> str:
        if path not in cache:
            result = run_git(["log", "--first-parent", "--format=%s", "-1", f"{base}..HEAD", "--", path], cwd=project_root)
            cache[path] = parse_branch(result.stdout.strip()) if result.succeeded else "?"
        return cache[path]
    return find


def format_changed_table(list_row: Sequence[ChangedRow]) -> list[str]:
    list_line = ["| 항목 | 새것/바뀐 것 | 실패 종류 | 까닭 | 갈래 |", "| --- | --- | --- | --- | --- |"]
    for row in list_row:
        cells = (row.item, row.change, row.kind or "-", row.reason, row.branch)
        list_line.append("| " + " | ".join(cell.replace("|", "\\|") for cell in cells) + " |")
    return list_line


def list_new_references(status_text: str) -> list[str]:
    """`git status --porcelain --untracked-files=all` 의 추적 안 된 골든 · 스냅숏 — 이 단계가 처음 쓴 기준 파일이다(앞 실행이 남긴 것도 아직 사람이 보지 않았다)."""
    return sorted(line[3:].strip() for line in status_text.splitlines()
                  if line.startswith("?? ") and ("/golden/" in line or "/Snapshots/" in line))


def decide_changed(list_row: Sequence[ChangedRow], list_recorded: Sequence[str], sha: str) -> str:
    return "CHANGED_FAILED" if list_recorded or any(row.kind for row in list_row) else f"CHANGED_OK={sha}"


def run_new_scenario_determinism(list_item: Sequence[CtestItem], map_scenario: dict[str, list[ChangedFile]], content_root: Path, build_dir: Path,
                                 find_branch: Callable[[str], str], map_quarantine: dict[str, QuarantineEntry]) -> list[ChangedRow]:
    """새 시나리오마다 혼자 · 앞 단계 하나 · 두 번 이어 돌려 사진을 바이트로 견준 줄(사진을 찍지 않는 시나리오는 통과 줄).

    바뀐 시나리오는 빼고 **새것만**, 백엔드 하나에서 돈다 — 앞 단계에 달린 값은 게임 쪽에서 생겨 백엔드와 무관하고, 시나리오 하나에 기동 셋 · 단계 다섯이 든다.
    나머지 시나리오는 체인이 위치를 섞어 돌므로 골든이 본다."""
    from dev.scenario_determinism import check_determinism, make_runner
    from dev.scenario_bisect import read_scenario_name
    list_row: list[ChangedRow] = []
    for scenario, list_changed in sorted(map_scenario.items()):
        is_new = any(changed.status == "A" and changed.path == f"Content/{scenario}" for changed in list_changed)
        item = next((item for item in list_item if scenario in (get_argument(item.command, "scenario") or "").split(",")), None)
        has_photo = "<Screenshot" in (content_root / scenario).read_text(encoding="utf-8") or "<ExpectImage" in (content_root / scenario).read_text(encoding="utf-8")
        if not is_new or item is None or not has_photo or item.name in map_quarantine:
            continue
        list_path = (get_argument(item.command, "scenario") or "").split(",")
        name = read_scenario_name(scenario)
        working = Path(item.working_directory) if item.working_directory else build_dir
        runner = make_runner(item.command, name, content_root, DETERMINISM_BACKEND, build_dir, working)
        print(f"[Changed] run {scenario}: determinism ({DETERMINISM_BACKEND})", flush=True)
        list_problem = check_determinism(list_path, list_path.index(scenario), name, working / "Saved" / "Automation", runner)
        list_row.append(ChangedRow(f"{scenario} (결정성)", describe_change(list_changed), "Determinism" if list_problem else "",
                                   "; ".join(list_problem) or "통과", find_branch(list_changed[0].path)))
    return list_row


def run_changed(project_root: Path, preset: str, base: str) -> int:
    build_dir = get_build_dir(preset, project_root)
    if not build_dir.is_dir():
        print(f"[Changed] {build_dir} 가 없다 — 빌드 뒤에 돈다")
        print("CHANGED_FAILED")
        return ExitCode.CHECK_INVALID
    sha = run_git_or_raise(["rev-parse", "HEAD"], cwd=project_root).strip()
    list_changed = parse_name_status(run_git_or_raise(["diff", "--name-status", "--no-renames", f"{base}...HEAD"], cwd=project_root))
    list_test_file = [path for path in run_git_or_raise(["ls-files", "--", "Source"], cwd=project_root).splitlines() if "/Tests/" in path and path.endswith(".cpp")]
    list_content_file = run_git_or_raise(["ls-files", "--", "Content"], cwd=project_root).splitlines()
    list_scenario_file = [path for path in list_content_file if path.endswith(SCENARIO_SUFFIX)]
    map_folder = select_unit_folders(list_changed, list_test_file)
    map_scenario = select_scenarios(list_changed, list_scenario_file)
    # 시나리오 · 골든이 그대로여도 그 시나리오가 여는 에셋 · 쓰는 감시 값이 바뀌었으면 돈다(scenario_reach).
    map_reached, list_reach_line = select_reached(read_reach(project_root, [changed.path for changed in list_changed], list_content_file), set(map_scenario),
                                                  int(read_verify_config(project_root, "_changedReachPerFile")), int(read_verify_config(project_root, "_changedReachTotal")))
    for line in list_reach_line:
        print(f"[Changed] {line}")
    map_changed_file = {changed.path: changed for changed in list_changed}
    for scenario, list_path in map_reached.items():
        map_scenario[scenario] = [map_changed_file.get(path) or map_changed_file[f"Content/{path}"] for path in list_path]
    list_editor_file = select_editor_files(list_changed, lambda path: (project_root / path).read_text(encoding="utf-8", errors="replace"))
    print(f"[Changed] {base}...HEAD: 바뀐 파일 {len(list_changed)} · 기능 폴더 {len(map_folder)} · 시나리오 {len(map_scenario)}(닿음 {len(map_reached)}) · 에디터 파일 {len(list_editor_file)}")
    if not map_folder and not map_scenario and not list_editor_file:
        print(f"CHANGED_OK={sha}")
        return ExitCode.PASSED
    # ctest 항목도 이 빌드가 정한다(구성이 CMake 파일의 바뀜을 다시 읽는다). 묶음 검증은 앞 단계가 이미 지어 몇 초다.
    try:
        build_preset_or_exit(preset, cwd=project_root)
    except SystemExit as problem:
        print(f"[Changed] {problem}")
        print("CHANGED_FAILED")
        return ExitCode.VIOLATION
    shown = run_process(["ctest", "--show-only=json-v1"], cwd=build_dir)
    if not shown.succeeded:
        print(f"[Changed] ctest 항목을 읽지 못했다: {make_one_line(shown.stderr or shown.stdout)}")
        print("CHANGED_FAILED")
        return ExitCode.VIOLATION
    list_item = parse_ctest_items(shown.stdout)
    output = build_dir / CHANGED_OUTPUT
    shutil.rmtree(output, ignore_errors=True)
    output.mkdir(parents=True)
    find_branch = make_find_branch(project_root, base)
    list_unit_run = make_unit_runs(list_item, map_folder, output)
    list_quarantine, list_quarantine_problem = read_quarantine(project_root, datetime.date.today())
    if list_quarantine_problem:
        for problem in list_quarantine_problem:
            print(f"[Changed] {problem}")
        print("CHANGED_FAILED")
        return ExitCode.VIOLATION
    map_quarantine = {entry.test: entry for entry in list_quarantine}
    map_net = {scenario: item.test_name for item in read_net_items() for scenario in item.scenarios}
    list_scenario_run, list_row = make_scenario_runs(list_item, map_scenario, project_root / "Content", build_dir, output, find_branch, map_quarantine, map_net,
                                                     read_manual_scenarios(project_root))
    editor_unit_run = make_editor_unit_run(list_item, list_editor_file, output)
    if editor_unit_run is not None:
        list_unit_run.append(editor_unit_run)   # 단위와 함께 — 시나리오 읽기 오류 · 없는 이름을 시나리오보다 먼저 알린다

    def execute(run: ChangedRun) -> None:
        print(f"[Changed] run {run.item}: {' '.join(run.command)}", flush=True)   # `run ` 머리 — preflight.sh 가 이 줄을 빼고 표만 찍는다
        working = Path(run.working_directory) if run.working_directory else build_dir
        run.result = run_process(run.command, cwd=working, timeout_seconds=run.timeout_seconds)

    # 단위는 CPU 일이라 함께, 시나리오는 시간 · 그림을 판정하므로 하나씩(묶음 검증의 gpu 단계를 혼자 도는 것과 같은 까닭).
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, len(list_unit_run))) as pool:
        list(pool.map(execute, list_unit_run))
    for run in list_scenario_run:
        execute(run)
    list_row += run_new_scenario_determinism(list_item, map_scenario, project_root / "Content", build_dir, find_branch, map_quarantine)
    map_changed = {changed.path: changed for changed in list_changed}
    for run in list_unit_run:
        list_row += read_unit_rows(run, map_changed, find_branch)
    for run in list_scenario_run:
        list_row += read_scenario_rows(run, find_branch)
    for line in format_changed_table(list_row):
        print(line)
    list_recorded = list_new_references(run_git_or_raise(["status", "--porcelain", "--untracked-files=all", "--", "Content", "Source"], cwd=project_root))
    if list_recorded:
        print(f"[Changed] 처음 쓴 기준 파일 {len(list_recorded)} 개 — 그림 · 글을 보고 커밋한 뒤 `preflight.sh --from changed` 로 기록 없이 통과시킨다:")
        for path in list_recorded:
            print(f"  {path}")
    final = decide_changed(list_row, list_recorded, sha)
    print(final)
    return ExitCode.PASSED if final.startswith("CHANGED_OK=") else ExitCode.VIOLATION


def run_changed_self_check() -> None:
    list_changed = parse_name_status("A\tSource/Engine/Foo/Tests/TestFoo.cpp\nM\tSource/Engine/Foo/Foo.cpp\nD\tSource/Gone.cpp\n"
                                     "M\tSource/Engine/Bar/Deep/Bar.cpp\nM\tSource/Editor/Panel/Tests/TestPanel.cpp\n"
                                     "A\tSource/Engine/Foo/Tests/Snapshots/FooTest.case.greeting.txt\nM\tSource/Engine/NoTests/X.cpp\n"
                                     "A\tSource/GameFramework/Kits/Feature/Inv/Tests/TestInv.cpp\n"
                                     "A\tContent/game/g/automation/new.scenario.xml\nM\tContent/game/g/automation/golden/old.view.dx12.ppm\n"
                                     "M\tContent/engine/automation/rhiswap/x.scenario.xml\nA\tContent/game/g/automation/sequence/pair.1.scenario.xml\n")
    assert [changed.status for changed in list_changed].count("A") == 5 and all(changed.path != "Source/Gone.cpp" for changed in list_changed), "지운 파일은 돌 것이 없다"
    list_test_file = ["Source/Engine/Foo/Tests/TestFoo.cpp", "Source/Engine/Bar/Tests/TestBar.cpp", "Source/Editor/Panel/Tests/TestPanel.cpp",
                      "Source/GameFramework/Kits/Feature/Inv/Tests/TestInv.cpp"]
    assert find_feature_folder("Source/Engine/Foo/Foo.cpp", list_test_file) == "Source/Engine/Foo/"
    assert find_feature_folder("Source/Engine/Bar/Deep/Bar.cpp", list_test_file) == "Source/Engine/Bar/", "테스트 없는 폴더는 한 단계 위"
    assert find_feature_folder("Source/Engine/NoTests/X.cpp", list_test_file) is None
    map_folder = select_unit_folders(list_changed, list_test_file)
    assert sorted(map_folder) == ["Source/Engine/Bar/", "Source/Engine/Foo/", "Source/GameFramework/Kits/Feature/Inv/"], map_folder   # 에디터 · 테스트 없는 폴더는 빠진다
    assert len(map_folder["Source/Engine/Foo/"]) == 3, "케이스 · 소스 · 스냅숏이 한 폴더로 모인다"

    list_scenario_file = ["Content/game/g/automation/new.scenario.xml", "Content/game/g/automation/old.scenario.xml",
                          "Content/game/g/automation/old.view.scenario.xml", "Content/engine/automation/rhiswap/x.scenario.xml",
                          "Content/game/g/automation/sequence/pair.1.scenario.xml"]
    assert find_golden_scenario("Content/game/g/automation/golden/old.view.dx12.ppm", list_scenario_file) == "Content/game/g/automation/old.view.scenario.xml", "가장 긴 이름"
    map_scenario = select_scenarios(list_changed, list_scenario_file)
    assert sorted(map_scenario) == ["engine/automation/rhiswap/x.scenario.xml", "game/g/automation/new.scenario.xml", "game/g/automation/old.view.scenario.xml",
                                    "game/g/automation/sequence/pair.1.scenario.xml"], map_scenario

    def read_no_text(path: str) -> str:
        return ""
    assert [changed.path for changed in select_editor_files(list_changed, read_no_text)] == ["Source/Editor/Panel/Tests/TestPanel.cpp"], "에디터 소스"
    header_changed = [ChangedFile("M", "Source/Engine/Foo/Light.h"), ChangedFile("M", "Source/Engine/Foo/Plain.h"), ChangedFile("M", "Source/Engine/Foo/Light.cpp")]
    header_text = {"Source/Engine/Foo/Light.h": 'x\n    REFLECT( Meta = "Icon=Curve" )\n', "Source/Engine/Foo/Plain.h": "// REFLECT( 주석 )\n"}
    assert [changed.path for changed in select_editor_files(header_changed, lambda path: header_text[path])] == ["Source/Engine/Foo/Light.h"], "메타만 바뀐 엔진 헤더"
    editor_changed = [ChangedFile("M", path) for path in ("Source/GameFramework/Plugin/Editor/X.cpp", "Content/game/g/automation/editor/e.scenario.xml",
                                                          "Content/editor/fonts/i.glyphs.xml", "Content/game/g/automation/editor/golden/e.dx12.ppm")]
    assert len(select_editor_files(editor_changed, read_no_text)) == 3, "편집기 확장 · 편집기 시나리오 · 에디터 데이터(골든은 아니다)"
    editor_item = CtestItem(EDITOR_UNIT_TEST_ITEM, ("App.exe", "--test-editor", "-unattended", "-log-file=L/E.log", "--test-report=R/E.json"), "B/Bin")
    editor_run = make_editor_unit_run([editor_item], editor_changed, Path("O"))
    assert editor_run is not None and not editor_run.is_scenario and "--test-editor" in editor_run.command, editor_run
    assert f"--test-report={Path('O') / 'UnitTests.Editor.json'}" in editor_run.command and "--record-missing-snapshots" in editor_run.command, editor_run.command
    assert make_editor_unit_run([editor_item], [], Path("O")) is None, "에디터 쪽이 그대로면 돌지 않는다"
    assert make_unit_runs([editor_item], {"Source/Engine/Foo/": list_changed[:1]}, Path("O")) == [], "에디터 항목은 기능 폴더를 받지 않는다"

    shown = json.dumps({"tests": [
        {"name": "UnitTests", "command": ["App.exe", "--test", "-unattended", "-log-file=L/UnitTests.log", "--test-report=R/UnitTests.json"],
         "properties": [{"name": "WORKING_DIRECTORY", "value": "B/Bin"}, {"name": "TIMEOUT", "value": 600.0}]},
        {"name": "UnitTests.OtherWorkingFolder", "command": ["App.exe", "--test", "--test-filter=FileSystemTest.*"]},
        {"name": "UnitTests.Rpg", "command": ["App.exe", "--test", "-game=Rpg", "--test-source=Source/GameFramework/Kits/Feature/Inv/,Source/Games/Rpg/"]},
        {"name": "UnitTests.OnlineKits", "command": ["Bot.exe", "-test", "-test-source=Source/GameFramework/Kits/Online/"]},
        {"name": "Scenario.g", "command": ["App.exe", "-scenario=game/g/automation/first.scenario.xml,game/g/automation/new.scenario.xml", "-scenario-keep-going",
                                           "-scenario-backends=all", "-scenario-backend-split=independent", "-unattended", "-log-file=L/Scenario.g.log", "-gv_first=1"]},
        {"name": "Scenario.g.sequence.pair", "command": ["cmake", "-DAPP=App.exe", "-DPAIR=game/g/automation/sequence/pair", "-P", "Run.cmake"],
         "properties": [{"name": "TIMEOUT", "value": 600}]},
        {"name": "Disabled"}]})
    list_item = parse_ctest_items(shown)
    assert [item.name for item in list_item][-1] == "Scenario.g.sequence.pair" and list_item[0].working_directory == "B/Bin"
    assert list_item[0].timeout_seconds == 600 and list_item[1].timeout_seconds == CTEST_DEFAULT_TIMEOUT_SECONDS, "시한은 ctest TIMEOUT, 없으면 ctest 기본"
    by_item = {run.item: run for run in make_unit_runs(list_item, map_folder, Path("O"))}
    assert sorted(by_item) == ["UnitTests", "UnitTests.Rpg"], by_item   # 거르개 항목은 빠지고, 온라인 키트는 바뀐 폴더가 없다
    assert sorted(get_argument(by_item["UnitTests"].command, "test-source").split(",")) == ["Source/Engine/Bar/", "Source/Engine/Foo/"]
    assert get_argument(by_item["UnitTests.Rpg"].command, "test-source") == "Source/GameFramework/Kits/Feature/Inv/", "게임 항목은 제 폴더 아래 것만"
    unit_command = by_item["UnitTests"].command
    assert "--record-missing-snapshots" in unit_command and f"--test-report={Path('O') / 'UnitTests.json'}" in unit_command
    assert f"-log-file={Path('O') / 'UnitTests.log'}" in unit_command and "-log-file=L/UnitTests.log" not in unit_command
    assert set_argument(["Bot.exe", "-test", "-test-source=A/"], "test-source", "B/", find_dash(["Bot.exe", "-test"], "test"))[-1] == "-test-source=B/", "봇은 대시 하나"

    with tempfile.TemporaryDirectory() as temporary:
        content_root = Path(temporary)
        (content_root / "game/g/automation").mkdir(parents=True)
        (content_root / "game/g/automation/first.scenario.xml").write_text('<Scenario>\n  <Launch variable="gv_first" value="1"/>\n</Scenario>\n', encoding="utf-8")
        (content_root / "game/g/automation/new.scenario.xml").write_text('<Scenario>\n\t<Launch variable="gv_new" value="two words"/>\n</Scenario>\n', encoding="utf-8")
        list_scenario_run, list_row = make_scenario_runs(list_item, {key: value for key, value in map_scenario.items() if "old.view" not in key}, content_root,
                                                         Path("B"), Path("O"), lambda path: "w-x", map_manual={"engine/automation/rhiswap/x.scenario.xml": "까닭"})
        _, list_lost_row = make_scenario_runs(list_item, {"engine/automation/rhiswap/x.scenario.xml": map_scenario["engine/automation/rhiswap/x.scenario.xml"]}, content_root,
                                              Path("B"), Path("O"), lambda path: "w-x")
        assert [row.kind for row in list_lost_row] == ["NotRun"], "어느 항목도 띄우지 않는 시나리오는 실패다"
        net_item = CtestItem("Net.x", ("py",), "", 30)
        list_net_run, list_net_row = make_scenario_runs([net_item], {key: map_scenario["engine/automation/rhiswap/x.scenario.xml"] for key in ("a.scenario.xml", "b.scenario.xml")}, content_root,
                                                        Path("B"), Path("O"), lambda path: "w-x", map_net={"a.scenario.xml": "Net.x", "b.scenario.xml": "Net.x"})
        assert [run.item for run in list_net_run] == ["Net.x"] and not list_net_row, "여러 프로세스 항목은 한 번만 돈다"
    command = next(run.command for run in list_scenario_run if run.item == "game/g/automation/new.scenario.xml")
    assert get_argument(command, "scenario") == "game/g/automation/new.scenario.xml" and get_argument(command, "scenario-backends") == "all"
    assert get_argument(command, "scenario-backend-split") is None and get_argument(command, "scenario-keep-going") is None, "모든 백엔드에서 혼자"
    assert "-gv_first=1" not in command and command[-1] == "-gv_new=two words" and "-scenario-record-missing-golden" in command, command
    assert any(run.command[0] == "ctest" and "^Scenario\\.g\\.sequence\\.pair$" in run.command and run.timeout_seconds == 600 for run in list_scenario_run), "쌍은 ctest 항목 그대로"
    assert next(run.timeout_seconds for run in list_scenario_run if run.item == "game/g/automation/new.scenario.xml") == SINGLE_SCENARIO_TIMEOUT_SECONDS, "체인에서 꺼낸 하나"
    assert [row.item for row in list_row] == ["engine/automation/rhiswap/x.scenario.xml"] and list_row[0].kind == "", "까닭이 적힌 것은 실패가 아니다"
    chain_item = next(item for item in list_item if "game/g/automation/new.scenario.xml" in (get_argument(item.command, "scenario") or "").split(","))
    quarantined = {chain_item.name: QuarantineEntry(chain_item.name, "흔들림", datetime.date(2099, 1, 1))}
    list_scenario_run_q, list_row_q = make_scenario_runs(list_item, {key: value for key, value in map_scenario.items() if key == "game/g/automation/new.scenario.xml"}, Path("C"),
                                                         Path("B"), Path("O"), lambda path: "w-x", quarantined)
    assert not list_scenario_run_q and len(list_row_q) == 1 and list_row_q[0].kind == "" and "격리" in list_row_q[0].reason, "격리한 항목은 돌지 않고 표에 사유를 남긴다"

    def make_report(backend: str, result: str, kind: str = "", step: str = "", reason: str = "") -> dict:
        return {"name": "s", "backend": backend, "result": result, "startedAtUnixMs": 1, "failure": {"kind": kind, "step": step, "reason": reason}}
    premise = fold_scenario_failures([make_report(backend, "Failed", "Assertion", "#3", "PiP equal") for backend in ("dx11", "dx12", "vk", "gl")])
    assert premise == [("Assertion", "PiP equal", "전제")], premise
    # 모든 백엔드가 건너뛴 새 시나리오는 통과가 아니다(requiresModule 이 꺼진 모듈 — editor.netsequence 가 "통과" 로 적혔다)
    skipped_rows = fold_scenario_rows("s", "새것", "b", {"dx12": {"result": "Skipped", "reason": "requiresModule: X is not loaded"}, "vk": {"result": "Skipped", "reason": "requiresModule: X is not loaded"}})
    assert [row.kind for row in skipped_rows] == ["NotRun"], skipped_rows
    partial_rows = fold_scenario_rows("s", "새것", "b", {"dx12": {"result": "Passed"}, "gl": {"result": "Skipped", "reason": "no GL"}})
    assert partial_rows[0].kind == "" and "건너뜀: gl" in partial_rows[0].reason, partial_rows
    single = fold_scenario_failures([make_report("dx11", "Passed"), make_report("vk", "Failed", "GoldenMismatch", "#5", "1.2% differing"), make_report("gl", "Skipped")])
    assert single == [("GoldenMismatch", "1.2% differing", "백엔드 vk")], single
    assert parse_branch("Merge branch 'w-st5'") == "w-st5" and parse_branch("Merge branch 'w-a' into main") == "w-a"
    assert parse_branch("ST-1: 고친다").startswith("(직접) ")

    unit_run = ChangedRun("UnitTests", [ChangedFile("A", "Source/Engine/Foo/Tests/TestFoo.cpp")], [], "", Path("O/none.json"), 1,
                          result=ProcessResult(return_code=1, stdout="TEST_RESULT=NO_TESTS run=0 failed=0 skipped=0\n"))
    [no_tests] = read_unit_rows(unit_run, {}, lambda path: "w-u")
    assert no_tests.kind == "NO_TESTS" and no_tests.change == "새것" and no_tests.branch == "w-u", no_tests
    with tempfile.TemporaryDirectory() as temporary:
        report_path = Path(temporary) / "UnitTests.json"
        report_path.write_text(json.dumps({"runCount": 2, "cases": [
            {"name": "Foo.new", "file": "D:/repo/Source/Engine/Foo/Tests/TestFoo.cpp", "passed": False, "skipped": False, "failureKind": "SnapshotMissing",
             "failures": ["TestFoo.cpp(9): no snapshot yet: recorded x"]},
            {"name": "Foo.old", "file": "D:/repo/Source/Engine/Foo/Tests/TestFoo.cpp", "passed": True, "skipped": False, "failureKind": "", "failures": []}]}),
            encoding="utf-8")
        snapshot_run = ChangedRun("UnitTests", list_changed[:1], [], "", report_path, 1, result=ProcessResult(return_code=1))
        [missing] = read_unit_rows(snapshot_run, {changed.path: changed for changed in list_changed}, lambda path: "w-s")
    assert missing == ChangedRow("UnitTests: Foo.new", "새것", "MissingBaseline", "TestFoo.cpp(9): no snapshot yet: recorded x", "w-s"), missing
    rows = [ChangedRow("a", "새것", "", "통과", "w"), ChangedRow("b|c", "바뀐 것", "MissingBaseline", "no golden", "w")]
    table = format_changed_table(rows)
    assert table[0] == "| 항목 | 새것/바뀐 것 | 실패 종류 | 까닭 | 갈래 |" and table[3] == "| b\\|c | 바뀐 것 | MissingBaseline | no golden | w |", table
    assert list_new_references("?? Content/game/g/automation/golden/new.v.dx12.ppm\n?? Saved/x\n M Source/a.cpp\n?? Source/E/Tests/Snapshots/A.b.txt\n") == \
        ["Content/game/g/automation/golden/new.v.dx12.ppm", "Source/E/Tests/Snapshots/A.b.txt"]
    assert decide_changed(rows[:1], [], "abc") == "CHANGED_OK=abc"
    assert decide_changed(rows, [], "abc") == "CHANGED_FAILED" and decide_changed(rows[:1], ["x"], "abc") == "CHANGED_FAILED", "실패 · 처음 쓴 기준 파일은 멈춘다"


def run_history_self_check() -> None:
    """실행 종류 · 흔들림 · 느려짐 · 단계 시간."""
    connection = sqlite3.connect(":memory:")
    connection.executescript(SCHEMA.replace("    runKind TEXT NOT NULL DEFAULT '',\n", ""))   # 옛 DB 모양에 칸을 더한다
    prepare_schema(connection)
    old = [{"name": "s", "result": "Failed", "startedAtUnixMs": 10, "backend": "dx12"}]
    new = [{"name": "s", "result": result, "startedAtUnixMs": started, "backend": "dx12"} for result, started in (("Passed", 100), ("Failed", 101))]
    assert ingest(connection, old + new, "c1", "verify", since_unix_ms=100) == 3
    assert sorted(connection.execute("SELECT commitSha, runKind FROM run")) == [("", "manual"), ("c1", "verify"), ("c1", "verify")], "표지 앞의 보고는 손 실행"
    flaky, group_count = find_flaky(connection, 20, set())
    assert flaky == [("s", "dx12", "c1")] and group_count == 1, (flaky, group_count)
    assert find_flaky(connection, 20, {"s"})[0] == [], "격리한 이름은 빠진다"
    assert decide_flaky(flaky, group_count, 0.01, 20)[0] is False and decide_flaky([], 100, 0.01, 20) == (True, "FLAKY=0 rate=0.00% limit=1.00% groups=100 commits<=20")
    assert ingest(connection, [{"name": "s", "result": "Failed", "startedAtUnixMs": 200, "backend": "dx12"}], "c2", "rerun") == 1
    assert find_flaky(connection, 20, set())[1] == 1, "다시 돌리기는 흔들림 판정에 들지 않는다"
    timed = [{"name": "t", "result": "Passed", "startedAtUnixMs": 300 + index, "wallMilliseconds": 100, "backend": "vk"} for index in range(3)]
    ingest(connection, timed, "c3", "verify")
    assert find_slow_runs(connection, [{"name": "t", "result": "Passed", "startedAtUnixMs": 400, "wallMilliseconds": 160, "backend": "vk"}], 1.5) != []
    assert find_slow_runs(connection, [{"name": "t", "result": "Passed", "startedAtUnixMs": 400, "wallMilliseconds": 140, "backend": "vk"}], 1.5) == []
    assert find_slow_runs(connection, [{"name": "t", "result": "Passed", "startedAtUnixMs": 302, "wallMilliseconds": 900, "backend": "vk"}], 1.5) == [], "표본이 모자라면 견주지 않는다"
    for index in range(3):
        assert record_stage(connection, "cpu", "c", "passed", 100.0, index, 1.5) is None, "표본이 모자라면 경고하지 않는다"
    assert record_stage(connection, "cpu", "c", "passed", 200.0, 10, 1.5) is not None and record_stage(connection, "cpu", "c", "failed", 900.0, 11, 1.5) is None


def run_self_check() -> None:
    connection = sqlite3.connect(":memory:")
    prepare_schema(connection)
    list_report = [{"name": "a", "result": result, "startedAtUnixMs": index, "wallMilliseconds": wall, "backend": "dx12"}
                  for index, (result, wall) in enumerate([("Passed", 100), ("Failed", 100), ("Passed", 300), ("Passed", 300)])]
    assert ingest(connection, list_report, "abc") == 4 and ingest(connection, list_report, "abc") == 0, "같은 보고는 한 번만"
    [stats] = compute_stats(connection, 3)
    assert stats.failures == 1 and abs(stats.flakiness - 2 / 3) < 1e-9, "흔들림 = 바뀐 횟수 / (실행 - 1)"
    assert stats.get_trend() == 3.0, "최근 절반 p95 / 앞 절반 p95"
    # 건너뜀(환경이 없다 — 시나리오 `Skipped`, 단위 테스트 `SW_TEST_SKIP`)은 실패로도 흔들림으로도 세지 않는다.
    list_skip = [{"name": "b", "result": result, "startedAtUnixMs": index, "wallMilliseconds": 10}
                 for index, result in enumerate(["Passed", "Skipped", "Passed", "Skipped"])]
    assert ingest(connection, list_skip, "abc") == 4
    [skip_stats] = [stats for stats in compute_stats(connection, 3) if stats.name == "b"]
    assert skip_stats.failures == 0 and skip_stats.flakiness == 0.0, "건너뜀은 실패가 아니다"
    assert compute_percentile([1, 2, 3, 4, 5, 6, 7, 8, 9, 10], 0.95) == 10.0
    assert make_ctest_exclude([QuarantineEntry("Scenario.a.b", "r", datetime.date(2030, 1, 1))]) == r"^Scenario\.a\.b$"
    # 시간표: 통과한 실행만, 백엔드별 p50 을 더한다. 표에 없는 이름은 중앙값, 빈 표는 오류.
    list_cost = [{"name": "c", "result": result, "startedAtUnixMs": 100 + index, "wallMilliseconds": wall, "backend": backend}
                 for index, (result, wall, backend) in enumerate([("Passed", 1000, "dx12"), ("Passed", 3000, "dx12"), ("Passed", 2000, "dx12"),
                                                                 ("Failed", 90000, "dx12"), ("Passed", 500, "vulkan")])]
    assert ingest(connection, list_cost, "abc") == 5
    map_cost = compute_scenario_costs(connection)
    assert map_cost["c"] == 2.5, map_cost   # dx12 p50 2 초 + vulkan 0.5 초, 진 90 초는 빠진다
    assert find_scenario_cost(map_cost, "c") == 2.5
    assert find_scenario_cost({"x": 1.0, "y": 3.0, "z": 9.0}, "new") == 3.0
    try:
        find_scenario_cost({}, "new")
        raise AssertionError("빈 표는 오류여야 한다")
    except ValueError:
        pass
    assert json.loads(make_scenario_costs_text(map_cost))["scenarioSeconds"] == map_cost
    run_changed_self_check()
    run_history_self_check()


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="시나리오 보고 이력(sqlite) · 흔들림 · p95 추세 · 격리 표")
    parser.add_argument("--self-check", action="store_true")
    sub = parser.add_subparsers(dest="command")
    ingest_parser = sub.add_parser("ingest")
    ingest_parser.add_argument("roots", nargs="+", type=Path)
    ingest_parser.add_argument("--commit", default="", help="보고를 낸 커밋(비우면 빈 글)")
    ingest_parser.add_argument("--kind", choices=RUN_KINDS, default="manual", help="실행 종류 — 묶음 검증은 verify, 다시 돌리기 도구는 rerun")
    ingest_parser.add_argument("--since", type=Path, default=None, help="이 파일의 수정 시각 뒤에 시작한 보고만 --commit · --kind 로(그 앞은 sha 없이 manual)")
    sub.add_parser("flaky", help="최근 검증 커밋의 흔들림 — 끝 줄 FLAKY=, 격리 표에 없는 것이 문턱을 넘으면 진다")
    stage_parser = sub.add_parser("stage", help="묶음 검증 단계 시간을 쌓는다(느려지면 경고)")
    stage_parser.add_argument("name")
    stage_parser.add_argument("--commit", required=True)
    stage_parser.add_argument("--result", choices=STAGE_RESULTS, required=True)
    stage_parser.add_argument("--seconds", type=float, required=True)
    report_parser = sub.add_parser("report")
    report_parser.add_argument("--min-runs", type=int, default=3)
    report_parser.add_argument("--top", type=int, default=30)
    quarantine_parser = sub.add_parser("quarantine")
    quarantine_parser.add_argument("--ctest-exclude", action="store_true")
    costs_parser = sub.add_parser("costs")
    costs_parser.add_argument("--write", action="store_true", help=f"{SCENARIO_COSTS_PATH} 를 쓴다(없으면 찍기만)")
    changed_parser = sub.add_parser("changed", help="origin/main 뒤에 들어온 테스트 · 시나리오를 먼저 돈다(preflight changed 단계)")
    changed_parser.add_argument("--preset", required=True, help="빌드 프리셋(build/<프리셋> 의 ctest 항목을 쓴다)")
    changed_parser.add_argument("--base", default="origin/main", help="돈 적이 있는 쪽(기본 origin/main — push 는 검증한 커밋만 한다)")
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
    if arguments.command == "changed":
        return run_changed(project_root, arguments.preset, arguments.base)
    if arguments.command not in ("ingest", "report", "costs", "flaky", "stage"):
        parser.error("ingest · report · quarantine · costs · changed · flaky · stage 중 하나")
    connection = open_database(project_root / DEFAULT_DATABASE)
    if arguments.command == "ingest":
        list_report = read_reports(arguments.roots)
        since_unix_ms = int(arguments.since.stat().st_mtime * 1000) if arguments.since is not None else None
        list_fresh = [report for report in list_report if since_unix_ms is None or int(report["startedAtUnixMs"]) >= since_unix_ms]
        list_slow = find_slow_runs(connection, list_fresh, read_verify_config(project_root, "_slowFactor")) if arguments.kind == "verify" else []
        added = ingest(connection, list_report, arguments.commit, arguments.kind, since_unix_ms)
        print(f"[TestHistory] {added} 보고를 더했다")
        for warning in list_slow[:20]:
            print(f"[TestHistory] 느려짐: {warning}")
        return ExitCode.PASSED
    if arguments.command == "flaky":
        commit_window = int(read_verify_config(project_root, "_flakyCommitWindow"))
        list_entry, list_problem = read_quarantine(project_root, datetime.date.today())
        if list_problem:
            print("\n".join(f"  - {problem}" for problem in list_problem))
            return ExitCode.VIOLATION
        list_flaky, group_count = find_flaky(connection, commit_window, {entry.test for entry in list_entry})
        for name, backend, sha in list_flaky[:40]:
            print(f"  흔들림: {name} [{backend}] {sha[:10]}")
        passed, final = decide_flaky(list_flaky, group_count, read_verify_config(project_root, "_flakyRateLimit"), commit_window)
        print(final)
        return ExitCode.PASSED if passed else ExitCode.VIOLATION
    if arguments.command == "stage":
        warning = record_stage(connection, arguments.name, arguments.commit, arguments.result, arguments.seconds,
                               int(datetime.datetime.now().timestamp() * 1000), read_verify_config(project_root, "_slowFactor"))
        if warning:
            print(f"[TestHistory] 느려짐: {warning}")
        return ExitCode.PASSED
    if arguments.command == "costs":
        map_cost = compute_scenario_costs(connection)
        if not map_cost:
            print("[TestHistory] 통과한 실행이 없다 — 시간표를 쓰지 않는다")
            return ExitCode.VIOLATION
        if arguments.write:
            (project_root / SCENARIO_COSTS_PATH).write_text(make_scenario_costs_text(map_cost), encoding="utf-8", newline="\n")
            print(f"[TestHistory] {SCENARIO_COSTS_PATH} 에 {len(map_cost)} 시나리오를 썼다")
        else:
            for name, seconds in sorted(map_cost.items(), key=lambda item: -item[1]):
                print(f"  {name}: {seconds:.1f} s")
        return ExitCode.PASSED
    for stats in compute_stats(connection, arguments.min_runs)[: arguments.top]:
        print(f"  {stats.name} [{stats.backend}] 실행 {stats.runs} · 실패 {stats.failures} · 흔들림 {stats.flakiness:.2f} · p95 {stats.p95_recent:.0f} ms (추세 x{stats.get_trend():.2f})")
    return ExitCode.PASSED


if __name__ == "__main__":
    sys.exit(main())
