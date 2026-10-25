#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ProcessLimits.attach 의 Job 핸들 소유 — 본문이 정상 · 예외로 끝나고 runApp 이 정상 · 시간 초과로 끝나도 핸들을 한 번 닫는다."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path
from unittest import mock

kScriptsRoot = Path(__file__).resolve().parents[2] / "Scripts"
sys.path.insert(0, str(kScriptsRoot))

from common import AppRun, ProcessLimits as ProcessLimitsModule  # noqa: E402


class JobHandleOwnershipTest(unittest.TestCase):
    def setUp(self) -> None:
        self.listClosed: list[object] = []
        for patcher in (mock.patch.object(ProcessLimitsModule.ProcessLimits, "_attachInternal", return_value=777),
                        mock.patch.object(ProcessLimitsModule, "closeJobHandle", side_effect=self.listClosed.append)):
            patcher.start()
            self.addCleanup(patcher.stop)

    def testClosesAfterNormalBody(self) -> None:
        with ProcessLimitsModule.ProcessLimits().attach(mock.Mock()):
            self.assertEqual(self.listClosed, [])
        self.assertEqual(self.listClosed, [777])

    def testClosesWhenBodyRaises(self) -> None:
        with self.assertRaises(ValueError):
            with ProcessLimitsModule.ProcessLimits().attach(mock.Mock()):
                raise ValueError("boom")
        self.assertEqual(self.listClosed, [777])

    def testRunAppClosesAfterExit(self) -> None:
        result = AppRun.runApp(Path(sys.executable), ["-c", "pass"], timeoutSeconds=30.0)
        self.assertTrue(result.bLaunched)
        self.assertEqual(self.listClosed, [777])

    def testRunAppClosesAfterTimeout(self) -> None:
        result = AppRun.runApp(Path(sys.executable), ["-c", "import time; time.sleep(60)"], timeoutSeconds=0.5)
        self.assertTrue(result.bTimedOut)
        self.assertEqual(self.listClosed, [777])


if __name__ == "__main__":
    unittest.main()
