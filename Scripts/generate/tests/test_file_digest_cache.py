"""
FileDigestCache 회귀 검사 — 캐시가 있든 없든 같은 지문이고, 바뀐 파일만 다시 읽으며, 캐시에 지운 파일의 줄이 남지 않는다.

실행: python -X utf8 Scripts/generate/tests/test_file_digest_cache.py. 파일은 임시 폴더에 만든다.
"""

from __future__ import annotations

import json
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from generate.file_digest_cache import FileDigestCache  # noqa: E402


class FileDigestCacheTest(unittest.TestCase):
    def test_cached_digest_matches_fresh_and_rereads_only_changed_files(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "a.h").write_bytes(b"int a;\r\n")
            (root / "b.h").write_bytes(b"int b;\n")
            cache_path = root / "cache" / "digest.json"
            first = FileDigestCache(cache_path)
            digest = first.tree_digest(root, ["a.h", "b.h"])
            first.save()

            # CRLF 와 LF 는 같은 지문이다(체크아웃마다 다른 줄 끝).
            (root / "a.h").write_bytes(b"int a;\n")
            os.utime(root / "a.h", ns=(1, 1))
            self.assertEqual(FileDigestCache(root / "none.json").tree_digest(root, ["a.h", "b.h"]), digest)

            (root / "b.h").write_bytes(b"int c;\n")
            os.utime(root / "b.h", ns=(2, 2))
            second = FileDigestCache(cache_path)
            list_read: list[Path] = []
            original = Path.read_bytes

            def record_read(path: Path) -> bytes:
                list_read.append(path)
                return original(path)

            with mock.patch.object(Path, "read_bytes", record_read):
                changed = second.tree_digest(root, ["a.h", "b.h"])
            self.assertNotEqual(changed, digest)
            self.assertEqual(changed, FileDigestCache(root / "none2.json").tree_digest(root, ["a.h", "b.h"]))
            self.assertEqual(sorted(path.name for path in list_read), ["a.h", "b.h"])   # 둘 다 mtime 이 바뀌었다

            second.save()
            third = FileDigestCache(cache_path)
            with mock.patch.object(Path, "read_bytes", record_read):
                list_read.clear()
                third.tree_digest(root, ["b.h"])
            self.assertEqual(list_read, [])   # 안 바뀐 파일은 읽지 않는다
            third.save()
            self.assertEqual(set(json.loads(cache_path.read_text(encoding="utf-8"))["files"]), {"b.h"})   # 지운 a.h 의 줄은 남지 않는다


if __name__ == "__main__":
    unittest.main()
