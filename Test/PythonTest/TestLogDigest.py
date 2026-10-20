#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""LogDigest.digestLog — 성공은 끝 몇 줄만, 실패는 원인 줄과 그 앞뒤를 보이는 순수 요약. 가짜 ninja · ctest 출력으로."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Scripts"))

from common import LogDigest  # noqa: E402


def makeNinjaProgress(count: int) -> list[str]:
    return [f"[{index}/{count}] Building CXX object Source/Core/File{index}.cpp.obj" for index in range(1, count + 1)]


class DigestSuccessTest(unittest.TestCase):
    def testSuccessKeepsOnlyTheTailAndTheTiming(self) -> None:
        listShown = LogDigest.digestLog(makeNinjaProgress(500), 0, 12.34, Path("x/build.log"))
        self.assertEqual(listShown[:LogDigest.kTailLineCount], makeNinjaProgress(500)[-LogDigest.kTailLineCount:])
        self.assertIn("[성공] 12.3 s", listShown[-2])
        self.assertEqual(listShown[-1], f"[로그] 전체 출력: {Path('x/build.log')}")
        self.assertLess(len(listShown), 10)

    def testSuccessIgnoresBlankLines(self) -> None:
        listShown = LogDigest.digestLog(["a", "", "  ", "b"], 0, 0.0)
        self.assertEqual(listShown[:2], ["a", "b"])

    def testSuccessWithWarningWordsStaysShort(self) -> None:
        listLine = makeNinjaProgress(100) + ["warning: unused variable 'error_count'"] + makeNinjaProgress(100)
        self.assertLess(len(LogDigest.digestLog(listLine, 0, 1.0)), 10)


class DigestFailureTest(unittest.TestCase):
    def testNinjaCompileErrorShowsTheCauseWithContext(self) -> None:
        listLine = (makeNinjaProgress(300)
                    + ["FAILED: Source/Core/Bad.cpp.obj", "cl.exe /c Source/Core/Bad.cpp", "Source/Core/Bad.cpp(12): error C2065: 'x': undeclared identifier",
                       "    int y = x;", "ninja: build stopped: subcommand failed."])
        listShown = LogDigest.digestLog(listLine, 1, 3.0, Path("build.log"))
        text = "\n".join(listShown)
        self.assertIn("FAILED: Source/Core/Bad.cpp.obj", text)
        self.assertIn("error C2065", text)
        self.assertIn("    int y = x;", text)
        self.assertNotIn("File1.cpp.obj", text)
        self.assertIn("[종료 1]", text)

    def testFailedTestNamesAreShown(self) -> None:
        listLine = ["Test project X", "    Start 1: SceneTest", " 1/2 Test #1: SceneTest ........   Passed    0.1 sec",
                    " 2/2 Test #2: ResourceTest ...***Failed    0.2 sec", "", "The following tests FAILED:", "\t  2 - ResourceTest (Failed)"]
        text = "\n".join(LogDigest.digestLog(listLine, 8, 1.0))
        self.assertIn("ResourceTest ...***Failed", text)
        self.assertIn("2 - ResourceTest (Failed)", text)

    def testManyErrorsAreCapped(self) -> None:
        listLine: list[str] = []
        for index in range(1000):
            listLine += [f"Bad{index}.cpp(1): error C1: boom", "filler", "filler", "filler", "filler", "filler", "filler"]
        listShown = LogDigest.digestLog(listLine, 1, 1.0)
        self.assertLessEqual(len(listShown), LogDigest.kMaxFailureLines + LogDigest.kTailLineCount + 6)
        self.assertTrue(any("로그에서 본다" in line for line in listShown))

    def testFailureWithoutAnyKnownPatternStillShowsTheTail(self) -> None:
        listShown = LogDigest.digestLog(["one", "two", "three"], 3, 1.0)
        self.assertIn("three", listShown)
        self.assertIn("[종료 3]", "\n".join(listShown))


if __name__ == "__main__":
    unittest.main()
