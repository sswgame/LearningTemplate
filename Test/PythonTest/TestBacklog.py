#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""영역별 백로그 읽기(common.Backlog) · 목록 명령(dev/ListBacklog) · 게이트(CheckBacklog)의 판단 — 임시 폴더의 작은 백로그로."""

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
sys.path.insert(0, str(kScriptsRoot / "dev"))
sys.path.insert(0, str(kScriptsRoot / "lint"))
sys.path.insert(0, str(kScriptsRoot / "lint" / "gate"))

import CheckBacklog  # noqa: E402
import ListBacklog  # noqa: E402
from common.Backlog import parseBacklogText, readBacklog  # noqa: E402

kEditorText = (
    "# 백로그: 에디터\n\n"
    "- `B-3` [검증대기] **에디터 검증.** 첫 줄.\n"
    "  둘째 줄.\n\n"
    "- `D3-1` [기능] **패널 로드맵.**\n\n"
    "---\n\n- 항목이 아닌 목록\n"
)
kEngineText = "# 백로그: 엔진\n\n- `C-1` [정리] **엔진 분할.**\n\n- `G-1` [조건부] **렌더 패스 등록.**\n"


def writeRepositoryInternal(root: Path, mapFile: dict[str, str]) -> None:
    for relPath, text in mapFile.items():
        path = root / relPath
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")


class ParseTest(unittest.TestCase):
    def testItemKeepsIndentedLinesAndStopsAtBlankLine(self) -> None:
        listItem, listProblem = parseBacklogText(kEditorText, "editor")
        self.assertEqual([], listProblem)
        self.assertEqual(["B-3", "D3-1"], [item.itemId for item in listItem])
        self.assertEqual("- `B-3` [검증대기] **에디터 검증.** 첫 줄.\n  둘째 줄.", listItem[0].text)
        self.assertEqual(("검증대기", "에디터 검증.", 3), (listItem[0].status, listItem[0].title, listItem[0].lineNumber))

    def testMissingOrUnknownStatusIsAProblem(self) -> None:
        _, listProblem = parseBacklogText("- `C-1` **태그 없음.**\n- `C-2` [완료] **틀린 태그.**\n", "x")
        self.assertEqual(2, len(listProblem))
        self.assertIn("`C-1`", listProblem[0])
        self.assertIn("완료", listProblem[1])

    def testReadmeIsNotAnItemFile(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            writeRepositoryInternal(root, {"docs/backlog/README.md": "- `C-9` [정리] **안내 속 예시.**\n",
                                           "docs/backlog/engine.md": kEngineText})
            listItem, _ = readBacklog(root)
            self.assertEqual(["C-1", "G-1"], [item.itemId for item in listItem])


class ListBacklogTest(unittest.TestCase):
    def setUp(self) -> None:
        self.listItem = parseBacklogText(kEditorText, "editor")[0] + parseBacklogText(kEngineText, "engine")[0]

    def testCountTableHasEveryStatusAndTotals(self) -> None:
        listLine = ListBacklog.formatCountTable(self.listItem).splitlines()
        self.assertEqual(1 + 8 + 1, len(listLine))
        self.assertEqual(["결함", "0", "0", "0"], listLine[1].split())
        self.assertEqual(["합계", "2", "2", "4"], listLine[-1].split())

    def testSelectFiltersAndOrdersByStatus(self) -> None:
        self.assertEqual(["D3-1"], [item.itemId for item in ListBacklog.selectItems(self.listItem, "기능", None)])
        self.assertEqual(["C-1", "G-1"], [item.itemId for item in ListBacklog.selectItems(self.listItem, None, "engine")])
        self.assertEqual(["B-3", "C-1", "D3-1", "G-1"], [item.itemId for item in ListBacklog.selectItems(self.listItem, None, None)])

    def testIdPrintsOnlyThatItem(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            writeRepositoryInternal(root, {"docs/backlog/editor.md": kEditorText})
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                self.assertEqual(0, ListBacklog.main(["--root", temp, "--id", "b-3"]))
            self.assertEqual("<!-- docs/backlog/editor.md:3 -->\n- `B-3` [검증대기] **에디터 검증.** 첫 줄.\n  둘째 줄.\n", output.getvalue())
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(1, ListBacklog.main(["--root", temp, "--id", "Z-1"]))


class CheckBacklogTest(unittest.TestCase):
    def scan(self, mapFile: dict[str, str]) -> list[str]:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            writeRepositoryInternal(root, mapFile)
            return CheckBacklog.CheckBacklogGate().scan(root, argparse.Namespace()).listViolation

    def testCleanBacklogAndCorrectReferencesPass(self) -> None:
        self.assertEqual([], self.scan({
            "docs/backlog/editor.md": kEditorText,
            "docs/backlog/engine.md": kEngineText,
            "docs/Guide.md": "남은 일은 [백로그](backlog/editor.md) `D3-1` 과 `docs/backlog/engine.md` 의 `G-1` 입니다.\n",
            "Source/X.h": "// docs/backlog/editor.md#백로그-에디터 참고\n",
        }))

    def testDuplicateIdAcrossFiles(self) -> None:
        listViolation = self.scan({"docs/backlog/editor.md": kEditorText, "docs/backlog/engine.md": kEngineText + "\n- `B-3` [정리] **겹침.**\n"})
        self.assertEqual(1, len(listViolation))
        self.assertIn("`B-3`", listViolation[0])

    def testReferenceToItemInAnotherFile(self) -> None:
        listViolation = self.scan({"docs/backlog/editor.md": kEditorText, "docs/backlog/engine.md": kEngineText,
                                   "docs/Guide.md": "[백로그](backlog/engine.md) `D3-1`\n"})
        self.assertEqual(1, len(listViolation))
        self.assertIn("backlog/editor.md", listViolation[0])

    def testReferenceToMissingFileOrHeading(self) -> None:
        listViolation = self.scan({"docs/backlog/editor.md": kEditorText,
                                   "Source/X.h": "// docs/backlog/gone.md\n// docs/backlog/editor.md#없는-제목\n"})
        self.assertEqual(2, len(listViolation))


if __name__ == "__main__":
    unittest.main()
