#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""RunLintSuite 의 인자 풀기 — CMake 변수 참조와 빌드 폴더가 필요한 린트를 건너뛰는 규칙."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

kRepositoryRoot = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(kRepositoryRoot / "Scripts"))

from common.ExitCode import ExitCode  # noqa: E402
from lint import RunLintSuite  # noqa: E402
from lint.LintCatalog import LintTarget  # noqa: E402


class RunLintSuiteTest(unittest.TestCase):
    def testBuildDirectoryArgumentSkipsWithoutBuild(self) -> None:
        target = LintTarget("CheckX", "Scripts/lint/gate/CheckX.py", "x", 15, ("--build-dir", "${CMAKE_BINARY_DIR}"))
        self.assertIsNone(RunLintSuite.resolveArgumentsInternal(target, Path("/repo"), None))
        self.assertEqual(RunLintSuite.resolveArgumentsInternal(target, Path("/repo"), Path("/b")),
                         ["--root", str(Path("/repo")), "--build-dir", str(Path("/b"))])

    def testSourceDirectoryReferenceIsRepositoryRoot(self) -> None:
        target = LintTarget("CheckY", "Scripts/lint/gate/CheckY.py", "y", 15, ("--config", "${CMAKE_SOURCE_DIR}/Config/a.json"))
        self.assertEqual(RunLintSuite.resolveArgumentsInternal(target, Path("/repo"), None),
                         ["--root", str(Path("/repo")), "--config", f"{Path('/repo')}/Config/a.json"])


