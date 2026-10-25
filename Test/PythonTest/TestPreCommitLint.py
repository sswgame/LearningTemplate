#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""PreCommitLint.selectGatesForStaged — 훅이 게이트를 고르는 규칙(preCommitPattern · preCommitFileArgument · preCommitSkipReason)."""

from __future__ import annotations

import contextlib
import io
import re
import sys
import types
import unittest
from pathlib import Path
from unittest import mock

kRepositoryRoot = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(kRepositoryRoot / "Scripts"))

from common.ExitCode import ExitCode  # noqa: E402
from lint.LintCatalog import LintScript, discoverLintScripts, discoverLintTargets  # noqa: E402
from lint.LintGate import LintGate  # noqa: E402
from lint import PreCommitLint  # noqa: E402
from lint.PreCommitLint import GateOutcome, StagedContentProbe, formatGateSummary, selectGatesForStaged  # noqa: E402


def makeScript(name: str, **mapAttribute: object) -> LintScript:
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

    def testOnlyRunningWholeTreeGatesGoToBackground(self) -> None:
        listStagedPath = [self.root / "Source/A.h"]
        mapBackground = {plan.script.name: plan.bBackground
                         for plan in selectGatesForStaged(self.root, listStagedPath, listStagedPath, self.listScript)}
        self.assertEqual(mapBackground, {"Always": True, "SourceFiles": False, "CmakeWholeTree": False, "Skipped": False})


class PreCommitLintRealGateTest(unittest.TestCase):
    """실제 게이트의 훅 선언 — 트리 전체 게이트가 관계없는 커밋에서도 돌면 훅의 바닥 시간이 그만큼 는다."""

    def plan(self, gateName: str, listStaged: list[str]) -> str:
        listScript = [script for script in discoverLintScripts("gate") if script.name == gateName]
        listStagedPath = [kRepositoryRoot / path for path in listStaged]
        return selectGatesForStaged(kRepositoryRoot, listStagedPath, listStagedPath, listScript)[0].skipReason

    def testDataFileReferencesSkipsDocumentOnlyCommit(self) -> None:
        self.assertTrue(self.plan("CheckDataFileReferences", ["docs/plans/Candidates-engine.md", "Resource/engine/a.xml"]))

    def testDataFileReferencesRunsForListFile(self) -> None:
        self.assertEqual(self.plan("CheckDataFileReferences", ["Source/Core/Predefined/X.xxx"]), "")
        self.assertEqual(self.plan("CheckDataFileReferences", ["cmake/Engine/X.cmake"]), "")


