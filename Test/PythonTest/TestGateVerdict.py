#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""GateVerdict — 게이트 결과의 종류(통과 · 건너뜀 · 불성립 · 실패)와 사유를 읽고 세는 약속."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

kRepositoryRoot = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(kRepositoryRoot / "Scripts"))

from common.ExitCode import ExitCode  # noqa: E402
from lint.GateVerdict import (categoryOfReason, formatOutcomeLine, formatReasonCounts, kUnknownReason, kVerdictFailed,  # noqa: E402
                              kVerdictPassed, kVerdictSkipped, kVerdictUnavailable, mergeExitCodes, readOutcomeReason, verdictOfExitCode)


class GateVerdictTest(unittest.TestCase):
    def testExitCodesMapToDistinctVerdicts(self) -> None:
        self.assertEqual(verdictOfExitCode(ExitCode.Passed), kVerdictPassed)
        self.assertEqual(verdictOfExitCode(ExitCode.Skipped), kVerdictSkipped)
        self.assertEqual(verdictOfExitCode(ExitCode.PrerequisiteMissing), kVerdictUnavailable)
        self.assertEqual(verdictOfExitCode(ExitCode.Violation), kVerdictFailed)
        self.assertEqual(verdictOfExitCode(ExitCode.CheckInvalid), kVerdictFailed)

    def testUnknownExitCodeIsFailureNeverPass(self) -> None:
        self.assertEqual(verdictOfExitCode(42), kVerdictFailed)
        self.assertEqual(verdictOfExitCode(-1), kVerdictFailed)

    def testReasonRoundTripsThroughTheOutcomeLine(self) -> None:
        output = "잡담\n" + formatOutcomeLine("CheckX", True, "빌드 폴더 없음 — 컴파일 DB 가 없다") + "\n[PreCommitLint] 뒤 줄\n"
        self.assertEqual(readOutcomeReason(output), "빌드 폴더 없음 — 컴파일 DB 가 없다")
        self.assertEqual(readOutcomeReason(formatOutcomeLine("CheckY", False, "병합 커밋 — 이유")), "병합 커밋 — 이유")

    def testOrdinaryOutputHasNoReason(self) -> None:
        self.assertEqual(readOutcomeReason("[CheckX] OK (3 files)\n"), "")
        self.assertEqual(readOutcomeReason(""), "")

    def testCategoryIsTheTextBeforeTheFirstDash(self) -> None:
        self.assertEqual(categoryOfReason("빌드 폴더 없음 — 컴파일 DB 가 없다 — 그 프리셋을 configure"), "빌드 폴더 없음")
        self.assertEqual(categoryOfReason("해당 파일 변경 없음"), "해당 파일 변경 없음")
        self.assertEqual(categoryOfReason(""), kUnknownReason)

    def testReasonCountsGroupByCategoryMostFirst(self) -> None:
        listReason = ["해당 파일 변경 없음", "병합 커밋 — a", "해당 파일 변경 없음", "해당 파일 변경 없음 — 내용 규칙", "병합 커밋 — b", "트리 전체"]
        self.assertEqual(formatReasonCounts(listReason), "해당 파일 변경 없음 3 · 병합 커밋 2 · 트리 전체 1")
        self.assertEqual(formatReasonCounts([]), "")

    def testMergeKeepsTheWorstCode(self) -> None:
        self.assertEqual(mergeExitCodes([0, 0]), 0)
        self.assertEqual(mergeExitCodes([0, ExitCode.PrerequisiteMissing]), ExitCode.PrerequisiteMissing)
        self.assertEqual(mergeExitCodes([ExitCode.PrerequisiteMissing, ExitCode.Violation]), ExitCode.Violation)
        self.assertEqual(mergeExitCodes([ExitCode.Violation, ExitCode.CheckInvalid, 0]), ExitCode.CheckInvalid)
        self.assertEqual(mergeExitCodes([]), 0)


if __name__ == "__main__":
    unittest.main()
