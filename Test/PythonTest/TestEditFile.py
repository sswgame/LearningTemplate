#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""dev/RunEditFile 의 판단 — 한 번만 나오는 글만 바꾸고, 줄 끝 · BOM 을 보존하고, 틀리면 아무것도 쓰지 않는다(임시 폴더)."""

from __future__ import annotations

import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

kScriptsRoot = Path(__file__).resolve().parents[2] / "Scripts"
sys.path.insert(0, str(kScriptsRoot))

from dev import RunEditFile  # noqa: E402


class EditFileTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tempDir = tempfile.TemporaryDirectory()
        self.addCleanup(self.tempDir.cleanup)
        self.root = Path(self.tempDir.name)

    def writeBytesInternal(self, name: str, data: bytes) -> Path:
        path = self.root / name
        path.write_bytes(data)
        return path

    def runMainInternal(self, listArg: list[str]) -> tuple[int, str]:
        err = io.StringIO()
        with contextlib.redirect_stderr(err), contextlib.redirect_stdout(io.StringIO()):
            code = RunEditFile.main([*listArg, "--root", str(self.root)])
        return code, err.getvalue()

    def runEditInternal(self, name: str, old: str, new: str) -> tuple[int, str]:
        (self.root / "old.txt").write_bytes(old.encode("utf-8"))
        (self.root / "new.txt").write_bytes(new.encode("utf-8"))
        return self.runMainInternal([str(self.root / name), "--old-file", str(self.root / "old.txt"), "--new-file", str(self.root / "new.txt")])

    def testReplacesOnceAndKeepsCrlf(self) -> None:
        path = self.writeBytesInternal("a.txt", "가\r\n나\r\n다\r\n".encode("utf-8"))
        code, _ = self.runEditInternal("a.txt", "나\n", "나나\n라\n")  # old · new 를 LF 로 줘도 된다
        self.assertEqual(code, 0)
        self.assertEqual(path.read_bytes(), "가\r\n나나\r\n라\r\n다\r\n".encode("utf-8"))

    def testKeepsLfAndBom(self) -> None:
        path = self.writeBytesInternal("a.txt", b"\xef\xbb\xbfone\ntwo\n")
        self.assertEqual(self.runEditInternal("a.txt", "two", "2")[0], 0)
        self.assertEqual(path.read_bytes(), b"\xef\xbb\xbfone\n2\n")

    def testZeroOrManyMatchesWriteNothing(self) -> None:
        original = b"x\nx\ny\n"
        path = self.writeBytesInternal("a.txt", original)
        for old in ("x", "없음"):
            code, err = self.runEditInternal("a.txt", old, "z")
            self.assertEqual(code, 1)
            self.assertIn("번 나옵니다", err)
            self.assertEqual(path.read_bytes(), original)

    def testRejectsNulAndControlInNew(self) -> None:
        original = b"a\nb\n"
        path = self.writeBytesInternal("a.txt", original)
        self.assertEqual(self.runEditInternal("a.txt", "a", "x\0y")[0], 1)
        self.assertEqual(self.runEditInternal("a.txt", "a", "x\x08y")[0], 1)
        self.assertEqual(path.read_bytes(), original)
        self.assertEqual(self.runEditInternal("a.txt", "a", "x\ty")[0], 0)  # 탭은 허용
        self.assertEqual(path.read_bytes(), b"x\ty\nb\n")

    def testRejectsMixedLineEndingsAndOutsideRoot(self) -> None:
        mixed = b"a\r\nb\nc\r\n"
        path = self.writeBytesInternal("mixed.txt", mixed)
        self.assertEqual(self.runEditInternal("mixed.txt", "a", "z")[0], 1)
        self.assertEqual(path.read_bytes(), mixed)
        with tempfile.TemporaryDirectory() as other:
            outside = Path(other) / "o.txt"
            outside.write_bytes(b"a\n")
            code, err = self.runEditInternal(str(outside), "a", "z")
            self.assertEqual(code, 1)
            self.assertIn("저장소 밖", err)
            self.assertEqual(outside.read_bytes(), b"a\n")

    def testChangesJsonIsAtomic(self) -> None:
        first = self.writeBytesInternal("a.txt", b"one\r\ntwo\r\n")
        second = self.writeBytesInternal("b.txt", b"alpha\n")
        good = [{"path": str(first), "old": "one", "new": "1"}, {"path": str(first), "old": "two", "new": "2"}, {"path": str(second), "old": "alpha", "new": "A"}]
        bad = [good[0], {"path": str(second), "old": "없음", "new": "A"}]
        jsonPath = self.root / "changes.json"
        jsonPath.write_text(json.dumps(bad), encoding="utf-8")
        self.assertEqual(self.runMainInternal(["--changes", str(jsonPath)])[0], 1)
        self.assertEqual(first.read_bytes(), b"one\r\ntwo\r\n")  # 앞 변경도 쓰이지 않았다
        jsonPath.write_text(json.dumps(good), encoding="utf-8")
        self.assertEqual(self.runMainInternal(["--changes", str(jsonPath)])[0], 0)
        self.assertEqual(first.read_bytes(), b"1\r\n2\r\n")
        self.assertEqual(second.read_bytes(), b"A\n")

    def testBadArguments(self) -> None:
        jsonPath = self.root / "bad.json"
        jsonPath.write_text('[{"path": "a", "old": "x"}]', encoding="utf-8")
        self.assertEqual(self.runMainInternal(["--changes", str(jsonPath)])[0], 1)
        self.assertEqual(self.runMainInternal([])[0], 1)


if __name__ == "__main__":
    unittest.main()
