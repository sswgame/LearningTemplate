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

from dev import ListPlans  # noqa: E402
from common.Plans import PlanFormatError, kListPlanStatus, parsePlanText, readPlans  # noqa: E402

kGoodText = (
    "# 이펙트 계획\n\n"
    "> 상태: 구현중 · 선행: Vfx 1단계 · 영역: graphics · 옛번호: C-1,D3-2 · 진행: 2/5\n\n"
    "## 1. 배경\n\n```\n# 코드 안의 줄\n```\n\n### 1.1 세부\n"
)
kHoldText = "# 보류 계획\n\n> 상태: 보류 · 선행: 없음 · 영역: editor · 옛번호: 없음 · 진행: 해당없음\n"
kRefText = "# 참고 문서\n\n> 상태: 참고 · 선행: 없음 · 영역: engine · 옛번호: 없음 · 진행: 해당없음\n"


def writePlansInternal(root: Path, mapFile: dict[str, str]) -> None:
    folder = root / "docs" / "plans"
    folder.mkdir(parents=True, exist_ok=True)
    for name, text in mapFile.items():
        (folder / name).write_text(text, encoding="utf-8")


class ParsePlanTest(unittest.TestCase):
    def testGoodHeaderIsRead(self) -> None:
        plan = parsePlanText(kGoodText, "Vfx.md")
        self.assertEqual(("이펙트 계획", "구현중", "graphics", "Vfx 1단계", ["C-1", "D3-2"], "2/5"),
                         (plan.title, plan.status, plan.area, plan.prerequisite, plan.listOldNumber, plan.progress))
        self.assertEqual(["# 이펙트 계획", "## 1. 배경", "### 1.1 세부"], plan.listHeading)

    def testEightStatusesAllowed(self) -> None:
        self.assertEqual(8, len(kListPlanStatus))
        self.assertEqual("보류", parsePlanText(kHoldText, "A.md").status)
        self.assertEqual([], parsePlanText(kRefText, "B.md").listOldNumber)

    def testMissingHeaderStopsWithFileName(self) -> None:
        with self.assertRaisesRegex(PlanFormatError, "Bad.md"):
            parsePlanText("# 제목\n\n본문만 있다.\n", "Bad.md")

    def testWrongStatusOrAreaOrOldNumberStops(self) -> None:
        for text in ("> 상태: 완료 · 선행: 없음 · 영역: editor · 옛번호: 없음 · 진행: 해당없음",
                     "> 상태: 끝 · 선행: 없음 · 영역: foo · 옛번호: 없음 · 진행: 해당없음",
                     "> 상태: 끝 · 선행: 없음 · 영역: editor · 옛번호: foo · 진행: 해당없음"):
            with self.assertRaisesRegex(PlanFormatError, "Wrong.md"):
                parsePlanText(f"# 제목\n\n{text}\n", "Wrong.md")

    def testBadProgressStops(self) -> None:
        for progress in ("3/2", "0/0", "2", "a/b", "해당 없음"):
            with self.assertRaisesRegex(PlanFormatError, "P.md"):
                parsePlanText(f"# 제목\n\n> 상태: 끝 · 선행: 없음 · 영역: editor · 옛번호: 없음 · 진행: {progress}\n", "P.md")

    def testTableProgressIsCountedFromStepTable(self) -> None:
        header = "# 표 계획\n\n> 상태: 구현중 · 선행: 없음 · 영역: engine · 옛번호: 없음 · 진행: 표\n\n"
        table = "| 단계 | 내용 | 상태 |\n|---|---|---|\n| 0 | a | 아직 안 함 |\n| 1 | b | 끝남(`abc`) |\n| 2 | c | 끝남 |\n\n본문\n| x | y |\n"
        self.assertEqual("2/3", parsePlanText(header + table, "T.md").progress)
        with self.assertRaisesRegex(PlanFormatError, "T.md"):
            parsePlanText(header + "표 없음\n", "T.md")

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


