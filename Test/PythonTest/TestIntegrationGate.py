#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""dev/RunIntegrationGate(`integ-gate`)의 판단 — ctest JUnit 요약 읽기, 증거를 받는 조건, 증거 파일 읽기. 빌드 · ctest 는 돌리지 않는다."""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ElementTree
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Scripts"))

from dev import RunIntegrationGate  # noqa: E402

kJunit = (
    '<?xml version="1.0"?><testsuite name="(empty)" tests="4">'
    '<testcase name="A" status="run"/>'
    '<testcase name="B" status="run"><failure message="Failed"/></testcase>'
    '<testcase name="C" status="notrun"/>'
    '<testcase name="D" status="run"><skipped/></testcase>'
    '</testsuite>'
)
kInputHash = "a" * 40


def makeRecordInternal(**override: object) -> dict:
    record: dict = {"inputs": kInputHash, "labels": "nogpu|lint",
                    "build": {"bKeepGoing": True, "bPassed": True, "returnCode": 0},
                    "ctest": {"bRan": True, "returnCode": 0, "total": 10, "passed": 10, "failed": 0, "skipped": 0, "failedNames": []},
                    "bPassed": True}
    record.update(override)
    return record


class JunitSummaryTest(unittest.TestCase):
    def testCountsPassedFailedAndSkipped(self) -> None:
        summary = RunIntegrationGate.parseJunitSummary(kJunit)
        self.assertEqual(summary, {"total": 4, "passed": 1, "failed": 1, "skipped": 2, "failedNames": ["B"]})

    def testBrokenXmlStopsInsteadOfReadingAsEmpty(self) -> None:
        with self.assertRaises(ElementTree.ParseError):
            RunIntegrationGate.parseJunitSummary("<testsuite><testcase")


class EvaluateEvidenceTest(unittest.TestCase):
    def testMissingEvidenceIsRejectedWithTheCommand(self) -> None:
        listReason = RunIntegrationGate.evaluateEvidence(None, kInputHash)
        self.assertEqual(1, len(listReason))
        self.assertIn("integ-gate run", listReason[0])

    def testPassingEvidenceIsAccepted(self) -> None:
        self.assertEqual([], RunIntegrationGate.evaluateEvidence(makeRecordInternal(), kInputHash))

    def testEachGapIsRejected(self) -> None:
        mapCase = {
            "다른 빌드 입력": makeRecordInternal(inputs="b" * 40),
            "좁은 라벨": makeRecordInternal(labels="lint"),
            "keep-going 아님": makeRecordInternal(build={"bKeepGoing": False, "bPassed": True, "returnCode": 0}),
            "빌드 실패": makeRecordInternal(build={"bKeepGoing": True, "bPassed": False, "returnCode": 1}, ctest={"bRan": False}),
            "ctest 안 돔": makeRecordInternal(ctest={"bRan": False}),
            "항목 0": makeRecordInternal(ctest={"bRan": True, "returnCode": 0, "total": 0, "failed": 0}),
            "ctest 실패": makeRecordInternal(ctest={"bRan": True, "returnCode": 8, "total": 3, "failed": 1, "failedNames": ["X"]}),
            "bPassed 거짓": makeRecordInternal(bPassed=False),
        }
        for name, record in mapCase.items():
            with self.subTest(name):
                self.assertNotEqual([], RunIntegrationGate.evaluateEvidence(record, kInputHash))

    def testFailedNamesAreShownInTheReason(self) -> None:
        record = makeRecordInternal(ctest={"bRan": True, "returnCode": 8, "total": 3, "failed": 1, "failedNames": ["LintFoo"]})
        self.assertIn("LintFoo", " ".join(RunIntegrationGate.evaluateEvidence(record, kInputHash)))


class ReadEvidenceTest(unittest.TestCase):
    def testMissingIsNoneAndBrokenStops(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            self.assertIsNone(RunIntegrationGate.readEvidenceRecord(directory, kInputHash))
            (directory / f"{kInputHash}.json").write_text("{깨짐", encoding="utf-8")
            with self.assertRaises(ValueError):
                RunIntegrationGate.readEvidenceRecord(directory, kInputHash)
            (directory / f"{kInputHash}.json").write_text(json.dumps(makeRecordInternal()), encoding="utf-8")
            self.assertEqual(kInputHash, RunIntegrationGate.readEvidenceRecord(directory, kInputHash)["inputs"])


if __name__ == "__main__":
    unittest.main()
