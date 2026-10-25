#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""LintGate — 대상 고르기(`selectTargetFiles` = `common.Search.resolveFileArguments`)와 껍데기(종료 코드 0 · 1 · 2, `--root` 기본)."""

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

from common.ExitCode import ExitCode  # noqa: E402
from lint.LintGate import GateError, GateResult, LintGate, checkSelfTestCase  # noqa: E402


class _FixedGate(LintGate):
    """테스트용 게이트 — 받은 결과를 그대로 돌려준다. 본 루트를 남긴다."""

    description = "테스트용"
    selfTestSkipReason = "테스트용 — 등록되지 않는다"
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
    """테스트용 — 표 한 줄, 훑기가 무엇을 보고 쓰는지 바깥에서 정한다."""

    description = "테스트용"
    selfTestSkipReason = "테스트용 — 등록되지 않는다"
    mapExemption = {"Source/Allowed.cpp": "테스트용 예외"}
    bSee = False
    bUse = False

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser)

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        if self.bSee:
            self.seeExemption("Source/Allowed.cpp")
        if self.bUse:
            self.useExemption("Source/Allowed.cpp")
        return GateResult(summary="테스트")


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


class _OutcomeGate(LintGate):
    """테스트용 — 대상 파일을 `selectTargetFiles` 로 고르고, 바깥이 정한 결과를 낸다. 건너뜀 · 불성립이 통과와 갈라지는지 본다."""

    description = "테스트용"
    selfTestSkipReason = "테스트용 — 등록되지 않는다"
    result: GateResult | None = None

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser)

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        listPath = self.selectTargetFiles(repositoryRoot, args.files, listScanRoot=("Source",), suffixes=(".h",))
        return self.result if self.result is not None else GateResult(summary=f"{len(listPath)} files")


class LintGateOutcomeTest(unittest.TestCase):
    """통과 · 건너뜀 · 불성립은 종료 코드부터 다르다 — `OK` 줄과 같아 보이는 건너뜀은 없다."""

    def setUp(self) -> None:
        self._tempDir = tempfile.TemporaryDirectory()
        self.root = Path(self._tempDir.name).resolve()
        (self.root / "Source").mkdir()

    def tearDown(self) -> None:
        self._tempDir.cleanup()
        _OutcomeGate.result = None

    def runGate(self, argv: list[str] | None = None) -> tuple[int, str]:
        buffer = io.StringIO()
        with contextlib.redirect_stdout(buffer), contextlib.redirect_stderr(buffer):
            code = _OutcomeGate.run(["--root", str(self.root), *(argv or [])])
        return code, buffer.getvalue()

    def addHeader(self, name: str = "A.h") -> None:
        (self.root / "Source" / name).write_text("#pragma once\n", encoding="utf-8")

    def testUnavailableIsFiveAndShowsItsReason(self) -> None:
        _OutcomeGate.result = GateResult.unavailable("빌드 폴더 없음 — 컴파일 DB 가 없다")
        code, output = self.runGate()
        self.assertEqual(code, ExitCode.PrerequisiteMissing)
        self.assertIn(f"[{_OutcomeGate.name}] UNAVAILABLE (빌드 폴더 없음 — 컴파일 DB 가 없다)", output)
        self.assertNotIn("OK", output)

    def testSkippedWithFilesIsSeventySeven(self) -> None:
        self.addHeader()
        _OutcomeGate.result = GateResult.skipped("병합 커밋 — 상대 쪽은 이미 검사했다")
        code, output = self.runGate(["--files", "Source/A.h"])
        self.assertEqual(code, ExitCode.Skipped)
        self.assertIn("SKIPPED (병합 커밋 — 상대 쪽은 이미 검사했다)", output)

    def testSkippedInAWholeScanBecomesUnavailable(self) -> None:
        # 전체 훑기에 "볼 것이 없다" 는 정상이 아니라 입력이 없는 것이다 — 건너뜀을 이 길로 새게 두지 않는다.
        _OutcomeGate.result = GateResult.skipped("대상 없음 — 어쩌고")
        code, output = self.runGate()
        self.assertEqual(code, ExitCode.PrerequisiteMissing)
        self.assertIn("UNAVAILABLE (전체 훑기에 건너뜀은 없다", output)

    def testSkipOrUnavailableCannotCarryViolations(self) -> None:
        with self.assertRaises(ValueError):
            GateResult(listViolation=["a.h:1: 위반"], skipReason="이유")
        with self.assertRaises(ValueError):
            GateResult(skipReason="이유", unavailableReason="이유")

    def testZeroFilesInAWholeScanIsUnavailableNotOk(self) -> None:
        code, output = self.runGate()
        self.assertEqual(code, ExitCode.PrerequisiteMissing)
        self.assertIn("UNAVAILABLE (훑을 파일 없음", output)
        self.assertNotIn("OK", output)

    def testZeroFilesWithFilesArgumentIsSkippedNotOk(self) -> None:
        self.addHeader()
        (self.root / "Source" / "notes.txt").write_text("x\n", encoding="utf-8")
        code, output = self.runGate(["--files", "Source/notes.txt"])
        self.assertEqual(code, ExitCode.Skipped)
        self.assertIn("SKIPPED (대상 파일 없음", output)

    def testFilesFoundIsStillOk(self) -> None:
        self.addHeader()
        self.assertEqual(self.runGate()[0], ExitCode.Passed)
        self.assertEqual(self.runGate(["--files", "Source/A.h"])[0], ExitCode.Passed)

    def testViolationWinsOverEmptySelection(self) -> None:
        _OutcomeGate.result = GateResult(listViolation=["Source/A.h:1: 위반"])
        self.assertEqual(self.runGate()[0], ExitCode.Violation)

    def testOneNonEmptySelectionIsEnough(self) -> None:
        self.addHeader()

        class _TwoSelections(_OutcomeGate):
            def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
                self.selectTargetFiles(repositoryRoot, None, listScanRoot=("Nowhere",), suffixes=(".h",))
                self.selectTargetFiles(repositoryRoot, None, listScanRoot=("Source",), suffixes=(".h",))
                return GateResult(summary="둘 가운데 하나는 찼다")

        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(_TwoSelections.run(["--root", str(self.root)]), ExitCode.Passed)

    def testCheckedCountZeroIsUnavailable(self) -> None:
        # 파일 선택 도우미를 쓰지 않는 게이트도 본 수를 알리면 같은 규칙을 받는다.
        _OutcomeGate.result = GateResult(summary="0 manifests", checkedCount=0)
        self.assertEqual(self.runGate()[0], ExitCode.PrerequisiteMissing)

    def testGateCannotBeSkippedByBothTheHookAndCtest(self) -> None:
        # 훅 건너뜀은 "통합이 대신 본다" 는 약속이다 — CTest 에서도 빠지면 CI 잡 말고는 보는 곳이 없다.
        with self.assertRaises(TypeError):
            type("_BothSkipGate", (LintGate,), {"__module__": __name__, "preCommitSkipReason": "트리 전체 — CI", "ctestSkipReason": "느리다 — CI"})

    def testSelfTestCaseMayNotExpectExitZero(self) -> None:
        with self.assertRaises(TypeError):
            checkSelfTestCase("X", {"name": "n", "expectedViolation": "v", "expectedExitCode": 0})
        checkSelfTestCase("X", {"name": "n", "expectedViolation": "v", "expectedExitCode": ExitCode.PrerequisiteMissing})


if __name__ == "__main__":
    unittest.main()
