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
import unittest.mock
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

    def select(self, listFile: list[str] | None, **kwargs: object) -> list[str]:
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


class _ExemptGate(LintGate):
    """시험용 — 표 한 줄, 훑기가 무엇을 보고 쓰는지 바깥에서 정한다."""

    description = "시험용"
    selfTestSkipReason = "시험용 — 등록되지 않는다"
    mapExemption = {"Source/Allowed.cpp": "시험용 예외"}
    bSee = False
    bUse = False

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser)

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        if self.bSee:
            self.seeExemption("Source/Allowed.cpp")
        if self.bUse:
            self.useExemption("Source/Allowed.cpp")
        return GateResult(summary="시험")


class LintGateExemptionTest(unittest.TestCase):
    """`mapExemption` — 쓴 줄은 통과, 본 대상에 쓰지 않은 줄 · 실제 저장소에서 대상이 없는 줄은 낡은 예외, 이유 없는 줄은 종료 2."""

    def runGate(self, root: Path, bSee: bool, bUse: bool, argv: list[str] | None = None) -> int:
        _ExemptGate.bSee, _ExemptGate.bUse = bSee, bUse
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            return _ExemptGate.run(["--root", str(root), *(argv or [])])

    def testUsedExemptionPasses(self) -> None:
        with tempfile.TemporaryDirectory() as tempDir:
            self.assertEqual(self.runGate(Path(tempDir), True, True), 0)

    def testSeenButUnusedExemptionIsStale(self) -> None:
        with tempfile.TemporaryDirectory() as tempDir:
            self.assertEqual(self.runGate(Path(tempDir), True, False), 1)

    def testUnseenExemptionIsStaleOnlyInARealRepository(self) -> None:
        with tempfile.TemporaryDirectory() as tempDir:
            root = Path(tempDir)
            self.assertEqual(self.runGate(root, False, False), 0)      # 셀프테스트 조각 같은 임시 트리 — 판단하지 않는다
            (root / ".git").mkdir()
            self.assertEqual(self.runGate(root, False, False), 1)

    def testPartialScanDoesNotJudgeStaleness(self) -> None:
        with tempfile.TemporaryDirectory() as tempDir:
            root = Path(tempDir)
            (root / ".git").mkdir()
            (root / "Probe.cpp").write_text("int x;\n", encoding="utf-8")
            self.assertEqual(self.runGate(root, True, False, ["--files", "Probe.cpp"]), 0)

    def testExemptionWithoutReasonStopsTheGate(self) -> None:
        with tempfile.TemporaryDirectory() as tempDir, unittest.mock.patch.dict(_ExemptGate.mapExemption, {"Source/Allowed.cpp": " "}):
            self.assertEqual(self.runGate(Path(tempDir), True, True), 2)

    def testRecordIsClearedBetweenRuns(self) -> None:
        with tempfile.TemporaryDirectory() as tempDir:
            self.assertEqual(self.runGate(Path(tempDir), True, True), 0)
            self.assertEqual(self.runGate(Path(tempDir), True, False), 1)


if __name__ == "__main__":
    unittest.main()
