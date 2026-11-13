"""
Pack 모드 스테이징 회귀 검사 — 배포 패키지는 묶은 모드(`mod.xml` + `content.pack`)만 담고, 형식이 틀린 id(경로 탈출) · 묶지 않은 모드는 실패다.

실행: python -X utf8 Scripts/package/tests/test_pack_mods.py (CTest `Scripts.PackMods`). 프로젝트 폴더는 임시 폴더다.
"""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from package.pack import PackageError, MOD_EXPORT_FOLDER, select_staged_mods  # noqa: E402


class PackModsTest(unittest.TestCase):
    def test_packed_mod_is_staged_with_its_two_files(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            exported = root / MOD_EXPORT_FOLDER / "demo_lantern"
            exported.mkdir(parents=True)
            (exported / "mod.xml").write_text("<Mod/>", encoding="utf-8")
            (exported / "content.pack").write_bytes(b"pack")
            list_mod = select_staged_mods(root, ["demo_lantern"])
            self.assertEqual(1, len(list_mod))
            self.assertEqual("demo_lantern", list_mod[0][0])
            self.assertEqual(["mod.xml", "content.pack"], [path.name for path in list_mod[0][1]])

    def test_unpacked_mod_fails(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            with self.assertRaises(PackageError) as context:
                select_staged_mods(Path(folder), ["demo_lantern"])
            self.assertIn("mod.pack demo_lantern", str(context.exception))

    def test_bad_id_fails_before_touching_paths(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            for bad_id in ("../escape", "Upper", "a/b", ""):
                with self.assertRaises(PackageError):
                    select_staged_mods(Path(folder), [bad_id])


if __name__ == "__main__":
    unittest.main()
