"""
묶음 검증 기록(`verify_record.py`) — 단계 건너뛰기 판정 · pre-push 게이트(자체 검사), 그리고 설치하는 pre-push 훅이 검사 뒤 Git LFS 로 넘기는지.

실행: python -X utf8 Scripts/dev/tests/test_verify_record.py (CTest `Scripts.VerifyRecord`). git 을 띄우지 않는다.
"""

from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from dev.verify_record import run_self_check  # noqa: E402
from setup.git_hooks import install_pre_commit_hook  # noqa: E402


class VerifyRecordTest(unittest.TestCase):
    def test_self_check(self) -> None:
        run_self_check()

    def test_pre_push_hook_checks_then_hands_refs_to_lfs(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / ".git").mkdir()
            self.assertTrue(install_pre_commit_hook(root))
            script = (root / ".git" / "hooks" / "pre-push").read_text(encoding="utf-8")
        self.assertIn("verify_record.py\"", script)
        self.assertIn('"$checker" pre-push || exit 1', script)
        self.assertLess(script.index("pre-push || exit 1"), script.index("git lfs pre-push"), "검사가 LFS 업로드보다 먼저")
        bash = shutil.which("bash")
        if bash is not None:
            self.assertEqual(subprocess.run([bash, "-n"], input=script.encode("utf-8"), check=False).returncode, 0, "sh 문법")


if __name__ == "__main__":
    unittest.main()
