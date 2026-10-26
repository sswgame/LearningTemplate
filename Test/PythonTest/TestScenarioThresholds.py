#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""qa/ScenarioThresholds(`scenario-thresholds`) — 보고의 지표 줄을 단언과 짝짓고, 문서 규칙으로 문턱을 제안하고, 승격할 때 줄 끝을 지킨다."""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

kScriptsRoot = Path(__file__).resolve().parents[2] / "Scripts"
sys.path.insert(0, str(kScriptsRoot))

from qa import ScenarioThresholds as Thresholds  # noqa: E402

kScenarioText = (
    '<Scenario name="t.thresholds" fixedDelta="0.0166667" timeoutFrames="100">\r\n'
    '\t<!-- <ExpectImage file="old.ppm" metric="meanLuma" atLeast="0.9" provisional="주석 안의 것은 센다면 안 된다"/> -->\r\n'
    '\t<At frame="5"><ExpectImage file="a.ppm" metric="meanLuma" atLeast="0.05" provisional="측정값 기록 없음"/></At>\r\n'
    '\t<At frame="9"><ExpectImage file="a.ppm" metric="meanLuma" atMost="0.9" measured="2026-10-25 all:0.5"/></At>\r\n'
    '\t<At frame="12"><ExpectImage file="b.ppm" metric="differingFraction" reference="a.ppm" atMost="0.001" provisional="어림"/><Pass/></At>\r\n'
    '</Scenario>\r\n')


def writeScenario(root: Path) -> str:
    resourceId = "engine/automation/t.scenario.xml"
    path = root / resourceId
    path.parent.mkdir(parents=True)
    path.write_bytes(kScenarioText.encode("utf-8"))
    return resourceId


def writeReport(folder: Path, backend: str, listMetric: list[str], exitCode: int = 0) -> None:
    data = {"name": "t.thresholds", "result": "PASS", "exitCode": exitCode, "startedAt": "2026-10-27 09:00:00", "metrics": listMetric}
    (folder / f"t_{backend}.json").write_text(json.dumps(data), encoding="utf-8")


class RuleTest(unittest.TestCase):
    def testRoundSignificantMovesInTheSafeDirection(self) -> None:
        self.assertAlmostEqual(0.33, Thresholds.roundSignificant(0.3304, 2, bUp=False))
        self.assertAlmostEqual(0.19, Thresholds.roundSignificant(0.1892, 2, bUp=True))
        self.assertAlmostEqual(-0.0043, Thresholds.roundSignificant(-0.0043015, 2, bUp=True))

    def testProposeFollowsTheDocumentedRule(self) -> None:
        self.assertAlmostEqual(0.33, Thresholds.proposeThreshold("atLeast", 0.47206, 0.47206))     # 잰 값의 70 % 아래
        self.assertAlmostEqual(0.18, Thresholds.proposeThreshold("atMost", 0.12, 0.12))            # 1.5 배 위
        self.assertAlmostEqual(0.001, Thresholds.proposeThreshold("atMost", 0.0, 0.0))             # 0 이 기준이면 0.001
        self.assertAlmostEqual(-0.0043, Thresholds.proposeThreshold("atMost", -0.0086, -0.0086))   # 부호 있는 값은 0 과 잰 값 사이 절반쯤

    def testLooserThresholdHoldsTheMarginAndTighterOneDoesNot(self) -> None:
        self.assertTrue(Thresholds.holdsMargin("atLeast", 0.05, 0.5, 0.5))
        self.assertFalse(Thresholds.holdsMargin("atLeast", 0.45, 0.5, 0.5))
        self.assertTrue(Thresholds.holdsMargin("atMost", 0.5, 0.2, 0.2))
        self.assertFalse(Thresholds.holdsMargin("atMost", 0.21, 0.2, 0.2))


