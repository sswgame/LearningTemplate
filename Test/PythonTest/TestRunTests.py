#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""`Scripts/dev/RunTests.py`(`py -3 -m Scripts test <패턴>`)의 목록 읽기와 필터 조각 점검 테스트."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

kRepositoryRoot = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(kRepositoryRoot / "Scripts"))

from dev.RunTests import _kListedCaseRe, findUnmatchedPieces  # noqa: E402


class RunTestsFilterTest(unittest.TestCase):
    def testListedLineKeepsParameterSuffix(self) -> None:
        match = _kListedCaseRe.match("  RHIDeviceTest.UploadTexture2DAllBackends.dx12")
        self.assertIsNotNone(match)
        self.assertEqual("UploadTexture2DAllBackends.dx12", match.group(2))
        self.assertEqual("AppScenarioTest", _kListedCaseRe.match("  AppScenarioTest.HarvestValley.fog.vk").group(1))

    def testPieceThatSelectsNothingIsReported(self) -> None:
        listCase = ["RHIDeviceTest.Upload.dx12", "RHIDeviceTest.Upload.vk", "RHIDeviceTest.Other"]
        self.assertEqual(["RHIDeviceTest.Upload"], findUnmatchedPieces("RHIDeviceTest.Upload,RHIDeviceTest.Other", listCase))
        self.assertEqual([], findUnmatchedPieces("RHIDeviceTest.Upload.*:RHIDeviceTest.Other", listCase))
        self.assertEqual([], findUnmatchedPieces("RHIDeviceTest.*-RHIDeviceTest.Missing", listCase))   # 빼기 조각은 보지 않는다


if __name__ == "__main__":
    unittest.main()
