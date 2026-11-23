"""
팩 경로 목록 읽기(`pack_index.py`)와 배포 확인의 리다이렉트 표 검사(`shipping_check.find_missing_redirect_tables`) 회귀 검사.

실행: python -X utf8 Scripts/package/tests/test_pack_index.py. 파일은 임시 폴더에 만든다.
"""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from dev.shipping_check import find_missing_redirect_tables  # noqa: E402
from package.pack_index import FLAG_ENCRYPTED, FLAG_HAS_STRING_POOL, HEADER, MAGIC, PackIndexError, read_pack_paths  # noqa: E402


def write_pack(path: Path, list_path: list[str], flags: int = FLAG_HAS_STRING_POOL) -> None:
    """페이로드 8 바이트 뒤에 경로 목록과 헤더만 있는 팩."""
    pool = b"".join(name.encode("utf-8") + b"\0" for name in list_path)
    header = HEADER.pack(MAGIC, 3, 0, 0, 0, 2048, flags, len(list_path), 0, 0, 8, len(pool), 8, 0, 0, 0, 65536, b"")
    path.write_bytes(b"\0" * 8 + pool + header)


class PackIndexTest(unittest.TestCase):
    def setUp(self) -> None:
        self._folder = tempfile.TemporaryDirectory()
        self.root = Path(self._folder.name)

    def tearDown(self) -> None:
        self._folder.cleanup()

    def test_header_matches_the_engine_layout(self) -> None:
        self.assertEqual(HEADER.size, 96)   # PackTypes.h static_assert( sizeof( PackHeader ) == 96 )

    def test_reads_paths_and_rejects_others(self) -> None:
        write_pack(self.root / "a.pack", ["game/x", "maps/A.scene.bin"])
        self.assertEqual(read_pack_paths(self.root / "a.pack"), {"game/x", "maps/a.scene.bin"})
        write_pack(self.root / "b.pack", ["x"], FLAG_HAS_STRING_POOL | FLAG_ENCRYPTED)
        (self.root / "c.pack").write_bytes(b"\0" * 200)
        for name in ("b.pack", "c.pack", "none.pack"):
            with self.assertRaises(PackIndexError):
                read_pack_paths(self.root / name)

    def test_finds_redirect_tables_missing_from_the_shipped_packs(self) -> None:
        content = self.root / "Content"
        for folder in ("engine", "game/playground", "game/other"):
            (content / folder).mkdir(parents=True)
            (content / folder / "redirects.xml").write_text("<Redirects/>", encoding="utf-8")
        write_pack(self.root / "engine.pack", ["engine", "redirects.xml"])
        write_pack(self.root / "common.pack", ["common"])
        write_pack(self.root / "game_playground.pack", ["game/playground"])
        list_name = ["engine.pack", "common.pack", "game_playground.pack"]
        self.assertEqual(find_missing_redirect_tables(content, self.root, list_name), (2, ["game/playground/redirects.xml (game_playground.pack)"]))
        write_pack(self.root / "game_playground.1.pack", ["game/playground", "redirects.xml"])   # 나눈 팩에 있어도 된다
        self.assertEqual(find_missing_redirect_tables(content, self.root, list_name), (2, []))


if __name__ == "__main__":
    unittest.main()
