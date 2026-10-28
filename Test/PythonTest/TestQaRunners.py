#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
QA 러너의 App 없이 테스트할 수 있는 부분 — 이미지 지표(`ImageMetrics`), 프로파일 표 읽기 · 기울기(`AppRun`), 성능 판정(`PerfRegression`).

지표 테스트는 "같은 장면을 조금 움직인 그림은 통과, 물체가 사라지거나 색이 바뀐 그림은 실패" 를 본다 — 자동 플레이 캡처가 겪는 것이 앞의 것이고
회귀가 남기는 것이 뒤의 것이다. 정확한 픽셀 비교였다면 앞의 것에서 진다.
"""

from __future__ import annotations

import re
import sys
import tempfile
import unittest
from pathlib import Path

kRepositoryRoot = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(kRepositoryRoot / "Scripts"))

from common.BuildTree import BuildTree  # noqa: E402
from common.ExitCode import ExitCode  # noqa: E402
from common.AppRun import AppRunResult, kAppRhiUnusableExitCode, readRhiUnusableExitCode, computeSlopePerMinute, findUsableBackends, loadGameTable, parseProfileTable, parseProfileWall  # noqa: E402
from common.ImageMetrics import (RgbImage, compareMetrics, computeMetrics, decodePng, deriveTolerance, downscale,  # noqa: E402
                                 encodePng, parsePpm)
from common.AppRun import kStartupRe, splitRunSeconds  # noqa: E402
from common.AppRun import runApp  # noqa: E402
from common.QualityPreset import SettingsIsolation, checkSettingsIsolation, describeQualityMismatch, readScalabilityPreset, readTableQuality, writePresetSettingsFile  # noqa: E402
from qa.GoldenImages import formatCaseLine  # noqa: E402
from qa.PerfRegression import compareInternal  # noqa: E402


def makeSceneInternal(width: int = 160, height: int = 90, boxX: int = 60, boxColor: tuple[int, int, int] = (200, 40, 40),
                      background: tuple[int, int, int] = (30, 40, 60),
                      bBox: bool = True) -> RgbImage:
    pixels = bytearray(width * height * 3)
    for y in range(height):
        for x in range(width):
            color = boxColor if bBox and boxX <= x < boxX + 40 and 30 <= y < 70 else background
            offset = (y * width + x) * 3
            pixels[offset:offset + 3] = bytes(color)
    return RgbImage(width, height, pixels)


class ImageCodecTest(unittest.TestCase):
    def testPngRoundTrip(self) -> None:
        image = makeSceneInternal()
        self.assertEqual(image.pixels, decodePng(encodePng(image)).pixels)

    def testPpmWithComment(self) -> None:
        data = b"P6\n# comment\n2 1\n255\n" + bytes([1, 2, 3, 4, 5, 6])
        image = parsePpm(data)
        self.assertEqual((2, 1), (image.width, image.height))
        self.assertEqual((4, 5, 6), image.getPixel(1, 0))

    def testDownscaleAveragesAreas(self) -> None:
        image = RgbImage(2, 2, bytearray([0, 0, 0, 255, 255, 255, 255, 255, 255, 0, 0, 0]))
        small = downscale(image, 1, 1)
        self.assertEqual((128, 128, 128), small.getPixel(0, 0))


class MetricsTest(unittest.TestCase):
    def setUp(self) -> None:
        self.listRecorded = [computeMetrics(makeSceneInternal(boxX=58 + offset)) for offset in range(3)]
        self.tolerance = deriveTolerance(self.listRecorded)
        self.reference = self.listRecorded[0]

    def testSmallMotionPasses(self) -> None:
        self.assertEqual([], compareMetrics(self.reference, computeMetrics(makeSceneInternal(boxX=63)), self.tolerance))

    def testBackgroundIsRemoved(self) -> None:
        metrics = computeMetrics(makeSceneInternal())
        self.assertAlmostEqual((40 * 40) / (160 * 90), metrics.foregroundFraction, places=3)
        self.assertEqual([30.0, 40.0, 60.0], metrics.background)
        self.assertGreater(metrics.redMinusBlue, 100)

    def testSceneWithoutBackgroundIsAllForeground(self) -> None:
        # 1 인칭처럼 위는 하늘 · 아래는 땅이면 모서리가 둘씩 갈린다 — 배경을 고르지 않고 화면 전체를 전경으로 본다
        width, height = 160, 90
        pixels = bytearray(width * height * 3)
        for y in range(height):
            color = (20, 30, 60) if y < height // 2 else (120, 100, 70)
            for x in range(width):
                pixels[(y * width + x) * 3:(y * width + x) * 3 + 3] = bytes(color)
        metrics = computeMetrics(RgbImage(width, height, pixels))
        self.assertEqual(1.0, metrics.foregroundFraction)
        self.assertEqual([-1.0, -1.0, -1.0], metrics.background)
        for actual, expected in zip(metrics.foregroundMean, metrics.meanColor):
            self.assertAlmostEqual(expected, actual)

    def testMissingObjectFails(self) -> None:
        listViolation = compareMetrics(self.reference, computeMetrics(makeSceneInternal(bBox=False)), self.tolerance)
        self.assertTrue(any(violation.metric == "foregroundFraction" for violation in listViolation), listViolation)

    def testColorChangeFails(self) -> None:
        listViolation = compareMetrics(self.reference, computeMetrics(makeSceneInternal(boxColor=(40, 40, 200))), self.tolerance)
        self.assertTrue(any(violation.metric.startswith("foregroundMean") or violation.metric == "redMinusBlue" for violation in listViolation))

    def testObjectMovedAcrossTheScreenFails(self) -> None:
        listViolation = compareMetrics(self.reference, computeMetrics(makeSceneInternal(boxX=5)), self.tolerance)
        self.assertTrue(any(violation.metric.startswith("gridLuma") for violation in listViolation), listViolation)

    def testToleranceHasAFloor(self) -> None:
        identical = deriveTolerance([self.reference, self.reference])
        self.assertGreater(identical["foregroundFraction"], 0.0)


class ProfileTableTest(unittest.TestCase):
    kOutput = [
        "[2026-10-04 12:00:00] [Info] [Profile] ===== frame breakdown — 600 frames =====",
        "[2026-10-04 12:00:00] [Info] [Profile] scope                             avg_us   p50_us   p99_us   min_us   max_us   per_frame",
        "[2026-10-04 12:00:00] [Info] [Profile] GT.Frame                          1234   1200   2100   900   5000   1.0",
        "[2026-10-04 12:00:00] [Info] [Profile] RT.Pass.execute                   80   70   200   10   900   12.5",
        "[Profile] wall  600 frames in 900 ms  = 1500 us/frame",
    ]

    def testRowsAreParsed(self) -> None:
        mapRow = parseProfileTable(self.kOutput)
        self.assertEqual({"GT.Frame", "RT.Pass.execute"}, set(mapRow))
        self.assertEqual(1200, mapRow["GT.Frame"].p50Micro)
        self.assertEqual(2100, mapRow["GT.Frame"].p99Micro)
        self.assertEqual(12.5, mapRow["RT.Pass.execute"].perFrame)

    def testWallLine(self) -> None:
        self.assertEqual((600, 900, 1500), parseProfileWall(self.kOutput))

    def testSlopePerMinute(self) -> None:
        self.assertAlmostEqual(60.0, computeSlopePerMinute([(0.0, 0.0), (1.0, 1.0), (2.0, 2.0)]))
        self.assertEqual(0.0, computeSlopePerMinute([(0.0, 5.0)]))
        self.assertAlmostEqual(0.0, computeSlopePerMinute([(0.0, 5.0), (10.0, 5.0), (20.0, 5.0)]))


class UsableBackendTest(unittest.TestCase):
    def testShippingBuildOffersOnlyItsLinkedBackend(self) -> None:
        with tempfile.TemporaryDirectory() as tempDir:
            cachePath = Path(tempDir) / "CMakeCache.txt"
            cachePath.write_text("SW_SHIPPING_BUILD:BOOL=OFF\n", encoding="utf-8")
            self.assertEqual({"dx12", "dx11", "vk", "gl"}, set(findUsableBackends(BuildTree(Path(tempDir)))))
            cachePath.write_text("SW_SHIPPING_BUILD:BOOL=ON\nSW_SHIPPING_RHI_BACKEND:STRING=Vulkan\n", encoding="utf-8")
            self.assertEqual(["vk"], findUsableBackends(BuildTree(Path(tempDir))))

    def testIgnoredGlobalsAreFoundFromTheWarningLine(self) -> None:
        result = AppRunResult(listLine=["[2026-10-16 0:53:20] [Engine] [App] [Warning] - gv_benchMeshes: 그런 전역 변수가 없습니다. 무시됩니다",
                                        "[Info] - gv_benchAnimate: set"])
        self.assertEqual(["gv_benchMeshes"], result.findIgnoredGlobals(["-gv_benchMeshes=8", "-gv_benchAnimate=0", "-dx12"]))


class PerfCompareTest(unittest.TestCase):
    def testRegressionNeedsBothRatioAndFloor(self) -> None:
        baseline = {"GT.Frame": {"p50_us": 1000.0, "p99_us": 2000.0}}
        tolerance = {"p50_us": 0.15, "p99_us": 0.35}
        listRegression, _ = compareInternal(baseline, {"GT.Frame": {"p50_us": 1290.0, "p99_us": 2000.0}}, tolerance, 150.0)
        self.assertEqual([], listRegression)
        listRegression, _ = compareInternal(baseline, {"GT.Frame": {"p50_us": 1310.0, "p99_us": 2000.0}}, tolerance, 150.0)
        self.assertEqual(1, len(listRegression))
        _, listNote = compareInternal(baseline, {"GT.Frame": {"p50_us": 500.0, "p99_us": 2000.0}}, tolerance, 150.0)
        self.assertTrue(any("faster" in note for note in listNote))


class CaseSecondsTest(unittest.TestCase):
    def testGoldenLineCarriesTheCaseSeconds(self) -> None:
        line = formatCaseLine("NileCity", "dx12", "pass", 12.34, "foreground 0.5")
        self.assertIn("PASS", line)
        self.assertIn("12.3s", line)

    def testStartupAndMeasureSecondsSplitTheRun(self) -> None:
        startupSeconds, measureSeconds = splitRunSeconds(60.0, 2500.0)
        self.assertAlmostEqual(2.5, startupSeconds)
        self.assertAlmostEqual(57.5, measureSeconds)

    def testSplitNeedsAStartupReport(self) -> None:
        self.assertEqual((None, None), splitRunSeconds(60.0, None))

    def testStartupLineIsParsed(self) -> None:
        match = kStartupRe.search("[Info] startup 1830 ms, 412 file opens")
        self.assertIsNotNone(match)
        self.assertEqual("1830", match.group(1))
        self.assertEqual("412", match.group(2))


class RhiUnusableExitCodeTest(unittest.TestCase):
    def testHeaderIsTheSingleSource(self) -> None:
        """App 의 "백엔드 못 돌림" 종료 코드는 C++ 헤더가 정본이다 — `ExitCode.Skipped` 와 CMake `SKIP_RETURN_CODE` 가 어긋나면 여기서 멈춘다."""
        self.assertEqual(kAppRhiUnusableExitCode, readRhiUnusableExitCode(kRepositoryRoot / "Source/Engine/Graphics/RHI/RHIInitResult.h"))
        self.assertEqual(int(ExitCode.Skipped), kAppRhiUnusableExitCode)
        cmakeText = (kRepositoryRoot / "Test/PythonTest/CMakeLists.txt").read_text(encoding="utf-8")
        # CMake 는 같은 헤더 줄을 읽어 변수 하나로 쓴다 — `SKIP_RETURN_CODE` 에 숫자 리터럴이 다시 생기면 멈춘다.
        self.assertIn("kRHIUnusableHereExitCode", cmakeText)
        listCode = re.findall(r"SKIP_RETURN_CODE\s+(\S+)", cmakeText)
        self.assertTrue(listCode)
        self.assertEqual({"${SW_RHI_UNUSABLE_EXIT_CODE}"}, set(listCode))
        for cmakePath in (kRepositoryRoot / "Test").rglob("CMakeLists.txt"):
            self.assertNotRegex(cmakePath.read_text(encoding="utf-8"), r"SKIP_RETURN_CODE\s+\d", str(cmakePath))

    def testMissingDefinitionStopsLoudly(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            header = Path(temporary) / "RHIInitResult.h"
            header.write_text("// 정의 없음", encoding="utf-8")
            with self.assertRaises(SystemExit):
                readRhiUnusableExitCode(header)


class GameTableTest(unittest.TestCase):
    def testEveryTestGameHasAnAutoplayEntry(self) -> None:
        table = loadGameTable(kRepositoryRoot)
        listGame = sorted(path.name for path in (kRepositoryRoot / "Source/Games").iterdir() if (path / "CMakeLists.txt").is_file())
        self.assertEqual(listGame, sorted(table["games"]))
        for name, game in table["games"].items():
            self.assertGreater(int(game["settled_frames"]), 0, name)
            for argument in game["arguments"]:
                self.assertTrue(argument.startswith("-gv_"), f"{name}: {argument}")
                variable = argument[1:].split("=", 1)[0]
                listSource = list((kRepositoryRoot / "Source").rglob("*.cpp"))
                self.assertTrue(any(variable in path.read_text(encoding="utf-8", errors="replace") for path in listSource
                                    if "Games" in path.parts or "GameFramework" in path.parts or "Graphics" in path.parts or "Empty" in path.parts), f"{name}: {variable}")

    def testGoldenReferencesMatchTheTable(self) -> None:
        import json
        table = loadGameTable(kRepositoryRoot)
        goldenRoot = kRepositoryRoot / "Test/QA/Golden"
        for referencePath in sorted(goldenRoot.glob("*/*.json")):
            record = json.loads(referencePath.read_text(encoding="utf-8"))
            game = table["games"][referencePath.parent.name]
            self.assertEqual(game["arguments"], record["capture"]["arguments"], referencePath)
            self.assertEqual(game["settled_frames"], record["capture"]["settled_frames"], referencePath)
            self.assertTrue(referencePath.with_suffix(".png").is_file(), referencePath)


class QualityPresetTest(unittest.TestCase):
    """골든 · 기준을 뜨는 러너는 품질 프리셋을 표에서 읽어 설정 파일로 명시하고, 기준의 프리셋과 다르거나 없으면 거절한다."""

    def testTableNamesAnExistingPreset(self) -> None:
        table = loadGameTable(kRepositoryRoot)
        self.assertEqual("high", readTableQuality(table))   # 스키마 기본값이 high 라 기존 골든의 그림이 바뀌지 않는다
        self.assertEqual("high", readScalabilityPreset(kRepositoryRoot, readTableQuality(table))["graphics.quality"])

    def testTableWithoutQualityStopsInsteadOfFallingBack(self) -> None:
        with self.assertRaises(KeyError):
            readTableQuality({"games": {}})

    def testUnknownPresetIsAnError(self) -> None:
        with self.assertRaises(ValueError):
            readScalabilityPreset(kRepositoryRoot, "no-such-preset")

    def testWrittenSettingsFileCarriesThePresetAndIsPassedToApp(self) -> None:
        import json
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "usersettings.json"
            listArgument = writePresetSettingsFile(kRepositoryRoot, "low", path)
            self.assertEqual([f"-gv_userSettingsFile={path.as_posix()}"], listArgument)
            document = json.loads(path.read_text(encoding="utf-8"))
            self.assertEqual("low", document["values"]["graphics.quality"])
            self.assertEqual("0.75", document["values"]["graphics.renderScale"])

    def testMismatchOrMissingRecordedQualityIsRejected(self) -> None:
        self.assertEqual("", describeQualityMismatch("high", "high"))
        self.assertIn("'low'", describeQualityMismatch("low", "high"))
        self.assertIn("does not record", describeQualityMismatch(None, "high"))

    def testLoadBaselineRejectsOtherQuality(self) -> None:
        from qa.Load import judgeBaseline
        listRegression, _ = judgeBaseline({"quality": "low", "metrics": {}}, {}, "high")
        self.assertEqual(1, len(listRegression))
        self.assertEqual(([], []), judgeBaseline({"quality": "high", "metrics": {}}, {}, "high"))


class SettingsIsolationEntranceTest(unittest.TestCase):
    """App 을 띄우는 공통 입구(`runApp`)는 설정 격리 선언을 필수로 받고 인자와 어긋나면 띄우기 전에 멈춘다 — 프리셋 없이 띄우는 호출을 쓸 수 없다."""

    def testDeclarationIsRequired(self) -> None:
        with self.assertRaises(TypeError):
            runApp(Path(sys.executable), ["-c", "pass"])   # type: ignore[call-arg]

    def testDeclarationMustMatchTheArguments(self) -> None:
        for isolation, listArgument in ((SettingsIsolation.PRESET_FILE, ["-gv_profileFrames=1"]),
                                        (SettingsIsolation.PRESET_FILE, ["-gv_userSettingsFile="]),
                                        (SettingsIsolation.ISOLATED_AUTOMATED, ["-unattended"]),
                                        (SettingsIsolation.ISOLATED_AUTOMATED, ["-gv_userSettingsFile=a.json", "-scenario-report=x"]),
                                        (SettingsIsolation.UNATTENDED_ONLY, ["-gv_userSettingsFile=a.json"])):
            with self.assertRaises(ValueError, msg=(isolation, listArgument)):
                runApp(Path(sys.executable), listArgument, settings=isolation)

    def testMatchingDeclarationsPass(self) -> None:
        for isolation, listArgument in ((SettingsIsolation.PRESET_FILE, ["-gv_userSettingsFile=a.json"]),
                                        (SettingsIsolation.ISOLATED_AUTOMATED, ["-scenario=x.xml", "-gv_userSettingsFile=a.json"]),
                                        (SettingsIsolation.UNATTENDED_ONLY, ["-unattended"]),
                                        (SettingsIsolation.NOT_APPLICABLE, [])):
            checkSettingsIsolation(isolation, listArgument)

    def testEveryRunAppCallerStatesItsIsolation(self) -> None:
        """Scripts 의 `runApp(` 호출이 모두 `settings=` 를 적는다(새 호출자가 빠뜨리면 여기서 알린다 — 런타임 TypeError 의 정적 짝)."""
        import re
        listMissing = []
        for path in sorted((kRepositoryRoot / "Scripts").rglob("*.py")):
            if path.name == "AppRun.py":
                continue
            text = path.read_text(encoding="utf-8")
            for match in re.finditer(r"(?<![\w.])runApp\(", text):
                call = text[match.start():match.start() + 600]
                if "settings=" not in call:
                    listMissing.append(f"{path.relative_to(kRepositoryRoot)}: {text.count(chr(10), 0, match.start()) + 1}")
        self.assertEqual([], listMissing)


if __name__ == "__main__":
    unittest.main(verbosity=2)
