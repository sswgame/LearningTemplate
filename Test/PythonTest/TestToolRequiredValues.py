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

from common import Host  # noqa: E402
from common.PackFormat import PackFormatSpec, kPackFormatConfigRelative  # noqa: E402
from conventions import FileScan  # noqa: E402


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

    def testUnreadableSourceIsAViolation(self) -> None:
        with tempfile.TemporaryDirectory() as tempDir:
            root = Path(tempDir)
            path = root / "A.cpp"
            path.write_text("int a;\n", encoding="utf-8")
            with mock.patch.object(FileScan, "readSourceTextInternal", side_effect=OSError("잠김")):
                listViolation = FileScan.checkFileConventionsInternal(path, root)
        self.assertEqual(1, len(listViolation))
        self.assertEqual("Style/FileRead", listViolation[0].rule_category)


if __name__ == "__main__":
    unittest.main()
