#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""generate.GenerateEditorLayers — 층 표와 Source/Editor 폴더가 맞으면 CMake 변수를 만들고, 어긋나면 멈춘다."""

from __future__ import annotations

import contextlib
import io
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Scripts"))

from common.GeneratedFile import GeneratorError  # noqa: E402
from generate import GenerateEditorLayers  # noqa: E402


class GenerateEditorLayersTest(unittest.TestCase):
    def generate(self, listActual: list[str]) -> str:
        with tempfile.TemporaryDirectory() as folder, contextlib.redirect_stdout(io.StringIO()), \
                mock.patch.object(GenerateEditorLayers, "listActualLayersInternal", lambda _dir: listActual):
            output = Path(folder) / "EditorLayers.cmake"
            GenerateEditorLayers.generateEditorLayers(output)
            return output.read_text(encoding="utf-8")

    def testRealTreeMakesTiers(self) -> None:
        text = self.generate(GenerateEditorLayers.listActualLayersInternal(GenerateEditorLayers.getProjectRoot() / "Source" / "Editor"))
        self.assertIn('set(SW_EDITOR_TIER_LIST "0;1;2;3;4;5;6;7;8")', text)
        self.assertIn("Panels", text)

    def testFolderWithoutTierStops(self) -> None:
        realLayers = GenerateEditorLayers.listActualLayersInternal(GenerateEditorLayers.getProjectRoot() / "Source" / "Editor")
        with self.assertRaises(GeneratorError):
            self.generate(realLayers + ["Probe"])

    def testTierWithoutFolderStops(self) -> None:
        with self.assertRaises(GeneratorError):
            self.generate(["Panels"])


if __name__ == "__main__":
    unittest.main()
