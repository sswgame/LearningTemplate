#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""계획 문서 머리 줄 읽기(common.Plans)와 목록 명령(dev/ListPlans)의 판단 — 임시 폴더의 작은 계획 폴더로."""

from __future__ import annotations

import argparse
import contextlib
import io
import sys
import tempfile
import unittest
from pathlib import Path

kScriptsRoot = Path(__file__).resolve().parents[2] / "Scripts"
sys.path.insert(0, str(kScriptsRoot))
sys.path.insert(0, str(kScriptsRoot / "lint"))
sys.path.insert(0, str(kScriptsRoot / "lint" / "gate"))

import CheckDocPaths  # noqa: E402
import CheckStatusBoard  # noqa: E402
from dev import ListPlans  # noqa: E402
from dev import RunStatusBoard  # noqa: E402
from common.MarkdownAnchor import collectAnchors  # noqa: E402
from common.StatusBoard import StatusBoardError, parseRoadmap  # noqa: E402
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
kReadmeText = "# R\n\n소개\n\n## 지금 진행 상황\n\n<!-- status-board:start -->\n<!-- status-board:end -->\n\n끝\n"


def makeRepositoryInternal(root: Path) -> None:
    writePlansInternal(root, {"Vfx.md": kGoodText, "Hold.md": kHoldText, "Ref.md": kRefText, "Candidates-engine.md": kCandidateText,
                              "Roadmap.md": kRoadmapText})
    (root / "README.md").write_text(kReadmeText, encoding="utf-8")


def runBoardInternal(root: str, listArg: list[str]) -> tuple[int, str, str]:
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        code = RunStatusBoard.main(["--root", root, *listArg])
    return code, out.getvalue(), err.getvalue()


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


class StatusBoardTest(unittest.TestCase):
    def testRoadmapEntriesKeepSubBulletsInTheirItem(self) -> None:
        listEntry = parseRoadmap(kRoadmapText)
        self.assertEqual([None, "Vfx", "Ref"], [entry.stem for entry in listEntry])
        self.assertEqual("문서 없는 항목입니다.", listEntry[0].lead)
        with self.assertRaises(StatusBoardError):
            parseRoadmap("# 로드맵\n\n1. [A](A.md)\n")

    def testLongLeadIsTruncated(self) -> None:
        self.assertEqual(80, len(parseRoadmap("## 지금 순서\n\n1. " + "가" * 200 + "\n")[0].lead))

    def testBoardRowsRanksAndCandidateCounts(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            makeRepositoryInternal(Path(temp))
            code, out, _ = runBoardInternal(temp, [])
            self.assertEqual(0, code)
            listLine = out.splitlines()
            self.assertEqual("| 1 | 문서 없는 항목입니다. | | | | | |", listLine[2])
            self.assertEqual("| 2 | [Vfx](docs/plans/Vfx.md) — 이펙트 계획 | 구현중 | 2/5 | Vfx 1단계 | graphics | C-1, D3-2 |", listLine[3])
            self.assertTrue(listLine[4].startswith("| - | [Hold](docs/plans/Hold.md) — 보류 계획 | 보류 |"))
            self.assertEqual(5, len([line for line in listLine if line.startswith("|")]))
            self.assertNotIn("참고 문서", out)
            self.assertNotIn("후보 모음", out)
            self.assertEqual("후보 문서 1개 · 후보 절 2개", listLine[-1])

    def testWriteThenCheckThenStale(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            makeRepositoryInternal(Path(temp))
            readme = Path(temp) / "README.md"
            self.assertEqual(1, runBoardInternal(temp, ["--check", str(readme)])[0])
            self.assertEqual(0, runBoardInternal(temp, ["--write", str(readme)])[0])
            text = readme.read_text(encoding="utf-8")
            self.assertTrue(text.startswith("# R\n\n소개\n") and text.rstrip().endswith("끝"))
            self.assertEqual(0, runBoardInternal(temp, ["--check", str(readme)])[0])
            readme.write_text(text.replace("구현중", "설계"), encoding="utf-8")
            code, _, err = runBoardInternal(temp, ["--check", str(readme)])
            self.assertEqual(1, code)
            self.assertIn("구현중", err)

    def testWriteWithoutMarkersStops(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            makeRepositoryInternal(Path(temp))
            readme = Path(temp) / "README.md"
            readme.write_text("# R\n", encoding="utf-8")
            self.assertEqual(1, runBoardInternal(temp, ["--write", str(readme)])[0])

    def testRoadmapLinkToMissingPlanOrMissingRoadmapStops(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            makeRepositoryInternal(Path(temp))
            roadmap = Path(temp) / "docs" / "plans" / "Roadmap.md"
            roadmap.write_text("# 로드맵\n\n## 지금 순서\n\n1. [없음](Gone.md)\n", encoding="utf-8")
            code, _, err = runBoardInternal(temp, [])
            self.assertEqual(1, code)
            self.assertIn("Gone.md", err)
            roadmap.unlink()
            self.assertEqual(1, runBoardInternal(temp, [])[0])

    def testGateFlagsStaleReadmeAndAcceptsFresh(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            makeRepositoryInternal(Path(temp))
            gate = CheckStatusBoard.CheckStatusBoardGate()
            self.assertIn("README.md: 표가 낡았습니다", gate.scan(Path(temp), argparse.Namespace()).listViolation)
            runBoardInternal(temp, ["--write", str(Path(temp) / "README.md")])
            self.assertEqual([], gate.scan(Path(temp), argparse.Namespace()).listViolation)


class PlansCheckAndFindTest(unittest.TestCase):
    def testCandidateAnchorsAreSharedWithTheDocPathsGate(self) -> None:
        setAnchor = collectAnchors("## 지금 순서\n### Wayland\n제목: 창 지원\n## 두 낱말 제목\n")
        self.assertTrue({"지금-순서", "wayland", "두-낱말-제목"} <= setAnchor)
        self.assertIs(collectAnchors, CheckDocPaths.collectAnchors)

    def testCheckAcceptsGoodRoadmapLinks(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            makeRepositoryInternal(Path(temp))
            roadmap = Path(temp) / "docs" / "plans" / "Roadmap.md"
            roadmap.write_text("# 로드맵\n\n## 지금 순서\n\n1. [이펙트](Vfx.md#1-배경), 후보 [W](Candidates-engine.md#wayland), 내 [절](#지금-순서), [웹](https://x.y/z).\n",
                               encoding="utf-8")
            code, out, _ = runPlansInternal(temp, ["--check"])
            self.assertEqual(0, code)
            self.assertIn("로드맵 위반 0 건", out)

    def testCheckReportsMissingDocFileAndAnchorWithLine(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            makeRepositoryInternal(Path(temp))
            roadmap = Path(temp) / "docs" / "plans" / "Roadmap.md"
            roadmap.write_text("# 로드맵\n\n## 지금 순서\n\n1. [없는](Gone.md)\n2. [절](Vfx.md#없는-제목)\n3. [파일](../Nope.md)\n", encoding="utf-8")
            code, out, _ = runPlansInternal(temp, ["--check"])
            self.assertEqual(1, code)
            self.assertIn("Roadmap.md:5: 항목의 문서 `Gone.md`", out)
            self.assertIn("Roadmap.md:6: 링크 `Vfx.md#없는-제목` 의 제목(앵커)", out)
            self.assertIn("Roadmap.md:7: 링크 `../Nope.md` 의 파일이 없습니다", out)
            self.assertIn("로드맵 위반 4 건", out)

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
