#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""검증 묶음 매니페스트(common.VerifyManifest)와 실행기(dev/RunVerifyBatch)의 판단 — 임시 폴더의 작은 저장소로, 실제 저장소 매니페스트의 일관성까지."""

from __future__ import annotations

import contextlib
import copy
import json
import os
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path
from typing import Iterator
from unittest import mock

kScriptsRoot = Path(__file__).resolve().parents[2] / "Scripts"
sys.path.insert(0, str(kScriptsRoot))

from common.ConflictSchedule import runConflictSchedule  # noqa: E402
from common import Timeline  # noqa: E402
from common.AppBinary import runHeadlessTask  # noqa: E402
from common.AppRun import runApp  # noqa: E402
from common.QualityPreset import SettingsIsolation  # noqa: E402
from common.ExitCode import ExitCode  # noqa: E402
from common.Timeline import kTimelineEnvName, kTimelineItemEnvName, summarizeAppArguments  # noqa: E402
from common.VerifyManifest import (ItemResult, ShardPlan, VerifyItem, VerifyManifest, VerifyManifestError, buildItemCommand, checkVerifyPending, combineShardResults,  # noqa: E402
                                   extractMetricValues, findManifestProblems, findPendingLineViolations, findReferencedIds, formatResultTable, getStageIndex,
                                   groupByGpuMode, interpretOutcome, judgeMetrics, kStatusBlocked, kStatusFailed, kStatusPassed, kStatusSkipped,
                                   makeNotRunResult, makeResult, makeShardArguments, orderItems, parseManifest, planShards, readGpuSlots, readManifest,
                                   resolveCoveredResult, summarizeBenchRows)
from common.Paths import getProjectRoot  # noqa: E402
from dev import RunVerifyBatch  # noqa: E402

kBaseItem = {"title": "t", "platform": "windows", "pending": False, "source": "docs/plans/A.md 절", "result_doc": "docs/plans/A.md 절"}


def makeItem(itemId: str, kind: str, config: str, gpu: str, **extra: object) -> dict:
    item = {**kBaseItem, "id": itemId, "kind": kind, "config": config, "gpu": gpu, "timeout_minutes": 10}
    item.update(extra)
    return item


def makeData(*listItem: dict) -> dict:
    return {"configs": [{"name": "Debug", "preset": "Ninja-Debug"}, {"name": "Release", "preset": "Ninja-Release"}], "items": list(listItem)}


kGoodItems = (
    makeItem("full-build", "build", "Debug", "none"),
    makeItem("ctest-nogpu", "ctest", "Debug", "none", label="nogpu"),
    makeItem("some-test", "test", "Debug", "none", filter="SomeTest.*", extra_args=["--host_suites=all"]),
    makeItem("scenario-run", "test", "Release", "shared", filter="AppScenarioTest.*"),
    makeItem("a-scenario", "covered", "Release", "shared", covered=["x.scenario.xml"], covered_by="scenario-run"),
    makeItem("perf-frame", "measure", "Release", "exclusive", tool="scripts", args=["perf", "--app", "{app}"], limit="기준 +15 %", judge="exit"),
    makeItem("later-bench", "measure", "Release", "exclusive", tool="scripts", args=["test", "B.*", "--preset", "{preset}"], limit="보고만", judge="exit"),
    makeItem("cook-shaders", "cook", "Release", "none", tool="app", args=["--cook-shaders"]),
    makeItem("not-yet", "measure", "Release", "exclusive", need_command="케이스가 아직 없다"),
    makeItem("by-hand", "test", "Release", "shared", manual="실제 장치를 쥔 사람이 감각으로 판정한다"),
)


class ParseManifestTest(unittest.TestCase):
    def testGoodManifestIsRead(self) -> None:
        manifest = parseManifest(makeData(*copy.deepcopy(kGoodItems)))
        self.assertEqual(len(kGoodItems), len(manifest.listItem))
        self.assertTrue(manifest.findItem("full-build").bRunnable)
        self.assertTrue(manifest.findItem("a-scenario").bCovered)
        self.assertFalse(manifest.findItem("not-yet").bRunnable)
        self.assertFalse(manifest.findItem("by-hand").bRunnable)
        self.assertTrue(manifest.findItem("by-hand").bUnrunnable)

    def testMissingRequiredKeyStops(self) -> None:
        item = makeItem("ctest-nogpu", "ctest", "Debug", "none")   # label 없음
        with self.assertRaisesRegex(VerifyManifestError, "label"):
            parseManifest(makeData(item))
        item = makeItem("x", "build", "Debug", "none")
        del item["source"]
        with self.assertRaisesRegex(VerifyManifestError, "source"):
            parseManifest(makeData(item))

    def testNeedCommandExcusesKindKeysButNotCommonKeys(self) -> None:
        parseManifest(makeData(makeItem("later", "measure", "Release", "exclusive", need_command="이유", timeout_minutes=0)))
        with self.assertRaisesRegex(VerifyManifestError, "need_command"):
            parseManifest(makeData(makeItem("later", "measure", "Release", "exclusive", need_command="  ")))

    def testManualExcusesKindKeysAndExcludesNeedCommand(self) -> None:
        parseManifest(makeData(makeItem("hand", "measure", "Release", "exclusive", manual="사람 귀가 필요하다", timeout_minutes=0)))
        with self.assertRaisesRegex(VerifyManifestError, "manual"):
            parseManifest(makeData(makeItem("hand", "measure", "Release", "exclusive", manual="  ")))
        with self.assertRaisesRegex(VerifyManifestError, "함께 쓰지 않는다"):
            parseManifest(makeData(makeItem("hand", "measure", "Release", "exclusive", manual="귀", need_command="명령")))

    def testPendingItemWithoutCommandMustBeManual(self) -> None:
        with self.assertRaisesRegex(VerifyManifestError, "일회성"):
            parseManifest(makeData(makeItem("later", "measure", "Release", "exclusive", need_command="이유", pending=True)))
        parseManifest(makeData(makeItem("hand", "measure", "Release", "exclusive", manual="사람 눈", pending=True)))   # 사람이 해야 하는 일회성은 된다
        parseManifest(makeData(makeItem("later", "measure", "Release", "exclusive", need_command="이유", pending=False)))

    def testUnknownKeyDuplicateIdAndBadValuesStop(self) -> None:
        with self.assertRaisesRegex(VerifyManifestError, "모르는 칸"):
            parseManifest(makeData(makeItem("x", "build", "Debug", "none", lable="typo")))
        with self.assertRaisesRegex(VerifyManifestError, "두 번"):
            parseManifest(makeData(makeItem("x", "build", "Debug", "none"), makeItem("x", "build", "Debug", "none")))
        with self.assertRaisesRegex(VerifyManifestError, "gpu"):
            parseManifest(makeData(makeItem("x", "build", "Debug", "both")))
        with self.assertRaisesRegex(VerifyManifestError, "구성"):
            parseManifest(makeData(makeItem("x", "build", "Nightly", "none")))
        with self.assertRaisesRegex(VerifyManifestError, "timeout_minutes"):
            parseManifest(makeData(makeItem("x", "build", "Debug", "none", timeout_minutes=0)))
        with self.assertRaisesRegex(VerifyManifestError, "id"):
            parseManifest(makeData(makeItem("Bad_Id", "build", "Debug", "none")))

    def testCoveredNeedsMatchingCoveringItem(self) -> None:
        cover = makeItem("scenario-run", "test", "Release", "shared", filter="AppScenarioTest.*")
        with self.assertRaisesRegex(VerifyManifestError, "covered_by"):
            parseManifest(makeData(makeItem("a", "covered", "Release", "shared", covered=["x.scenario.xml"], covered_by="nothing"), cover))
        with self.assertRaisesRegex(VerifyManifestError, "gpu"):
            parseManifest(makeData(makeItem("a", "covered", "Release", "none", covered=["x.scenario.xml"], covered_by="scenario-run"), cover))

    def testPlaceholderAndMetricRulesStop(self) -> None:
        with self.assertRaisesRegex(VerifyManifestError, "자리 표시"):
            parseManifest(makeData(makeItem("m", "measure", "Release", "exclusive", tool="scripts", args=["perf", "{nope}"], limit="l", judge="exit")))
        metric = {"name": "p50", "pattern": r"p50 (\d+)", "unit": "us"}
        with self.assertRaisesRegex(VerifyManifestError, "max"):
            parseManifest(makeData(makeItem("m", "measure", "Release", "exclusive", tool="scripts", args=["perf"], limit="l", judge="metrics", metrics=[metric])))
        with self.assertRaisesRegex(VerifyManifestError, "묶음"):
            parseManifest(makeData(makeItem("m", "measure", "Release", "exclusive", tool="scripts", args=["perf"], limit="l", judge="exit",
                                            metrics=[{"name": "p50", "pattern": "p50", "unit": "us", "max": 1}])))


