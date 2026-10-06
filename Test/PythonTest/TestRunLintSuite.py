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


if __name__ == "__main__":
    unittest.main()
