"""
커밋 훅 범위(`lint_scope`) — 일부만 스테이지한 파일을 찾는다(`list_staged_with_unstaged_changes`). 병합 중이면 손으로 푼 충돌만 본다.

실행: python -X utf8 Scripts/lint/tests/test_lint_scope.py (CTest `Scripts.LintScope`). 임시 저장소에서 git 을 띄운다.
"""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common import HOOK_GIT_ENVIRONMENT  # noqa: E402
from common.git import run_git, run_git_or_raise  # noqa: E402
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


class MergeScopeTest(unittest.TestCase):
    def test_only_hand_resolved_files_remain(self) -> None:
        """임시 저장소에서 양쪽이 고친 파일 · 한쪽만 고친 파일 · 한쪽이 지운 파일 · 새 파일을 섞어 병합하고, 손으로 푼 충돌만 남는지 본다."""
        def git(root: Path, *list_argument: str) -> None:
            result = run_git(["-c", "user.name=lint", "-c", "user.email=lint@localhost", "-c", "core.autocrlf=false", *list_argument], cwd=root)
            if result.return_code != 0 and list_argument[0] != "merge":   # 충돌 난 병합은 0 이 아니다
                raise RuntimeError(f"git {' '.join(list_argument)}: {result.stderr.strip()}")

        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            git(root, "init", "-q", "-b", "main")
            for name in ("a", "b", "c"):
                (root / f"{name}.txt").write_text(f"{name}\n", encoding="utf-8")
            git(root, "add", ".")
            git(root, "commit", "-qm", "base")
            git(root, "checkout", "-qb", "side")
            (root / "b.txt").write_text("b side\n", encoding="utf-8")
            (root / "c.txt").write_text("c side\n", encoding="utf-8")
            (root / "n.txt").write_text("n\n", encoding="utf-8")
            git(root, "add", ".")
            git(root, "commit", "-qm", "side")
            git(root, "checkout", "-q", "main")
            (root / "c.txt").write_text("c main\n", encoding="utf-8")
            git(root, "rm", "-q", "a.txt")
            git(root, "commit", "-qam", "main")
            git(root, "merge", "-q", "side")
            (root / "c.txt").write_text("c merged\n", encoding="utf-8")
            git(root, "add", "c.txt")
            with mock.patch.dict(HOOK_GIT_ENVIRONMENT, clear=True):   # 커밋 훅 안에서 돌아도 임시 저장소를 본다
                scope = make_staged_scope(root)
        self.assertEqual((scope.list_file, scope.list_removed), (("c.txt",), ()))


if __name__ == "__main__":
    unittest.main()
