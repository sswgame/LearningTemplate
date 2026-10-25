#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
qa/ScenarioRun(`scenario`) — 가짜 App 으로 인자 조립 · 종료 코드 풀이 · 모호한 이름 · 풀 수 없는 선언을 본다.

가짜 App 은 `runApp` 자리에 꽂은 함수다: 받은 인자를 적어 두고, 인자의 `-scenario-report=` 자리에 보고 JSON 을 쓰고, 정해 둔 종료 코드로 끝난다.
GPU 잠금은 실제 잠금 코드를 임시 폴더(`--lock-root`)에서 돌린다 — 끝난 뒤 잠금이 남지 않아야 한다.
"""

from __future__ import annotations

import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

kScriptsRoot = Path(__file__).resolve().parents[2] / "Scripts"
sys.path.insert(0, str(kScriptsRoot))

from common.AppRun import AppRunResult, kAppRhiUnusableExitCode  # noqa: E402
from common.ExitCode import ExitCode  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402
from common import ScenarioArguments  # noqa: E402
from common.ScenarioFile import findScenarioCandidates, readScenarioDeclaration  # noqa: E402
from common.ScenarioOutcome import kExitCodeHeaderRelPath, readOutcomeExitCodes  # noqa: E402
from qa import ScenarioRun  # noqa: E402

kMotionBlurXml = """<Scenario name="game.empty.motionblur" fixedDelta="0.0166667" timeoutFrames="600" startAfter="ScenePlaying" input="exclusive">
    <Launch variable="gv_firstScene" value="game/empty/maps/rendertest3d.scene.xml"/>
    <Launch variable="gv_deferred" value="1"/>
    <At frame="40"><Pass/></At>
</Scenario>
"""
kPlainXml = """<Scenario name="engine.runner" fixedDelta="0.0166667" timeoutFrames="10" startAfter="Immediately" input="exclusive">
    <At frame="1"><Pass/></At>
</Scenario>
"""
kEditorXml = """<Scenario name="editor.addcomponent" fixedDelta="0.0166667" timeoutFrames="10" startAfter="Immediately" input="exclusive">
    <At frame="1"><Pass/></At>
</Scenario>
"""
kLoadXml = """<Scenario name="load.bench" fixedDelta="0.0166667" startAfter="ScenePlaying" input="exclusive">
    <Load durationSeconds="20" meshes="8000" movers="1"/>
</Scenario>
"""
kBadLaunchXml = """<Scenario name="game.empty.badlaunch" fixedDelta="0.0166667" timeoutFrames="10" startAfter="Immediately" input="exclusive">
    <Launch variable="firstScene" value="x"/>
    <At frame="1"><Pass/></At>