kCandidateText = (
    "# 후보 모음\n\n> 상태: 후보 · 선행: 없음 · 영역: engine · 옛번호: 없음 · 진행: 해당없음\n\n"
    "### Wayland\n제목: Linux 창 계층의 Wayland 지원\n옛번호: C-30, D2-13\n\n본문.\n\n"
    "### Plain\n제목: 번호 없는 후보\n옛번호: 없음\n"
)
kRoadmapText = (
    "# 로드맵\n\n## 배경\n\n1. [무시](Hold.md) 다른 절의 번호는 순서가 아니다.\n\n## 지금 순서\n\n"
    "1. 문서 없는 항목입니다.\n   - 하위 불릿은 이 항목에 속한다.\n"
    "2. **[이펙트](Vfx.md)** 를 먼저, [결정](../09_Decisions.md) 참고.\n   - 이어지는 줄에 [보류](Hold.md) 가 있어도 첫 링크가 문서다.\n   1. 하위 번호 목록도 같은 항목이다.\n"
    "3. [참고](Ref.md) 는 표에서 빠진다.\n\n## 다음\n\n4. [보류](Hold.md)\n"
)


def makeRepositoryInternal(root: Path) -> None:
    writePlansInternal(root, {"Vfx.md": kGoodText, "Hold.md": kHoldText, "Ref.md": kRefText, "Candidates-engine.md": kCandidateText,
                              "Roadmap.md": kRoadmapText})
    (root / "docs" / "09_Decisions.md").write_text("# 결정\n", encoding="utf-8")  # 로드맵이 링크하는 계획 밖 문서 — 링크 점검이 있는 파일로 본다


def runPlansInternal(root: str, listArg: list[str]) -> tuple[int, str, str]:
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        code = ListPlans.main(["--root", root, *listArg])
    return code, out.getvalue(), err.getvalue()


class CandidateTest(unittest.TestCase):
    def testCandidateSectionsAreRead(self) -> None:
        plan = parsePlanText(kCandidateText, "Candidates-engine.md")
        self.assertEqual([("Wayland", "Linux 창 계층의 Wayland 지원", ["C-30", "D2-13"]), ("Plain", "번호 없는 후보", [])],
                         [(c.name, c.title, c.listOldNumber) for c in plan.listCandidate])

    def testCandidateNeedsZeroProgressNameAndOldNumberLine(self) -> None:
        header = "# 후보\n\n> 상태: 후보 · 선행: 없음 · 영역: engine · 옛번호: 없음 · 진행: 해당없음\n\n"
        with self.assertRaisesRegex(PlanFormatError, "C.md"):
            parsePlanText(kCandidateText.replace("해당없음", "1/2"), "C.md")
        with self.assertRaisesRegex(PlanFormatError, "C.md:5"):
            parsePlanText(header + "### 한글이름\n제목: 제목\n옛번호: 없음\n", "C.md")
        with self.assertRaisesRegex(PlanFormatError, "C.md:5"):
            parsePlanText(header + "### Name — 제목\n옛번호: 없음\n", "C.md")
        with self.assertRaisesRegex(PlanFormatError, "C.md:5"):
            parsePlanText(header + "### Name\n옛번호: 없음\n", "C.md")
        with self.assertRaisesRegex(PlanFormatError, "C.md:5"):
            parsePlanText(header + "### Name\n제목: 제목\n본문만.\n", "C.md")
        with self.assertRaisesRegex(PlanFormatError, "C.md:5"):
            parsePlanText(header + "### Name\n제목: \n옛번호: 없음\n", "C.md")


class PlansFindTest(unittest.TestCase):
    def testFindOldNumberNameUnitAndUnknown(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            makeRepositoryInternal(Path(temp))
            self.assertIn("Candidates-engine.md:5 (후보 절 `Wayland`", runPlansInternal(temp, ["--find", "c-30"])[1])
            self.assertIn("docs/plans/Vfx.md (머리 줄 옛번호)", runPlansInternal(temp, ["--find", "D3-2"])[1])
            self.assertIn("Candidates-engine.md:5", runPlansInternal(temp, ["--find", "Wayland"])[1])
            self.assertIn("docs/plans/Vfx.md\n", runPlansInternal(temp, ["--find", "Vfx"])[1])
            self.assertIn("단위 `V2`", runPlansInternal(temp, ["--find", "Vfx.V2"])[1])
            self.assertEqual(1, runPlansInternal(temp, ["--find", "Nothing"])[0])


if __name__ == "__main__":
    unittest.main()
