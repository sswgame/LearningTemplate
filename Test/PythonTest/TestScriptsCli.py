#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""`py -3 -m Scripts` — 이름 있는 명령은 표, 린트 · 보고서는 lint/ 의 폴더에서 이름으로(gate · fix · report · selftest)."""

from __future__ import annotations

import os
import subprocess
import sys
import unittest
from pathlib import Path

kRepositoryRoot = Path(__file__).resolve().parents[2]


def runCli(*listArgument: str) -> subprocess.CompletedProcess:
    """저장소 루트에서 `python -m Scripts …` — `-m Scripts` 는 그 자리에서만 풀린다."""
    environment = dict(os.environ, PYTHONIOENCODING="utf-8")
    return subprocess.run([sys.executable, "-m", "Scripts", *listArgument], cwd=kRepositoryRoot, capture_output=True,
                          text=True, encoding="utf-8", errors="replace", env=environment, timeout=120)


class ScriptsCliTest(unittest.TestCase):
    def testHelpListsNamedAndFolderCommands(self) -> None:
        result = runCli("--help")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("golden", result.stdout)
        self.assertIn("gate|fix|report|selftest", result.stdout)

    def testFolderCommandWithoutNameListsTheFolder(self) -> None:
        result = runCli("gate")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("CheckEngineLayers", result.stdout)

    def testUnknownNameFails(self) -> None:
        result = runCli("gate", "NoSuchGate")
        self.assertEqual(result.returncode, 1)
        self.assertIn("NoSuchGate", result.stderr)

    def testReportRunsByName(self) -> None:
        result = runCli("report", "RunBuildScriptInventory", "--help")
        self.assertEqual(result.returncode, 0, result.stderr)

    def testFixerRunsByName(self) -> None:
        result = runCli("fix", "FormatIncludeOrder", "--help")
        self.assertEqual(result.returncode, 0, result.stderr)

    def testNamedCommandForwardsArguments(self) -> None:
        # 표의 명령도 남은 인자를 main(argv) 로 받는다 — 인자를 못 받던 lint · defender 도 --help 가 뜬다.
        for command in ("test", "lint", "defender"):
            with self.subTest(command=command):
                result = runCli(command, "--help")
                self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
