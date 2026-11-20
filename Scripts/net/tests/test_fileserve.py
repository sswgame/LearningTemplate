"""
fileserve 회귀 검사 — 루트 밖 경로(`..` · 절대 · 역슬래시 · 드라이브 · 링크)는 거절하고, stat · list · 구간 읽기가 장치 쪽(`FileServeContentSource`)이 기대하는 글을 낸다.

실행: python -X utf8 Scripts/net/tests/test_fileserve.py (CTest `Scripts.Fileserve`). 루프백에 임시 서버를 띄운다.
"""

from __future__ import annotations

import os
import sys
import tempfile
import threading
import unittest
import urllib.error
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from net.fileserve import FileServeError, MAX_CHUNK_BYTES, describe, list_files, make_server, read_range, resolve_request_path  # noqa: E402


class FileServeTest(unittest.TestCase):
    def setUp(self) -> None:
        self._temp = tempfile.TemporaryDirectory()
        self.base = Path(self._temp.name)
        self.root = self.base / "Packs"
        (self.root / "sub").mkdir(parents=True)
        (self.root / "a.pack").write_bytes(bytes(range(256)) * 4)
        (self.root / "b.pack").write_bytes(b"bee")
        (self.base / "secret.txt").write_text("outside", encoding="utf-8")

    def tearDown(self) -> None:
        self._temp.cleanup()

    def test_paths_outside_the_root_are_refused(self) -> None:
        for bad in ("../secret.txt", "sub/../../secret.txt", "/etc/passwd", "C:/Windows", "sub\\..\\..\\secret.txt", "a\x00b", "./a.pack", "NUL", "CON/x"):
            with self.subTest(bad=bad):
                with self.assertRaises(FileServeError):
                    resolve_request_path(self.root, bad)

    def test_symlink_out_of_the_root_is_missing(self) -> None:
        link = self.root / "escape.txt"
        try:
            os.symlink(self.base / "secret.txt", link)
        except (OSError, NotImplementedError):
            self.skipTest("symlinks are not available here")
        with self.assertRaises(FileServeError) as caught:
            describe(self.root, "escape.txt")
        self.assertEqual(404, caught.exception.status)

    def test_stat_list_and_range(self) -> None:
        self.assertEqual("folder", describe(self.root, ""))
        kind, size, mtime = describe(self.root, "a.pack").split(" ")
        self.assertEqual(("file", "1024"), (kind, size))
        self.assertTrue(mtime.isdigit())
        self.assertEqual("a.pack\nb.pack\n", list_files(self.root, ""))   # 폴더 `sub` 는 들지 않는다
        self.assertEqual(bytes(range(10, 20)), read_range(self.root, "a.pack", 10, 10))
        for offset, size in ((0, 0), (0, MAX_CHUNK_BYTES + 1), (1020, 10)):
            with self.subTest(offset=offset, size=size):
                with self.assertRaises(FileServeError):
                    read_range(self.root, "a.pack", offset, size)

    def test_server_round_trip(self) -> None:
        server = make_server(self.root, "127.0.0.1", 0)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            base = f"http://127.0.0.1:{server.server_address[1]}"
            with urllib.request.urlopen(f"{base}/stat?path=b.pack", timeout=5) as response:
                self.assertTrue(response.read().decode("utf-8").startswith("file 3 "))
            with urllib.request.urlopen(f"{base}/file?path=a.pack&offset=1&size=3", timeout=5) as response:
                self.assertEqual(bytes([1, 2, 3]), response.read())
            with self.assertRaises(urllib.error.HTTPError) as caught:
                urllib.request.urlopen(f"{base}/stat?path=..%2Fsecret.txt", timeout=5)
            self.assertEqual(400, caught.exception.code)
            with self.assertRaises(urllib.error.HTTPError) as caught:
                urllib.request.urlopen(f"{base}/stat?path=missing.pack", timeout=5)
            self.assertEqual(404, caught.exception.code)
            with self.assertRaises(urllib.error.HTTPError) as caught:
                urllib.request.urlopen(f"{base}/file?path=a.pack&offset=%C2%B2&size=1", timeout=5)   # '²' 는 isdigit 이지만 정수가 아니다
            self.assertEqual(400, caught.exception.code)
        finally:
            server.shutdown()
            server.server_close()


if __name__ == "__main__":
    unittest.main()
