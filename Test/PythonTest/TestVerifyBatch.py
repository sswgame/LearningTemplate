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
from common.ExitCode import ExitCode  # noqa: E402
from common.Timeline import kTimelineEnvName, kTimelineItemEnvName, summarizeAppArguments  # noqa: E402
from common.VerifyManifest import (ItemResult, VerifyItem, VerifyManifestError, buildItemCommand, checkVerifyPending, extractMetricValues, findPendingLineViolations,  # noqa: E402
                                   findReferencedIds, formatResultTable, groupByGpuMode, interpretOutcome, judgeMetrics, kStatusBlocked,
                                   kStatusFailed, kStatusPassed, kStatusSkipped, makeNotRunResult, makeResult, orderItems, parseManifest,
                                   readManifest, resolveCoveredResult, summarizeBenchRows)
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
)


class ParseManifestTest(unittest.TestCase):
    def testGoodManifestIsRead(self) -> None:
        manifest = parseManifest(makeData(*copy.deepcopy(kGoodItems)))
        self.assertEqual(len(kGoodItems), len(manifest.listItem))
        self.assertTrue(manifest.findItem("full-build").bRunnable)
        self.assertTrue(manifest.findItem("a-scenario").bCovered)
        self.assertFalse(manifest.findItem("not-yet").bRunnable)

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
        self.assertEqual(["cook-shaders", "scenario-run", "a-scenario", "perf-frame", "later-bench", "not-yet"], [item.itemId for item in listGroup[1][1]])

    def testGpuGroupsAreConsecutiveRunsOfTheSameMode(self) -> None:
        listItem = orderItems(self.manifest, list(self.manifest.listItem))[1][1]
        self.assertEqual([("none", ["cook-shaders"]), ("shared", ["scenario-run", "a-scenario"]), ("exclusive", ["perf-frame", "later-bench", "not-yet"])],
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
                runApp(Path(sys.executable), ["-c", "pass"])
            listLine = path.read_text(encoding="utf-8").splitlines()
            self.assertEqual(2, len(listLine))
            self.assertRegex(listLine[0], r"^\d\d:\d\d:\d\d 앱시작 perf-x pid=\d+ ")
            self.assertRegex(listLine[1], r"^\d\d:\d\d:\d\d 앱끝 perf-x pid=\d+ 종료 0 ")

    def testStandaloneRunWritesTimelineUnderBuildFolderWithItemDash(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            env = {key: value for key, value in os.environ.items() if key not in (kTimelineEnvName, kTimelineItemEnvName)}
            with mock.patch.dict("os.environ", env, clear=True), mock.patch.object(Timeline, "getProjectRoot", return_value=Path(folder)), \
                    mock.patch.object(Timeline, "_standalonePath", None):
                runApp(Path(sys.executable), ["-c", "pass"])
                runApp(Path(sys.executable), ["-c", "pass"])   # 같은 프로세스의 실행은 같은 파일에 쌓인다
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
            runApp(Path(sys.executable), ["-c", "pass"])
        self.assertTrue(listStdout[0].closed)   # 안 닫으면 가비지 수집 때 ResourceWarning 이 난다

    def testHeadlessTaskLeavesTimelineLinesAndKeepsCapture(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "timeline.log"
            with mock.patch.dict("os.environ", {kTimelineEnvName: str(path)}):
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

    def testMissingManifestOnlyMattersWhenAPendingLineExists(self) -> None:
        root = self.makeRoot("본문\n")
        (root / "Test" / "QA" / "VerifyBatch.json").unlink()
        self.assertEqual([], findPendingLineViolations(root, None))
        (root / "docs" / "plans" / "A.md").write_text(self.kPlanHeader + "실행 대기: 글\n", encoding="utf-8")
        self.assertEqual(1, len(findPendingLineViolations(root, None)))


class FakeItemRunner:
    """`runItemInternal` 대신 자는 가짜 — 항목마다 `(시작, 끝)` 을 적고 `setFail` 의 id 는 진다."""

    def __init__(self, mapSeconds: dict[str, float], setFail: frozenset[str] = frozenset()) -> None:
        self.mapSeconds = mapSeconds
        self.setFail = setFail
        self.mapSpan: dict[str, tuple[float, float]] = {}
        self.listStarted: list[str] = []
        self.lock = threading.Lock()

    def __call__(self, item: VerifyItem, preset: str, repositoryRoot: Path, owner: str, outputDir: Path) -> ItemResult:
        start = time.monotonic()
        with self.lock:
            self.listStarted.append(item.itemId)
        time.sleep(self.mapSeconds.get(item.itemId, 0.1))
        with self.lock:
            self.mapSpan[item.itemId] = (start, time.monotonic())
        return makeResult(item.itemId, kStatusFailed if item.itemId in self.setFail else kStatusPassed, "", "d")

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

    def runWith(self, jobs: int, runner: FakeItemRunner) -> list[ItemResult]:
        with mock.patch.object(RunVerifyBatch, "runItemInternal", runner), mock.patch.object(RunVerifyBatch, "holdLocksInternal", fakeHoldLocks), \
                mock.patch.object(RunVerifyBatch, "findMainRoot", lambda root: root):
            return RunVerifyBatch.runBatch(self.manifest, self.listItem, Path("."), "o", 1, Path("."), jobs)

    def testPlanSplitsLockFreeItemsAndKeepsLockGroupsWhole(self) -> None:
        listRun = [item for item in orderItems(self.manifest, self.listItem)[0][1] if item.bRunnable]
        listUnit = RunVerifyBatch.planUnits("Debug", listRun)
        self.assertEqual([("none", ["b"]), ("none", ["a"]), ("shared", ["cs"]), ("none", ["tn"]), ("shared", ["ts"]), ("exclusive", ["m"])],
                         [(unit.gpuMode, [item.itemId for item in unit.listItem]) for unit in listUnit])
        self.assertEqual([True, False, False, False, False, True], [unit.bAlone for unit in listUnit])
        self.assertTrue(RunVerifyBatch.hasConflict(listUnit[2], listUnit[4]))        # 같은 gpu 잠금 자원
        self.assertFalse(RunVerifyBatch.hasConflict(listUnit[1], listUnit[2]))       # 잠금 없는 것과 공유
        self.assertTrue(RunVerifyBatch.hasConflict(listUnit[0], listUnit[1]))        # 빌드는 혼자

    def testConflictingUnitsAreSerialAndOthersOverlap(self) -> None:
        runner = FakeItemRunner({"cs": 0.4})
        listResult = self.runWith(2, runner)
        for other in ("a", "cs", "tn", "ts", "m"):
            self.assertFalse(runner.isOverlapped("b", other), f"빌드가 {other} 와 겹쳤다")
        for other in ("a", "cs", "tn", "ts"):
            self.assertFalse(runner.isOverlapped("m", other), f"측정이 {other} 와 겹쳤다")
        self.assertFalse(runner.isOverlapped("cs", "ts"))                            # 같은 gpu 잠금
        self.assertTrue(runner.isOverlapped("a", "cs"))                              # 잠금 없는 것과 공유는 겹친다
        self.assertTrue(runner.isOverlapped("tn", "cs"))
        self.assertLess(runner.mapSpan["cs"][1], runner.mapSpan["ts"][0] + 0.001)
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
