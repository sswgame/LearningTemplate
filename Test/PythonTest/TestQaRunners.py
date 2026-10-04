#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
QA 러너의 App 없이 시험할 수 있는 부분 — 이미지 지표(`ImageMetrics`), 프로파일 표 읽기 · 기울기(`AppRun`), 성능 판정(`PerfRegression`).

지표 시험은 "같은 장면을 조금 움직인 그림은 통과, 물체가 사라지거나 색이 바뀐 그림은 실패" 를 본다 — 자동 플레이 캡처가 겪는 것이 앞의 것이고
회귀가 남기는 것이 뒤의 것이다. 정확한 픽셀 비교였다면 앞의 것에서 진다.
"""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

kRepositoryRoot = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(kRepositoryRoot / "Scripts"))

from common.AppRun import computeSlopePerMinute, findUsableBackends, loadGameTable, parseProfileTable, parseProfileWall  # noqa: E402
from common.ImageMetrics import (RgbImage, compareMetrics, computeMetrics, decodePng, deriveTolerance, downscale,  # noqa: E402
                                 encodePng, parsePpm)
from qa.PerfRegression import compareInternal  # noqa: E402


def makeSceneInternal(width: int = 160, height: int = 90, boxX: int = 60, boxColor=(200, 40, 40), background=(30, 40, 60),
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
            self.assertEqual(["dx12", "dx11", "vk", "gl"], findUsableBackends(Path(tempDir)))
            cachePath.write_text("SW_SHIPPING_BUILD:BOOL=ON\nSW_SHIPPING_RHI_BACKEND:STRING=Vulkan\n", encoding="utf-8")
            self.assertEqual(["vk"], findUsableBackends(Path(tempDir)))


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


class GameTableTest(unittest.TestCase):
    def testEveryTestGameHasAnAutoplayEntry(self) -> None:
        table = loadGameTable(kRepositoryRoot)
        listGame = sorted(path.name for path in (kRepositoryRoot / "Source/Games").iterdir() if (path / "CMakeLists.txt").is_file())
        self.assertEqual(listGame, sorted(table["games"]))
        for name, game in table["games"].items():
            self.assertGreater(int(game["screenshot_frame"]), 10, name)
            for argument in game["arguments"]:
                self.assertTrue(argument.startswith("-gv_"), f"{name}: {argument}")
                variable = argument[1:].split("=", 1)[0]
                listSource = list((kRepositoryRoot / "Source").rglob("*.cpp"))
                self.assertTrue(any(variable in path.read_text(encoding="utf-8", errors="replace") for path in listSource
                                    if "Games" in path.parts or "GameFramework" in path.parts or "Graphics" in path.parts or "Empty" in path.parts), f"{name}: {variable}")

    def testGoldenReferencesMatchTheTable(self) -> None:
        import json
        table = loadGameTable(kRepositoryRoot)
        goldenRoot = kRepositoryRoot / "Test/Qa/Golden"
        for referencePath in sorted(goldenRoot.glob("*/*.json")):
            record = json.loads(referencePath.read_text(encoding="utf-8"))
            game = table["games"][referencePath.parent.name]
            self.assertEqual(game["arguments"], record["capture"]["arguments"], referencePath)
            self.assertEqual(game["screenshot_frame"], record["capture"]["frame"], referencePath)
            self.assertTrue(referencePath.with_suffix(".png").is_file(), referencePath)


if __name__ == "__main__":
    unittest.main(verbosity=2)