class OrderingTest(unittest.TestCase):
    def setUp(self) -> None:
        self.manifest = parseManifest(makeData(*copy.deepcopy(kGoodItems)))

    def testConfigsRunInManifestOrderAndKindsInKindOrder(self) -> None:
        listGroup = orderItems(self.manifest, list(self.manifest.listItem))
        self.assertEqual(["Debug", "Release"], [name for name, _ in listGroup])
        self.assertEqual(["full-build", "ctest-nogpu", "some-test"], [item.itemId for item in listGroup[0][1]])
        # 쿠킹(셰이더 바이너리를 쓰는 실행보다 앞) → 테스트 → 덮개 → 측정(독점은 맨 끝, 파일 순서)
        self.assertEqual(["cook-shaders", "scenario-run", "by-hand", "a-scenario", "perf-frame", "later-bench", "not-yet"], [item.itemId for item in listGroup[1][1]])

    def testGpuGroupsAreConsecutiveRunsOfTheSameMode(self) -> None:
        listItem = orderItems(self.manifest, list(self.manifest.listItem))[1][1]
        self.assertEqual([("none", ["cook-shaders"]), ("shared", ["scenario-run", "by-hand", "a-scenario"]), ("exclusive", ["perf-frame", "later-bench", "not-yet"])],
                         [(mode, [item.itemId for item in group]) for mode, group in groupByGpuMode(listItem)])

    def testSelectItemsByIdAndConfig(self) -> None:
        listItem = RunVerifyBatch.selectItems(self.manifest, ["perf-frame"], [])
        self.assertEqual(["perf-frame"], [item.itemId for item in listItem])
        self.assertEqual(3, len(RunVerifyBatch.selectItems(self.manifest, [], ["Debug"])))
        with self.assertRaises(VerifyManifestError):
            RunVerifyBatch.selectItems(self.manifest, ["nope"], [])
        with self.assertRaises(VerifyManifestError):
            RunVerifyBatch.selectItems(self.manifest, [], ["Nightly"])


class CommandTest(unittest.TestCase):
    def setUp(self) -> None:
        self.manifest = parseManifest(makeData(*copy.deepcopy(kGoodItems)))
        self.root = Path(tempfile.mkdtemp()).resolve()

    def testBuildCtestAndTestCommands(self) -> None:
        build = buildItemCommand(self.manifest.findItem("full-build"), "Ninja-Debug", self.root, "integD", self.root)
        self.assertEqual(["full-build", "--owner", "integD", "--preset", "Ninja-Debug"], build[3:])
        ctest = buildItemCommand(self.manifest.findItem("ctest-nogpu"), "Ninja-Debug", self.root, "integD", self.root)
        self.assertEqual(["ctest", "--test-dir"], ctest[:2])
        self.assertIn("nogpu", ctest)
        test = buildItemCommand(self.manifest.findItem("some-test"), "Ninja-Debug", self.root, "integD", self.root)
        self.assertEqual(["test", "SomeTest.*", "--preset", "Ninja-Debug", "--no-build", "--host_suites=all"], test[3:])

    def testPlaceholdersExpandAndMissingAppStops(self) -> None:
        item = self.manifest.findItem("later-bench")
        self.assertEqual(["test", "B.*", "--preset", "Ninja-Release"], buildItemCommand(item, "Ninja-Release", self.root, "o", self.root)[3:])
        with self.assertRaisesRegex(VerifyManifestError, "App 이 없습니다"):
            buildItemCommand(self.manifest.findItem("perf-frame"), "Ninja-Release", self.root, "o", self.root)
        binDir = self.root / "build" / "Ninja-Release" / "Bin"
        binDir.mkdir(parents=True)
        (binDir / "App.exe").write_text("", encoding="utf-8")
        command = buildItemCommand(self.manifest.findItem("perf-frame"), "Ninja-Release", self.root, "o", self.root)
        self.assertEqual(str(binDir / "App.exe"), command[command.index("--app") + 1])
        appCommand = buildItemCommand(self.manifest.findItem("cook-shaders"), "Ninja-Release", self.root, "o", self.root)
        self.assertEqual([str(binDir / "App.exe"), "--cook-shaders"], appCommand)


