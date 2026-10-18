#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""계획 문서 머리 줄 읽기(common.Plans)와 목록 명령(dev/ListPlans)의 판단 — 임시 폴더의 작은 계획 폴더로."""

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

import ListPlans  # noqa: E402
from common.Plans import PlanFormatError, kListPlanStatus, parsePlanText, readPlans  # noqa: E402

kGoodText = (
    "# 이펙트 계획\n\n"
    "> 상태: 구현중 · 선행: Vfx 1단계 · 영역: graphics · 백로그: C-1,D3-2\n\n"
    "## 1. 배경\n\n```\n# 코드 안의 줄\n```\n\n### 1.1 세부\n"
)
kHoldText = "# 보류 계획\n\n> 상태: 보류 · 선행: 없음 · 영역: editor · 백로그: 없음\n"
kRefText = "# 참고 문서\n\n> 상태: 참고 · 선행: 없음 · 영역: engine · 백로그: 없음\n"


def writePlansInternal(root: Path, mapFile: dict[str, str]) -> None:
    folder = root / "docs" / "plans"
    folder.mkdir(parents=True, exist_ok=True)
    for name, text in mapFile.items():
        (folder / name).write_text(text, encoding="utf-8")


class ParsePlanTest(unittest.TestCase):
    def testGoodHeaderIsRead(self) -> None:
        plan = parsePlanText(kGoodText, "Vfx.md")
        self.assertEqual(("이펙트 계획", "구현중", "graphics", "Vfx 1단계", ["C-1", "D3-2"]),
                         (plan.title, plan.status, plan.area, plan.prerequisite, plan.listBacklogId))
        self.assertEqual(["# 이펙트 계획", "## 1. 배경", "### 1.1 세부"], plan.listHeading)

    def testSevenStatusesAllowed(self) -> None:
        self.assertEqual(7, len(kListPlanStatus))
        self.assertEqual("보류", parsePlanText(kHoldText, "A.md").status)
        self.assertEqual([], parsePlanText(kRefText, "B.md").listBacklogId)

    def testMissingHeaderStopsWithFileName(self) -> None:
        with self.assertRaisesRegex(PlanFormatError, "Bad.md"):
            parsePlanText("# 제목\n\n본문만 있다.\n", "Bad.md")

    def testWrongStatusOrAreaOrBacklogStops(self) -> None:
        for text in ("> 상태: 완료 · 선행: 없음 · 영역: editor · 백로그: 없음",
                     "> 상태: 끝 · 선행: 없음 · 영역: foo · 백로그: 없음",
                     "> 상태: 끝 · 선행: 없음 · 영역: editor · 백로그: C-1, D-2"):
            with self.assertRaisesRegex(PlanFormatError, "Wrong.md"):
                parsePlanText(f"# 제목\n\n{text}\n", "Wrong.md")

    def testNonDocumentFilesAreSkipped(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            writePlansInternal(Path(temp), {"README.md": "머리 줄 없음\n", "NEXT.md": "x\n", "WorkerRules.md": "x\n", "Roadmap.md": "x\n", "Vfx.md": kGoodText})
            self.assertEqual(["Vfx.md"], [plan.fileName for plan in readPlans(Path(temp))])


class ListPlansTest(unittest.TestCase):
    def runInternal(self, mapFile: dict[str, str], listArg: list[str]) -> tuple[int, str, str]:
        with tempfile.TemporaryDirectory() as temp:
            writePlansInternal(Path(temp), mapFile)
            out, err = io.StringIO(), io.StringIO()
            with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
                code = ListPlans.main(["--root", temp, *listArg])
            return code, out.getvalue(), err.getvalue()

    def testTableAndFilters(self) -> None:
        mapFile = {"Vfx.md": kGoodText, "Hold.md": kHoldText, "Ref.md": kRefText}
        code, out, _ = self.runInternal(mapFile, [])
        self.assertEqual(0, code)
        self.assertIn("3 건", out)
        self.assertIn("C-1,D3-2", out)
        self.assertIn("1 건", self.runInternal(mapFile, ["--status", "보류"])[1])
        self.assertIn("1 건", self.runInternal(mapFile, ["--area", "graphics"])[1])

    def testFileShowsHeaderAndHeadings(self) -> None:
        code, out, _ = self.runInternal({"Vfx.md": kGoodText}, ["--file", "Vfx"])
        self.assertEqual(0, code)
        self.assertIn("> 상태: 구현중", out)
        self.assertIn("### 1.1 세부", out)

    def testBadDocumentStopsWholeCommand(self) -> None:
        code, _, err = self.runInternal({"Vfx.md": kGoodText, "Bad.md": "# 제목\n\n본문\n"}, [])
        self.assertEqual(1, code)
        self.assertIn("Bad.md", err)


if __name__ == "__main__":
    unittest.main()
