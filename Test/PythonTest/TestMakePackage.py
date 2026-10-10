#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
패키징 진입점(`Scripts/dev/MakePackage.py`)의 단위 시험 — 빌드와 쿠킹을 건너뛰고 스테이징과 검사만 돌린다.

가짜 Bin(실행 파일 둘 · DLL · 팩 · Dev 산출물)을 임시 폴더에 깔고, 패키지에 그 타깃의 실행 파일 · DLL · 팩 · 고지만 들고
Dev 산출물(`Saved/` · `Symbols/` · `Modules/`)과 다른 타깃의 실행 파일은 들지 않는지 본다. 단계 줄과 끝 줄의 모양은 에디터 창이 읽는 계약이다.
"""

from __future__ import annotations

import contextlib
import io
import sys
import tempfile
import unittest
from pathlib import Path

kRepositoryRoot = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(kRepositoryRoot / "Scripts" / "dev"))
sys.path.insert(0, str(kRepositoryRoot / "Scripts"))

import MakePackage  # noqa: E402


class MakePackageTest(unittest.TestCase):
    """임시 Bin 하나를 스테이징한다."""

    def setUp(self) -> None:
        self._tempDir = tempfile.TemporaryDirectory()
        self.root = Path(self._tempDir.name)
        self.binDir = self.root / "Bin"
        self.binDir.mkdir()
        for name in (MakePackage.kMapTargetExecutable["Client"], MakePackage.kMapTargetExecutable["Server"], "OnlineLoadBot.exe",
                     "Engine" + MakePackage.kListLibrarySuffix[0], MakePackage.kNoticeFileName):
            (self.binDir / name).write_bytes(b"x")
        for folder in (MakePackage.kPackFolderName, "Saved", "Symbols", "Modules"):
            (self.binDir / folder).mkdir()
            (self.binDir / folder / "item.bin").write_bytes(b"y")

    def tearDown(self) -> None:
        self._tempDir.cleanup()

    def runMain(self, target: str) -> tuple[int, str]:
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            exitCode = MakePackage.main(["--target", target, "--game", "Empty", "--skip-build", "--skip-cook", "--bin-dir", str(self.binDir),
                                         "--output", str(self.root / "Packages")])
        return exitCode, output.getvalue()

    def testClientPackageHoldsOnlyShippedFiles(self) -> None:
        exitCode, output = self.runMain("Client")
        self.assertEqual(0, exitCode, output)
        stagingDir = self.root / "Packages" / "Empty-Client"
        listName = sorted(path.name for path in stagingDir.iterdir())
        self.assertEqual(sorted([MakePackage.kMapTargetExecutable["Client"], "Engine" + MakePackage.kListLibrarySuffix[0], MakePackage.kNoticeFileName,
                                 MakePackage.kPackFolderName]), listName)
        self.assertTrue((stagingDir / MakePackage.kPackFolderName / "item.bin").is_file())

    def testServerPackageCarriesTheServerConfig(self) -> None:
        exitCode, output = self.runMain("Server")
        self.assertEqual(0, exitCode, output)
        stagingDir = self.root / "Packages" / "Empty-Server"
        self.assertTrue((stagingDir / MakePackage.kMapTargetExecutable["Server"]).is_file())
        self.assertFalse((stagingDir / MakePackage.kMapTargetExecutable["Client"]).exists())
        self.assertTrue((stagingDir / "Config" / "Server" / "Empty.json").is_file())

    def testProgressLinesFollowTheContract(self) -> None:
        exitCode, output = self.runMain("Client")
        self.assertEqual(0, exitCode, output)
        listLine = [line for line in output.splitlines() if line.startswith("[package] ")]
        listStep = [line for line in listLine if line.startswith("[package] step ")]
        self.assertEqual([f"[package] step {index + 1}/4 {name}" for index, name in enumerate(MakePackage.kListStepName)], listStep)
        self.assertTrue(listLine[-1].startswith("[package] done "), listLine[-1])

    def testMissingBinFailsTheBuildStep(self) -> None:
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            exitCode = MakePackage.main(["--target", "Client", "--skip-build", "--skip-cook", "--bin-dir", str(self.root / "NoSuchBin"),
                                         "--output", str(self.root / "Packages")])
        self.assertEqual(1, exitCode)
        self.assertIn("[package] FAILED build ", output.getvalue())


if __name__ == "__main__":
    unittest.main()
