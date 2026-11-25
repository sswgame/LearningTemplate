"""
프로세스 묶음 실행기(common/process_group.py) 회귀 검사 — 로그 줄 기다리기 · 표준 입력 명령 · quit 정상 종료 · 블록을 나갈 때 남은 프로세스 멈추기 ·
없는 실행 파일 거절 · 허용 밖 경고 줄 모으기 · 판정 종료 코드.

실행: python -X utf8 Scripts/common/tests/test_process_group.py. 자식은 이 파이썬이고 파일은 임시 폴더에 만든다.
"""

from __future__ import annotations

import json
import socket
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common import process_group  # noqa: E402
from common.exit_code import ExitCode  # noqa: E402
from common.log_lines import count_id, payloads, problem_lines  # noqa: E402
from common.process_group import ChildProcess, ProcessGroup, free_port, report  # noqa: E402

# 준비 줄을 찍고 표준 입력 줄을 되울리다 quit 에 0 으로 끝나는 자식.
_ECHO_CHILD = "import sys\nprint('[t] [x] [Info] - [id:Child.Ready] ready', flush=True)\nfor line in sys.stdin:\n    print('[t] [x] [Info] - [id:Child.Got] ' + line.strip(), flush=True)\n    if line.strip() == 'quit':\n        sys.exit(0)\n"


class ProcessGroupTest(unittest.TestCase):
    def setUp(self) -> None:
        self._folder = tempfile.TemporaryDirectory()
        root = Path(self._folder.name)
        (root / "Bin").mkdir()
        self._patches = [mock.patch.object(process_group, "get_project_root", return_value=root),
                         mock.patch.object(process_group, "get_bin_dir", return_value=root / "Bin")]
        for patch in self._patches:
            patch.start()

    def tearDown(self) -> None:
        for patch in self._patches:
            patch.stop()
        self._folder.cleanup()

    def test_wait_send_and_quit(self) -> None:
        with ProcessGroup("test", "Preset", "Run") as group:
            child = group.start("child", [sys.executable, "-c", _ECHO_CHILD])
            self.assertTrue(child.wait_for_log("Child.Ready", 30))
            child.send("hello")
            self.assertTrue(child.wait_for_log("Child.Got", 30))
            child.send("again")
            self.assertTrue(child.wait_for_log("Child.Got", 30, 2))
            child.stop(timeout_seconds=30)
        self.assertEqual(child.return_code, 0)
        self.assertIn("[id:Child.Got] quit", child.text())
        self.assertEqual(child.log, Path(self._folder.name) / "Saved" / "Run" / "child.log")

    def test_wait_for_stops_when_the_process_exits(self) -> None:
        with ProcessGroup("test", "Preset", "Run") as group:
            child = group.start("child", [sys.executable, "-c", "print('bye')"], stdin=False)
            self.assertFalse(child.wait_for_log("Child.Never", 30))
            self.assertTrue(child.wait(30))

    def test_leaving_the_block_stops_running_processes(self) -> None:
        with ProcessGroup("test", "Preset", "Run") as group:
            child = group.start("child", [sys.executable, "-c", "import time\ntime.sleep(600)"], stdin=False)
        self.assertIsNotNone(child.return_code)

    def test_missing_executable_is_rejected(self) -> None:
        with ProcessGroup("test", "Preset", "Run") as group:
            self.assertIsNone(group.require("NoSuchProgram"))

    def test_problem_lines_and_report(self) -> None:
        text = "[t] [s] [Info] - fine\n[t] [s] [Warning] - [id:A.Allowed] one\n[t] [s] [Error] [tick=3] - [id:A.Broken] two - dash\n[Error] [id:A.Assert] f.cpp:3: x\n -> f.cpp:4\n"
        problems = problem_lines("server", text, frozenset({"A.Allowed"}))
        self.assertEqual(problems, ["server log: [Error] A.Broken: two - dash", "server log: [Error] A.Assert: f.cpp:3: x"])
        self.assertEqual(report("test", problems, [], "ok"), ExitCode.VIOLATION)
        self.assertEqual(report("test", [], [], "ok"), ExitCode.PASSED)

    def test_ids_and_json_payloads(self) -> None:
        text = '[t] [s] [Info] - [id:A.Value] {"x":-1.4305115e-06}\n[t] [s] [Info] - [id:A.Other] x\n'
        self.assertEqual(count_id(text, "A.Value"), 1)
        self.assertEqual(payloads(text, "A.Value"), [{"x": -1.4305115e-06}])
        with self.assertRaises(ValueError):
            payloads(text, "A.Other")

    def test_scenario_report_records_and_failure(self) -> None:
        with ProcessGroup("test", "Preset", "Run") as group:
            child = group.start("child", [sys.executable, "-c", "raise SystemExit(10)"], stdin=False)
            child.report_path = group.out_dir / "child.report.json"
            child.report_path.write_text(json.dumps({"records": ["frame 3 Hub.PawnFloorX = -1.4305115e-06", "frame 4 Object.X(Player) = 2"],
                                                     "failure": {"kind": "WatchMismatch", "reason": "why", "step": "step 4"}}), encoding="utf-8")
            child.wait(30)
        self.assertEqual(child.records(), {"Hub.PawnFloorX": -1.4305115e-06, "Object.X(Player)": 2.0})
        self.assertIn("WatchMismatch why at step 4", child.scenario_problems()[0])
        self.assertEqual(ChildProcess("none", child.process, child.log).records(), {})

    def test_free_port_can_be_bound(self) -> None:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            probe.bind(("127.0.0.1", free_port(socket.SOCK_DGRAM)))


if __name__ == "__main__":
    unittest.main()
