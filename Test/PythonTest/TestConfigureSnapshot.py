#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ConfigureSnapshot 의 diff · profile 이 무엇을 다르다고 하는지(빌드 폴더 없이 — 스냅숏 JSON 을 직접 만든다)."""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Scripts"))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Scripts" / "dev"))

import ConfigureSnapshot  # noqa: E402


def makeSnapshotInternal() -> dict:
    return {
        "configuration": "Debug",
        "cache": {"SW_ENABLE_PCH": "ON"},
        "generated": {"generated/sw/config/ConfigVars.cmake": "aa"},
        "targets": {"Engine": {"type": "SHARED_LIBRARY", "link": ["a.lib", "b.lib"],
                               "compileGroups": {"CXX:x.cpp": {"defines": ["A", "B"], "sources": ["x.cpp"]}}}},
    }


class ConfigureSnapshotTest(unittest.TestCase):
    def diffInternal(self, before: dict, after: dict) -> list[str]:
        with tempfile.TemporaryDirectory() as folder:
            beforePath, afterPath = Path(folder) / "a.json", Path(folder) / "b.json"
            beforePath.write_text(json.dumps(before), encoding="utf-8")
            afterPath.write_text(json.dumps(after), encoding="utf-8")
            return ConfigureSnapshot.diffSnapshots(beforePath, afterPath)

    def testSameSnapshotHasNoDifference(self) -> None:
        self.assertEqual(self.diffInternal(makeSnapshotInternal(), makeSnapshotInternal()), [])

    def testDefineOrderDoesNotCountButValueDoes(self) -> None:
        after = makeSnapshotInternal()
        after["targets"]["Engine"]["compileGroups"]["CXX:x.cpp"]["defines"] = ["B", "C"]
        listLine = self.diffInternal(makeSnapshotInternal(), after)
        self.assertIn("- /targets/Engine/compileGroups/CXX:x.cpp/defines: A", listLine)
        self.assertIn("+ /targets/Engine/compileGroups/CXX:x.cpp/defines: C", listLine)

    def testLinkOrderIsReported(self) -> None:
        after = makeSnapshotInternal()
        after["targets"]["Engine"]["link"] = ["b.lib", "a.lib"]
        self.assertEqual(self.diffInternal(makeSnapshotInternal(), after), ["~ /targets/Engine/link: 같은 원소, 순서가 다르다"])

    def testMissingTargetIsReported(self) -> None:
        after = makeSnapshotInternal()
        after["targets"]["RHI_GL"] = {"type": "MODULE_LIBRARY"}
        self.assertEqual(self.diffInternal(makeSnapshotInternal(), after), ["+ /targets/RHI_GL"])

    def testProfileSumsByLocation(self) -> None:
        listEvent = [{"ph": "X", "name": "execute_process", "dur": 2000, "args": {"location": "a.cmake:3"}},
                     {"ph": "X", "name": "execute_process", "dur": 1000, "args": {"location": "a.cmake:3"}},
                     {"ph": "X", "name": "file", "dur": 500, "args": {"location": "b.cmake:9"}}]
        with tempfile.TemporaryDirectory() as folder:
            tracePath = Path(folder) / "trace.json"
            tracePath.write_text(json.dumps(listEvent), encoding="utf-8")
            listLine = ConfigureSnapshot.summarizeProfile(tracePath, 5)
        self.assertTrue(listLine[0].strip().startswith("3.0 ms"))
        self.assertIn("x2", listLine[0])


if __name__ == "__main__":
    unittest.main()
