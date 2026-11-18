"""
커밋 훅 읽기 한도(`pre_commit.check_read_bound`)가 세는 git 블롭 읽기 — `cat-file --batch` 표준 입력의 `<판>:<경로>` 줄과 `show <판>:<경로>`.

실행: python -X utf8 Scripts/lint/tests/test_pre_commit_blob_reads.py (CTest `Scripts.PreCommitBlobReads`). git 을 띄우지 않는다.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from lint.pre_commit import list_blob_reads  # noqa: E402


class BlobReadsTest(unittest.TestCase):
    def test_batch_input_lines_are_reads(self) -> None:
        self.assertEqual(list_blob_reads(["C:/Git/git.exe", "cat-file", "--batch"], b":Content/a.xml\nHEAD:Content/b.xml\n"),
                         [("", "Content/a.xml"), ("HEAD", "Content/b.xml")])

    def test_batch_check_and_other_commands_read_nothing(self) -> None:
        self.assertEqual(list_blob_reads(["git", "cat-file", "--batch-check"], b"HEAD:Content/a.xml\n"), [])
        self.assertEqual(list_blob_reads(["git", "ls-files", "-z"], None), [])
        self.assertEqual(list_blob_reads(["clang-format", "show"], None), [])

    def test_show_names_its_blob(self) -> None:
        self.assertEqual(list_blob_reads(["git", "show", "--no-patch", "HEAD:Scripts/lint/rules.py"], None), [("HEAD", "Scripts/lint/rules.py")])


if __name__ == "__main__":
    unittest.main()
