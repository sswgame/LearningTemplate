#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""setup.HostTools 의 버전 고르기 — MSVC · Windows SDK 버전 폴더는 사전순이 아니라 자연순으로 최신을 고른다."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Scripts"))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Scripts" / "setup"))

import HostTools  # noqa: E402
from common import naturalVersionKey, selectLatestVersion  # noqa: E402


class HostToolsTest(unittest.TestCase):
    def testMsvcToolsetPicksNumericallyLatest(self) -> None:
        # 사전순이면 "14.9.1" 이 "14.44.35207" 앞에 온다 — MSVC 14.4x 툴셋에서 실제로 갈린다.
        self.assertEqual(selectLatestVersion(["14.9.1", "14.44.35207", "14.38.0"]), "14.44.35207")

    def testWindowsSdkPicksNumericallyLatest(self) -> None:
        self.assertEqual(selectLatestVersion(["10.0.9600.0", "10.0.26100.0", "10.0.22621.0"]), "10.0.26100.0")

    def testEmptyListIsEmptyString(self) -> None:
        self.assertEqual(selectLatestVersion([]), "")

    def testKeySortsDigitsAsNumbers(self) -> None:
        self.assertEqual(sorted(["llvm-21", "llvm-9", "llvm-14"], key=naturalVersionKey), ["llvm-9", "llvm-14", "llvm-21"])

    def testHostToolsUsesTheSharedSelection(self) -> None:
        # 버전 고르기는 common 한 자리 — HostTools 가 자기 정렬을 다시 들면 이 시험이 아니라 이 줄이 진다.
        self.assertIs(HostTools.selectLatestVersion, selectLatestVersion)


if __name__ == "__main__":
    unittest.main()