class ProposalTest(unittest.TestCase):
    def testStepsSkipCommentsAndKeepFileOrder(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            listStep = Thresholds.parseSteps(root, writeScenario(root))
            self.assertEqual(3, len(listStep))
            self.assertEqual([True, False, True], [step.bProvisional for step in listStep])
            self.assertEqual(("meanLuma", "0,0,1,1", "a.ppm"), listStep[0].key)
            self.assertEqual("t.thresholds", listStep[0].scenarioName)

    def testProposalPairsLinesByOrderAndKeepsLooseThresholds(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            resourceId = writeScenario(root)
            reports = root / "reports"
            reports.mkdir()
            for backend in ("dx12", "vk"):
                writeReport(reports, backend, ["meanLuma(0,0,1,1) a.ppm = 0.5", "meanLuma(0,0,1,1) a.ppm = 0.5", "differingFraction(0,0,1,1) b.ppm = 0.0004"])
            listStep = Thresholds.parseSteps(root, resourceId)
            listProposal = Thresholds.buildProposals(listStep, Thresholds.readMeasurements([reports]), ["dx12", "vk"])
            self.assertEqual(2, len(listProposal))
            first, second = listProposal
            self.assertEqual(Thresholds.kStatusPromote, first.status)
            self.assertEqual("2026-10-27 all:0.5", first.measuredText)
            self.assertIsNone(first.newThreshold)                      # 0.05 는 0.35 아래 — 유지
            self.assertEqual(Thresholds.kStatusPromote, second.status)
            self.assertIsNone(second.newThreshold)                     # 규칙 값 0.0006 보다 느슨한 0.001 은 그대로 둔다
            self.assertEqual(0.0004, second.mapBackendValue["dx12"])

    def testDefectBackendMissingAndLineCountMismatchAreNotPromoted(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            resourceId = writeScenario(root)
            reports = root / "reports"
            reports.mkdir()
            writeReport(reports, "dx12", ["meanLuma(0,0,1,1) a.ppm = 0.5", "meanLuma(0,0,1,1) a.ppm = 0.5", "differingFraction(0,0,1,1) b.ppm = 0.0004"])
            writeReport(reports, "vk", ["meanLuma(0,0,1,1) a.ppm = 0.2", "meanLuma(0,0,1,1) a.ppm = 0.5"])    # 첫 값이 크게 다르고 마지막 줄이 없다
            listStep = Thresholds.parseSteps(root, resourceId)
            listProposal = Thresholds.buildProposals(listStep, Thresholds.readMeasurements([reports]), ["dx12", "vk"])
            self.assertEqual(Thresholds.kStatusDefect, listProposal[0].status)
            self.assertEqual(Thresholds.kStatusSkip, listProposal[1].status)       # vk 의 b.ppm 줄이 없다
            self.assertIn("vk", listProposal[1].note)

    def testFailedRunReportsAreIgnored(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            reports = Path(folder)
            writeReport(reports, "dx12", ["meanLuma(0,0,1,1) a.ppm = 0.5"], exitCode=10)
            self.assertEqual([], Thresholds.readMeasurements([reports]))

    def testApplyReplacesOnlyThePromotedAttributesAndKeepsCrlf(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            resourceId = writeScenario(root)
            reports = root / "reports"
            reports.mkdir()
            for backend in ("dx12", "vk"):
                writeReport(reports, backend, ["meanLuma(0,0,1,1) a.ppm = 0.5", "meanLuma(0,0,1,1) a.ppm = 0.5", "differingFraction(0,0,1,1) b.ppm = 0.0004"])
            listProposal = Thresholds.buildProposals(Thresholds.parseSteps(root, resourceId), Thresholds.readMeasurements([reports]), ["dx12", "vk"])
            self.assertEqual(2, Thresholds.applyProposals(root, listProposal))
            raw = (root / resourceId).read_bytes()
            text = raw.decode("utf-8")
            self.assertNotIn(b"\n", raw.replace(b"\r\n", b""))
            self.assertIn('metric="meanLuma" atLeast="0.05" measured="2026-10-27 all:0.5"', text)
            self.assertIn('provisional="주석 안의 것은 센다면 안 된다"', text)   # 주석은 그대로
            self.assertEqual(1, text.count('provisional="주석'))
            self.assertEqual([False, False, False], [step.bProvisional for step in Thresholds.parseSteps(root, resourceId)])


if __name__ == "__main__":
    unittest.main()
