"""
테스트 이력(`run_history.py` · `changed_tests.py`) — 실행 종류 · 표지(--since) · 흔들림 판정 · 느려짐 경고 · 단계 시간 · changed 단계.

실행: python -X utf8 Scripts/dev/tests/test_test_history.py (CTest `Scripts.TestHistory`). 빌드 · git 을 띄우지 않는다.
"""

from __future__ import annotations

import datetime
import json
import sqlite3
import sys
import tempfile
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.run_process import ProcessResult  # noqa: E402
from dev.changed_tests import CTEST_DEFAULT_TIMEOUT_SECONDS, ChangedFile, ChangedRow, ChangedRun, CtestItem, EDITOR_UNIT_TEST_ITEM, SINGLE_SCENARIO_TIMEOUT_SECONDS, decide_changed, find_dash, find_feature_folder, find_golden_scenario, fold_scenario_failures, fold_scenario_rows, format_changed_table, get_argument, list_new_references, make_editor_unit_run, make_scenario_runs, make_unit_runs, parse_branch, parse_ctest_items, parse_name_status, read_unit_rows, select_editor_files, select_scenarios, select_unit_folders, set_argument  # noqa: E402
from dev.run_history import QuarantineEntry, SCHEMA, compute_percentile, compute_scenario_costs, compute_stats, decide_flaky, find_flaky, find_scenario_cost, find_slow_runs, ingest, make_ctest_exclude, make_scenario_costs_text, prepare_schema, record_stage  # noqa: E402


class TestHistoryTest(unittest.TestCase):
    def test_changed_selection(self) -> None:
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

    def test_kinds_flaky_and_slow(self) -> None:
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

    def test_statistics_and_costs(self) -> None:
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


if __name__ == "__main__":
    unittest.main()
