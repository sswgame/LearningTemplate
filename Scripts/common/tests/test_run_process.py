"""
cmake 입구(`run_process.run_cmake`) — 연결된 워크트리(`.git` 이 파일)에서는 구성 · 빌드를 시작하지 않고 멈춘다. 돌리기 전 증분 빌드(`build_preset_or_exit`)는 지면 오류 줄과 함께 멈춘다.

실행: python -X utf8 Scripts/common/tests/test_run_process.py (CTest `Scripts.RunProcess`). cmake 를 띄우지 않는다.
"""

from __future__ import annotations

import sys
import tempfile
import unittest
import unittest.mock
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.run_process import ProcessResult, build_preset_or_exit, run_cmake  # noqa: E402


class RunCmakeTest(unittest.TestCase):
    def test_linked_worktree_stops_before_cmake(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / ".git").write_text("gitdir: D:/repo/.git/worktrees/w\n", encoding="utf-8")
            with self.assertRaises(SystemExit) as raised:
                run_cmake(["--build", "--preset", "Ninja-Release"], cwd=root)
            self.assertIn("워크트리", str(raised.exception.code))

    def test_a_failed_build_stops_with_the_error_lines(self) -> None:
        failed = ProcessResult(return_code=1, stdout="[1/2] a.cpp\na.cpp(1): error: x\nninja: build stopped\n")
        with unittest.mock.patch("common.run_process.run_cmake", return_value=failed) as build, self.assertRaises(SystemExit) as raised:
            build_preset_or_exit("Ninja-Release", cwd=Path("."))
        build.assert_called_once_with(["--build", "--preset", "Ninja-Release"], cwd=Path("."))
        self.assertIn("a.cpp(1): error: x", str(raised.exception.code))
        self.assertNotIn("build stopped", str(raised.exception.code))
        with unittest.mock.patch("common.run_process.run_cmake", return_value=ProcessResult(return_code=0)):
            build_preset_or_exit("Ninja-Release", cwd=Path("."))


if __name__ == "__main__":
    unittest.main()
