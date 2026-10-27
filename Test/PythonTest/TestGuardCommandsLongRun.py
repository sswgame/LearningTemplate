#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""`.claude/hooks/GuardCommands.py` 의 전경 장기 실행 판단 — 전경 `verify-batch` · 20초 이상 `sleep` 만 막고, 백그라운드 · 짧은 sleep · 따옴표 안 글은 통과시킨다."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

kHookDir = Path(__file__).resolve().parents[2] / ".claude" / "hooks"
sys.path.insert(0, str(kHookDir))

import GuardCommands  # noqa: E402


class ForegroundLongRunTest(unittest.TestCase):
    def testForegroundLongScriptsAreBlocked(self) -> None:
        for command in ("py -3 -m Scripts verify-batch --owner x > /tmp/vb.txt 2>&1",
                        "cd /d/w/x && py -3 -m Scripts full-build --preset Ninja-Shipping",
                        "py -3 -m Scripts soak --app a.exe"):
            self.assertIn("run_in_background", GuardCommands.checkForegroundLongRun(command, False), command)

    def testSleepPollingLoopIsBlocked(self) -> None:
        self.assertIn("sleep", GuardCommands.checkForegroundLongRun("for i in $(seq 1 19); do grep -q x f && break; sleep 30; done", False))

    def testBackgroundCallPasses(self) -> None:
        self.assertEqual("", GuardCommands.checkForegroundLongRun("py -3 -m Scripts verify-batch --owner x", True))
        self.assertEqual("", GuardCommands.checkForegroundLongRun("sleep 60; echo done", True))

    def testListAndDryRunPass(self) -> None:
        self.assertEqual("", GuardCommands.checkForegroundLongRun("py -3 -m Scripts verify-batch --owner x --dry-run --only a b", False))
        self.assertEqual("", GuardCommands.checkForegroundLongRun("py -3 -m Scripts verify-batch --list", False))

    def testShortSleepAndQuotedTextPass(self) -> None:
        self.assertEqual("", GuardCommands.checkForegroundLongRun("sleep 3; ls", False))
        self.assertEqual("", GuardCommands.checkForegroundLongRun('git commit -m "verify-batch 는 sleep 30 으로 기다리지 않는다"', False))


if __name__ == "__main__":
    unittest.main()
