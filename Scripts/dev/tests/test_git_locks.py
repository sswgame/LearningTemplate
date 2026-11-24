"""
git-locks 회귀 검사 — 빈 · 닫힌 · 오래된 잠금만 찌꺼기이고, 열린 것 · 갓 만든 것 · 내용이 든 것은 지우지 않는다.

실행: python -X utf8 Scripts/dev/tests/test_git_locks.py (CTest `Scripts.GitLocks`). 임시 폴더만 쓴다.
"""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from dev.git_locks import MIN_AGE_SECONDS, describe_lock, list_lock_files  # noqa: E402


class GitLocksTest(unittest.TestCase):
    def setUp(self) -> None:
        self._folder = tempfile.TemporaryDirectory()
        self.addCleanup(self._folder.cleanup)
        self._git = Path(self._folder.name)
        self._lock = self._git / "worktrees" / "w" / "index.lock"
        self._lock.parent.mkdir(parents=True)
        self._lock.touch()
        self._later = self._lock.stat().st_mtime + MIN_AGE_SECONDS + 1

    def test_every_git_folder_is_listed(self) -> None:
        (self._git / "HEAD.lock").touch()
        (self._git / "index").touch()
        self.assertEqual(list_lock_files(self._git), [self._git / "HEAD.lock", self._lock])

    def test_only_empty_closed_old_locks_are_leftover(self) -> None:
        self.assertTrue(describe_lock(self._lock, self._later)[0])
        self.assertFalse(describe_lock(self._lock, self._later - MIN_AGE_SECONDS)[0])
        with self._lock.open("rb"):
            self.assertFalse(describe_lock(self._lock, self._later)[0])
        self._lock.write_bytes(b"DIRC")
        self.assertFalse(describe_lock(self._lock, self._later + MIN_AGE_SECONDS)[0])


if __name__ == "__main__":
    unittest.main()
