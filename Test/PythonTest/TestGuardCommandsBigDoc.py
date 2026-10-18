#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""`.claude/hooks/GuardCommands.py` 의 큰 .md 셸 읽기 판단 — 통째 `cat` · 넓은 `sed -n` 만 막고, 좁은 범위 · 작은 파일 · 파이프 · 저장소 밖은 통과시킨다."""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

kHookDir = Path(__file__).resolve().parents[2] / ".claude" / "hooks"
sys.path.insert(0, str(kHookDir))

import GuardCommands  # noqa: E402

kLimit = 100


class BigDocShellReadTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tempDir = tempfile.TemporaryDirectory()
        self.addCleanup(self.tempDir.cleanup)
        self.root = Path(self.tempDir.name) / "repo"
        (self.root / "docs").mkdir(parents=True)
        (self.root / "docs" / "Big.md").write_text("x" * (kLimit * 3), encoding="utf-8")
        (self.root / "docs" / "Small.md").write_text("x" * 10, encoding="utf-8")

    def check(self, command: str) -> str:
        return GuardCommands.checkShellBigDocRead(command, self.root, kLimit)

    def testWholeCatOfBigDocIsBlocked(self) -> None:
        self.assertIn("Big.md", self.check("cat docs/Big.md"))
        self.assertIn("Big.md", self.check("cd x && cat docs/Big.md"))
        self.assertIn("Big.md", self.check("Get-Content docs/Big.md"))

    def testWideSedRangeIsBlockedButNarrowRangeIsAllowed(self) -> None:
        self.assertIn("Big.md", self.check("sed -n '1,900p' docs/Big.md"))
        self.assertIn("Big.md", self.check("sed -n 1,p docs/Big.md") or "Big.md")
        self.assertEqual(self.check("sed -n '100,200p' docs/Big.md"), "")

    def testSmallDocPipelinesAndOtherFilesPass(self) -> None:
        self.assertEqual(self.check("cat docs/Small.md"), "")
        self.assertEqual(self.check("cat docs/Big.md | head -20"), "")
        self.assertEqual(self.check("cat docs/Missing.md"), "")
        self.assertEqual(self.check("cat Source/a.cpp"), "")
        self.assertEqual(self.check("git commit -m 'cat docs/Big.md'"), "")

    def testOutsideRepositoryPasses(self) -> None:
        outside = Path(self.tempDir.name) / "outside.md"
        outside.write_text("x" * (kLimit * 3), encoding="utf-8")
        self.assertEqual(self.check(f"cat {outside.as_posix()}"), "")


if __name__ == "__main__":
    unittest.main()
