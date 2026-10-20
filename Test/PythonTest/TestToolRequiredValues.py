#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""도구가 필수 값 · 읽기 실패를 기본값으로 덮지 않고 멈추거나 위반으로 알리는가 — 팩 계약 키, Git 설치 폴더 환경 변수, 규칙 검사의 파일 읽기."""

from __future__ import annotations

import json
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

kRepositoryRoot = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(kRepositoryRoot / "Scripts"))
sys.path.insert(0, str(kRepositoryRoot / "Scripts" / "lint"))

from common import BuildTree, Host  # noqa: E402
from common.CookContract import CookContractSpec, kCookContractConfigRelative  # noqa: E402
from common.PackFormat import PackFormatSpec, kPackFormatConfigRelative  # noqa: E402
from conventions import FileScan  # noqa: E402
from generate import CookAssets  # noqa: E402
from qa import PerfRegression  # noqa: E402


class ToolRequiredValuesTest(unittest.TestCase):
    def testPackFormatWithoutDeflateStrategyStops(self) -> None:
        spec = json.loads((kRepositoryRoot / kPackFormatConfigRelative).read_text(encoding="utf-8"))
        del spec["compression"]["deflateStrategy"]
        with self.assertRaises(KeyError):
            PackFormatSpec(spec)

    @unittest.skipUnless(sys.platform == "win32", "Windows 설치 폴더 탐색")
    def testGitSearchWithoutProgramFilesStops(self) -> None:
        with mock.patch.object(Host.shutil, "which", return_value=None), mock.patch.dict(os.environ, {}, clear=True):
            with self.assertRaisesRegex(RuntimeError, "ProgramFiles"):
                Host.resolveGitExecutable()

    def testCookContractWithoutAssetKindsStops(self) -> None:
        spec = json.loads((kRepositoryRoot / kCookContractConfigRelative).read_text(encoding="utf-8"))
        del spec["asset_kinds"]
        with self.assertRaises(KeyError):
            CookContractSpec(spec)

    def testBuildTreesWithoutPresetsStop(self) -> None:
        with tempfile.TemporaryDirectory() as tempDir:
            with self.assertRaises(FileNotFoundError):
                list(BuildTree.iterConfigured(Path(tempDir)))

    def testPackConfigWithoutCompressionStops(self) -> None:
        with self.assertRaises(KeyError):
            CookAssets.resolveCompressionCodecInternal({})
        with self.assertRaises(KeyError):
            CookAssets.shouldIncludeFile("a/b.txt", {"global_exclude_directories": []})

    def testPerfToleranceWithoutMetricStops(self) -> None:
        with self.assertRaises(KeyError):
            PerfRegression.compareInternal({"Scope": {"p75_us": 10.0}}, {"Scope": {"p75_us": 11.0}}, {"p50_us": 0.1}, 0.0)

    def testUnreadableSourceStops(self) -> None:
        with tempfile.TemporaryDirectory() as tempDir:
            root = Path(tempDir)
            path = root / "A.cpp"
            path.write_text("int a;\n", encoding="utf-8")
            with mock.patch.object(FileScan, "readSourceTextInternal", side_effect=OSError("잠김")):
                with self.assertRaises(OSError):
                    FileScan.checkFileConventionsInternal(path, root)


if __name__ == "__main__":
    unittest.main()
