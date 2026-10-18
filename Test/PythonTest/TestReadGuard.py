#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""`.claude/hooks/ReadGuard.py` 의 판단 — 큰 .md 의 통째 읽기만 막고, limit · offset · 작은 파일 · 저장소 밖 · 없는 파일 · 이상한 입력은 통과한다."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

kHookDir = Path(__file__).resolve().parents[2] / ".claude" / "hooks"
sys.path.insert(0, str(kHookDir))

import ReadGuard  # noqa: E402

kLimit = 100


class ReadGuardTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tempDir = tempfile.TemporaryDirectory()
        self.addCleanup(self.tempDir.cleanup)
        self.root = Path(self.tempDir.name) / "repo"
        self.root.mkdir()
        self.outside = Path(self.tempDir.name) / "outside"
        self.outside.mkdir()

    def writeInternal(self, directory: Path, name: str, size: int) -> str:
        path = directory / name
        path.write_bytes(b"a" * size)
        return str(path)

    def checkInternal(self, toolInput: dict) -> str:
        return ReadGuard.checkRead(toolInput, self.root, kLimit)

    def testBoundaryBlocksOnlyAboveLimit(self) -> None:
        self.assertEqual(self.checkInternal({"file_path": self.writeInternal(self.root, "A.md", kLimit)}), "")
        reason = self.checkInternal({"file_path": self.writeInternal(self.root, "B.md", kLimit + 1)})
        self.assertIn("py -3 -m Scripts doc B.md", reason)
        self.assertIn("grep -n", reason)

    def testLimitOrOffsetPasses(self) -> None:
        path = self.writeInternal(self.root, "Big.md", kLimit * 5)
        self.assertEqual(self.checkInternal({"file_path": path, "limit": 50}), "")
        self.assertEqual(self.checkInternal({"file_path": path, "offset": 10}), "")
        self.assertNotEqual(self.checkInternal({"file_path": path}), "")

    def testOtherKindsOutsideAndMissingPass(self) -> None:
        self.assertEqual(self.checkInternal({"file_path": self.writeInternal(self.root, "Big.txt", kLimit * 5)}), "")
        self.assertEqual(self.checkInternal({"file_path": self.writeInternal(self.outside, "Big.md", kLimit * 5)}), "")
        self.assertEqual(self.checkInternal({"file_path": str(self.root / "None.md")}), "")

    def testCodeFilesUseCodeLimit(self) -> None:
        big = self.writeInternal(self.root, "Big.cpp", kLimit * 5)
        self.assertEqual(ReadGuard.checkRead({"file_path": big}, self.root, kLimit, kLimit * 5), "")
        self.assertIn("graphify query", ReadGuard.checkRead({"file_path": big}, self.root, kLimit, kLimit * 5 - 1))
        self.assertEqual(ReadGuard.checkRead({"file_path": big, "limit": 50}, self.root, kLimit, 1), "")

    def testBrokenInputPasses(self) -> None:
        self.assertEqual(self.checkInternal({}), "")
        self.assertEqual(self.checkInternal({"file_path": 3}), "")
        self.assertEqual(self.checkInternal({"file_path": None}), "")

    def runHookInternal(self, stdin: str) -> int:
        return subprocess.run([sys.executable, str(kHookDir / "ReadGuard.py")], input=stdin.encode("utf-8"), capture_output=True, check=False).returncode

    def testHookProcessExitCodes(self) -> None:
        bigDoc = Path(__file__).resolve().parents[2] / "docs" / "09_Decisions.md"
        self.assertEqual(self.runHookInternal(json.dumps({"tool_name": "Read", "tool_input": {"file_path": str(bigDoc)}})), 2)
        self.assertEqual(self.runHookInternal(json.dumps({"tool_name": "Read", "tool_input": {"file_path": str(bigDoc), "limit": 10}})), 0)
        self.assertEqual(self.runHookInternal(json.dumps({"tool_name": "Bash", "tool_input": {"file_path": str(bigDoc)}})), 0)
        self.assertEqual(self.runHookInternal("not json"), 0)
        self.assertEqual(self.runHookInternal(""), 0)
        self.assertEqual(self.runHookInternal("[1, 2]"), 0)


if __name__ == "__main__":
    unittest.main()
