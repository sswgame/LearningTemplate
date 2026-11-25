"""
묶음 검증 기록(`verify_record.py`) — 단계 건너뛰기 판정 · pre-push 게이트, 그리고 설치하는 pre-push 훅이 검사 뒤 Git LFS 로 넘기는지.

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

from dev.verify_record import decide_skip, find_unverified_pushes, is_non_input  # noqa: E402
from setup.git_hooks import install_pre_commit_hook  # noqa: E402


class VerifyRecordTest(unittest.TestCase):
    def test_skip_and_push_checks(self) -> None:
        prefixes = ["docs/", ".claude/skills/", "CLAUDE.md"]
        assert is_non_input("docs/NEXT.md", prefixes) and is_non_input("CLAUDE.md", prefixes)
        assert not is_non_input("docs.txt", prefixes) and not is_non_input("Source/CLAUDE.md", prefixes), "파일 줄은 그 경로 하나만"
        assert decide_skip("cpu", "b" * 40, {"cpu": "a" * 40}, ["docs/x.md", ".claude/skills/p/SKILL.md"], prefixes)[0]
        assert not decide_skip("cpu", "b" * 40, {"cpu": "a" * 40}, ["docs/x.md", "Source/a.cpp"], prefixes)[0], "입력이 하나라도 바뀌면 돈다"
        assert not decide_skip("cpu", "b" * 40, {}, [], prefixes)[0], "기록이 없으면 돈다"
        assert not decide_skip("cpu", "b" * 40, {"cpu": "a" * 40}, None, prefixes)[0], "견주지 못하면 돈다"
        assert decide_skip("cpu", "a" * 40, {"cpu": "a" * 40}, None, prefixes)[0], "같은 커밋은 건너뛴다"
        protected = ["refs/heads/main"]
        good, bad = "a" * 40, "b" * 40
        assert find_unverified_pushes([f"refs/heads/w {good} refs/heads/main {bad}"], good, protected) == []
        assert len(find_unverified_pushes([f"refs/heads/w {bad} refs/heads/main {good}"], good, protected)) == 1
        assert len(find_unverified_pushes([f"(delete) {'0' * 40} refs/heads/main {good}"], good, protected)) == 1, "보호 ref 지우기는 막는다"
        assert find_unverified_pushes([f"refs/heads/w {bad} refs/heads/w {good}"], good, protected) == [], "보호하지 않는 ref 는 보지 않는다"
        assert len(find_unverified_pushes([f"refs/heads/w {bad} refs/heads/main {good}"], "", protected)) == 1, "기록이 없으면 막는다"

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
