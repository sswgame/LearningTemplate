#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""common.BuildTree — 빌드 폴더 고르기(.clangd · 프리셋 순서) · CMakeCache · 컴파일 DB · 짓지 않는 소스 · 인자 철자."""

from __future__ import annotations

import argparse
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Scripts"))

from common import BuildTree, BuildTreeError, addBuildTreeArguments  # noqa: E402


def makeRepositoryInternal(root: Path) -> None:
    presets = {"configurePresets": [{"name": "A"}, {"name": "B", "hidden": True}, {"name": "C"}]}
    (root / "CMakePresets.json").write_text(json.dumps(presets), encoding="utf-8")
    (root / "build" / "C" / "Bin").mkdir(parents=True)
    (root / "build" / "C" / "CMakeCache.txt").write_text(
        "// comment\nSW_SHIPPING_BUILD:BOOL=ON\nSW_SHIPPING_RHI_BACKEND:STRING=Vulkan\n", encoding="utf-8")
    (root / "build" / "C" / "Bin" / "App.exe").write_bytes(b"")
    (root / "build" / "A" / "generated" / "sw" / "config").mkdir(parents=True)
    (root / "build" / "A" / "CMakeCache.txt").write_text("SW_SHIPPING_BUILD:BOOL=OFF\n", encoding="utf-8")
    (root / "build" / "A" / "compile_commands.json").write_text("[]", encoding="utf-8")
    (root / "build" / "A" / "generated" / "sw" / "config" / "UnbuiltSources.txt").write_text("Source/X.cpp\n", encoding="utf-8")
    (root / ".clangd").write_text("CompileFlags:\n  CompilationDatabase: build/A\n", encoding="utf-8")


class BuildTreeTest(unittest.TestCase):
    def setUp(self) -> None:
        self._temp = tempfile.TemporaryDirectory()
        self.root = Path(self._temp.name).resolve()
        makeRepositoryInternal(self.root)

    def tearDown(self) -> None:
        self._temp.cleanup()

    def testDefaultIsTheTreeClangdPointsAt(self) -> None:
        self.assertEqual(BuildTree.findDefault(self.root).name, "A")

    def testDefaultWithoutClangdIsNinjaDebug(self) -> None:
        (self.root / ".clangd").unlink()
        self.assertEqual(BuildTree.findDefault(self.root).name, "Ninja-Debug")

    def testConfiguredTreesFollowPresetOrderAndSkipHidden(self) -> None:
        self.assertEqual([tree.name for tree in BuildTree.iterConfigured(self.root)], ["A", "C"])

    def testAppIsFoundInTheFirstTreeThatHasOne(self) -> None:
        appPath = BuildTree.findAppExecutable(self.root)
        self.assertIsNotNone(appPath)
        self.assertEqual(BuildTree.ofApp(appPath).name, "C")

    def testCacheValues(self) -> None:
        tree = BuildTree.fromPreset("C", self.root)
        self.assertTrue(tree.bShipping)
        self.assertEqual(tree.readCacheValue("SW_SHIPPING_RHI_BACKEND"), "Vulkan")
        self.assertFalse(BuildTree.fromPreset("A", self.root).bShipping)

    def testCompileDatabase(self) -> None:
        self.assertEqual(BuildTree.fromPreset("A", self.root).readCompileDatabase(), [])
        with self.assertRaises(BuildTreeError):
            BuildTree.fromPreset("B", self.root).readCompileDatabase()

    def testUnbuiltSourcesAreLowercasedRepositoryPaths(self) -> None:
        self.assertEqual(BuildTree.fromPreset("A", self.root).readUnbuiltSources(), {"source/x.cpp"})
        self.assertIsNone(BuildTree.fromPreset("C", self.root).readUnbuiltSources())

    def testArgumentsBuildDirWinsAndIsRepositoryRelative(self) -> None:
        parser = argparse.ArgumentParser(description="BuildTree 인자 시험")
        addBuildTreeArguments(parser)
        self.assertEqual(BuildTree.fromArguments(parser.parse_args(["--build-dir", "x"]), self.root).path, self.root / "x")
        self.assertEqual(BuildTree.fromArguments(parser.parse_args(["--preset", "C", "--build-dir", "build/A"]), self.root).name, "A")
        self.assertEqual(BuildTree.fromArguments(parser.parse_args([]), self.root).name, "Ninja-Debug")

    def testArgumentsWithoutDefaultPresetUseClangd(self) -> None:
        parser = argparse.ArgumentParser(description="BuildTree 인자 시험")
        addBuildTreeArguments(parser, defaultPreset=None)
        self.assertEqual(BuildTree.fromArguments(parser.parse_args([]), self.root).name, "A")


if __name__ == "__main__":
    unittest.main()
