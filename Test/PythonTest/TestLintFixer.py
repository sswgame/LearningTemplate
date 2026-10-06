#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""LintFixer 기반 — UTF-8 로 못 읽는 파일을 고쳐 쓰지 않는다(바이트 보존), 줄끝 그대로."""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Scripts"))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Scripts" / "lint"))

from LintFixer import FixPass, LintFixer  # noqa: E402


def replaceBadInternal(text: str) -> tuple[str, bool]:
    return text.replace("bad", "good"), "bad" in text


class _ProbeFixer(LintFixer):
    listPass = (FixPass(transform=replaceBadInternal, problem="bad", done="fixed", badSample="bad", goodSample="good"),)


class LintFixerTest(unittest.TestCase):
    def testNonUtf8FileIsReportedAndLeftUntouched(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "a.h"
            original = b"// \xb0\xa1 bad\r\n"           # CP949 '가' + 고칠 낱말
            path.write_bytes(original)
            listMessage = _ProbeFixer().processFile(path, checkOnly=False)
            self.assertEqual(path.read_bytes(), original)  # 고쳐 쓰면 UTF-8 이 아닌 두 바이트가 사라진다
            self.assertTrue(any("UTF-8" in message for message in listMessage), listMessage)

    def testNonUtf8FileIsAProblemUnderCheck(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "a.h"
            path.write_bytes(b"// \xb0\xa1 good\n")
            self.assertTrue(_ProbeFixer().processFile(path, checkOnly=True))

    def testLineEndingsArePreserved(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "b.h"
            path.write_bytes(b"bad\r\nx\r\n")
            _ProbeFixer().processFile(path, checkOnly=False)
            self.assertEqual(path.read_bytes(), b"good\r\nx\r\n")


if __name__ == "__main__":
    unittest.main()
