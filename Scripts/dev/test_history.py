"""
Scripts/dev/test_history.py

시나리오 보고 JSON(`AutomationReportWriter` — `name` · `result` · `backend` · `game` · `startedAtUnixMs` · `wallMilliseconds`)을 sqlite 에 쌓고,
테스트마다 흔들림 지수와 걸린 시간 p95 추세를 낸다. 격리 표(`Config/Rules/TestSelection/Quarantine.json`)는 사유 · 만료가 필수이고 지난 만료는 오류다.

**왜 따로 쌓는가.** 한 번의 실행 보고는 "졌다" 만 말한다. 같은 테스트가 실행마다 이겼다 졌다 하는지(흔들림), 점점 느려지는지(p95 추세)는
여러 묶음 검증을 모아야 보인다 — 언리얼 Gauntlet · Horde 의 테스트 이력, 크로미움 Flakiness Dashboard 와 같은 자리를 저장소 안 sqlite 하나로 둔다.
흔들림 지수 = 연속 두 실행의 결과가 바뀐 횟수 / (실행 수 - 1). 0 은 늘 같음, 1 은 매번 바뀜.

  py -3 Scripts/dev/test_history.py ingest <보고 폴더…> [--commit <sha>] [--kind verify|manual|rerun] [--since <표지 파일>]   # DB: Saved/TestHistory/history.sqlite
  py -3 Scripts/dev/test_history.py flaky                                    # 최근 검증 커밋의 흔들림 — 끝 줄 FLAKY=<수>, 문턱을 넘으면 진다
  py -3 Scripts/dev/test_history.py stage <단계> --commit <sha> --result passed|failed|skipped --seconds <초>   # 묶음 검증 단계 시간(느려지면 경고)
  py -3 Scripts/dev/test_history.py report [--min-runs 3] [--top 30]
  py -3 Scripts/dev/test_history.py quarantine [--ctest-exclude]              # 표를 검사한다(만료 · 빠진 칸은 실패), --ctest-exclude 는 ctest -E 정규식을 찍는다
  py -3 Scripts/dev/test_history.py costs [--write]                           # 시나리오 → 초 시간표(통과한 실행의 백엔드별 p50 합). --write 는 Config/Dev/ScenarioCosts.json 을 쓴다
  py -3 Scripts/dev/test_history.py changed --preset <프리셋> [--base origin/main]  # 새로 들어온 · 바뀐 테스트 · 시나리오를 먼저 돈다(`changed_tests.py`)

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
"""

from __future__ import annotations

import argparse
import datetime
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.command import Command, ScriptContext  # noqa: E402
from common.exit_code import ExitCode  # noqa: E402
from dev.changed_tests import run_changed  # noqa: E402
from dev.run_history import DEFAULT_DATABASE, RUN_KINDS, SCENARIO_COSTS_PATH, STAGE_RESULTS, compute_scenario_costs, compute_stats, decide_flaky, find_flaky, find_slow_runs, ingest, make_ctest_exclude, make_scenario_costs_text, open_database, read_quarantine, read_reports, read_verify_config, record_stage  # noqa: E402


class TestHistoryCommand(Command):
    name = "test-history"
    help = "테스트 이력(sqlite) — ingest · report(흔들림 · p95 추세) · flaky(끝 줄 FLAKY=) · stage(단계 시간) · quarantine · costs · changed"

    def add_arguments(self, parser: argparse.ArgumentParser) -> None:
        sub = parser.add_subparsers(dest="command", required=True)
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

    def run(self, context: ScriptContext, args: argparse.Namespace) -> int:
        project_root = context.root
        if args.command == "quarantine":
            list_entry, list_problem = read_quarantine(project_root, datetime.date.today())
            for problem in list_problem:
                print(f"  - {problem}")
            if args.ctest_exclude and not list_problem:
                print(make_ctest_exclude(list_entry))
            return ExitCode.VIOLATION if list_problem else ExitCode.PASSED
        if args.command == "changed":
            return run_changed(project_root, args.preset, args.base)
        connection = open_database(project_root / DEFAULT_DATABASE)
        if args.command == "ingest":
            list_report = read_reports(args.roots)
            since_unix_ms = int(args.since.stat().st_mtime * 1000) if args.since is not None else None
            list_fresh = [report for report in list_report if since_unix_ms is None or int(report["startedAtUnixMs"]) >= since_unix_ms]
            list_slow = find_slow_runs(connection, list_fresh, read_verify_config(project_root, "_slowFactor")) if args.kind == "verify" else []
            added = ingest(connection, list_report, args.commit, args.kind, since_unix_ms)
            print(f"[TestHistory] {added} 보고를 더했다")
            for warning in list_slow[:20]:
                print(f"[TestHistory] 느려짐: {warning}")
            return ExitCode.PASSED
        if args.command == "flaky":
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
        if args.command == "stage":
            warning = record_stage(connection, args.name, args.commit, args.result, args.seconds,
                                   int(datetime.datetime.now().timestamp() * 1000), read_verify_config(project_root, "_slowFactor"))
            if warning:
                print(f"[TestHistory] 느려짐: {warning}")
            return ExitCode.PASSED
        if args.command == "costs":
            map_cost = compute_scenario_costs(connection)
            if not map_cost:
                print("[TestHistory] 통과한 실행이 없다 — 시간표를 쓰지 않는다")
                return ExitCode.VIOLATION
            if args.write:
                (project_root / SCENARIO_COSTS_PATH).write_text(make_scenario_costs_text(map_cost), encoding="utf-8", newline="\n")
                print(f"[TestHistory] {SCENARIO_COSTS_PATH} 에 {len(map_cost)} 시나리오를 썼다")
            else:
                for name, seconds in sorted(map_cost.items(), key=lambda item: -item[1]):
                    print(f"  {name}: {seconds:.1f} s")
            return ExitCode.PASSED
        for stats in compute_stats(connection, args.min_runs)[: args.top]:
            print(f"  {stats.name} [{stats.backend}] 실행 {stats.runs} · 실패 {stats.failures} · 흔들림 {stats.flakiness:.2f} · p95 {stats.p95_recent:.0f} ms (추세 x{stats.get_trend():.2f})")
        return ExitCode.PASSED


if __name__ == "__main__":
    sys.exit(TestHistoryCommand.main())
