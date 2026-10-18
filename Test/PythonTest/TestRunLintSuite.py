#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""RunLintSuite 의 인자 풀기 — CMake 변수 참조와 빌드 폴더가 필요한 린트를 건너뛰는 규칙."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

kRepositoryRoot = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(kRepositoryRoot / "Scripts" / "lint"))
sys.path.insert(0, str(kRepositoryRoot / "Scripts"))

import RunLintSuite  # noqa: E402
from LintCatalog import LintTarget  # noqa: E402


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


if __name__ == "__main__":
    unittest.main()
