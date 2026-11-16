"""
vcpkg 설치 보호 검사 — 사용자가 끈 설치와 소스 트리 밖 설치 폴더는 읽기 전용이고, 소스 트리 안 설치 폴더만 설치할 수 있다
(cmake/Toolchain/Vcpkg/VcpkgInstallPolicy.cmake 를 `cmake -P` 로 부른다).

실행: python -X utf8 Scripts/setup/tests/test_vcpkg_install_policy.py <cmake 실행 파일> (CTest `Scripts.VcpkgInstallPolicy`).
"""

from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

POLICY_FILE = Path(__file__).resolve().parents[3] / "cmake/Toolchain/Vcpkg/VcpkgInstallPolicy.cmake"
CMAKE = ""


def read_only_reason(source_dir: Path, installed_dir: str, user_install: str) -> str:
    with tempfile.TemporaryDirectory() as folder:
        script = Path(folder) / "Probe.cmake"
        script.write_text(
            f'include("{POLICY_FILE.as_posix()}")\n'
            f'sw_vcpkg_install_read_only_reason("{source_dir.as_posix()}" "{installed_dir}" "{user_install}" reason)\n'
            'message(NOTICE "REASON=[${reason}]")\n',
            encoding="utf-8",
        )
        result = subprocess.run([CMAKE, "-P", str(script)], capture_output=True, text=True, check=True)
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

    def test_inside_source_tree_may_install(self) -> None:
        self.assertEqual(read_only_reason(self.source, (self.source / "build/vcpkg_installed").as_posix(), ""), "")

    def test_relative_inside_may_install(self) -> None:
        self.assertEqual(read_only_reason(self.source, "build/vcpkg_installed", "ON"), "")

    def test_user_off_is_read_only(self) -> None:
        reason = read_only_reason(self.source, (self.source / "build/vcpkg_installed").as_posix(), "OFF")
        self.assertIn("VCPKG_MANIFEST_INSTALL=OFF", reason)

    def test_other_tree_is_read_only(self) -> None:
        # 2026-11-10 의 사고 — 워크트리 구성이 본 트리 설치 폴더를 가리켰다.
        reason = read_only_reason(self.source, (self.root / "Main/build/vcpkg_installed").as_posix(), "")
        self.assertIn("outside the source tree", reason)

    def test_dot_dot_escape_is_read_only(self) -> None:
        reason = read_only_reason(self.source, (self.source / "../Main/build/vcpkg_installed").as_posix(), "ON")
        self.assertIn("outside the source tree", reason)

    def test_sibling_with_same_prefix_is_read_only(self) -> None:
        sibling = self.root / "Worktree2/build/vcpkg_installed"
        sibling.mkdir(parents=True)
        self.assertIn("outside the source tree", read_only_reason(self.source, sibling.as_posix(), ""))


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit("usage: test_vcpkg_install_policy.py <cmake executable>")
    CMAKE = sys.argv.pop(1)
    unittest.main()
