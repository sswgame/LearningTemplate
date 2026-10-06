#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""LintCatalog — 폴더가 목록이다: 파일 수 = 찾은 수, 게이트 없는 파일은 등록에서 멈춘다, 빌드 줄은 영어(ASCII)."""

from __future__ import annotations

import sys
import types
import unittest
from pathlib import Path

kRepositoryRoot = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(kRepositoryRoot / "Scripts"))
sys.path.insert(0, str(kRepositoryRoot / "Scripts" / "lint"))

from LintCatalog import LintScript, discoverLintScripts, makeLintTarget  # noqa: E402


class LintCatalogTest(unittest.TestCase):
    def testEveryFileInTheFolderIsDiscovered(self) -> None:
        for folderName in ("gate", "selftest"):
            with self.subTest(folder=folderName):
                listFile = [path for path in (kRepositoryRoot / "Scripts/lint" / folderName).glob("*.py") if path.stem != "__init__"]
                self.assertEqual(len(discoverLintScripts(folderName)), len(listFile))

    def testFileWithoutGateClassStopsRegistration(self) -> None:
        module = types.ModuleType("NoGateHere")
        script = LintScript(name="NoGateHere", folderName="gate", relPath="Scripts/lint/gate/NoGateHere.py",
                            scriptPath=kRepositoryRoot / "Scripts/lint/gate/NoGateHere.py", module=module)
        with self.assertRaises(ValueError):
            makeLintTarget(script)

    def testBuildCommentIsAscii(self) -> None:
        # ninja 가 찍는 줄 — 콘솔 코드 페이지에서 한글이 깨진 전례가 있어 영어다.
        for script in discoverLintScripts():
            with self.subTest(script=script.name):
                comment = makeLintTarget(script).buildComment
                self.assertTrue(comment)
                self.assertTrue(comment.isascii(), comment)


if __name__ == "__main__":
    unittest.main()
