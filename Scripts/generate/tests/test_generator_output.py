"""
configure 생성기의 입력 목록(`record_read_files` · `write_input_list`) 회귀 검사 — 뿌리 안에서 읽은 파일만 들고, 쓴 파일 · 출력 폴더 · 뿌리 밖은 빠진다.

실행: python -X utf8 Scripts/generate/tests/test_generator_output.py. 파일은 임시 폴더에 만든다.
"""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from generate.generator_output import record_read_files, write_input_list  # noqa: E402


class RecordReadFilesTest(unittest.TestCase):
    def test_only_reads_inside_root_outside_output(self) -> None:
        with tempfile.TemporaryDirectory() as folder, tempfile.TemporaryDirectory() as outside_folder:
            root = Path(folder)
            output_dir = root / "build" / "generated"
            output_dir.mkdir(parents=True)
            (root / "Table.toml").write_text("a = 1\n", encoding="utf-8")
            (output_dir / "Old.cmake").write_text("set(A 1)\n", encoding="utf-8")
            outside = Path(outside_folder) / "Other.json"
            outside.write_text("{}", encoding="utf-8")

            with record_read_files(root, output_dir) as files:
                (root / "Table.toml").read_text(encoding="utf-8")
                (output_dir / "Old.cmake").read_text(encoding="utf-8")   # 생성기가 바뀌었나 보려고 읽는 출력
                outside.read_text(encoding="utf-8")
                (root / "Written.txt").write_text("x", encoding="utf-8")
            (root / "After.txt").write_text("x", encoding="utf-8")
            (root / "After.txt").read_text(encoding="utf-8")   # 구간 밖 읽기는 들지 않는다

            self.assertEqual({path.name for path in files}, {"Table.toml"})

    def test_input_list_is_sorted_posix_lines(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            input_list = root / "out" / "X.input_list"
            self.assertTrue(write_input_list(input_list, {root / "b.json", root / "a.json"}, tag="Test"))
            lines = input_list.read_bytes().decode("utf-8").split("\n")
            self.assertEqual(lines[-1], "")
            self.assertEqual([Path(line).name for line in lines[:-1]], ["a.json", "b.json"])
            self.assertTrue(all("\\" not in line for line in lines))
            self.assertFalse(write_input_list(input_list, {root / "a.json", root / "b.json"}, tag="Test"))   # 같으면 쓰지 않는다


if __name__ == "__main__":
    unittest.main()
