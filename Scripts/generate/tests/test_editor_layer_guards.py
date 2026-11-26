"""
EditorLayerGuards 생성기 회귀 검사 — View 의 기본 금지 폴더(`[panelViewGuard] defaultGuard`)와 허용 폴더(`[panelViewAllowedFolders]`)를 읽고, 낡은 줄은 멈춘다.

실행: python -X utf8 Scripts/generate/tests/test_editor_layer_guards.py (CTest `Scripts.EditorLayerGuards`). 저장소의 실제 표도 한 번 읽는다.
"""

from __future__ import annotations

import sys
import tomllib
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.paths import get_project_root  # noqa: E402
from generate.editor_layer_guards import RULE_FILE, read_panel_view_default_guard  # noqa: E402
from generate.generator_output import GeneratorError  # noqa: E402


class EditorLayerGuardsDefaultGuardTest(unittest.TestCase):
    def test_repository_table_reads(self) -> None:
        data = tomllib.loads(RULE_FILE.read_text(encoding="utf-8"))
        list_default, list_folder = read_panel_view_default_guard(data, get_project_root())
        self.assertIn("^Engine/", list_default)
        self.assertTrue(all(folder.endswith("/") for folder in list_folder))

    def test_stale_folder_stops(self) -> None:
        data = {"panelViewGuard": {"defaultGuard": ["^Engine/"]}, "panelViewAllowedFolders": {"Engine/NoSuchFolderForTest/": "까닭"}}
        with self.assertRaises(GeneratorError):
            read_panel_view_default_guard(data, get_project_root())

    def test_folder_outside_default_guard_stops(self) -> None:
        data = {"panelViewGuard": {"defaultGuard": ["^DevTools/"]}, "panelViewAllowedFolders": {"Engine/Audio/": "까닭"}}
        with self.assertRaises(GeneratorError):
            read_panel_view_default_guard(data, get_project_root())

    def test_pattern_shape_is_top_folder(self) -> None:
        data = {"panelViewGuard": {"defaultGuard": ["^Engine/Audio/"]}, "panelViewAllowedFolders": {}}
        with self.assertRaises(GeneratorError):
            read_panel_view_default_guard(data, get_project_root())


if __name__ == "__main__":
    unittest.main()