</Scenario>
"""
kShippingCache = "SW_SHIPPING_BUILD:BOOL=ON\nSW_SHIPPING_RHI_BACKEND:STRING=DirectX12\nSW_ACTIVE_GAME:STRING=Empty\n"


def writeText(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def makeReport(exitCode: int, listFailure: list[str], listUnconfirmed: list[str], listUnhandled: list[str], reason: str) -> dict:
    """`AutomationReportWriter` 의 보고 모양."""
    return {"name": "x", "result": "R", "exitCode": exitCode, "frames": 10, "reason": reason, "failures": listFailure,
            "unconfirmed": listUnconfirmed, "unhandledLogs": listUnhandled, "allowedLogs": [], "metrics": [], "records": []}


class FakeApp:
    """`runApp` 대신 꽂는 가짜 App — 부름을 적고, 보고를 쓰고, 정한 종료 코드로 끝난다."""

    def __init__(self, mapExitCode: dict[str, int] | None = None, mapReport: dict[str, dict] | None = None, bTimedOut: bool = False) -> None:
        self.mapExitCode = mapExitCode or {}
        self.mapReport = mapReport or {}
        self.bTimedOut = bTimedOut
        self.outputFile: Path | None = None   # 이 판이 쓰는 산출물(있으면 매 부름마다 쓴다)
        self.listCall: list[tuple[Path, list[str], Path | None, float]] = []

    def __call__(self, appPath: Path, listArgument: list[str], *, cwd: Path | None = None, timeoutSeconds: float = 300.0, **_: object) -> AppRunResult:
        self.listCall.append((appPath, list(listArgument), cwd, timeoutSeconds))
        backend = next((argument[1:] for argument in listArgument if argument in ("-dx11", "-dx12", "-vk", "-gl")), "default")
        reportRel = next(argument.split("=", 1)[1] for argument in listArgument if argument.startswith("-scenario-report="))
        if backend in self.mapReport:
            assert cwd is not None
            writeText(cwd / reportRel, json.dumps(self.mapReport[backend]))
        if self.outputFile is not None:
            writeText(self.outputFile, backend)
        return AppRunResult(exitCode=self.mapExitCode.get(backend, 0), listLine=["[Info] fake app"], bLaunched=True, bTimedOut=self.bTimedOut, seconds=1.5)


class ScenarioRunCase(unittest.TestCase):
    """임시 저장소(Resource · Config/Game · build/Bin/App)를 만들고 `scenario` 를 가짜 App 으로 돌리는 틀."""

    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        writeText(self.root / "Resource/game/empty/automation/motionblur.scenario.xml", kMotionBlurXml)
        writeText(self.root / "Resource/game/empty/automation/kaykitrig.scenario.xml", kPlainXml)
        writeText(self.root / "Resource/game/empty/automation/kaykitcombat.scenario.xml", kPlainXml)
        writeText(self.root / "Resource/engine/automation/runner.scenario.xml", kPlainXml)
        writeText(self.root / "Resource/engine/automation/editor/addcomponent.scenario.xml", kEditorXml)
        writeText(self.root / "Resource/engine/automation/editor/savedstate.1.scenario.xml", kEditorXml)
        writeText(self.root / "Resource/engine/automation/load/bench.scenario.xml", kLoadXml)
        writeText(self.root / "Resource/game/empty/automation/badlaunch.scenario.xml", kBadLaunchXml)
        writeText(self.root / "Config/Game/Empty.json", '{ "_packRoot": "game/empty" }')
        self.appPath = self.root / "build/Bin/App.exe"
        writeText(self.appPath, "")
        self.lockRoot = self.root / "lock"

    def runMain(self, listArgument: list[str], fake: FakeApp) -> tuple[int, str, str]:
        """`scenario` 를 가짜 App 으로 돌려 (종료 코드, stdout, stderr)."""
        out, err = io.StringIO(), io.StringIO()
        with mock.patch.object(ScenarioRun, "runApp", fake), contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = ScenarioRun.main([*listArgument, "--app", str(self.appPath), "--lock-root", str(self.lockRoot), "--owner", "scenariotest"])
        return int(code), out.getvalue(), err.getvalue()

    def assertNoLockLeft(self) -> None:
        lockFolder = self.lockRoot / "build/.shared-locks"
        self.assertEqual([], [path.name for path in lockFolder.iterdir()] if lockFolder.is_dir() else [])


class ArgumentAssemblyTest(ScenarioRunCase):
    def testLaunchGameAndBackendArgumentsAreAllAttached(self) -> None:
        """실제로 빠뜨렸던 `-gv_firstScene` 이 시나리오의 `<Launch>` 에서 그대로 붙는다."""
        fake = FakeApp()
        code, _, _ = self.runMain(["motionblur"], fake)
        self.assertEqual(ExitCode.Passed, code)
        self.assertEqual(1, len(fake.listCall))
        appPath, listArgument, cwd, timeout = fake.listCall[0]
        self.assertEqual(["-unattended", "-scenario=game/empty/automation/motionblur.scenario.xml",
                          "-scenario-report=Saved/Automation/motionblur_dx12.json", "-dx12", "-game=Empty",
                          "-gv_firstScene=game/empty/maps/rendertest3d.scene.xml", "-gv_deferred=1"], listArgument)
        self.assertEqual(self.appPath, appPath)
        self.assertEqual(self.appPath.resolve().parent, cwd.resolve())
        self.assertEqual(ScenarioRun.kDefaultTimeoutSeconds, timeout)
        self.assertNoLockLeft()

    def testScenarioWithoutLaunchRunsWithoutGameVariables(self) -> None:
        fake = FakeApp()
        code, _, _ = self.runMain(["runner", "--backend", "vulkan"], fake)
        self.assertEqual(ExitCode.Passed, code)
        self.assertEqual(["-unattended", "-scenario=engine/automation/runner.scenario.xml", "-scenario-report=Saved/Automation/runner_vk.json", "-vk"],
                         fake.listCall[0][1])

    def testEditorScenarioGetsEditorAndStateFolder(self) -> None:
        fake = FakeApp()
        self.runMain(["addcomponent"], fake)
        listArgument = fake.listCall[0][1]
        self.assertIn("-EnableEditor", listArgument)
        stateArguments = [argument for argument in listArgument if argument.startswith("-gv_editorStateDir=")]
        self.assertEqual(1, len(stateArguments))
        self.assertTrue(stateArguments[0].endswith("addcomponent_dx12") or stateArguments[0].endswith("addcomponent_dx12/"), stateArguments[0])

    def testAllBackendsRunOnceEachAndShareOneTable(self) -> None:
        fake = FakeApp(mapExitCode={"vk": 15}, mapReport={"vk": makeReport(15, [], [], ["[Warning] [Caller] (id=Area.Event) 줄 그대로 | 막대 포함"], "unhandled")})
        code, out, _ = self.runMain(["motionblur", "--backend", "all"], fake)
        self.assertEqual(ExitCode.Violation, code)
        listSwitch = [next(argument for argument in call[1] if argument in ("-dx11", "-dx12", "-vk", "-gl")) for call in fake.listCall]
        self.assertEqual(["-dx11", "-dx12", "-vk", "-gl"], listSwitch)
        self.assertEqual(1, out.count("| 백엔드 | 종료 코드 | 결과 | 시간 |"))
        self.assertIn("[Warning] [Caller] (id=Area.Event) 줄 그대로 | 막대 포함", out)
        self.assertNoLockLeft()

    def testScreenshotDirGetsOnlyThisRunsFiles(self) -> None:
        """실행기는 산출물 폴더를 비우지 않으므로 지난 판 사진이 섞이지 않게 `scenario` 가 실행 전에 비운다."""
        outputFolder = self.appPath.parent / "Saved/Automation/game.empty.motionblur"
        writeText(outputFolder / "old.ppm", "stale")
        fake = FakeApp()
        fake.outputFile = outputFolder / "new.ppm"
        shotFolder = self.root / "shots"
        code, _, _ = self.runMain(["motionblur", "--screenshot-dir", str(shotFolder)], fake)
        self.assertEqual(ExitCode.Passed, code)
        self.assertEqual(["new.ppm"], sorted(path.name for path in (shotFolder / "dx12").iterdir()))

    def testDryRunPrintsCommandAndDoesNotRun(self) -> None:
        fake = FakeApp()
        code, out, _ = self.runMain(["motionblur", "--dry-run"], fake)
        self.assertEqual(ExitCode.Passed, code)
        self.assertEqual([], fake.listCall)
        self.assertIn("-gv_firstScene=game/empty/maps/rendertest3d.scene.xml", out)


class ExitCodeMeaningTest(ScenarioRunCase):
    def runWithExit(self, exitCode: int, report: dict | None, bTimedOut: bool = False) -> tuple[int, str]:
        fake = FakeApp({"dx12": exitCode}, {"dx12": report} if report is not None else {}, bTimedOut)
        code, out, _ = self.runMain(["runner"], fake)
        self.assertNoLockLeft()
        return code, out

    def testPassedIsOnlyExitCodeZero(self) -> None:
        code, out = self.runWithExit(0, makeReport(0, [], [], [], ""))
        self.assertEqual(ExitCode.Passed, code)
        self.assertIn("| dx12 | 0 | 통과 |", out)

    def testUnhandledLogsAreShownVerbatimWithExit15(self) -> None:
        line = "[Error] [Renderer] (id=Gfx.Missing) texture 'a|b' missing"
        code, out = self.runWithExit(15, makeReport(15, [], [], [line, "[Warning] [X] (no log id) 본문"], "unhandled logs"))
        self.assertEqual(ExitCode.Violation, code)
        self.assertIn("처리하지 않은 Warning/Error", out)
        self.assertIn(f"| dx12 | 처리하지 않은 로그 | {line} |", out)
        self.assertIn("| dx12 | 처리하지 않은 로그 | [Warning] [X] (no log id) 본문 |", out)

    def testUnconfirmedAndFailuresAreListedAndNotPassed(self) -> None:
        code, out = self.runWithExit(14, makeReport(14, [], ["ExpectImage provisional 0.06"], [], ""))
        self.assertEqual(ExitCode.Violation, code)
        self.assertIn("| dx12 | 미확정 | ExpectImage provisional 0.06 |", out)
        code, out = self.runWithExit(10, makeReport(10, ["frame 40: expected a got b"], [], [], "1 failure"))
        self.assertEqual(ExitCode.Violation, code)
        self.assertIn("| dx12 | 실패 | frame 40: expected a got b |", out)
        self.assertIn("| dx12 | 이유 | 1 failure |", out)

    def testLoadErrorTimeoutAndUnusableBackendAreSeparateMeanings(self) -> None:
        code, out = self.runWithExit(11, makeReport(11, [], [], [], "gv_firstScene differs"))
        self.assertEqual(ExitCode.Violation, code)
        self.assertIn("읽기 오류", out)
        code, out = self.runWithExit(kAppRhiUnusableExitCode, None)
        self.assertEqual(ExitCode.Violation, code)
        self.assertIn("이 기계에서 이 백엔드를 못 돎", out)
        self.assertIn("보고 없음", out)
        code, out = self.runWithExit(1, None, bTimedOut=True)
        self.assertEqual(ExitCode.Violation, code)
        self.assertIn("시한 180 초를 넘겨", out)

    def testOutcomeTableComesFromCppHeader(self) -> None:
        mapCode = readOutcomeExitCodes(getProjectRoot() / kExitCodeHeaderRelPath)
        self.assertEqual({"Passed": 0, "Failed": 10, "LoadError": 11, "TimedOut": 12, "Skipped": 13, "Unconfirmed": 14, "UnhandledLog": 15},
                         {name: mapCode[name] for name in ("Passed", "Failed", "LoadError", "TimedOut", "Skipped", "Unconfirmed", "UnhandledLog")})


class NameAndDeclarationTest(ScenarioRunCase):
    def assertCannotRun(self, listArgument: list[str], expectedText: str) -> None:
        fake = FakeApp()
        code, _, err = self.runMain(listArgument, fake)
        self.assertEqual(ExitCode.CheckInvalid, code, err)
        self.assertIn(expectedText, err)
        self.assertEqual([], fake.listCall, "풀 수 없는 시나리오는 App 을 띄우지 않는다")
        self.assertNoLockLeft()

    def testAmbiguousNameShowsCandidatesAndStops(self) -> None:
        self.assertCannotRun(["kaykit"], "모호하다")
        fake = FakeApp()
        _, _, err = self.runMain(["kaykit"], fake)
        self.assertIn("game/empty/automation/kaykitrig.scenario.xml", err)
        self.assertIn("game/empty/automation/kaykitcombat.scenario.xml", err)

    def testUnknownNameStops(self) -> None:
        self.assertCannotRun(["nosuchscenario"], "맞는 시나리오가 없다")

    def testExactStemBeatsPartialMatch(self) -> None:
        writeText(self.root / "Resource/game/empty/automation/motionblurplus.scenario.xml", kPlainXml)
        listCandidate = findScenarioCandidates(self.root / "Resource", "motionblur")
        self.assertEqual(["game/empty/automation/motionblur.scenario.xml"], listCandidate)
        self.assertEqual(2, len(findScenarioCandidates(self.root / "Resource", "motionblu")))

    def testScenarioNameAttributeAndResourcePathAreAccepted(self) -> None:
        self.assertEqual(["game/empty/automation/motionblur.scenario.xml"], findScenarioCandidates(self.root / "Resource", "game.empty.motionblur"))
        self.assertEqual(["game/empty/automation/motionblur.scenario.xml"],
                         findScenarioCandidates(self.root / "Resource", "game/empty/automation/motionblur.scenario.xml"))

    def testLoadScenarioPointsToLoadCommand(self) -> None:
        self.assertCannotRun(["bench"], "Scripts load")

    def testNumberedGroupMemberCannotRunAlone(self) -> None:
        self.assertCannotRun(["savedstate.1"], "저장 상태를 이어 읽는다")

    def testMalformedLaunchStopsWithReason(self) -> None:
        self.assertCannotRun(["badlaunch"], "gv_")

    def testDeclarationReadsPackAndStem(self) -> None:
        declaration = readScenarioDeclaration(self.root / "Resource", "game/empty/automation/motionblur.scenario.xml")
        self.assertEqual("game/empty", declaration.packFolder)
        self.assertEqual("motionblur", declaration.stem)
        self.assertEqual(("gv_firstScene", "game/empty/maps/rendertest3d.scene.xml"), declaration.listLaunch[0])

    def testGamePresetMissingStopsWithReason(self) -> None:
        (self.root / "Config/Game/Empty.json").unlink()
        self.assertCannotRun(["motionblur"], "를 팩으로 가진 게임이")


class AppArgumentTest(ScenarioRunCase):
    def testAppArgumentsAreAppendedLastInOrder(self) -> None:
        fake = FakeApp()
        code, _, _ = self.runMain(["runner", "--app-arg=-gv_deferred=1", "--app-arg=-EnableEditor"], fake)
        self.assertEqual(ExitCode.Passed, code)
        self.assertEqual(["-gv_deferred=1", "-EnableEditor"], fake.listCall[0][1][-2:])

    def testAppArgumentOverlappingAssembledOnesStops(self) -> None:
        """조립이 소유한 인자(`<Launch>` 변수 · 게임 · 백엔드 · 시나리오 · 에디터 시나리오의 에디터 인자)는 조용히 덮어쓰지 않고 오류다."""
        for listArgument, expected in ((["motionblur", "--app-arg=-gv_deferred=0"], "gv_deferred"), (["motionblur", "--app-arg=-game=Other"], "game"),
                                       (["runner", "--app-arg=-vk"], "vk"), (["runner", "--app-arg=-scenario=x"], "scenario"),
                                       (["runner", "--app-arg=-unattended"], "unattended"), (["addcomponent", "--app-arg=-EnableEditor"], "EnableEditor"),
                                       (["runner", "--app-arg=notdashed"], "'-' 로 시작")):
            fake = FakeApp()
            code, _, err = self.runMain(listArgument, fake)
            self.assertEqual(ExitCode.CheckInvalid, code, listArgument)
            self.assertIn(expected, err)
            self.assertEqual([], fake.listCall)


class ArgumentVocabularyTest(unittest.TestCase):
    def testVocabularyIsReadFromTheCppHeader(self) -> None:
        """정본(`ArgumentList.xxx` 의 철자 · `AutomationScenario.h` 의 상수)이 맞는 글을 준다 — 파이썬이 그것을 읽어 온다."""
        words = ScenarioArguments.kVocabulary
        self.assertEqual(("-unattended", "-scenario=", "-scenario-report=", "-EnableEditor", "-gv_editorStateDir=", "-game=", "Saved/Automation", "EditorState"),
                         (words.unattendedArgument, words.scenarioArgumentPrefix, words.scenarioReportArgumentPrefix, words.editorArgument,
                          words.editorStateDirArgumentPrefix, words.gameArgumentPrefix, words.automationFolder, words.editorStateSubfolder))

    def testMissingConstantStopsLoudly(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            writeText(root / ScenarioArguments.kAutomationHeaderRelPath, 'inline constexpr const utf8* kAutomationOutputFolder = "Saved/Automation";')
            writeText(root / ScenarioArguments.kArgumentListRelPath, 'SW_REGISTER_ARGUMENT( HELP, false, false, "help" )\n')
            with self.assertRaises(SystemExit):
                ScenarioArguments.readScenarioArgumentVocabulary(root)

    def testCppAssemblyUsesTheCanonicalSourcesNotLiterals(self) -> None:
        """C++ 조립 자리가 인자 글 · 산출물 폴더를 다시 적지 않는다 — 다시 적으면 정본이 둘이 된다(인자는 `getCommandLineArgumentSpelling`, 폴더는 `kAutomationOutputFolder`)."""
        for relPath in ("Test/AppTest/AppTestUtil.cpp", "Test/AppTest/TestAppScenario.cpp", "Test/AppTest/TestAppSmoke.cpp"):
            text = (getProjectRoot() / relPath).read_text(encoding="utf-8").replace('"-game=HarvestValley', "")
            for literal in ('"-unattended', '"-scenario=', '"-scenario-report=', '"-EnableEditor', '"-gv_editorStateDir='):
                self.assertNotIn(literal, text, f"{relPath}: {literal}")
        listEditorFile = ("Source/Editor/Common/Commands/Dev/EditorDevCommands.cpp", "Source/Editor/Common/Commands/Play/EditorScenarioRecordCommands.cpp",
                          "Source/Editor/SelfTest/TestRunnerPanel.cpp")
        for relPath in ("Source/Engine/Automation/AutomationRunner.cpp", "Source/Engine/Automation/ScenarioRecorder.cpp", "Test/AppTest/AppTestUtil.cpp", *listEditorFile):
            self.assertNotIn('"Saved/Automation', (getProjectRoot() / relPath).read_text(encoding="utf-8"), relPath)
        # 에디터가 App 을 띄우는 자리(`TestRunnerPanel`)는 앞에 공백이 든 조각(`" -scenario=`)으로 적었으므로 주석이 아닌 줄에서 인자 글 자체를 찾는다.
        listCodeLine = [line for line in (getProjectRoot() / listEditorFile[2]).read_text(encoding="utf-8").splitlines() if not line.lstrip().startswith(("//", "*", "/*"))]
        for literal in ("-unattended", "-scenario=", "-scenario-report=", "-EnableEditor", "-gv_editorStateDir="):
            self.assertEqual([], [line for line in listCodeLine if f'"{literal}' in line or f'" {literal}' in line], literal)   # 인자 조각으로 시작하는 글(설명 글은 제외)

    def testScenarioStemFollowsTheEngineRule(self) -> None:
        """엔진 `getAutomationScenarioStem`(마지막 `/` 뒤 파일 이름의 첫 `.` 앞)과 같은 값을 준다."""
        for resourceId, stem in (("game/empty/automation/fog.scenario.xml", "fog"), ("engine/automation/editor/savedstate.1.scenario.xml", "savedstate"),
                                 ("runner.scenario.xml", "runner")):
            self.assertEqual(stem, ScenarioRun.ScenarioDeclaration(resourceId, Path(resourceId), "", (), False, False, False).stem)


class ShippingBuildTest(ScenarioRunCase):
    def testShippingTakesNoSwitchNoGameAndRefusesDevelopmentVariables(self) -> None:
        writeText(self.root / "build/CMakeCache.txt", kShippingCache)
        fake = FakeApp()
        code, _, err = self.runMain(["motionblur"], fake)
        self.assertEqual(ExitCode.CheckInvalid, code)
        self.assertIn("배포(Shipping) 빌드는 개발 변수", err)
        code, _, _ = self.runMain(["kaykitrig"], fake)
        self.assertEqual(ExitCode.Passed, code)
        self.assertEqual(["-unattended", "-scenario=game/empty/automation/kaykitrig.scenario.xml", "-scenario-report=Saved/Automation/kaykitrig_dx12.json"],
                         fake.listCall[0][1])
        code, _, err = self.runMain(["kaykitrig", "--backend", "gl"], fake)
        self.assertEqual(ExitCode.CheckInvalid, code)
        self.assertIn("하나만 링크했다", err)


if __name__ == "__main__":
    unittest.main()
