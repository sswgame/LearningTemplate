"""
vcpkg 설치 보호 검사 — 사용자가 끈 설치와 소스 트리 밖 설치 폴더는 읽기 전용이고, 소스 트리 안 설치 폴더만 설치할 수 있다
(cmake/Modules/Toolchain/Vcpkg/VcpkgInstallPolicy.cmake 를 `cmake -P` 로 부른다).

실행: python -X utf8 Scripts/setup/Tests/TestVcpkgInstallPolicy.py <cmake 실행 파일> (CTest `Scripts.VcpkgInstallPolicy`).
"""

from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

kPolicyFile = Path(__file__).resolve().parents[3] / "cmake/Modules/Toolchain/Vcpkg/VcpkgInstallPolicy.cmake"
kCmake = ""


def readOnlyReason(sourceDir: Path, installedDir: str, userInstall: str) -> str:
    with tempfile.TemporaryDirectory() as folder:
        script = Path(folder) / "Probe.cmake"
        script.write_text(
            f'include("{kPolicyFile.as_posix()}")\n'
            f'sw_vcpkgInstallReadOnlyReason("{sourceDir.as_posix()}" "{installedDir}" "{userInstall}" reason)\n'
            'message(NOTICE "REASON=[${reason}]")\n',
            encoding="utf-8",
        )
        result = subprocess.run([kCmake, "-P", str(script)], capture_output=True, text=True, check=True)
    line = next(text for text in result.stderr.splitlines() if text.startswith("REASON=["))
    return line[len("REASON=[") : -1]


class VcpkgInstallPolicyTest(unittest.TestCase):
    def setUp(self) -> None:
        self._folder = tempfile.TemporaryDirectory()
        self.root = Path(self._folder.name)
        self.source = self.root / "Worktree"
        (self.source / "build/vcpkg_installed").mkdir(parents=True)
        (self.root / "Main/build/vcpkg_installed").mkdir(parents=True)

    def tearDown(self) -> None:
        self._folder.cleanup()

    def testInsideSourceTreeMayInstall(self) -> None:
        self.assertEqual(readOnlyReason(self.source, (self.source / "build/vcpkg_installed").as_posix(), ""), "")

    def testRelativeInsideMayInstall(self) -> None:
        self.assertEqual(readOnlyReason(self.source, "build/vcpkg_installed", "ON"), "")

    def testUserOffIsReadOnly(self) -> None:
        reason = readOnlyReason(self.source, (self.source / "build/vcpkg_installed").as_posix(), "OFF")
        self.assertIn("VCPKG_MANIFEST_INSTALL=OFF", reason)

    def testOtherTreeIsReadOnly(self) -> None:
        # 2026-11-10 의 사고 — 워크트리 구성이 본 트리 설치 폴더를 가리켰다.
        reason = readOnlyReason(self.source, (self.root / "Main/build/vcpkg_installed").as_posix(), "")
        self.assertIn("outside the source tree", reason)

    def testDotDotEscapeIsReadOnly(self) -> None:
        reason = readOnlyReason(self.source, (self.source / "../Main/build/vcpkg_installed").as_posix(), "ON")
        self.assertIn("outside the source tree", reason)

    def testSiblingWithSamePrefixIsReadOnly(self) -> None:
        sibling = self.root / "Worktree2/build/vcpkg_installed"
        sibling.mkdir(parents=True)
        self.assertIn("outside the source tree", readOnlyReason(self.source, sibling.as_posix(), ""))


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit("usage: TestVcpkgInstallPolicy.py <cmake executable>")
    kCmake = sys.argv.pop(1)
    unittest.main()
