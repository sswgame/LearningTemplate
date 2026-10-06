#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""common.Process — UTF-8 디코딩, 못 띄움 · 시간 초과를 예외 대신 칸으로."""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Scripts"))

from common import runProcess  # noqa: E402


class ProcessTest(unittest.TestCase):
    def testUtf8OutputIsDecodedRegardlessOfConsoleCodePage(self) -> None:
        result = runProcess([sys.executable, "-c", "import sys; sys.stdout.buffer.write('한글 — 경로'.encode('utf-8'))"])
        self.assertTrue(result.bSucceeded)
        self.assertEqual(result.stdout, "한글 — 경로")

    def testInvalidBytesAreReplacedNotRaised(self) -> None:
        result = runProcess([sys.executable, "-c", "import sys; sys.stdout.buffer.write(b'ok\\xff')"])
        self.assertTrue(result.stdout.startswith("ok"))

    def testMissingExecutableIsNotLaunched(self) -> None:
        result = runProcess(["sw-no-such-tool-xyz"])
        self.assertFalse(result.bLaunched)
        self.assertFalse(result.bSucceeded)

    def testTimeoutIsReported(self) -> None:
        result = runProcess([sys.executable, "-c", "import time; time.sleep(5)"], timeoutSeconds=0.5)
        self.assertTrue(result.bTimedOut)
        self.assertFalse(result.bSucceeded)

    def testNonZeroExitIsLaunchedButNotSucceeded(self) -> None:
        result = runProcess([sys.executable, "-c", "raise SystemExit(3)"])
        self.assertTrue(result.bLaunched)
        self.assertEqual(result.returnCode, 3)

    def testStdinIsPassed(self) -> None:
        result = runProcess([sys.executable, "-c", "import sys; print(sys.stdin.read().upper())"], stdinText="abc")
        self.assertEqual(result.stdout.strip(), "ABC")

    def testStdoutPathCollectsBothStreams(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            logPath = Path(folder) / "sub" / "log.txt"
            runProcess([sys.executable, "-c", "import sys; print('out'); print('err', file=sys.stderr)"], stdoutPath=logPath)
            self.assertIn("out", logPath.read_text(encoding="utf-8"))
            self.assertIn("err", logPath.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