class RunLintSuiteSummaryTest(unittest.TestCase):
    def testSummaryCountsPassAndFailAndNamesSlowestThree(self) -> None:
        listTiming = [RunLintSuite.LintTiming(f"Check{index}", float(index), 1 if index == 2 else 0, 15, "") for index in range(6)]
        summary = RunLintSuite.formatSummaryInternal(listTiming, 20.0)
        self.assertIn("6개: 통과 5 · 실패 1", summary)
        self.assertIn("가장 느린 Check5 5.0 s, Check4 4.0 s, Check3 3.0 s", summary)
        self.assertNotIn("Check1 1.0", summary)

    def testFailingLintShowsDetailAndPassingLintIsHiddenByDefault(self) -> None:
        import contextlib
        import io
        import tempfile
        from unittest import mock

        with tempfile.TemporaryDirectory() as tempDir:
            root = Path(tempDir)
            (root / "good.py").write_text("print('fine')\n", encoding="utf-8")
            (root / "bad.py").write_text("print('원인 줄 XYZ')\nraise SystemExit(3)\n", encoding="utf-8")
            listTarget = [LintTarget("CheckGood", "good.py", "g", 15, ()), LintTarget("CheckBad", "bad.py", "b", 15, ())]
            for bVerbose in (False, True):
                out, err = io.StringIO(), io.StringIO()
                with mock.patch.object(RunLintSuite, "discoverLintTargets", return_value=listTarget), \
                        contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
                    returnCode = RunLintSuite.main(["--root", str(root)] + (["--verbose"] if bVerbose else []))
                self.assertEqual(returnCode, 1)
                self.assertIn("원인 줄 XYZ", err.getvalue())
                self.assertIn("2개: 통과 1 · 실패 1", out.getvalue())
                self.assertEqual("CheckGood" in out.getvalue().split("[RunLintSuite]")[0], bVerbose)

    def testSummaryCountsSkippedAndUnavailableApartFromPassed(self) -> None:
        # 건너뜀(77)은 통과 칸에 들지 않고 사유별로 센다. 불성립(5)은 실패와 다른 칸이지만 종료 코드를 1 로 만든다(아래 main 테스트).
        listTiming = [RunLintSuite.LintTiming("CheckA", 1.0, 0, 15, ""),
                      RunLintSuite.LintTiming("CheckB", 0.0, ExitCode.Skipped, 15, "", RunLintSuite.kNoBuildDirReason),
                      RunLintSuite.LintTiming("CheckC", 2.0, ExitCode.PrerequisiteMissing, 15, "", "컴파일 DB 없음 — 없다"),
                      RunLintSuite.LintTiming("CheckD", 0.5, 1, 15, "")]
        summary = RunLintSuite.formatSummaryInternal(listTiming, 4.0)
        self.assertIn("4개: 통과 1 · 건너뜀 1(빌드 폴더 없음 1) · 불성립(실패로 센다) 1(컴파일 DB 없음 1) · 실패 1", summary)

    def testSkipAndUnavailableAreNotPassesInTheVerboseLine(self) -> None:
        self.assertIn("SKIPPED (빌드 폴더 없음", RunLintSuite.formatLintLineInternal(
            RunLintSuite.LintTiming("CheckB", 0.0, ExitCode.Skipped, 15, "", RunLintSuite.kNoBuildDirReason)))
        self.assertIn("UNAVAILABLE", RunLintSuite.formatLintLineInternal(RunLintSuite.LintTiming("CheckC", 1.0, ExitCode.PrerequisiteMissing, 15, "")))

    def runMain(self, listScript: dict[str, str], listTarget: list[LintTarget], listArgument: list[str]) -> tuple[int, str, str]:
        import contextlib
        import io
        import tempfile
        from unittest import mock

        with tempfile.TemporaryDirectory() as tempDir:
            root = Path(tempDir)
            for name, text in listScript.items():
                (root / name).write_text(text, encoding="utf-8")
            out, err = io.StringIO(), io.StringIO()
            with mock.patch.object(RunLintSuite, "discoverLintTargets", return_value=listTarget), \
                    contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
                returnCode = RunLintSuite.main(["--root", str(root), *listArgument])
        return returnCode, out.getvalue(), err.getvalue()

    def testUnavailableLintFailsTheSuiteAndShowsItsReason(self) -> None:
        # 필수 입력이 없어 검사를 못 한 린트는 통합에서 실패다 — 훅과 달리 허용하지 않는다.
        listTarget = [LintTarget("CheckGood", "good.py", "g", 15, ()), LintTarget("CheckNoDb", "nodb.py", "n", 15, ())]
        returnCode, out, err = self.runMain({"good.py": "print('fine')\n",
                                             "nodb.py": "import sys\nprint('[CheckNoDb] UNAVAILABLE (컴파일 DB 없음 — 없다)', file=sys.stderr)\nsys.exit(5)\n"},
                                            listTarget, [])
        self.assertEqual(returnCode, 1)
        self.assertIn("2개: 통과 1 · 불성립(실패로 센다) 1(컴파일 DB 없음 1) · 실패 0", out)
        self.assertIn("CheckNoDb 불성립", err)

    def testLintThatSkipsItselfIsCountedAsSkippedAndPasses(self) -> None:
        listTarget = [LintTarget("CheckSkip", "skip.py", "s", 15, ())]
        returnCode, out, _ = self.runMain({"skip.py": "import sys\nprint('[CheckSkip] SKIPPED (병합 커밋 — 같다)')\nsys.exit(77)\n"}, listTarget, [])
        self.assertEqual(returnCode, 0)
        self.assertIn("1개: 통과 0 · 건너뜀 1(병합 커밋 1) · 실패 0", out)

    def testLintNeedingABuildFolderIsCountedAsSkippedWithoutOneAndFailsUnderStrict(self) -> None:
        listTarget = [LintTarget("CheckGood", "good.py", "g", 15, ()),
                      LintTarget("CheckGlob", "glob.py", "x", 15, ("--build-dir", "${CMAKE_BINARY_DIR}"))]
        listScript = {"good.py": "print('fine')\n", "glob.py": "print('돌면 안 된다')\n"}
        returnCode, out, _ = self.runMain(listScript, listTarget, [])
        self.assertEqual(returnCode, 0)
        self.assertIn("2개: 통과 1 · 건너뜀 1(빌드 폴더 없음 1) · 실패 0", out)
        self.assertNotIn("돌면 안 된다", out)
        returnCode, _, err = self.runMain(listScript, listTarget, ["--strict"])
        self.assertEqual(returnCode, 1)
        self.assertIn("CheckGlob 건너뜀(--strict)", err)


if __name__ == "__main__":
    unittest.main()
