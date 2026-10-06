#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""PreCommitLint.selectGatesForStaged — 훅이 게이트를 고르는 규칙(preCommitPattern · preCommitFileArgument · preCommitSkipReason)."""

from __future__ import annotations

import sys
import types
import unittest
from pathlib import Path

kRepositoryRoot = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(kRepositoryRoot / "Scripts"))
sys.path.insert(0, str(kRepositoryRoot / "Scripts" / "lint"))

from LintCatalog import LintScript  # noqa: E402
from LintGate import LintGate  # noqa: E402
from PreCommitLint import selectGatesForStaged  # noqa: E402


def makeScript(name: str, **mapAttribute) -> LintScript:
    """게이트 클래스 하나가 든 가짜 모듈 — 훅은 파일이 아니라 클래스의 선언만 본다."""
    module = types.ModuleType(name)
    gateClass = type(f"{name}Gate", (LintGate,), {"__module__": name, "description": name, **mapAttribute})
    setattr(module, gateClass.__name__, gateClass)
    return LintScript(name=name, folderName="gate", relPath=f"Scripts/lint/gate/{name}.py",
                      scriptPath=kRepositoryRoot / f"Scripts/lint/gate/{name}.py", module=module)


class PreCommitLintSelectionTest(unittest.TestCase):
    def setUp(self) -> None:
        self.root = kRepositoryRoot
        self.listScript = [
            makeScript("Always"),
            makeScript("SourceFiles", preCommitPattern=("Source/*",), preCommitFileArgument="--files"),
            makeScript("CmakeWholeTree", preCommitPattern=("*.cmake", "*CMakeLists.txt")),
            makeScript("Skipped", preCommitSkipReason="빌드 폴더가 필요하다"),
        ]

    def plan(self, listStaged: list[str], listFileScoped: list[str] | None = None) -> dict[str, tuple[str, list[str]]]:
        listStagedPath = [self.root / path for path in listStaged]
        listScopedPath = listStagedPath if listFileScoped is None else [self.root / path for path in listFileScoped]
        listPlan = selectGatesForStaged(self.root, listStagedPath, listScopedPath, self.listScript)
        return {plan.script.name: (plan.skipReason, plan.listArgument) for plan in listPlan}

    def testPatternlessGateAlwaysRunsOnTheWholeTree(self) -> None:
        skipReason, listArgument = self.plan(["docs/a.md"])["Always"]
        self.assertEqual(skipReason, "")
        self.assertEqual(listArgument, ["--root", str(self.root)])

    def testFileGateGetsOnlyMatchingStagedFiles(self) -> None:
        skipReason, listArgument = self.plan(["Source/A.h", "docs/a.md", "Source/B.cpp"])["SourceFiles"]
        self.assertEqual(skipReason, "")
        self.assertEqual(listArgument, ["--root", str(self.root), "--files", str(self.root / "Source/A.h"), str(self.root / "Source/B.cpp")])

    def testGateWithoutMatchingFileIsSkipped(self) -> None:
        mapPlan = self.plan(["docs/a.md"])
        self.assertTrue(mapPlan["SourceFiles"][0])
        self.assertTrue(mapPlan["CmakeWholeTree"][0])

    def testWholeTreeGateRunsWithoutFiles(self) -> None:
        skipReason, listArgument = self.plan(["cmake/Engine/X.cmake"])["CmakeWholeTree"]
        self.assertEqual(skipReason, "")
        self.assertNotIn("--files", listArgument)

    def testMergeCommitScopesOnlyFileGates(self) -> None:
        # 병합 커밋: Source/A.h 는 한쪽 부모와 같다 — 파일 인자 게이트는 건너뛰고, 트리 전체 게이트는 그대로 돈다.
        mapPlan = self.plan(["Source/A.h", "cmake/X.cmake"], listFileScoped=["cmake/X.cmake"])
        self.assertIn("병합", mapPlan["SourceFiles"][0])
        self.assertEqual(mapPlan["CmakeWholeTree"][0], "")

    def testDeclaredSkipReasonWins(self) -> None:
        self.assertEqual(self.plan(["Source/A.h"])["Skipped"][0], "빌드 폴더가 필요하다")


if __name__ == "__main__":
    unittest.main()
