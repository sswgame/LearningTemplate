"""
쿠킹 입력 지문 도구(`cook_if_inputs_changed.py`) 회귀 검사 — 같으면 건너뛰고, 입력 · 실행 파일 · 출력이 바뀌면 다시 돌고, 실패한 뒤에는 반드시 다시 돈다.

실행: python -X utf8 Scripts/generate/tests/test_cook_if_inputs_changed.py. 파일은 임시 폴더에 만든다.
"""

from __future__ import annotations

import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from generate.cook_if_inputs_changed import fingerprint, CookIfInputsChangedCommand, split_commands  # noqa: E402


def touch_later(path: Path, text: str) -> None:
    """내용을 쓰고 수정 시각을 1 초 미룬다 — 같은 시각 칸에 떨어져 바뀜을 놓치지 않게."""
    path.write_text(text, encoding="utf-8")
    stat = path.stat()
    os.utime(path, ns=(stat.st_atime_ns, stat.st_mtime_ns + 1_000_000_000))


class CookIfInputsChangedTest(unittest.TestCase):
    def setUp(self) -> None:
        self._folder = tempfile.TemporaryDirectory()
        self.root = Path(self._folder.name)
        (self.root / "Content").mkdir()
        (self.root / "Bin" / "Saved").mkdir(parents=True)
        (self.root / "Content" / "a.xml").write_text("a", encoding="utf-8")
        (self.root / "Bin" / "App.exe").write_text("app", encoding="utf-8")
        self.counter = self.root / "count.txt"
        self.output = self.root / "Packs"

    def tearDown(self) -> None:
        self._folder.cleanup()

    def cook(self, exit_code: int = 0) -> int:
        # 명령: 출력 폴더에 팩을 쓰고 실행 횟수를 센다.
        script = (
            "import pathlib,sys;"
            f"p=pathlib.Path(r'{self.output}');p.mkdir(exist_ok=True);(p/'game.pak').write_text('pak');"
            f"c=pathlib.Path(r'{self.counter}');c.write_text(str(int(c.read_text()) + 1 if c.exists() else 1));"
            f"sys.exit({exit_code})"
        )
        return CookIfInputsChangedCommand.main(["--stamp", str(self.root / "Cook.inputs"), "--input", str(self.root / "Content"),
                     "--binaries", str(self.root / "Bin"), "--output", str(self.output), "--", sys.executable, "-c", script])

    def runs(self) -> int:
        return int(self.counter.read_text()) if self.counter.exists() else 0

    def test_skips_when_unchanged_and_runs_on_changes(self) -> None:
        self.assertEqual(self.cook(), 0)
        self.assertEqual(self.cook(), 0)
        self.assertEqual(self.runs(), 1)   # 두 번째는 건너뛴다
        (self.root / "Bin" / "run.log").write_text("log", encoding="utf-8")
        self.cook()
        self.assertEqual(self.runs(), 1)   # 실행 파일 폴더의 로그는 쿠커가 아니다
        touch_later(self.root / "Content" / "a.xml", "changed")
        self.cook()
        self.assertEqual(self.runs(), 2)   # 입력이 바뀌었다
        touch_later(self.root / "Bin" / "App.exe", "relinked")
        self.cook()
        self.assertEqual(self.runs(), 3)   # 쿠커가 바뀌었다
        (self.output / "game.pak").unlink()
        self.cook()
        self.assertEqual(self.runs(), 4)   # 출력을 지웠다
        (self.root / "Content" / "new.xml").write_text("n", encoding="utf-8")
        self.cook()
        self.assertEqual(self.runs(), 5)   # 새 입력 파일

    def test_binary_folder_ignores_runtime_folders(self) -> None:
        before = fingerprint([], [self.root / "Bin"])
        (self.root / "Bin" / "Saved" / "log.txt").write_text("run", encoding="utf-8")
        self.assertEqual(before, fingerprint([], [self.root / "Bin"]))

    def test_failure_runs_again_next_time(self) -> None:
        self.assertNotEqual(self.cook(exit_code=3), 0)
        self.assertFalse((self.root / "Cook.inputs").exists())
        self.cook()
        self.assertEqual(self.runs(), 2)

    def test_split_commands(self) -> None:
        self.assertEqual(split_commands(["a", "b", "--then", "c"]), [["a", "b"], ["c"]])
        with self.assertRaises(ValueError):
            split_commands(["a", "--then"])


if __name__ == "__main__":
    unittest.main()
