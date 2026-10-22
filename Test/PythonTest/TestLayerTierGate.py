#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""층 게이트의 "표 = 계산값" 검사(`lint.LayerGraph.findTierTableProblems`)와 `RunEngineLayerGraph --apply` 의 올리기 거부."""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

kRepositoryRoot = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(kRepositoryRoot / "Scripts"))

from lint.LayerGraph import EdgeMap, computeTiers, edgesOfFile, findTierTableProblems, newEdgeMap  # noqa: E402
from lint.report.RunEngineLayerGraph import writeTierTablesInternal  # noqa: E402


def makeEdgeMap(*listEdge: tuple[str, str]) -> EdgeMap:
    mapEdge = newEdgeMap()
    for sourceLayer, destLayer in listEdge:
        mapEdge[sourceLayer][destLayer].append(None)
    return mapEdge


def findProblems(mapEdge: EdgeMap, mapTable: dict[str, int], mapReason: dict[str, str] | None = None) -> list[str]:
    return findTierTableProblems(mapEdge, mapTable, mapReason or {}, tableLabel="[tier]", reasonLabel="[tierReason]")


class TierTableProblemTest(unittest.TestCase):
    def testMatchingTableHasNoProblem(self) -> None:
        mapEdge = makeEdgeMap(("B", "A"), ("C", "B"))
        self.assertEqual(findProblems(mapEdge, {"A": 0, "B": 1, "C": 2}), [])

    def testTableBelowComputedFails(self) -> None:
        mapEdge = makeEdgeMap(("B", "A"), ("C", "B"))
        listProblem = findProblems(mapEdge, {"A": 0, "B": 1, "C": 1})
        self.assertEqual(len(listProblem), 1)
        self.assertIn("C:", listProblem[0])
        self.assertIn("낮다", listProblem[0])

    def testRaiseWithoutReasonFails(self) -> None:
        mapEdge = makeEdgeMap(("B", "A"))
        listProblem = findProblems(mapEdge, {"A": 0, "B": 5})
        self.assertEqual(len(listProblem), 1)
        self.assertIn("이유가 없다", listProblem[0])

    def testRaiseWithReasonPasses(self) -> None:
        mapEdge = makeEdgeMap(("B", "A"))
        self.assertEqual(findProblems(mapEdge, {"A": 0, "B": 5}, {"B": "정적 정리가 위를 본다"}), [])

    def testStaleReasonFails(self) -> None:
        mapEdge = makeEdgeMap(("B", "A"))
        listProblem = findProblems(mapEdge, {"A": 0, "B": 1}, {"B": "옛 사유"})
        self.assertEqual(len(listProblem), 1)
        self.assertIn("낡았다", listProblem[0])

    def testReasonForUnknownLayerFails(self) -> None:
        listProblem = findProblems(makeEdgeMap(("B", "A")), {"A": 0, "B": 1}, {"Ghost": "없는 폴더"})
        self.assertEqual(len(listProblem), 1)
        self.assertIn("Ghost", listProblem[0])

    def testCycleFailsEvenWhenTableIsConsistent(self) -> None:
        mapEdge = makeEdgeMap(("A", "B"), ("B", "A"))
        listProblem = findProblems(mapEdge, {"A": 1, "B": 1})
        self.assertTrue(any("강결합 묶음" in problem for problem in listProblem))

    def testEdgesOfFileDropsSelfEdgesAndDuplicates(self) -> None:
        self.assertEqual(edgesOfFile("A", ["A", "B", "B", "C"]), [("A", "B"), ("A", "C")])

    def testComputedTierIsKahnLevel(self) -> None:
        self.assertEqual(computeTiers(makeEdgeMap(("B", "A"), ("C", "A"), ("C", "B"))), {"A": 0, "B": 1, "C": 2})


class ApplyRaiseGuardTest(unittest.TestCase):
    """`--apply` 는 내리기 · 같은 값만 쓰고, 올리기는 사유가 있을 때만 쓴다. `[tierReason]` 폴더는 건드리지 않는다."""

    kTable = "[tier]\nA = 0\nB = 1\nC = 2\nD = 3\n\n[subtier]\n"

    def runApply(self, mapTier: dict[str, int], mapReason: dict[str, str], raiseReason: str) -> tuple[str, list[str]]:
        with tempfile.TemporaryDirectory() as folder:
            rulePath = Path(folder) / "CheckEngineLayers.toml"
            rulePath.write_text(self.kTable, encoding="utf-8", newline="")
            _, listRefused = writeTierTablesInternal(rulePath, mapTier, mapReason, raiseReason)
            return rulePath.read_text(encoding="utf-8"), listRefused

    def testLoweringIsWritten(self) -> None:
        text, listRefused = self.runApply({"A": 0, "B": 1, "C": 1, "D": 3}, {}, "")
        self.assertIn("C = 1", text)
        self.assertEqual(listRefused, [])

    def testRaisingWithoutReasonIsRefused(self) -> None:
        text, listRefused = self.runApply({"A": 0, "B": 1, "C": 2, "D": 7}, {}, "")
        self.assertIn("D = 3", text)
        self.assertEqual(listRefused, ["D 3 -> 7"])

    def testRaisingWithReasonIsWrittenWithTheReasonComment(self) -> None:
        text, listRefused = self.runApply({"A": 0, "B": 1, "C": 2, "D": 7}, {}, "새 의존이 생겼다")
        self.assertIn("D = 7", text)
        self.assertIn("# 상향 3 -> 7: 새 의존이 생겼다", text)
        self.assertEqual(listRefused, [])

    def testReasonedLayerIsLeftAlone(self) -> None:
        text, listRefused = self.runApply({"A": 0, "B": 1, "C": 2, "D": 1}, {"D": "일부러 높다"}, "")
        self.assertIn("D = 3", text)
        self.assertEqual(listRefused, [])


if __name__ == "__main__":
    unittest.main()
