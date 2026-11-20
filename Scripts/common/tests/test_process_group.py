"""
프로세스 묶음 실행기(common/process_group.py) 회귀 검사 — 로그 줄 기다리기 · 표준 입력 명령 · quit 정상 종료 · 블록을 나갈 때 남은 프로세스 멈추기 ·
없는 실행 파일 거절 · 허용 밖 경고 줄 모으기 · 판정 종료 코드.

실행: python -X utf8 Scripts/common/tests/test_process_group.py. 자식은 이 파이썬이고 파일은 임시 폴더에 만든다.
"""

from __future__ import annotations

import re
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common import process_group  # noqa: E402
from common.exit_code import ExitCode  # noqa: E402
from common.process_group import ProcessGroup, problem_lines, report  # noqa: E402

# 준비 줄을 찍고 표준 입력 줄을 되울리다 quit 에 0 으로 끝나는 자식.
_ECHO_CHILD = "import sys\nprint('ready', flush=True)\nfor line in sys.stdin:\n    print('got ' + line.strip(), flush=True)\n    if line.strip() == 'quit':\n        sys.exit(0)\n"


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
            self.assertTrue(child.wait_for(re.compile("ready"), 30))
            child.send("hello")
            self.assertTrue(child.wait_for(re.compile("got hello"), 30))
            child.stop(timeout_seconds=30)
        self.assertEqual(child.return_code, 0)
        self.assertIn("got quit", child.text())
        self.assertEqual(child.log, Path(self._folder.name) / "Saved" / "Run" / "child.log")

    def test_wait_for_stops_when_the_process_exits(self) -> None:
        with ProcessGroup("test", "Preset", "Run") as group:
            child = group.start("child", [sys.executable, "-c", "print('bye')"], stdin=False)
            self.assertFalse(child.wait_for(re.compile("never"), 30))
            self.assertTrue(child.wait(30))

    def test_leaving_the_block_stops_running_processes(self) -> None:
        with ProcessGroup("test", "Preset", "Run") as group:
            child = group.start("child", [sys.executable, "-c", "import time\ntime.sleep(600)"], stdin=False)
        self.assertIsNotNone(child.return_code)

    def test_missing_executable_is_rejected(self) -> None:
        with ProcessGroup("test", "Preset", "Run") as group:
            self.assertIsNone(group.require("NoSuchProgram"))

    def test_problem_lines_and_report(self) -> None:
        text = "[Info] fine\n[Warning] allowed one\n[Error] broken\n"
        problems = problem_lines("server", text, re.compile("allowed"))
        self.assertEqual(problems, ["server log: [Error] broken"])
        self.assertEqual(report("test", problems, [], "ok"), ExitCode.VIOLATION)
        self.assertEqual(report("test", [], [], "ok"), ExitCode.PASSED)


if __name__ == "__main__":
    unittest.main()
