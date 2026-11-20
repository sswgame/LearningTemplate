"""
git 저장소 환경 변수 떼기(`common/__init__.py`) — 떼는 이름이 git 의 `rev-parse --local-env-vars` 와 같고, 커밋 훅 환경은 떼어 둔 값을 되돌린다.

실행: python -X utf8 Scripts/common/tests/test_git_environment.py (CTest `Scripts.test_git_environment`). git 을 한 번 띄운다.
"""

from __future__ import annotations

import os
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

import common  # noqa: E402
from common.git import make_hook_environment, run_git_or_raise  # noqa: E402


class GitEnvironmentTest(unittest.TestCase):
    def test_names_match_git(self) -> None:
        self.assertEqual(set(run_git_or_raise(["rev-parse", "--local-env-vars"]).split()), set(common.REPOSITORY_ENVIRONMENT_NAMES))

    def test_hook_environment_keeps_removed_names(self) -> None:
        self.assertFalse(any(name in os.environ for name in common.REPOSITORY_ENVIRONMENT_NAMES))
        common.HOOK_GIT_ENVIRONMENT["GIT_INDEX_FILE"] = "x/index"
        try:
            self.assertEqual(make_hook_environment()["GIT_INDEX_FILE"], "x/index")
            self.assertNotIn("GIT_INDEX_FILE", os.environ)
        finally:
            del common.HOOK_GIT_ENVIRONMENT["GIT_INDEX_FILE"]


if __name__ == "__main__":
    unittest.main()
