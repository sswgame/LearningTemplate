#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""`.claude/hooks/SessionGuard.py` 의 하드 한도 판단 — 세션당 한 번만 막고, 표시가 있거나 손상됐거나 세션 아이디가 없으면 통과시킨다."""

from __future__ import annotations

import os
import sys
import tempfile
import unittest
from pathlib import Path

kHookDir = Path(__file__).resolve().parents[2] / ".claude" / "hooks"
sys.path.insert(0, str(kHookDir))

import SessionGuard  # noqa: E402

kStep1, kStep2, kStep3 = SessionGuard.kHardLimitStepsBytes
kOver = kStep1
kUnder = kStep1 - 1


class HardLimitTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tempDir = tempfile.TemporaryDirectory()
        self.addCleanup(self.tempDir.cleanup)
        self.markerDir = str(Path(self.tempDir.name) / "sw-session-guard")
        self.handoff = str(Path(self.tempDir.name) / "NEXT.md")

    def block(self, sessionId: str, size: int, compactions: int = 0) -> int:
        return SessionGuard.shouldBlockPrompt(self.markerDir, sessionId, size, compactions, self.handoff)

    def pending(self, sessionId: str) -> bool:
        return SessionGuard.isHandoffPending(self.markerDir, sessionId, self.handoff)

    def touchHandoff(self, mtime: float) -> None:
        Path(self.handoff).write_text("x", encoding="utf-8")
        os.utime(self.handoff, (mtime, mtime))

    def testHandoffInstructionUntilHandoffUpdated(self) -> None:
        self.touchHandoff(1000)
        self.assertFalse(self.pending("s1"))
        self.assertEqual(self.block("s1", kStep1), 1)
        self.assertTrue(self.pending("s1"))
        self.touchHandoff(2000)
        self.assertFalse(self.pending("s1"))

    def testHigherStageRestartsHandoffInstruction(self) -> None:
        self.touchHandoff(1000)
        self.block("s1", kStep1)
        self.touchHandoff(2000)
        self.assertFalse(self.pending("s1"))
        self.assertEqual(self.block("s1", kStep2), 2)
        self.assertTrue(self.pending("s1"))

    def testNoMarkerOrCorruptMarkerHasNoInstruction(self) -> None:
        self.assertFalse(self.pending("s1"))
        Path(self.markerDir).mkdir(parents=True)
        (Path(self.markerDir) / "s1.json").write_bytes(b"\xff{")
        self.assertFalse(self.pending("s1"))

    def testStageRisesBlockAgainOnlyOnHigherStage(self) -> None:
        self.assertEqual(self.block("s1", kStep1), 1)
        self.assertEqual(self.block("s1", kStep2 - 1), 0)
        self.assertEqual(self.block("s1", kStep2), 2)
        self.assertEqual(self.block("s1", kStep2), 0)
        self.assertEqual(self.block("s1", kStep3), 3)

    def testUnderLimitPasses(self) -> None:
        self.assertFalse(self.block("s1", kUnder, SessionGuard.kHardLimitCompactions - 1))

    def testFirstOverBlocksSecondPasses(self) -> None:
        self.assertTrue(self.block("s1", kOver))
        self.assertFalse(self.block("s1", kOver))

    def testCompactionCountAlsoBlocks(self) -> None:
        self.assertTrue(self.block("s2", 0, SessionGuard.kHardLimitCompactions))

    def testMarkerIsPerSession(self) -> None:
        self.assertTrue(self.block("s1", kOver))
        self.assertTrue(self.block("s2", kOver))

    def testCorruptMarkerPasses(self) -> None:
        Path(self.markerDir).mkdir(parents=True)
        (Path(self.markerDir) / "s1.json").write_bytes(b"\xff{not json")
        self.assertFalse(self.block("s1", kOver))

    def testMissingOrUnsafeSessionIdPasses(self) -> None:
        self.assertFalse(self.block("", kOver))
        self.assertFalse(self.block("../x", kOver))


if __name__ == "__main__":
    unittest.main()
