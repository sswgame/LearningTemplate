#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""common.GeneratedFile — 바뀌었을 때만 쓰기(시각 그대로), 줄끝 고르기, CMake 값 이스케이프, 생성기 진입점의 종료 코드."""

from __future__ import annotations

import argparse
import contextlib
import io
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Scripts"))

from common import GeneratorError, formatCMakeSet, runGenerator, toCMakeValue, writeGeneratedFile  # noqa: E402


class GeneratedFileTest(unittest.TestCase):
    def testSameContentDoesNotRewrite(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "sub" / "a.h"
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertTrue(writeGeneratedFile(path, "x\ny\n", tag="T"))
                os.utime(path, ns=(1_000_000_000, 1_000_000_000))
                self.assertFalse(writeGeneratedFile(path, "x\ny\n", tag="T"))
                self.assertEqual(path.stat().st_mtime_ns, 1_000_000_000)
                self.assertTrue(writeGeneratedFile(path, "x\nz\n", tag="T"))

    def testNewlineIsChosenByTheGenerator(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            lfPath, platformPath = Path(folder) / "lf.h", Path(folder) / "p.h"
            writeGeneratedFile(lfPath, "a\nb\n", tag="T", newline="\n", bQuiet=True)
            writeGeneratedFile(platformPath, "a\nb\n", tag="T", bQuiet=True)
            self.assertEqual(lfPath.read_bytes(), b"a\nb\n")
            self.assertEqual(platformPath.read_bytes(), "a\nb\n".replace("\n", os.linesep).encode())
            self.assertFalse(writeGeneratedFile(platformPath, "a\nb\n", tag="T", bQuiet=True))

    def testCMakeValue(self) -> None:
        self.assertEqual(toCMakeValue(r"C:\Program Files\x"), r"C:\\Program Files\\x")
        self.assertEqual(toCMakeValue({"b", "a"}), "a;b")
        self.assertEqual(toCMakeValue(["b", "a"]), "b;a")
        self.assertEqual(toCMakeValue(True), "TRUE")
        self.assertEqual(toCMakeValue('a"b'), 'a\\"b')
        self.assertEqual(formatCMakeSet("SW_X", False), 'set(SW_X "FALSE")')

    def testGeneratorErrorIsOneLineAndExitCodeOne(self) -> None:
        def generate(_args: argparse.Namespace) -> None:
            raise GeneratorError("x")

        stderr = io.StringIO()
        with contextlib.redirect_stderr(stderr):
            exitCode = runGenerator([], tag="T", description="d", addArguments=lambda parser: None, generate=generate)
        self.assertEqual(exitCode, 1)
        self.assertEqual(stderr.getvalue(), "[T] x\n")
        self.assertEqual(runGenerator([], tag="T", description="d", addArguments=lambda parser: None, generate=lambda args: None), 0)


if __name__ == "__main__":
    unittest.main()
