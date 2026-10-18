#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""common/MarkdownSections(절 자르기 · 목차)와 dev/RunDoc(`doc`) — 번호 맞춤, 울타리 안 #, 후보 목록, 문서 이름 풀기."""

from __future__ import annotations

import contextlib
import io
import sys
import tempfile
import unittest
from pathlib import Path

kScriptsRoot = Path(__file__).resolve().parents[2] / "Scripts"
sys.path.insert(0, str(kScriptsRoot))
sys.path.insert(0, str(kScriptsRoot / "dev"))

import RunDoc  # noqa: E402
from common import MarkdownSections  # noqa: E402

kLines = ["# 제목", "## 5. 영역", "### 5-4. 데이터", "본문", "```", "# 울타리 안", "```", "#### 5-4.1 세부", "### 5-40. 다른", "### 15-4. 또 다른", "### 5-4a 가나"]


class SectionTest(unittest.TestCase):
    def testNumberMatchesOnlyTitleHead(self) -> None:
        self.assertEqual(MarkdownSections.extractSection(kLines, "5-4"), kLines[2:8])  # 5-40 · 15-4 는 맞지 않고 하위 절은 든다

    def testOutlineHasLineNumberAndBytes(self) -> None:
        listRow = MarkdownSections.formatOutline(kLines).splitlines()
        self.assertEqual(len(listRow), 7)  # 울타리 안은 제목이 아니다
        self.assertIn("### 5-4. 데이터", listRow[2])
        self.assertTrue(listRow[2].lstrip().startswith("3 "))

    def testNoMatchListsCandidates(self) -> None:
        with self.assertRaises(ValueError) as error:
            MarkdownSections.extractSection(kLines, "없음")
        self.assertIn("### 5-4. 데이터", str(error.exception))


class RunDocTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tempDir = tempfile.TemporaryDirectory()
        self.addCleanup(self.tempDir.cleanup)
        self.root = Path(self.tempDir.name)
        (self.root / "docs").mkdir()
        (self.root / "docs" / "09_Decisions.md").write_text("\n".join(kLines) + "\n", encoding="utf-8")

    def runInternal(self, listArg: list[str]) -> tuple[int, str, str]:
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = RunDoc.main([*listArg, "--root", str(self.root)])
        return code, out.getvalue(), err.getvalue()

    def testOutlineByNameAndPath(self) -> None:
        for name in ("09_Decisions", "09_Decisions.md", "docs/09_Decisions.md"):
            code, out, _ = self.runInternal([name])
            self.assertEqual(code, 0)
            self.assertIn("### 5-4. 데이터", out)

    def testSectionByNumber(self) -> None:
        code, out, _ = self.runInternal(["09_Decisions", "--section", "5-4"])
        self.assertEqual(code, 0)
        self.assertIn("#### 5-4.1 세부", out)
        self.assertNotIn("5-40", out)

    def testMissingDocOrSectionExitsOne(self) -> None:
        self.assertEqual(self.runInternal(["없는문서"])[0], 1)
        code, _, err = self.runInternal(["09_Decisions", "--section", "5-"])
        self.assertEqual(code, 1)
        self.assertIn("여럿", err)


if __name__ == "__main__":
    unittest.main()
