#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""common.Config — JSON 읽기(못 읽으면 빈 사전 + 한 줄), 깊은 병합, `${key}` 자기 참조 풀기. 지금 정의 그대로의 동작을 고정한다."""

from __future__ import annotations

import contextlib
import io
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Scripts"))

from common.Config import expandSelfReferencesInternal, mergeJsonDictInternal, readJsonDictInternal  # noqa: E402


class CommonConfigTest(unittest.TestCase):
    def testMissingFileIsEmptyAndSilent(self) -> None:
        stream = io.StringIO()
        with contextlib.redirect_stderr(stream):
            self.assertEqual(readJsonDictInternal(Path("no/such/file.json"), "없는 파일"), {})
        self.assertEqual(stream.getvalue(), "")

    def testBrokenJsonIsEmptyWithOneLine(self) -> None:
        with tempfile.TemporaryDirectory() as tempDir:
            path = Path(tempDir) / "broken.json"
            path.write_text("{ not json", encoding="utf-8")
            stream = io.StringIO()
            with contextlib.redirect_stderr(stream):
                self.assertEqual(readJsonDictInternal(path, "broken.json"), {})
            self.assertIn("[Config] Failed to read broken.json", stream.getvalue())

    def testNonObjectJsonIsEmpty(self) -> None:
        with tempfile.TemporaryDirectory() as tempDir:
            path = Path(tempDir) / "list.json"
            path.write_text("[1, 2]", encoding="utf-8")
            self.assertEqual(readJsonDictInternal(path, "list.json"), {})

    def testMergeIsDeepForDictsAndReplacesEverythingElse(self) -> None:
        base = {"a": {"x": 1, "y": 2}, "list": [1, 2], "keep": True}
        override = {"a": {"y": 3, "z": 4}, "list": [9], "new": "n"}
        self.assertEqual(mergeJsonDictInternal(base, override),
                         {"a": {"x": 1, "y": 3, "z": 4}, "list": [9], "keep": True, "new": "n"})
        self.assertEqual(base["a"], {"x": 1, "y": 2})   # 원본은 그대로

    def testSelfReferencesUseTopLevelScalars(self) -> None:
        config = {"version": "20.1.8", "url": "llvmorg-${version}/clang-${version}", "nested": {"list": ["${version}"]},
                  "path": "${sourceDir}/x"}
        expanded = expandSelfReferencesInternal(config)
        self.assertEqual(expanded["url"], "llvmorg-20.1.8/clang-20.1.8")
        self.assertEqual(expanded["nested"], {"list": ["20.1.8"]})
        self.assertEqual(expanded["path"], "${sourceDir}/x")   # 여기서 모르는 이름은 건드리지 않는다


if __name__ == "__main__":
    unittest.main()
