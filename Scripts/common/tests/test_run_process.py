"""
cmake 입구(`run_process.run_cmake`) — 연결된 워크트리(`.git` 이 파일)에서는 구성 · 빌드를 시작하지 않고 멈춘다.

실행: python -X utf8 Scripts/common/tests/test_run_process.py (CTest `Scripts.test_run_process`). cmake 를 띄우지 않는다.
"""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.run_process import run_cmake  # noqa: E402


class RunCmakeTest(unittest.TestCase):
    def test_linked_worktree_stops_before_cmake(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / ".git").write_text("gitdir: D:/repo/.git/worktrees/w\n", encoding="utf-8")
            with self.assertRaises(SystemExit) as raised:
                run_cmake(["--build", "--preset", "Ninja-Release"], cwd=root)
            self.assertIn("워크트리", str(raised.exception.code))


if __name__ == "__main__":
    unittest.main()
