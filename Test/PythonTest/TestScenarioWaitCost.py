#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""qa/ScenarioWaitCost(`scenario-waits`) — `<At>` 시점 셈이 C++ 과 같고, 긴 고정 대기와 조건 대기 후보를 가려내고, 모든 시나리오 파일이 읽힌다."""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ElementTree
from pathlib import Path

kScriptsRoot = Path(__file__).resolve().parents[2] / "Scripts"
sys.path.insert(0, str(kScriptsRoot))

from common.Paths import getProjectRoot  # noqa: E402
from qa import ScenarioWaitCost as WaitCost  # noqa: E402


def makeAt(attributes: str) -> ElementTree.Element:
    return ElementTree.fromstring(f"<At {attributes}/>")


class AtFrameTest(unittest.TestCase):
    def testAbsoluteAndRelativeTimings(self) -> None:
        self.assertEqual(40, WaitCost.computeAtFrame(makeAt('frame="40"'), 7, 1.0 / 60.0))
        self.assertEqual(17, WaitCost.computeAtFrame(makeAt('after="10"'), 7, 1.0 / 60.0))
        self.assertEqual(60, WaitCost.computeAtFrame(makeAt('time="1"'), 7, 1.0 / 60.0))
        self.assertEqual(37, WaitCost.computeAtFrame(makeAt('afterSeconds="0.5"'), 7, 1.0 / 60.0))   # 0.5 / (1/60) = 30 프레임

    def testNeedsExactlyOneTiming(self) -> None:
        with self.assertRaises(ValueError):
            WaitCost.computeAtFrame(makeAt('frame="1" after="2"'), 0, 1.0 / 60.0)
        with self.assertRaises(ValueError):
            WaitCost.computeAtFrame(makeAt(""), 0, 1.0 / 60.0)


class ScenarioAnalysisTest(unittest.TestCase):
    def testLongGapsAreCountedAndWaitUntilIsNotACandidate(self) -> None:
        text = ('<Scenario name="t" fixedDelta="0.0166667">'
                '<Launch variable="gv_x" value="1"/>'
                '<At frame="0"><Variable name="gv_a" value="1"/></At>'
                '<At frame="200"><Expect probe="A.B" equals="1"/></At>'
                '<At after="5"><WaitUntil probe="A.B" equals="2"/></At>'
                '<At after="100"><WaitUntil probe="A.B" equals="3"/></At>'
                '<At after="1"><Pass/></At>'
                '</Scenario>')
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "t.scenario.xml"
            path.write_text(text, encoding="utf-8")
            cost = WaitCost.analyzeScenario("t.scenario.xml", path)
        self.assertEqual(306, cost.lastFrame)
        self.assertEqual(2, cost.waitUntilCount)
        self.assertEqual(300, cost.gapFrames)
        first, second = cost.listGap
        self.assertEqual(200, first.frames)
        self.assertTrue(first.bProbeAssertNext)      # 고정 대기 뒤 Expect — 조건 대기로 바꿀 후보
        self.assertFalse(first.bConditionNext)
        self.assertEqual(100, second.frames)
        self.assertTrue(second.bConditionNext)       # 이미 WaitUntil 이 기다린다
        self.assertFalse(second.bProbeAssertNext)

    def testEveryScenarioFileInTheRepositoryIsReadable(self) -> None:
        listCost = WaitCost.collectCosts(getProjectRoot() / "Resource")
        self.assertGreater(len(listCost), 100)
        self.assertTrue(all(cost.lastFrame >= 0 for cost in listCost))

    def testMeasuredReportsKeepTheLargestPerScenario(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            for backend, wall in (("dx12", 1200), ("vk", 3400)):
                data = {"name": "t", "wallMilliseconds": wall, "frames": 10,
                        "timeline": {"scheduledWaitMilliseconds": wall // 2, "heldMilliseconds": 5, "heldFrames": 2, "scheduledWaitFrames": 7, "startMilliseconds": 100}}
                (Path(folder) / f"t_{backend}.json").write_text(json.dumps(data), encoding="utf-8")
            mapMeasured = WaitCost.readMeasuredMilliseconds(Path(folder))
        self.assertEqual(3400.0, mapMeasured["t"]["wallMilliseconds"])
        self.assertEqual(1700.0, mapMeasured["t"]["scheduledWaitMilliseconds"])


if __name__ == "__main__":
    unittest.main()