class PreCommitLintContentRuleTest(unittest.TestCase):
    """내용 규칙(`preCommitContentPattern` · `preCommitChangedLinePattern`) — 토큰이 없는 소스 커밋에서는 트리 전체 게이트가 서지 않는다."""

    class FakeProbe(StagedContentProbe):
        def __init__(self, mapBlob: dict[str, str], mapChanged: dict[str, str]) -> None:
            super().__init__(kRepositoryRoot)
            self._mapBlob = dict(mapBlob)
            self._mapChangedLine = dict(mapChanged)

        def hasContentMatch(self, listRelative: list[str], pattern: str) -> bool:
            return any(re.search(pattern, self._mapBlob.get(relative, "")) is not None for relative in listRelative)

    def plan(self, gateName: str, listStaged: list[str], mapBlob: dict[str, str], mapChanged: dict[str, str]) -> str:
        listScript = [script for script in discoverLintScripts("gate") if script.name == gateName]
        listStagedPath = [kRepositoryRoot / path for path in listStaged]
        probe = self.FakeProbe(mapBlob, mapChanged)
        return selectGatesForStaged(kRepositoryRoot, listStagedPath, listStagedPath, listScript, probe)[0].skipReason

    def testConfigReferenceSkipsSourceWithoutConfigTokens(self) -> None:
        self.assertTrue(self.plan("CheckConfigReference", ["Source/Core/A.cpp"], {"Source/Core/A.cpp": "int main() {}"}, {}))

    def testConfigReferenceRunsForGlobalVariableOrReflect(self) -> None:
        self.assertEqual(self.plan("CheckConfigReference", ["Source/Core/A.cpp"], {"Source/Core/A.cpp": "SW_GLOBAL_VARIABLE( int32, gv_a, 1, \"x\" );"}, {}), "")
        self.assertEqual(self.plan("CheckConfigReference", ["Source/Core/A.h"], {"Source/Core/A.h": "REFLECT( X ) struct S {};"}, {}), "")

    def testConfigReferenceRunsForListFilesWithoutLookingAtContent(self) -> None:
        self.assertEqual(self.plan("CheckConfigReference", ["Config/Engine/A.json"], {}, {}), "")

    def testDuplicateTypeNamesLooksOnlyAtChangedLines(self) -> None:
        self.assertTrue(self.plan("CheckDuplicateTypeNames", ["Source/A.h"], {}, {"Source/A.h": "int x = 1;"}))
        self.assertEqual(self.plan("CheckDuplicateTypeNames", ["Source/A.h"], {}, {"Source/A.h": "struct Foo"}), "")

    def testTreeWideCrossFileGatesAreLeftToCi(self) -> None:
        # 훅의 바닥이던 트리 전체 대조 게이트는 훅에서 돌지 않는다 — 그 대신 CI 린트 잡(RunLintSuite)과 ctest -L lint 의 목록에 남아 있어야 한다.
        listRegistered = {target.name for target in discoverLintTargets()}
        for gateName in ("CheckNameReferences", "CheckKitNamespaces"):
            self.assertIn("CI", self.plan(gateName, ["Source/A.cpp"], {"Source/A.cpp": "SW_LOG_CALLER( \"x\" ); gv_x"}, {}))
            self.assertIn(gateName, listRegistered)

    def testRenderOwnershipRunsOnlyForItsHeaders(self) -> None:
        self.assertTrue(self.plan("CheckRenderOwnership", ["Source/Engine/Scene/A.cpp"], {}, {}))
        self.assertEqual(self.plan("CheckRenderOwnership", ["Source/Engine/Renderer/Scene/GPUSceneSnapshot.h"], {}, {}), "")

    def testEngineLayersTakesStagedFiles(self) -> None:
        listScript = [script for script in discoverLintScripts("gate") if script.name == "CheckEngineLayers"]
        listStaged = [kRepositoryRoot / "Source/Engine/Scene/A.cpp"]
        plan = selectGatesForStaged(kRepositoryRoot, listStaged, listStaged, listScript)[0]
        self.assertEqual(plan.skipReason, "")
        self.assertIn("--files", plan.listArgument)
        self.assertFalse(plan.bBackground)

    def testWithoutProbeAnyMatchingGlobRuns(self) -> None:
        listScript = [script for script in discoverLintScripts("gate") if script.name == "CheckConfigReference"]
        listStaged = [kRepositoryRoot / "Source/Core/A.cpp"]
        self.assertEqual(selectGatesForStaged(kRepositoryRoot, listStaged, listStaged, listScript)[0].skipReason, "")


