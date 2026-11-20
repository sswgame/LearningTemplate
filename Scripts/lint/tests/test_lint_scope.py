"""
커밋 훅 범위(`lint_scope`) — 일부만 스테이지한 파일을 찾는다(`list_staged_with_unstaged_changes`).

실행: python -X utf8 Scripts/lint/tests/test_lint_scope.py (CTest `Scripts.LintScope`). 임시 저장소에서 git 을 띄운다.
"""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.git import run_git_or_raise  # noqa: E402
from lint.lint_scope import list_staged_with_unstaged_changes, make_staged_scope  # noqa: E402


class StagedWithUnstagedChangesTest(unittest.TestCase):
    def test_finds_staged_file_with_more_working_tree_changes(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)

            def git(*list_argument: str) -> None:
                run_git_or_raise(["-c", "user.name=t", "-c", "user.email=t@localhost", "-c", "core.autocrlf=false", *list_argument], cwd=root)

            git("init", "-q", "-b", "main")
            for name in ("a.txt", "b.txt", "c.txt"):
                (root / name).write_text("1\n", encoding="utf-8")
            git("add", ".")
            git("commit", "-qm", "base")
            (root / "a.txt").write_text("2\n", encoding="utf-8")
            (root / "b.txt").write_text("2\n", encoding="utf-8")
            git("add", "a.txt", "b.txt")
            (root / "b.txt").write_text("3\n", encoding="utf-8")   # 스테이지 뒤 또 고친 파일
            (root / "c.txt").write_text("2\n", encoding="utf-8")   # 스테이지하지 않은 파일은 커밋에 들지 않아 상관없다
            scope = make_staged_scope(root)
            self.assertEqual(scope.list_file, ("a.txt", "b.txt"))
            self.assertEqual(list_staged_with_unstaged_changes(root, scope), ["b.txt"])
            git("add", "b.txt")
            self.assertEqual(list_staged_with_unstaged_changes(root, make_staged_scope(root)), [])


if __name__ == "__main__":
    unittest.main()
