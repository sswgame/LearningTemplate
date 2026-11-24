"""
new-kit 회귀 검사 — 뼈대의 파일 자리 · 매니페스트 의존 · 이름 바꿔 넣기, 카탈로그 키 표 줄, 만들 수 없는 요청의 거절.

실행: python -X utf8 Scripts/dev/tests/test_new_kit.py (CTest `Scripts.NewKit`). 저장소에 쓰지 않는다.
"""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.paths import get_project_root  # noqa: E402
from dev.tests.kit_skeleton_checks import find_problems  # noqa: E402
from dev.new_kit import KIT_ROOT, REFERENCE_TABLE, add_reference_rows, describe_invalid_request, make_files  # noqa: E402

GROUP = "Feature/World"


class NewKitTest(unittest.TestCase):
    def setUp(self) -> None:
        self._files = make_files("ZzFishing", GROUP)

    def test_manifests_name_the_kit_and_its_editor_extension(self) -> None:
        kit = json.loads(self._files["GF_ZzFishing.module.json"])
        self.assertEqual((kit["_kind"], kit["_listTarget"]), ("Kit", ["Client", "Server"]))
        extension = json.loads(self._files["Editor/GF_EditorPlugin_ZzFishing.module.json"])
        self.assertEqual(extension["_listConfiguration"], ["Dev"])
        self.assertEqual([row["_name"] for row in extension["_listDependency"]], ["EditorModule", "GF_ZzFishing"])
        self.assertIn("sw_add_game_framework_kit(GF_ZzFishing)", self._files["CMakeLists.txt"])
        self.assertIn("sw_add_editor_extension(GF_EditorPlugin_ZzFishing)", self._files["Editor/CMakeLists.txt"])

    def test_includes_inside_the_kit_point_at_written_files(self) -> None:
        prefix = f'#include "GameFramework/Kits/{GROUP}/ZzFishing/'
        for relative, text in self._files.items():
            for line in text.splitlines():
                if line.startswith(prefix):
                    self.assertIn(line[len(prefix):].rstrip('"'), self._files, relative)

    def test_code_uses_only_names_the_repository_has(self) -> None:
        set_own = {"getEntry", "getEntries", "hasEntry", "validateEntries", "ReadsEntriesAndRejectsDuplicates"}
        self.assertEqual(find_problems(self._files, f"GameFramework/Kits/{GROUP}/ZzFishing", "ZzFishing", set_own), [])

    def test_no_placeholder_is_left(self) -> None:
        for relative, text in self._files.items():
            self.assertNotIn("{name}", text, relative)
            self.assertEqual(text.count("{"), text.count("}"), relative)

    def test_reference_rows_are_added_once(self) -> None:
        table = (get_project_root() / REFERENCE_TABLE).read_bytes().decode("utf-8")
        added = add_reference_rows(table, "ZzFishing")
        self.assertIn('<CatalogKey tag="ZzFishingDef" catalog="ZzFishingCatalog" entry="Entry"/>', added)
        self.assertIn('<Field owner="ZzFishingComponent" field="_entry" tag="ZzFishingDef"/>', added)
        self.assertEqual(add_reference_rows(added, "ZzFishing"), added)
        with self.assertRaises(RuntimeError):
            add_reference_rows("<Other/>", "ZzFishing")

    def test_invalid_requests_are_refused(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            for group in (GROUP, "Feature/Online"):
                (root / KIT_ROOT / group).mkdir(parents=True)
            self.assertIsNone(describe_invalid_request(root, "ZzFishing", GROUP))
            self.assertIn("PascalCase", describe_invalid_request(root, "fishing", GROUP) or "")
            self.assertIn("group folder", describe_invalid_request(root, "ZzFishing", "Feature/Nowhere") or "")
            self.assertIn("group folder", describe_invalid_request(root, "ZzFishing", "Feature/../Feature") or "")
            self.assertIn("new-online-kit", describe_invalid_request(root, "ZzFishing", "Feature/Online") or "")
            (root / KIT_ROOT / GROUP / "Other").mkdir()
            (root / KIT_ROOT / GROUP / "Other" / "GF_ZzFishing.module.json").write_text("{}", encoding="utf-8")
            self.assertIn("already exists", describe_invalid_request(root, "ZzFishing", GROUP) or "")


if __name__ == "__main__":
    unittest.main()