class PreCommitLintSummaryTest(unittest.TestCase):
    """기본 출력은 통과 · 건너뜀을 한 줄로 접고, 실패한 게이트의 상세만 그대로 보인다."""

    @staticmethod
    def makeGate(name: str, returnCode: int, text: str) -> LintScript:
        def run(cls, argv: list[str] | None = None) -> int:
            print(text)
            return returnCode
        # 파일 인자를 받는 게이트는 하위 프로세스가 아니라 이 프로세스에서 돈다(그래야 가짜 `run` 이 불린다).
        return makeScript(name, run=classmethod(run), preCommitFileArgument="--files")

    def runHook(self, listScript: list[LintScript], bVerbose: bool) -> tuple[bool, str]:
        listPlan = [PreCommitLint.GateRunPlan(script) for script in listScript]
        buffer = io.StringIO()
        with mock.patch.object(PreCommitLint, "discoverLintScripts", return_value=listScript), \
                mock.patch.object(PreCommitLint, "selectGatesForStaged", return_value=listPlan), \
                mock.patch.object(PreCommitLint, "StagedContentProbe", return_value=None), \
                contextlib.redirect_stdout(buffer):
            bFailed = PreCommitLint.runGatesInternal(kRepositoryRoot, [], [], bVerbose)
        return bFailed, buffer.getvalue()

    def testSummaryLineCountsEachStatus(self) -> None:
        line = formatGateSummary([GateOutcome("A", PreCommitLint.kGatePassed, 0.5), GateOutcome("B", PreCommitLint.kGateSkipped),
                                  GateOutcome("C", PreCommitLint.kGatePassed, 1.25), GateOutcome("D", PreCommitLint.kGateFailed, 0.1)])
        self.assertEqual(line, "[PreCommitLint] 게이트 4개: 통과 2 · 건너뜀 1 · 실패 1 (가장 긴 것 C 1.25 s)")

    def testSummaryWithOnlySkippedGatesHasNoSlowest(self) -> None:
        self.assertEqual(formatGateSummary([GateOutcome("B", PreCommitLint.kGateSkipped)]),
                         "[PreCommitLint] 게이트 1개: 통과 0 · 건너뜀 1 · 실패 0")

    def testPassingGatesPrintOnlyTheSummary(self) -> None:
        bFailed, output = self.runHook([self.makeGate("PassA", 0, "[PassA] OK (많은 줄)"), self.makeGate("PassB", 0, "[PassB] OK")], False)
        self.assertFalse(bFailed)
        self.assertNotIn("PassA", output.replace("가장 긴 것 PassA", "").replace("가장 긴 것 PassB", ""))
        self.assertEqual(len(output.strip().splitlines()), 1)

    def testFailingGateKeepsItsHeaderAndDetail(self) -> None:
        bFailed, output = self.runHook([self.makeGate("PassA", 0, "[PassA] OK"), self.makeGate("BadGate", 1, "  - Source/A.cpp:3 위반 상세")], False)
        self.assertTrue(bFailed)
        self.assertIn("[2/2] BadGate ... 실패", output)
        self.assertIn("  - Source/A.cpp:3 위반 상세", output)
        self.assertIn("실패 1", output)
        self.assertNotIn("[PassA] OK", output)

    def testSummaryCountsSkipAndUnavailableAsTheirOwnColumnsByReason(self) -> None:
        line = formatGateSummary([GateOutcome("A", PreCommitLint.kGatePassed, 0.5),
                                  GateOutcome("B", PreCommitLint.kGateSkipped, reason="해당 파일 변경 없음"),
                                  GateOutcome("C", PreCommitLint.kGateSkipped, reason="해당 파일 변경 없음 — 내용 규칙에 맞는 변경 없음"),
                                  GateOutcome("D", PreCommitLint.kGateSkipped, reason="병합 커밋 — 부모와 같다"),
                                  GateOutcome("E", PreCommitLint.kGateUnavailable, 0.1, "빌드 폴더 준비 안 됨 — 컴파일 DB 가 없습니다")])
        self.assertEqual(line, "[PreCommitLint] 게이트 5개: 통과 1 · 건너뜀 3(해당 파일 변경 없음 2 · 병합 커밋 1)"
                               " · 불성립(훅이라 허용) 1(빌드 폴더 준비 안 됨 1) · 실패 0 (가장 긴 것 A 0.50 s)")

    def testUnavailableGateIsAllowedInTheHookButNeverFolded(self) -> None:
        # 헤더 자립 게이트가 빌드 없는 PC 에서 낸 불성립 — 커밋을 막지 않되 통과 줄과 같아 보이면 안 된다.
        reasonLine = "[NoBuild] UNAVAILABLE (빌드 폴더 준비 안 됨 — 컴파일 DB 가 없습니다: build/Ninja-Debug)"
        bFailed, output = self.runHook([self.makeGate("PassA", 0, "[PassA] OK"), self.makeGate("NoBuild", ExitCode.PrerequisiteMissing, reasonLine)], False)
        self.assertFalse(bFailed)
        self.assertIn("[2/2] NoBuild ... 불성립(빌드 없는 PC 라 허용): 빌드 폴더 준비 안 됨 — 컴파일 DB 가 없습니다: build/Ninja-Debug", output)
        self.assertIn("불성립(훅이라 허용) 1(빌드 폴더 준비 안 됨 1)", output)
        self.assertIn("통과 1", output)

    def testSkippedGateIsCountedWithItsReasonAndDoesNotFail(self) -> None:
        bFailed, output = self.runHook([self.makeGate("Skip", ExitCode.Skipped, "[Skip] SKIPPED (병합 커밋 — 부모와 같다)")], False)
        self.assertFalse(bFailed)
        self.assertIn("통과 0 · 건너뜀 1(병합 커밋 1) · 실패 0", output)
        self.assertNotIn("[1/1] Skip", output)

    def testHookOnlySkipGateIsANormalSkipCountedByReasonCategory(self) -> None:
        # `preCommitSkipReason` 게이트(트리 전체 대조 — 통합이 대신 본다)는 정상 건너뜀이다: 불성립 칸이 아니라 건너뜀 칸에 사유 분류로 센다.
        script = makeScript("TreeWide", preCommitSkipReason="트리 전체 대조 — CI 린트 잡이 push 마다 본다")
        listPlan = selectGatesForStaged(kRepositoryRoot, [kRepositoryRoot / "Source/A.cpp"], [kRepositoryRoot / "Source/A.cpp"], [script])
        buffer = io.StringIO()
        with mock.patch.object(PreCommitLint, "discoverLintScripts", return_value=[script]), \
                mock.patch.object(PreCommitLint, "selectGatesForStaged", return_value=listPlan), \
                mock.patch.object(PreCommitLint, "StagedContentProbe", return_value=None), \
                contextlib.redirect_stdout(buffer):
            bFailed = PreCommitLint.runGatesInternal(kRepositoryRoot, [], [], False)
        self.assertFalse(bFailed)
        self.assertIn("건너뜀 1(트리 전체 대조 1)", buffer.getvalue())
        self.assertNotIn("불성립", buffer.getvalue())

    def testUnknownExitCodeIsAFailureNotAPass(self) -> None:
        bFailed, output = self.runHook([self.makeGate("Odd", 9, "이상한 종료")], False)
        self.assertTrue(bFailed)
        self.assertIn("실패 1", output)

    def testVerboseStillReadsTheReasonOfAnUnavailableGate(self) -> None:
        bFailed, output = self.runHook([self.makeGate("NoBuild", ExitCode.PrerequisiteMissing, "[NoBuild] UNAVAILABLE (도구 없음 — clang 이 없다)")], True)
        self.assertFalse(bFailed)
        self.assertIn("불성립(훅이라 허용) 1(도구 없음 1)", output)

    def testVerboseShowsEveryGateLine(self) -> None:
        # 이 프로세스에서 도는 게이트의 출력은 verbose 에서 붙잡히지 않고 그대로 흐른다 — 여기서는 redirect 한 버퍼로 들어온다.
        bFailed, output = self.runHook([self.makeGate("PassA", 0, "[PassA] OK (많은 줄)")], True)
        self.assertFalse(bFailed)
        self.assertIn("[1/1] PassA ...", output)
        self.assertIn("[PassA] OK (많은 줄)", output)


if __name__ == "__main__":
    unittest.main()