class OutcomeTest(unittest.TestCase):
    def setUp(self) -> None:
        metric = {"name": "p50", "pattern": r"p50 (\d+) us", "unit": "us", "max": 100}
        data = makeData(makeItem("judged", "measure", "Release", "exclusive", tool="scripts", args=["perf"], limit="p50 ≤ 100 us", judge="metrics", metrics=[metric]),
                        makeItem("plain", "measure", "Release", "exclusive", tool="scripts", args=["perf"], limit="보고만", judge="exit"))
        self.manifest = parseManifest(data)
        self.judged = self.manifest.findItem("judged")
        self.plain = self.manifest.findItem("plain")

    def interpret(self, item: VerifyItem, returnCode: int = 0, bLaunched: bool = True, bTimedOut: bool = False, text: str = "") -> ItemResult:
        return interpretOutcome(item, returnCode, bLaunched, bTimedOut, text, 1.0, "log", "2026-10-24 10:00")

    def testExitCodeAndSpecialEndings(self) -> None:
        self.assertEqual(kStatusPassed, self.interpret(self.plain).status)
        self.assertEqual(kStatusFailed, self.interpret(self.plain, returnCode=1).status)
        self.assertEqual(kStatusFailed, self.interpret(self.plain, bLaunched=False).status)
        self.assertEqual(kStatusFailed, self.interpret(self.plain, bTimedOut=True).status)
        self.assertEqual(kStatusSkipped, self.interpret(self.plain, returnCode=int(ExitCode.Skipped)).status)

    def testMetricLimitsDecidePassOrFail(self) -> None:
        self.assertEqual(kStatusPassed, self.interpret(self.judged, text="p50 90 us\np50 80 us").status)
        failed = self.interpret(self.judged, text="p50 150 us")
        self.assertEqual((kStatusFailed, "p50 150 us"), (failed.status, failed.valueText))
        self.assertIn("한계", failed.detail)
        self.assertEqual(kStatusFailed, self.interpret(self.judged, text="값이 없는 출력").status)

    def testMetricHelpers(self) -> None:
        mapValue = extractMetricValues(self.judged.listMetric, "p50 7 us\np50 9 us")
        self.assertEqual({"p50": 9.0}, mapValue)
        self.assertEqual([], judgeMetrics(self.judged.listMetric, mapValue))
        self.assertEqual("A p50 2 us", summarizeBenchRows("[Bench] A  min 1 us  p50 2 us  p90 3 us  p99 4 us  max 5 us", 3))
        self.assertEqual("", summarizeBenchRows("출력에 벤치 줄이 없다", 3))

    def testCoveredFollowsCoveringItemAndNotRunItemsExplainWhy(self) -> None:
        manifest = parseManifest(makeData(*copy.deepcopy(kGoodItems)))
        covered = manifest.findItem("a-scenario")
        passed = makeResult("scenario-run", kStatusPassed, "", "d")
        self.assertEqual(kStatusPassed, resolveCoveredResult(covered, {"scenario-run": passed}, "d").status)
        failed = makeResult("scenario-run", kStatusFailed, "x", "d")
        self.assertEqual(kStatusFailed, resolveCoveredResult(covered, {"scenario-run": failed}, "d").status)
        blocked = makeResult("scenario-run", kStatusBlocked, "x", "d")
        self.assertEqual(kStatusBlocked, resolveCoveredResult(covered, {"scenario-run": blocked}, "d").status)
        self.assertEqual(kStatusSkipped, resolveCoveredResult(covered, {}, "d").status)
        passed.startedAt = "2026-10-26T00:50:13"
        self.assertEqual("2026-10-26T00:50:13", resolveCoveredResult(covered, {"scenario-run": passed}, "d").startedAt)   # 덮개 항목은 덮개 대상의 시작 시각을 잇는다
        self.assertEqual("", resolveCoveredResult(covered, {"scenario-run": blocked}, "d").startedAt)
        notRun = makeNotRunResult(manifest.findItem("not-yet"), "d")
        self.assertEqual((kStatusSkipped, "명령 없음: 케이스가 아직 없다"), (notRun.status, notRun.detail))
        byHand = makeNotRunResult(manifest.findItem("by-hand"), "d")
        self.assertEqual((kStatusSkipped, "사람 확인: 실제 장치를 쥔 사람이 감각으로 판정한다"), (byHand.status, byHand.detail))

    def testResultTableHasOneRowPerResultWithEnvironment(self) -> None:
        listResult = [makeResult("judged", kStatusPassed, "", "2026-10-24 10:00", valueText="p50 90 us")]
        table = formatResultTable(self.manifest, listResult, "PC X")
        listRow = table.splitlines()
        self.assertEqual(3, len(listRow))
        self.assertIn("PC X · Release · gpu exclusive", listRow[2])
        self.assertIn("p50 ≤ 100 us", listRow[2])
        self.assertIn("| —(안 돎) | 2026-10-24 10:00 |", listRow[2])   # 시작 열: 시도한 시각이 없는 항목은 `-` 가 아니라 "안 돎"
        listResult[0].startedAt = "2026-10-24T00:50:13"
        self.assertIn("| 00:50:13 | 2026-10-24 10:00 |", formatResultTable(self.manifest, listResult, "PC X").splitlines()[2])


