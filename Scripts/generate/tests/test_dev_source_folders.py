"""
개발 전용 소스 자리 표 생성 검사 — 저장소 표가 읽히고, `Source/` 밖 · `/` 로 끝나지 않는 · 없는 폴더 · 빈 표는 오류이며, 생성 글이 표의 접두어를 `Source/` 를 떼고 낸다.

실행: python -X utf8 Scripts/generate/tests/test_dev_source_folders.py. 잘못된 표는 임시 폴더에 만든다.
"""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.paths import get_project_root  # noqa: E402
from generate.dev_source_folders import CONFIG_RELATIVE, load_tables, make_header_text  # noqa: E402
from generate.generator_output import GeneratorError  # noqa: E402

GOOD = {"_devFolderAllowed": {"Source/Games/": "x"}, "_devModuleRoots": {"Source/DevTools/": "x"}, "_editorFolderAllowed": {"Source/Games/": "x"}}


class DevSourceFoldersTest(unittest.TestCase):
    def _load(self, data: dict[str, object]) -> dict[str, list[str]]:
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "Source/Games").mkdir(parents=True)
            (root / "Source/DevTools").mkdir(parents=True)
            path = root / CONFIG_RELATIVE
            path.parent.mkdir(parents=True)
            path.write_text(json.dumps(data), encoding="utf-8")
            return load_tables(root)

    def test_repository_table_loads(self) -> None:
        tables = load_tables(get_project_root())
        self.assertIn("DevTools/", tables["_devModuleRoots"])
        self.assertIn("GameFramework/Kits/", tables["_devFolderAllowed"])

    def test_prefixes_lose_the_source_folder(self) -> None:
        tables = self._load(GOOD)
        self.assertEqual(["Games/"], tables["_devFolderAllowed"])
        self.assertIn('inline constexpr const char* kArrDevModuleRoot[] = { "DevTools/" };', make_header_text(tables))

    def test_bad_rows_are_errors(self) -> None:
        for section, prefix in (("_devModuleRoots", "Source/Missing/"), ("_devModuleRoots", "Engine/"), ("_devFolderAllowed", "Source/Games"), ("_editorFolderAllowed", "Source\\Games/")):
            data = json.loads(json.dumps(GOOD))
            data[section] = {prefix: "x"}
            with self.assertRaises(GeneratorError, msg=f"{section} {prefix}"):
                self._load(data)
        with self.assertRaises(GeneratorError):
            self._load({**GOOD, "_devModuleRoots": {}})


if __name__ == "__main__":
    unittest.main()
