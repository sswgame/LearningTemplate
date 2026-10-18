#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""dev/RunAgentLog 의 판단 — 표 마지막 행 뒤에 넣고 줄 끝을 보존하며, 표가 없거나 값이 틀리면 쓰지 않는다. 그리고 plans --section(dev/ListPlans.extractSection)."""

from __future__ import annotations

import contextlib
import datetime
import io
import sys
import tempfile
import unittest
from pathlib import Path

kScriptsRoot = Path(__file__).resolve().parents[2] / "Scripts"
sys.path.insert(0, str(kScriptsRoot))
sys.path.insert(0, str(kScriptsRoot / "dev"))

import RunAgentLog  # noqa: E402
import ListPlans  # noqa: E402

kTable = "머리말\n\n| 날짜 | 일 | 조각 | 에이전트(모델 · effort) | 토큰 | 도구 호출 | 비고 |\n|---|---|---|---|---|---|---|\n| 2026-10-19 | 가 | 나 | w | 약 1만 | 3 | x |\n\n뒷글\n"
kArgs = ["--task", "일", "--piece", "조각", "--agent", "worker(sonnet, medium)", "--tokens", "약 16만", "--calls", "65", "--note", "비고"]


class AgentLogTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tempDir = tempfile.TemporaryDirectory()
        self.addCleanup(self.tempDir.cleanup)
        self.root = Path(self.tempDir.name)
        (self.root / "docs").mkdir()
        self.doc = self.root / "docs" / "12_Agents.md"

    def runMainInternal(self, listArg: list[str]) -> tuple[int, str]:
        err = io.StringIO()
        with contextlib.redirect_stderr(err), contextlib.redirect_stdout(io.StringIO()):
            code = RunAgentLog.main([*listArg, "--root", str(self.root)], today=datetime.date(2026, 10, 20))
        return code, err.getvalue()

    def testInsertsAfterLastRowKeepingCrlf(self) -> None:
        self.doc.write_bytes(kTable.replace("\n", "\r\n").encode("utf-8"))
        self.assertEqual(self.runMainInternal(kArgs)[0], 0)
        expected = kTable.replace("| x |\n", "| x |\n| 2026-10-20 | 일 | 조각 | worker(sonnet, medium) | 약 16만 | 65 | 비고 |\n").replace("\n", "\r\n")
        self.assertEqual(self.doc.read_bytes(), expected.encode("utf-8"))

    def testKeepsLf(self) -> None:
        self.doc.write_bytes(kTable.encode("utf-8"))
        self.assertEqual(self.runMainInternal(kArgs)[0], 0)
        self.assertNotIn(b"\r", self.doc.read_bytes())

    def testTableMissingOrBadValueWritesNothing(self) -> None:
        self.doc.write_bytes(b"no table\n")
        code, err = self.runMainInternal(kArgs)
        self.assertEqual(code, 1)
        self.assertIn("표 머리 줄", err)
        self.assertEqual(self.doc.read_bytes(), b"no table\n")
        self.doc.write_bytes(kTable.encode("utf-8"))
        badTokens = [*kArgs]
        badTokens[badTokens.index("--tokens") + 1] = "많이"
        self.assertEqual(self.runMainInternal(badTokens)[0], 1)
        badPipe = [*kArgs]
        badPipe[badPipe.index("--note") + 1] = "a|b"
        self.assertEqual(self.runMainInternal(badPipe)[0], 1)
        self.assertEqual(self.doc.read_bytes(), kTable.encode("utf-8"))


class ExtractSectionTest(unittest.TestCase):
    kLines = ["# 제목", "", "## 1. 가", "본문 1", "```", "## 울타리 안", "```", "### 1.1 세부", "세부 본문", "## 2. 나", "본문 2", "## 3. 나머지 나"]

    def testReturnsBodyUntilNextSameLevelIncludingSubsection(self) -> None:
        self.assertEqual(ListPlans.extractSection(self.kLines, "1. 가"), self.kLines[2:9])
        self.assertEqual(ListPlans.extractSection(self.kLines, "세부"), ["### 1.1 세부", "세부 본문"])

    def testNoMatchOrManyMatchesListCandidates(self) -> None:
        with self.assertRaises(ValueError) as none:
            ListPlans.extractSection(self.kLines, "없는 절")
        self.assertIn("## 2. 나", str(none.exception))
        with self.assertRaises(ValueError) as many:
            ListPlans.extractSection(self.kLines, "나")
        self.assertIn("여럿", str(many.exception))
        self.assertNotIn("## 1. 가", str(many.exception))
        with self.assertRaises(ValueError):
            ListPlans.extractSection(self.kLines, "울타리")  # 울타리 안의 # 줄은 제목이 아니다


if __name__ == "__main__":
    unittest.main()
