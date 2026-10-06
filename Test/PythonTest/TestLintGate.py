#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""LintGate — 대상 고르기(`selectTargetFiles` = `common.resolveFileArguments`)와 껍데기(종료 코드 0 · 1 · 2, `--root` 기본)."""

from __future__ import annotations

import argparse
import contextlib
import io
import sys
import tempfile
import unittest
from pathlib import Path

kRepositoryRoot = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(kRepositoryRoot / "Scripts"))
sys.path.insert(0, str(kRepositoryRoot / "Scripts" / "lint"))

from LintGate import GateError, GateResult, LintGate  # noqa: E402


class _FixedGate(LintGate):
    """시험용 게이트 — 받은 결과를 그대로 돌려준다. 본 루트를 남긴다."""

    description = "시험용"
    selfTestSkipReason = "시험용 — 등록되지 않는다"
    seenRoot: Path | None = None
    result: GateResult | None = None
    bRaise = False

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        type(self).seenRoot = repositoryRoot
        if self.bRaise:
            raise GateError("성립하지 않는다")
        return self.result or GateResult(listViolation=[])


def runQuietly(argv: list[str]) -> int:
    with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
        return _FixedGate.run(argv)


class LintGateSelectionTest(unittest.TestCase):
    def setUp(self) -> None:
        self._tempDir = tempfile.TemporaryDirectory()
        self.root = Path(self._tempDir.name).resolve()
        for relPath in ("Source/A.h", "Source/B.CPP", "Source/build/Gen.h", "Tools/T.h", "Source/notes.txt"):
            path = self.root / relPath
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("x\n", encoding="utf-8")

    def tearDown(self) -> None:
        self._tempDir.cleanup()

    def select(self, listFile: list[str] | None, **kwargs) -> list[str]:
        listPath = LintGate.selectTargetFiles(self.root, listFile, suffixes=(".h", ".cpp"), **kwargs)
        return [path.relative_to(self.root).as_posix() for path in listPath]

    def testRelativePathsResolveAgainstRepositoryRoot(self) -> None:
        self.assertEqual(self.select(["Source/A.h"]), ["Source/A.h"])

    def testSuffixIsCaseInsensitive(self) -> None:
        self.assertEqual(self.select(["Source/B.CPP"]), ["Source/B.CPP"])

    def testFilesOutsideRepositoryAreDropped(self) -> None:
        with tempfile.TemporaryDirectory() as outsideDir:
            outside = Path(outsideDir) / "Outside.h"
            outside.write_text("x\n", encoding="utf-8")
            self.assertEqual(self.select([str(outside)]), [])

    def testBuildFolderIsExcludedForArgumentsAndWalk(self) -> None:
        self.assertEqual(self.select(["Source/build/Gen.h"]), [])
        self.assertNotIn("Source/build/Gen.h", self.select(None))

    def testScanRootLimitsArgumentsAndWalk(self) -> None:
        self.assertEqual(self.select(["Tools/T.h"], listScanRoot=("Source",)), [])
        self.assertEqual(self.select(None, listScanRoot=("Source",)), ["Source/A.h", "Source/B.CPP"])

    def testWrongSuffixIsDropped(self) -> None:
        self.assertEqual(self.select(["Source/notes.txt"]), [])


class LintGateShellTest(unittest.TestCase):
    def tearDown(self) -> None:
        _FixedGate.result = None
        _FixedGate.bRaise = False

    def testCleanIsZero(self) -> None:
        self.assertEqual(runQuietly(["--root", str(kRepositoryRoot)]), 0)

    def testViolationIsOne(self) -> None:
        _FixedGate.result = GateResult(listViolation=["a.h:1: 위반"])
        self.assertEqual(runQuietly(["--root", str(kRepositoryRoot)]), 1)

    def testGateErrorIsTwo(self) -> None:
        _FixedGate.bRaise = True
        self.assertEqual(runQuietly(["--root", str(kRepositoryRoot)]), 2)

    def testRootDefaultsToRepository(self) -> None:
        runQuietly([])
        self.assertEqual(_FixedGate.seenRoot.resolve(), kRepositoryRoot.resolve())


if __name__ == "__main__":
    unittest.main()
