"""
플랫폼 덮어쓰기 굽기 회귀 검사 — Config/Platform/<플랫폼>/EngineConfig.json 만 받고, 플랫폼이 아닌 폴더 · 객체가 아닌 글은 configure 오류이며,
생성 줄은 빌드 플랫폼(SW_PLATFORM_NAME)의 글을 고르고 없으면 nullptr 이다.

실행: python -X utf8 Scripts/generate/tests/test_shipping_host_defaults.py. 파일은 임시 폴더에 만든다.
"""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.project_constants import DIR_RUNTIME_PLATFORM_CONFIG  # noqa: E402
from generate.generator_output import GeneratorError  # noqa: E402
from generate.shipping_host_defaults import _platform_override_lines, collect_platform_overrides  # noqa: E402


class PlatformOverrideTest(unittest.TestCase):
    def _write(self, root: Path, platform: str, text: str) -> None:
        folder = root / DIR_RUNTIME_PLATFORM_CONFIG / platform
        folder.mkdir(parents=True, exist_ok=True)
        (folder / "EngineConfig.json").write_text(text, encoding="utf-8")

    def test_collect_platform_files_and_select_by_platform_name(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            self.assertEqual(collect_platform_overrides(root), {})
            self._write(root, "Android", '{ "_window": { "_width": 1280 } }')
            overrides = collect_platform_overrides(root)
            self.assertEqual(list(overrides), ["Android"])
            lines = _platform_override_lines(overrides)
            self.assertIn('isSamePlatformName( SW_PLATFORM_NAME, "Android" )', lines)
            self.assertIn(": nullptr;", lines)
        self.assertIn("kEngineConfigPlatformOverrideJSON = nullptr;", _platform_override_lines({}))

    def test_unknown_platform_folder_and_non_object_are_errors(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            self._write(root, "Andriod", "{}")
            with self.assertRaises(GeneratorError):
                collect_platform_overrides(root)
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            self._write(root, "Web", "[1]")
            with self.assertRaises(GeneratorError):
                collect_platform_overrides(root)


if __name__ == "__main__":
    unittest.main()