class TimelineTest(unittest.TestCase):
    def testSummaryKeepsOnlyGameScenarioBackendAndGvArguments(self) -> None:
        listArgument = ["-game=Foo", "-dx12", "-scenario=a.xml", "-gv_benchMeshes=8000", "-vsync=0", "--cooked-dir=x"]
        self.assertEqual("-game=Foo -dx12 -scenario=a.xml -gv_benchMeshes=8000", summarizeAppArguments(listArgument, frozenset({"-dx12"})))

    def testRunAppAppendsStartAndEndLinesToEnvFile(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "timeline.log"
            with mock.patch.dict("os.environ", {kTimelineEnvName: str(path), kTimelineItemEnvName: "perf-x"}):
                runApp(Path(sys.executable), ["-c", "pass"], settings=SettingsIsolation.NOT_APPLICABLE)
            listLine = path.read_text(encoding="utf-8").splitlines()
            self.assertEqual(2, len(listLine))
            self.assertRegex(listLine[0], r"^\d\d:\d\d:\d\d 앱시작 perf-x pid=\d+ ")
            self.assertRegex(listLine[1], r"^\d\d:\d\d:\d\d 앱끝 perf-x pid=\d+ 종료 0 ")

    def testStandaloneRunWritesTimelineUnderBuildFolderWithItemDash(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            env = {key: value for key, value in os.environ.items() if key not in (kTimelineEnvName, kTimelineItemEnvName)}
            with mock.patch.dict("os.environ", env, clear=True), mock.patch.object(Timeline, "getProjectRoot", return_value=Path(folder)), \
                    mock.patch.object(Timeline, "_standalonePath", None):
                runApp(Path(sys.executable), ["-c", "pass"], settings=SettingsIsolation.NOT_APPLICABLE)
                runApp(Path(sys.executable), ["-c", "pass"], settings=SettingsIsolation.NOT_APPLICABLE)   # 같은 프로세스의 실행은 같은 파일에 쌓인다
            listFile = list((Path(folder) / Timeline.kStandaloneRelDir).glob("*.log"))
            self.assertEqual(1, len(listFile))
            listLine = listFile[0].read_text(encoding="utf-8").splitlines()
            self.assertEqual(4, len(listLine))
            self.assertRegex(listLine[0], r"^\d\d:\d\d:\d\d 앱시작 - pid=\d+ ")

    def testRunAppClosesStdoutPipe(self) -> None:
        listStdout: list[object] = []
        realPopen = subprocess.Popen

        def spy(*args: object, **kwargs: object) -> subprocess.Popen:
            process = realPopen(*args, **kwargs)
            listStdout.append(process.stdout)
            return process

        with tempfile.TemporaryDirectory() as folder, mock.patch.object(subprocess, "Popen", spy), \
                mock.patch.dict("os.environ", {kTimelineEnvName: str(Path(folder) / "t.log")}):
            runApp(Path(sys.executable), ["-c", "pass"], settings=SettingsIsolation.NOT_APPLICABLE)
        self.assertTrue(listStdout[0].closed)   # 안 닫으면 가비지 수집 때 ResourceWarning 이 난다

    def testHeadlessTaskLeavesTimelineLinesAndKeepsCapture(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "timeline.log"
            with mock.patch.dict("os.environ", {kTimelineEnvName: str(path)}):
                os.environ.pop(kTimelineItemEnvName, None)   # verify-batch 항목 안에서 돌면 바깥 항목 id 가 물려 온다 — 항목 없음(`-`)을 본다
                completed = runHeadlessTask(Path(sys.executable), ["-c", "import sys; print('out'); print('err', file=sys.stderr)"], bCapture=True)
            self.assertEqual((0, "out", "err"), (completed.returncode, completed.stdout.strip(), completed.stderr.strip()))
            listLine = path.read_text(encoding="utf-8").splitlines()
            self.assertRegex(listLine[0], r"^\d\d:\d\d:\d\d 앱시작 - pid=\d+ ")
            self.assertRegex(listLine[1], r"^\d\d:\d\d:\d\d 앱끝 - pid=\d+ 종료 0 ")

    def testRunItemWritesStartEndLinesAndStartedAt(self) -> None:
        item = parseManifest(makeData(makeItem("some-test", "test", "Debug", "none", filter="X.*"))).listItem[0]
        with tempfile.TemporaryDirectory() as folder:
            outputDir = Path(folder)
            with mock.patch.object(RunVerifyBatch, "buildItemCommand", return_value=[sys.executable, "-c", "pass"]):
                result = RunVerifyBatch.runItemInternal(item, "Ninja-Debug", outputDir, "me", outputDir)
            listLine = (outputDir / "timeline.log").read_text(encoding="utf-8").splitlines()
            self.assertRegex(listLine[0], r"^\d\d:\d\d:\d\d 시작 some-test Debug$")
            self.assertRegex(listLine[1], r"^\d\d:\d\d:\d\d 끝 some-test Debug 통과 \d+초$")
            self.assertRegex(result.startedAt, r"^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d$")


class PendingLineTest(unittest.TestCase):
    kPlanHeader = "# 계획\n\n> 상태: 측정대기 · 선행: 없음 · 영역: engine · 옛번호: 없음 · 진행: 해당없음\n\n"

    def makeRoot(self, docText: str, pendingItem: bool = False) -> Path:
        root = Path(tempfile.mkdtemp())
        (root / "docs" / "plans").mkdir(parents=True)
        (root / "docs" / "plans" / "A.md").write_text(self.kPlanHeader + docText, encoding="utf-8")
        (root / "Test" / "QA").mkdir(parents=True)
        (root / "Test" / "TestSome.cpp").write_text("SW_TEST_CASE( SomeTest, Works )\n", encoding="utf-8")
        item = makeItem("some-test", "test", "Debug", "none", filter="SomeTest.*", pending=pendingItem)
        (root / "Test" / "QA" / "VerifyBatch.json").write_text(json.dumps(makeData(item)), encoding="utf-8")
        return root

    def testReferencedIdsAreReadFromTheList(self) -> None:
        self.assertEqual(["a", "b-c"], findReferencedIds("실행 대기: 매니페스트 id `a`, `b-c`. `other` 는 아니다"))
        self.assertEqual(["a", "b"], findReferencedIds("매니페스트 id `a` · `b`"))
        self.assertEqual([], findReferencedIds("`a` 만 있다"))

    def testLineWithoutIdOrWithUnknownIdIsAViolation(self) -> None:
        listViolation = checkVerifyPending(self.makeRoot("실행 대기: 통합에서 돌린다.\n"))
        self.assertEqual(1, len(listViolation))
        self.assertIn("A.md:5", listViolation[0])
        self.assertEqual(1, len(checkVerifyPending(self.makeRoot("**남은 확인(통합에서 확인):** 매니페스트 id `nope`.\n"))))

    def testMarkerWordsWithIdAreFineAndHeadingsAndFencesAreSkipped(self) -> None:
        self.assertEqual([], checkVerifyPending(self.makeRoot("확인 대기: 매니페스트 id `some-test`.\n## 확인 대기\n```\n실행 대기: 코드 안\n```\n")))

    def testPendingItemNeedsAReferencingLine(self) -> None:
        listViolation = checkVerifyPending(self.makeRoot("본문\n", pendingItem=True))
        self.assertEqual(1, len(listViolation))
        self.assertIn("some-test", listViolation[0])
        self.assertEqual([], checkVerifyPending(self.makeRoot("확인 대기: 매니페스트 id `some-test`.\n", pendingItem=True)))

    def testItemWithoutCommandNeedsAnOwnerLineEvenWhenNotPending(self) -> None:
        for key, value in (("need_command", "케이스가 아직 없다"), ("manual", "사람 눈이 필요하다")):
            root = self.makeRoot("본문\n")
            item = makeItem("some-test", "test", "Debug", "none", filter="SomeTest.*", **{key: value})
            (root / "Test" / "QA" / "VerifyBatch.json").write_text(json.dumps(makeData(item)), encoding="utf-8")
            listViolation = checkVerifyPending(root)
            self.assertEqual(1, len(listViolation))
            self.assertIn(f"명령 없는 항목 `some-test`({key})", listViolation[0])
            (root / "docs" / "plans" / "A.md").write_text(self.kPlanHeader + "후보: 매니페스트 id `some-test`.\n", encoding="utf-8")
            self.assertEqual([], checkVerifyPending(root))

    def testMissingManifestOnlyMattersWhenAPendingLineExists(self) -> None:
        root = self.makeRoot("본문\n")
        (root / "Test" / "QA" / "VerifyBatch.json").unlink()
        self.assertEqual([], findPendingLineViolations(root, None))
        (root / "docs" / "plans" / "A.md").write_text(self.kPlanHeader + "실행 대기: 글\n", encoding="utf-8")
        self.assertEqual(1, len(findPendingLineViolations(root, None)))


class FakeItemRunner:
    """`runItemInternal` 대신 자는 가짜 — 항목마다 `(시작, 끝)` 을 적고 `setFail` 의 id 는 진다. 샤드 조각은 `id#조각번호`(0 부터)로 적는다."""

    def __init__(self, mapSeconds: dict[str, float], setFail: frozenset[str] = frozenset()) -> None:
        self.mapSeconds = mapSeconds
        self.setFail = setFail
        self.mapSpan: dict[str, tuple[float, float]] = {}
        self.listStarted: list[str] = []
        self.mapShardArg: dict[str, tuple[str, ...]] = {}
        self.lock = threading.Lock()

    def __call__(self, item: VerifyItem, preset: str, repositoryRoot: Path, owner: str, outputDir: Path, shardIndex: int | None = None,
                 listShardArg: tuple[str, ...] = ()) -> ItemResult:
        key = item.itemId if shardIndex is None else f"{item.itemId}#{shardIndex}"
        start = time.monotonic()
        with self.lock:
            self.listStarted.append(key)
            self.mapShardArg[key] = listShardArg
        time.sleep(self.mapSeconds.get(key, self.mapSeconds.get(item.itemId, 0.1)))
        with self.lock:
            self.mapSpan[key] = (start, time.monotonic())
        bFail = key in self.setFail or item.itemId in self.setFail
        return makeResult(item.itemId, kStatusFailed if bFail else kStatusPassed, "boom" if bFail else "", "d", logPath=f"logs/{key}.log")

    def isOverlapped(self, first: str, second: str) -> bool:
        (firstStart, firstEnd), (secondStart, secondEnd) = self.mapSpan[first], self.mapSpan[second]
        return firstStart < secondEnd and secondStart < firstEnd


@contextlib.contextmanager
def fakeHoldLocks(mainRoot: Path, gpuMode: str, owner: str, purpose: str, waitSeconds: int) -> Iterator[int]:
    yield 0


class ParallelBatchTest(unittest.TestCase):
    """겹침 규칙 — 매니페스트 순서를 뒤섞어 둔 가짜 항목(sleep)으로 `runBatch` 를 돌린다. 실제 잠금 · 프로세스는 쓰지 않는다."""

    def setUp(self) -> None:
        listManifestItem = (
            makeItem("m", "measure", "Debug", "exclusive", tool="scripts", args=["perf"], limit="l", judge="exit"),
            makeItem("ts", "test", "Debug", "shared", filter="T.*"),
            makeItem("tn", "test", "Debug", "none", filter="T.*"),
            makeItem("cs", "ctest", "Debug", "shared", label="x"),
            makeItem("a", "ctest", "Debug", "none", label="y"),
            makeItem("b", "build", "Debug", "none"),
            makeItem("need", "test", "Debug", "none", filter="T.*", need_command="이유"),
        )
        self.manifest = parseManifest(makeData(*listManifestItem))
        self.listItem = list(self.manifest.listItem)

    def runWith(self, jobs: int, runner: FakeItemRunner, bFailFast: bool = False) -> list[ItemResult]:
        with mock.patch.object(RunVerifyBatch, "runItemInternal", runner), mock.patch.object(RunVerifyBatch, "holdLocksInternal", fakeHoldLocks), \
                mock.patch.object(RunVerifyBatch, "findMainRoot", lambda root: root):
            return RunVerifyBatch.runBatch(self.manifest, self.listItem, Path("."), "o", 1, Path("."), jobs, bFailFast=bFailFast)

    def testPlanSplitsLockFreeItemsAndKeepsLockGroupsWhole(self) -> None:
        listRun = [item for item in orderItems(self.manifest, self.listItem)[0][1] if item.bRunnable]
        listUnit = RunVerifyBatch.planUnits("Debug", listRun)
        self.assertEqual([("none", ["b"]), ("none", ["a"]), ("shared", ["cs"]), ("none", ["tn"]), ("shared", ["ts"]), ("exclusive", ["m"])],
                         [(unit.gpuMode, [item.itemId for item in unit.listItem]) for unit in listUnit])
        self.assertEqual([True, True, True, False, False, True], [unit.bAlone for unit in listUnit])   # 빌드 · ctest · 측정은 혼자
        self.assertTrue(RunVerifyBatch.hasConflict(listUnit[2], listUnit[4]))        # 같은 gpu 잠금 자원
        self.assertFalse(RunVerifyBatch.hasConflict(listUnit[3], listUnit[4]))       # 잠금 없는 test 와 공유 test
        self.assertTrue(RunVerifyBatch.hasConflict(listUnit[0], listUnit[1]))        # 빌드는 혼자
        self.assertTrue(RunVerifyBatch.hasConflict(listUnit[1], listUnit[3]))        # ctest 는 혼자 — 둘이 겹치면 CPU 경합으로 린트 제한 시간이 가짜로 진다

    def testConflictingUnitsAreSerialAndOthersOverlap(self) -> None:
        runner = FakeItemRunner({"tn": 0.4})
        listResult = self.runWith(2, runner)
        for other in ("a", "cs", "tn", "ts", "m"):
            self.assertFalse(runner.isOverlapped("b", other), f"빌드가 {other} 와 겹쳤다")
        for other in ("a", "cs", "tn", "ts"):
            self.assertFalse(runner.isOverlapped("m", other), f"측정이 {other} 와 겹쳤다")
        for other in ("tn", "ts", "cs"):
            self.assertFalse(runner.isOverlapped("a", other), f"ctest 가 {other} 와 겹쳤다")
        self.assertFalse(runner.isOverlapped("cs", "ts"))                            # 같은 gpu 잠금(그리고 ctest 는 혼자)
        self.assertTrue(runner.isOverlapped("tn", "ts"))                             # 잠금 없는 test 와 공유 test 는 겹친다
        self.assertEqual([item.itemId for item in self.listItem], [result.itemId for result in listResult])   # 보고는 매니페스트 순서
        self.assertEqual(kStatusSkipped, listResult[-1].status)
        self.assertEqual({kStatusPassed}, {result.status for result in listResult[:-1]})

    def testJobsOneRunsOneAtATimeInTheOldOrder(self) -> None:
        runner = FakeItemRunner({})
        self.runWith(1, runner)
        self.assertEqual(["b", "a", "cs", "tn", "ts", "m"], runner.listStarted)
        listId = list(runner.mapSpan)
        for first, second in zip(listId, listId[1:]):
            self.assertFalse(runner.isOverlapped(first, second))

    def testFailureDoesNotStopOthersAndFailedBuildBlocksTheConfig(self) -> None:
        runner = FakeItemRunner({}, frozenset({"tn"}))
        mapStatus = {result.itemId: result.status for result in self.runWith(2, runner)}
        self.assertEqual(kStatusFailed, mapStatus["tn"])
        self.assertEqual([kStatusPassed] * 5, [mapStatus[itemId] for itemId in ("m", "ts", "cs", "a", "b")])
        runner = FakeItemRunner({}, frozenset({"b"}))
        mapStatus = {result.itemId: result.status for result in self.runWith(2, runner)}
        self.assertEqual(["b"], runner.listStarted)
        self.assertEqual([kStatusFailed] + [kStatusBlocked] * 5, [mapStatus[itemId] for itemId in ("b", "a", "cs", "tn", "ts", "m")])

    def testFailFastStopsAfterTheFailingStageAndKeepsAllInTheTable(self) -> None:
        runner = FakeItemRunner({}, frozenset({"a"}))      # ctest(2 단계)가 진다
        listResult = self.runWith(2, runner, bFailFast=True)
        mapResult = {result.itemId: result for result in listResult}
        self.assertEqual(["b", "a", "cs"], runner.listStarted)                           # 같은 단계의 cs 는 끝까지 돌고, 뒤 단계(test · measure)는 돌지 않는다
        self.assertEqual([kStatusPassed, kStatusFailed, kStatusPassed], [mapResult[itemId].status for itemId in ("b", "a", "cs")])
        for itemId in ("tn", "ts", "m"):
            self.assertEqual(kStatusSkipped, mapResult[itemId].status)
            self.assertIn(f"{RunVerifyBatch.kSkipFailFastMarker} ctest", mapResult[itemId].detail)
            self.assertIn("`a`", mapResult[itemId].detail)
        self.assertEqual(7, len(listResult))
        runner = FakeItemRunner({})
        self.runWith(1, runner, bFailFast=True)
        self.assertEqual(["b", "a", "cs", "tn", "ts", "m"], runner.listStarted)          # 지지 않으면 전부 돈다

    def testStagesFollowTheKindOrder(self) -> None:
        manifest = parseManifest(makeData(*copy.deepcopy(kGoodItems)))
        self.assertEqual([0, 1, 2, 3, 3, 5], [getStageIndex(manifest.findItem(itemId)) for itemId in ("full-build", "cook-shaders", "ctest-nogpu", "some-test", "a-scenario", "perf-frame")])


kMeasureExtra = {"tool": "scripts", "args": ["perf"], "limit": "l", "judge": "exit"}


class RequiresTest(unittest.TestCase):
    """`requires` — 읽을 때 막는 규칙, 선행이 통과하지 못하면 건너뛰는 실행, 선행과 겹치지 않는 일정, `--failed-from` 이 읽는 줄."""

    def parse(self, *listItem: dict) -> VerifyManifest:
        return parseManifest(makeData(*listItem))

    def testRequiresMustPointAtAnEarlierRunnableItemOfTheSameConfig(self) -> None:
        cook = makeItem("ck", "cook", "Release", "none", tool="app", args=["--c"])
        manifest = self.parse(cook, makeItem("dep", "test", "Release", "shared", filter="T.*", requires=["ck"]))
        self.assertEqual(("ck",), manifest.findItem("dep").listRequires)
        for bad, message in ((["nope"], "매니페스트에 없습니다"), (["dep"], "겹치지 않는 다른 항목"), (["ck", "ck"], "겹치지 않는 다른 항목"), (["later"], "앞서 돌지 않습니다")):
            with self.assertRaisesRegex(VerifyManifestError, message):
                self.parse(cook, makeItem("dep", "test", "Release", "shared", filter="T.*", requires=bad), makeItem("later", "measure", "Release", "exclusive", **kMeasureExtra))
        with self.assertRaisesRegex(VerifyManifestError, "같은 구성"):
            self.parse(makeItem("dbg", "test", "Debug", "none", filter="T.*"), makeItem("dep", "test", "Release", "none", filter="T.*", requires=["dbg"]))
        with self.assertRaisesRegex(VerifyManifestError, "같은 구성"):                      # 덮개 · 명령 없음 항목은 선행이 될 수 없다
            self.parse(makeItem("nc", "test", "Release", "none", filter="T.*", need_command="이유"), makeItem("dep", "test", "Release", "none", filter="T.*", requires=["nc"]))
        with self.assertRaisesRegex(VerifyManifestError, "requires"):
            self.parse(makeItem("run", "test", "Release", "shared", filter="T.*"),
                       makeItem("cov", "covered", "Release", "shared", covered=["x.scenario.xml"], covered_by="run", requires=["run"]))

    def runBatchWith(self, listItem: list[VerifyItem], manifest: VerifyManifest, runner: FakeItemRunner, jobs: int = 2) -> dict[str, ItemResult]:
        with mock.patch.object(RunVerifyBatch, "runItemInternal", runner), mock.patch.object(RunVerifyBatch, "holdLocksInternal", fakeHoldLocks), \
                mock.patch.object(RunVerifyBatch, "findMainRoot", lambda root: root):
            return {result.itemId: result for result in RunVerifyBatch.runBatch(manifest, listItem, Path("."), "o", 1, Path("."), jobs)}

    def testDependentIsSkippedWhenThePrerequisiteDidNotPass(self) -> None:
        manifest = self.parse(makeItem("ck", "cook", "Release", "none", tool="app", args=["--c"]),
                              makeItem("dep", "test", "Release", "none", filter="T.*", requires=["ck"]),
                              makeItem("free", "test", "Release", "none", filter="T.*"))
        runner = FakeItemRunner({}, frozenset({"ck"}))
        mapResult = self.runBatchWith(list(manifest.listItem), manifest, runner)
        self.assertEqual(kStatusFailed, mapResult["ck"].status)
        self.assertEqual(kStatusSkipped, mapResult["dep"].status)
        self.assertIn(f"{RunVerifyBatch.kSkipRequiresMarker} `ck` 가 {kStatusFailed}", mapResult["dep"].detail)
        self.assertEqual(kStatusPassed, mapResult["free"].status)                    # 선행이 없는 항목은 그대로 돈다
        self.assertNotIn("dep", runner.listStarted)
        runner = FakeItemRunner({})
        mapResult = self.runBatchWith(list(manifest.listItem), manifest, runner)
        self.assertEqual({kStatusPassed}, {result.status for result in mapResult.values()})

    def testPrerequisiteThatIsNotSelectedIsNotChecked(self) -> None:
        manifest = self.parse(makeItem("ck", "cook", "Release", "none", tool="app", args=["--c"]), makeItem("dep", "test", "Release", "none", filter="T.*", requires=["ck"]))
        runner = FakeItemRunner({})
        mapResult = self.runBatchWith([manifest.findItem("dep")], manifest, runner)      # `--only dep` — 이전 실행의 결과를 쓴다고 보고 돈다
        self.assertEqual(kStatusPassed, mapResult["dep"].status)

    def testDependentNeverOverlapsItsPrerequisite(self) -> None:
        manifest = self.parse(makeItem("first", "test", "Release", "none", filter="T.*"), makeItem("second", "test", "Release", "none", filter="T.*", requires=["first"]),
                              makeItem("third", "test", "Release", "none", filter="T.*"))
        runner = FakeItemRunner({"first": 0.3})
        self.runBatchWith(list(manifest.listItem), manifest, runner, jobs=3)
        self.assertFalse(runner.isOverlapped("first", "second"))
        self.assertTrue(runner.isOverlapped("first", "third"))                      # 선행이 없는 항목끼리는 겹친다

    def testFailedFromAlsoPicksTheItemsSkippedBecauseOfAFailure(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            log = Path(folder) / "vb.txt"
            log.write_text("\n".join([
                "[VerifyBatch] 끝 a: 실패 종료 코드 1 (3 초)",
                "[VerifyBatch] 끝 b: 건너뜀 선행 `a` 가 실패 — 이 항목은 그 결과를 읽는다",
                "[VerifyBatch] 끝 c 샤드 1/4: 실패 종료 코드 1 (3 초)",
                "[VerifyBatch] 끝 d: 통과  (1 초)",
                "[VerifyBatch] 끝 e: 건너뜀 앞 단계 ctest 의 `a` 가 졌다 — --fail-fast 로 뒤를 돌리지 않음",
                "[VerifyBatch] 끝 f: 막힘 Debug 빌드가 졌다",
                "[VerifyBatch] 끝 g: 건너뜀 도구가 판정을 건너뜀(77)"]), encoding="utf-8")
            self.assertEqual(["a", "b", "e", "f"], RunVerifyBatch.readFailedIds(log))         # 샤드 조각 줄(`c 샤드 1/4`)과 다른 건너뜀은 다시 고르지 않는다

    def testItemThatReadsScenarioReportsNeedsItsProducerAsPrerequisite(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "docs" / "plans").mkdir(parents=True)
            (root / "docs" / "plans" / "A.md").write_text("# 계획\n", encoding="utf-8")
            reader = makeItem("reader", "measure", "Release", "none", tool="scripts", args=["scenario-waits", "--measured-dir", "build/{preset}/AppRuns"], limit="l", judge="exit")
            producer = makeItem("producer", "test", "Release", "shared", filter="T.*")
            (root / "Test" / "QA").mkdir(parents=True)
            (root / "Test" / "TestT.cpp").write_text("SW_TEST_CASE( T, X )\n", encoding="utf-8")
            problems = findManifestProblems(root, parseManifest(makeData(producer, reader)))
            self.assertEqual(1, len(problems))
            self.assertIn("requires", problems[0])
            self.assertEqual([], findManifestProblems(root, parseManifest(makeData(producer, {**reader, "requires": ["producer"]}))))


class ShardTest(unittest.TestCase):
    """`shards` — 항목 읽기 규칙, 정책에서 읽는 계획, 조각 단위의 겹침, 조각 결과를 하나로 합치기."""

    def makeRoot(self, mapExecutable: dict, mapSuiteFolder: dict[str, str], gpuSlots: int = 2) -> Path:
        root = Path(tempfile.mkdtemp()).resolve()
        (root / "Test").mkdir()
        for suite, folder in mapSuiteFolder.items():
            (root / "Test" / folder).mkdir(parents=True, exist_ok=True)
            (root / "Test" / folder / f"Test{suite}.cpp").write_text(f"SW_TEST_CASE( {suite}, Works )\n", encoding="utf-8")
        (root / "Test" / "TestPolicy.json").write_text(json.dumps({"gpuSlots": gpuSlots, "executables": mapExecutable}), encoding="utf-8")
        return root

    def shardItem(self, filterText: str = "ShardATest.*", count: int = 3, gpu: str = "shared") -> VerifyItem:
        return parseManifest(makeData(makeItem("sh", "test", "Release", gpu, filter=filterText, shards=count))).listItem[0]

    def testShardsKeyRules(self) -> None:
        for bad in (1, 17, True, "3", 2.5):
            with self.assertRaisesRegex(VerifyManifestError, "shards"):
                parseManifest(makeData(makeItem("sh", "test", "Release", "shared", filter="T.*", shards=bad)))
        with self.assertRaisesRegex(VerifyManifestError, "shards"):
            parseManifest(makeData(makeItem("sh", "ctest", "Release", "none", label="x", shards=2)))
        with self.assertRaisesRegex(VerifyManifestError, "shards"):
            parseManifest(makeData(makeItem("sh", "test", "Release", "none", filter="T.*", need_command="이유", shards=2)))
        self.assertEqual(4, parseManifest(makeData(makeItem("sh", "test", "Release", "shared", filter="T.*", shards=4))).listItem[0].shardCount)

    def testPlanReadsTheExecutablePolicyAndCostTables(self) -> None:
        root = self.makeRoot({"ExeA": {"caseCosts": {"ShardATest.Slow": 30}, "hostCaseCosts": {"ShardATest.Host": 90}}}, {"ShardATest": "ExeA"})
        plan = planShards(root, self.shardItem())
        self.assertEqual(("ExeA", True), (plan.executable, plan.bMayOverlap))
        self.assertEqual("--test_costs=ShardATest.Slow=30,ShardATest.Host=90", plan.costArgument)
        self.assertEqual(("--test_shard=1/3", plan.costArgument), makeShardArguments(plan, 1, 3))
        self.assertEqual(("--test_shard=0/2",), makeShardArguments(ShardPlan("X", True, ""), 0, 2))
        self.assertEqual(2, readGpuSlots(root))

    def testHostResourceLockMakesTheShardsRunInOrder(self) -> None:
        root = self.makeRoot({"ExeA": {"hostResourceLock": "AppBin"}}, {"ShardATest": "ExeA"})
        self.assertFalse(planShards(root, self.shardItem()).bMayOverlap)

    def testSuitesMustLiveInOneExecutable(self) -> None:
        root = self.makeRoot({"ExeA": {}, "ExeB": {}}, {"ShardATest": "ExeA", "ShardBTest": "ExeB"})
        with self.assertRaisesRegex(VerifyManifestError, "실행 파일 하나"):
            planShards(root, self.shardItem("ShardATest.*,ShardBTest.*"))
        with self.assertRaisesRegex(VerifyManifestError, "실행 파일 하나"):
            planShards(root, self.shardItem("NoSuchTest.*"))
        with self.assertRaisesRegex(VerifyManifestError, "Suite.Case"):
            planShards(root, self.shardItem("*"))
        self.assertTrue(any("실행 파일 하나" in problem for problem in findManifestProblemsOfShards(root, "NoSuchTest.*")))

    def testPolicyWithoutGpuSlotsStops(self) -> None:
        root = self.makeRoot({}, {})
        (root / "Test" / "TestPolicy.json").write_text("{}", encoding="utf-8")
        with self.assertRaisesRegex(VerifyManifestError, "gpuSlots"):
            readGpuSlots(root)

    def testShardUnitsOverlapOnlyWithTheirOwnSiblingsAndOnlyWhenThePolicyAllows(self) -> None:
        item = self.shardItem(count=3)
        other = parseManifest(makeData(makeItem("other", "test", "Release", "shared", filter="O.*"))).listItem[0]
        for bMayOverlap in (True, False):
            plan = ShardPlan("ExeA", bMayOverlap, "")
            listUnit = RunVerifyBatch.planUnits("Release", [item, other], {"sh": plan})
            self.assertEqual([("sh", 0), ("sh", 1), ("sh", 2), ("other", None)], [(unit.listItem[0].itemId, unit.shardIndex) for unit in listUnit])
            self.assertEqual(not bMayOverlap, RunVerifyBatch.hasConflict(listUnit[0], listUnit[1]))     # 형제 조각
            self.assertTrue(RunVerifyBatch.hasConflict(listUnit[2], listUnit[3]))                       # 다른 공유 항목과는 늘 순서대로
            self.assertFalse(listUnit[0].bAlone)

    def testCommandCarriesTheShardArguments(self) -> None:
        item = self.shardItem()
        command = buildItemCommand(item, "Ninja-Release", Path("."), "o", Path("."), ("--test_shard=2/3", "--test_costs=a=1"))
        self.assertEqual(["test", "ShardATest.*", "--preset", "Ninja-Release", "--no-build", "--test_shard=2/3", "--test_costs=a=1"], command[3:])

    def runShards(self, plan: ShardPlan, runner: FakeItemRunner, gpuSlots: int = 2, jobs: int = 4) -> list[ItemResult]:
        item = self.shardItem(count=4)
        manifest = parseManifest(makeData(makeItem("sh", "test", "Release", "shared", filter="ShardATest.*", shards=4)))
        with mock.patch.object(RunVerifyBatch, "runItemInternal", runner), mock.patch.object(RunVerifyBatch, "holdLocksInternal", fakeHoldLocks), \
                mock.patch.object(RunVerifyBatch, "findMainRoot", lambda root: root):
            return RunVerifyBatch.runBatch(manifest, [item], Path("."), "o", 1, Path("."), jobs, mapShardPlan={"sh": plan}, gpuSlots=gpuSlots)

    @staticmethod
    def peakOverlap(runner: FakeItemRunner) -> int:
        listEdge = sorted([(start, 1) for start, _ in runner.mapSpan.values()] + [(end, -1) for _, end in runner.mapSpan.values()])
        running = peak = 0
        for _, delta in listEdge:
            running += delta
            peak = max(peak, running)
        return peak

    def testShardsRunAsOneItemWithTheirArgumentsAndGpuSlotLimit(self) -> None:
        runner = FakeItemRunner({"sh": 0.15})
        listResult = self.runShards(ShardPlan("ExeA", True, "--test_costs=a=1"), runner)
        self.assertEqual(1, len(listResult))
        self.assertEqual((kStatusPassed, "4 샤드"), (listResult[0].status, listResult[0].detail))
        self.assertEqual({f"sh#{index}" for index in range(4)}, set(runner.listStarted))
        self.assertEqual({("--test_shard=%d/4" % index, "--test_costs=a=1") for index in range(4)}, set(runner.mapShardArg.values()))
        self.assertEqual(2, self.peakOverlap(runner))                               # gpuSlots 2 — 조각 넷이 있어도 둘까지만 겹친다

    def testShardsRunInOrderWhenThePolicyKeepsAHostLock(self) -> None:
        runner = FakeItemRunner({"sh": 0.1})
        self.runShards(ShardPlan("ExeA", False, ""), runner)
        self.assertEqual(1, self.peakOverlap(runner))
        self.assertEqual(["sh#0", "sh#1", "sh#2", "sh#3"], runner.listStarted)

    def testAFailedShardFailsTheItemAndNamesTheShard(self) -> None:
        runner = FakeItemRunner({}, frozenset({"sh#1"}))
        listResult = self.runShards(ShardPlan("ExeA", True, ""), runner)
        self.assertEqual(kStatusFailed, listResult[0].status)
        self.assertEqual("샤드 2/4: boom", listResult[0].detail)
        self.assertEqual("logs/sh#1.log", listResult[0].logPath)                    # 처음 진 조각의 로그

    def testCombineShardResults(self) -> None:
        item = self.shardItem(count=3)

        def part(status: str, detail: str = "", startedAt: str = "") -> ItemResult:
            result = makeResult("sh", status, detail, "d", logPath=detail or "log")
            result.startedAt = startedAt
            return result

        combined = combineShardResults(item, [part(kStatusPassed, startedAt="2026-10-27T10:00:05"), part(kStatusPassed, startedAt="2026-10-27T10:00:01"), part(kStatusSkipped, "x")], 12.0, "d")
        self.assertEqual((kStatusPassed, "3 샤드(건너뜀 1)", "2026-10-27T10:00:01", 12.0), (combined.status, combined.detail, combined.startedAt, combined.seconds))
        self.assertEqual(kStatusSkipped, combineShardResults(item, [part(kStatusSkipped, "s")] * 3, 1.0, "d").status)
        self.assertEqual(kStatusBlocked, combineShardResults(item, [part(kStatusPassed), part(kStatusBlocked, "잠금"), part(kStatusPassed)], 1.0, "d").status)
        failed = combineShardResults(item, [part(kStatusPassed), part(kStatusFailed, "a"), part(kStatusBlocked, "b")], 1.0, "d")
        self.assertEqual((kStatusFailed, "샤드 2/3: a"), (failed.status, failed.detail))


def findManifestProblemsOfShards(root: Path, filterText: str) -> list[str]:
    """`shards` 항목 하나가 든 매니페스트를 `findManifestProblems` 로 점검한다(소스 · 결과 문서는 이 임시 저장소에 만든다)."""
    (root / "docs" / "plans").mkdir(parents=True, exist_ok=True)
    (root / "docs" / "plans" / "A.md").write_text("# 계획\n", encoding="utf-8")
    manifest = parseManifest(makeData(makeItem("sh", "test", "Release", "shared", filter=filterText, shards=3)))
    return findManifestProblems(root, manifest)


class ProfileTest(unittest.TestCase):
    """기본 실행(정합성 검증)과 측정 프로필 — 측정은 `--measure` · `--measure-only` 로만 들고, 이름으로 부른 항목은 거르지 않는다."""

    def setUp(self) -> None:
        self.manifest = parseManifest(makeData(
            makeItem("build", "build", "Release", "none"),
            makeItem("cook", "cook", "Release", "none", tool="app", args=["--c"]),
            makeItem("unit", "test", "Release", "none", filter="T.*"),
            makeItem("bench", "measure", "Release", "exclusive", **kMeasureExtra),
            makeItem("check", "measure", "Release", "exclusive", functional=True, **kMeasureExtra),
            makeItem("debug-only", "test", "Debug", "none", filter="T.*")))

    def select(self, **kwargs: object) -> list[str]:
        options = {"bExplicitSelection": False, "bMeasure": False, "bMeasureOnly": False, **kwargs}
        return [item.itemId for item in RunVerifyBatch.selectProfileItems(self.manifest, list(self.manifest.listItem), **options)]

    def testDefaultLeavesOutPlainMeasureButKeepsFunctionalOnes(self) -> None:
        self.assertEqual(["build", "cook", "unit", "check", "debug-only"], self.select())
        self.assertEqual(["build", "cook", "unit", "bench", "check", "debug-only"], self.select(bMeasure=True))
        self.assertEqual(6, len(self.select(bExplicitSelection=True)))                # 이름으로 부른 항목은 거르지 않는다

    def testMeasureOnlyKeepsMeasureItemsPlusTheirBuildAndCook(self) -> None:
        self.assertEqual(["build", "cook", "bench", "check"], self.select(bMeasureOnly=True))

    def testFunctionalIsForMeasureItemsOnly(self) -> None:
        with self.assertRaisesRegex(VerifyManifestError, "functional"):
            parseManifest(makeData(makeItem("x", "test", "Release", "none", filter="T.*", functional=True)))
        with self.assertRaisesRegex(VerifyManifestError, "functional"):
            parseManifest(makeData(makeItem("x", "measure", "Release", "exclusive", functional="yes", **kMeasureExtra)))
        self.assertTrue(self.manifest.findItem("check").bIncludedByDefault)
        self.assertFalse(self.manifest.findItem("bench").bIncludedByDefault)

    def testRepositoryDefaultRunLeavesAllRunnableMeasureOut(self) -> None:
        manifest = readManifest(getProjectRoot())
        listDefault = RunVerifyBatch.selectProfileItems(manifest, list(manifest.listItem), bExplicitSelection=False, bMeasure=False, bMeasureOnly=False)
        self.assertEqual([], [item.itemId for item in listDefault if item.kind == "measure" and not item.bFunctional])
        self.assertTrue(any(item.kind == "measure" for item in manifest.listItem))   # 매니페스트에는 측정이 있고 기본 실행만 거른다


class ConflictScheduleTest(unittest.TestCase):
    def testScheduleRespectsJobsLimitAndSurfacesTheFirstError(self) -> None:
        mapCount = {"running": 0, "peak": 0}
        lock = threading.Lock()

        def work(unit: int) -> None:
            with lock:
                mapCount["running"] += 1
                mapCount["peak"] = max(mapCount["peak"], mapCount["running"])
            time.sleep(0.05)
            with lock:
                mapCount["running"] -= 1

        runConflictSchedule(list(range(6)), lambda first, second: False, work, 3)
        self.assertEqual(3, mapCount["peak"])

        listDone: list[int] = []

        def failing(unit: int) -> None:
            listDone.append(unit)
            if unit == 1:
                raise RuntimeError("boom")

        with self.assertRaisesRegex(RuntimeError, "boom"):
            runConflictSchedule(list(range(5)), lambda first, second: True, failing, 2)
        self.assertEqual([0, 1], listDone)            # 예외 뒤로는 새 일을 시작하지 않는다
        with self.assertRaises(ValueError):
            runConflictSchedule([1], lambda first, second: False, work, 0)


class RepositoryManifestTest(unittest.TestCase):
    def testRepositoryManifestIsReadableAndConsistentWithDocsAndDisk(self) -> None:
        root = getProjectRoot()
        readManifest(root)
        self.assertEqual([], checkVerifyPending(root))


if __name__ == "__main__":
    unittest.main()
