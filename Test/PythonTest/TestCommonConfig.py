#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""common.Config — 필수 · 선택 설정 읽기(없거나 깨지면 멈춘다, 선택이 없으면 None), 깊은 병합, `${key}` 자기 참조 풀기."""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Scripts"))

from common.Config import (  # noqa: E402
    ConfigError,
    expandSelfReferencesInternal,
    mergeJsonDictInternal,
    readOptionalConfig,
    requireConfig,
)


class CommonConfigTest(unittest.TestCase):
    def testRequiredMissingFileStops(self) -> None:
        with self.assertRaisesRegex(ConfigError, "없는 파일"):
            requireConfig(Path("no/such/file.json"), "없는 파일")

    def testOptionalMissingFileIsNone(self) -> None:
        self.assertIsNone(readOptionalConfig(Path("no/such/file.json"), "없는 파일"))

    def testBrokenJsonStopsEvenWhenOptional(self) -> None:
        with tempfile.TemporaryDirectory() as tempDir:
            path = Path(tempDir) / "broken.json"
            path.write_text("{ not json", encoding="utf-8")
            with self.assertRaisesRegex(ConfigError, "broken.json"):
                requireConfig(path, "broken.json")
            with self.assertRaisesRegex(ConfigError, "broken.json"):
                readOptionalConfig(path, "broken.json")

    def testNonObjectJsonStops(self) -> None:
        with tempfile.TemporaryDirectory() as tempDir:
            path = Path(tempDir) / "list.json"
            path.write_text("[1, 2]", encoding="utf-8")
            with self.assertRaises(ConfigError):
                requireConfig(path, "list.json")

    def testObjectIsReturned(self) -> None:
        with tempfile.TemporaryDirectory() as tempDir:
            path = Path(tempDir) / "ok.json"
            path.write_text('{"a": 1}', encoding="utf-8")
            self.assertEqual(requireConfig(path, "ok.json"), {"a": 1})
            self.assertEqual(readOptionalConfig(path, "ok.json"), {"a": 1})

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
