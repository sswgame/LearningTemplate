"""
vcpkg 설치 보호 검사 — 사용자가 끈 설치와 소스 트리 밖 설치 폴더는 읽기 전용이고, 소스 트리 안 설치 폴더만 설치할 수 있다
(cmake/Toolchain/Vcpkg/VcpkgInstallPolicy.cmake 를 `cmake -P` 로 부른다). 게이트 전체(Vcpkg.cmake)는 빈 캐시로 한 번 읽어 본다.

실행: SW_CMAKE_COMMAND=<cmake 실행 파일> python -X utf8 Scripts/setup/tests/test_vcpkg_install_policy.py (CTest `Scripts.VcpkgInstallPolicy` 가 환경 변수를 준다).
"""

from __future__ import annotations

import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
POLICY_FILE = ROOT / "cmake/Toolchain/Vcpkg/VcpkgInstallPolicy.cmake"
CMAKE_ENVIRONMENT = "SW_CMAKE_COMMAND"  # IntegrationTests/CMakeLists.txt 가 스크립트 테스트 모두에 ${CMAKE_COMMAND} 를 준다
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


def parse_install_plan(output: str) -> tuple[str, str]:
    """`sw_vcpkg_parse_install_plan` 이 읽은 (지울, 지을) 포트 목록 — CMake 리스트 글 그대로."""
    with tempfile.TemporaryDirectory() as folder:
        plan = Path(folder) / "plan.txt"
        plan.write_bytes(output.encode("utf-8"))
        script = Path(folder) / "Probe.cmake"
        script.write_text(
            f'include("{POLICY_FILE.as_posix()}")\n'
            f'file(READ "{plan.as_posix()}" output)\n'
            'sw_vcpkg_parse_install_plan("${output}" remove build)\n'
            'message(NOTICE "REMOVE=[${remove}]")\n'
            'message(NOTICE "BUILD=[${build}]")\n',
            encoding="utf-8",
        )
        result = subprocess.run([CMAKE, "-P", str(script)], capture_output=True, text=True, check=True)
    lines = result.stderr.splitlines()
    remove = next(text for text in lines if text.startswith("REMOVE=["))
    build = next(text for text in lines if text.startswith("BUILD=["))
    return remove[len("REMOVE=[") : -1], build[len("BUILD=[") : -1]


def overlay_hash(source_dir: Path) -> str:
    with tempfile.TemporaryDirectory() as folder:
        script = Path(folder) / "Probe.cmake"
        script.write_text(
            f'include("{POLICY_FILE.as_posix()}")\n'
            f'sw_vcpkg_hash_overlay_ports("{source_dir.as_posix()}" hash)\n'
            'message(NOTICE "HASH=[${hash}]")\n',
            encoding="utf-8",
        )
        result = subprocess.run([CMAKE, "-P", str(script)], capture_output=True, text=True, check=True)
    line = next(text for text in result.stderr.splitlines() if text.startswith("HASH=["))
    return line[len("HASH=[") : -1]


class VcpkgOverlayHashTest(unittest.TestCase):
    def test_every_port_file_changes_the_hash(self) -> None:
        # 예전에는 portfile · vcpkg.json · *.patch 만 세어, 래퍼 · *.diff 를 고쳐도 설치를 건너뛰었다.
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder)
            port = source / "ThirdParty/zlib/vcpkg-port/zlib"
            (port / "windows").mkdir(parents=True)
            list_file = [port / name for name in ("portfile.cmake", "vcpkg.json", "a.patch", "a.diff", "vcpkg-cmake-wrapper.cmake", "usage", "windows/b.diff")]
            for path in list_file:
                path.write_bytes(b"one\n")
            last = overlay_hash(source)
            for path in list_file:
                with self.subTest(file=path.name):
                    path.write_bytes(b"two\n")
                    changed = overlay_hash(source)
                    self.assertNotEqual(last, changed)
                    last = changed
            (port / "usage").rename(port / "usage.txt")
            self.assertNotEqual(last, overlay_hash(source))

    def test_line_endings_do_not_change_the_hash(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            portfile = Path(folder) / "ThirdParty/zlib/vcpkg-port/zlib/portfile.cmake"
            portfile.parent.mkdir(parents=True)
            portfile.write_bytes(b"one\ntwo\n")
            lf_hash = overlay_hash(Path(folder))
            portfile.write_bytes(b"one\r\ntwo\r\n")
            self.assertEqual(lf_hash, overlay_hash(Path(folder)))


class VcpkgEmptyCacheTest(unittest.TestCase):
    def test_empty_cache_decides_every_input(self) -> None:
        # 게이트의 값(트리플릿 · 오버레이)을 정하는 절이 통째로 지워져도 캐시가 찬 빌드 폴더는 옛 값으로 구성된다 — 빈 캐시에서만 드러난다.
        with tempfile.TemporaryDirectory() as folder:
            script = Path(folder) / "Probe.cmake"
            script.write_text(
                f'set(CMAKE_BINARY_DIR "{Path(folder).as_posix()}")\n'
                f'set(SW_VCPKG_ROOT "{Path(folder).as_posix()}")\n'
                f'set(VCPKG_INSTALLED_DIR "{ROOT.as_posix()}/build/vcpkg_installed_absent")\n'
                'foreach(name IN ITEMS add_library target_compile_definitions)\nfunction(${name})\nendfunction()\nendforeach()\n'  # 스크립트 모드에 없는 타깃 명령
                f'include("{(POLICY_FILE.parent / "Vcpkg.cmake").as_posix()}")\n'
                "foreach(name IN ITEMS VCPKG_TARGET_TRIPLET VCPKG_HOST_TRIPLET VCPKG_OVERLAY_TRIPLETS VCPKG_OVERLAY_PORTS)\n"
                'message(NOTICE "${name}=[${${name}}]")\n'
                "endforeach()\n",
                encoding="utf-8",
            )
            result = subprocess.run([CMAKE, "-P", str(script)], cwd=ROOT, capture_output=True, text=True, encoding="utf-8")
            self.assertEqual(result.returncode, 0, result.stderr)
            values = dict(text[:-1].split("=[", 1) for text in result.stderr.splitlines() if "=[" in text)
            triplet_dir = Path(values["VCPKG_OVERLAY_TRIPLETS"])
            self.assertNotEqual(values["VCPKG_HOST_TRIPLET"], "")
            self.assertTrue((triplet_dir / f"{values['VCPKG_TARGET_TRIPLET']}.cmake").is_file())
        overlay_ports = {Path(text) for text in values["VCPKG_OVERLAY_PORTS"].split(";")}
        self.assertEqual(overlay_ports, set(ROOT.glob("ThirdParty/*/vcpkg-port")))


class VcpkgInstallPlanTest(unittest.TestCase):
    def test_sections(self) -> None:
        output = (
            "Computing installation plan...\n"
            "The following packages will be removed:\n"
            "    oldport:x64-windows@1.0\n"
            "The following packages will be built and installed:\n"
            "  * zlib:x64-windows@1.3.1 -- C:/overlay/zlib\n"
            "    mimalloc:x64-windows@2.1.7\n"
            "The following packages are already installed:\n"
            "    fmt:x64-windows@11.0.2\n"
            "Additional packages (*) will be modified to complete this operation.\n"
        )
        self.assertEqual(parse_install_plan(output), ("oldport", "zlib;mimalloc"))

    def test_nothing_to_do(self) -> None:
        output = "Computing installation plan...\nAll requested packages are currently installed.\n"
        self.assertEqual(parse_install_plan(output), ("", ""))

    def test_crlf_and_feature_list(self) -> None:
        # 기능 목록의 `;` 가 CMake 리스트를 쪼개지 않는다(`,` 로 바꿔 읽는다).
        output = "The following packages will be rebuilt:\r\n    imgui[core;docking-experimental]:x64-windows@1.91\r\n"
        self.assertEqual(parse_install_plan(output), ("", "imgui[core,docking-experimental]"))


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
    CMAKE = os.environ.get(CMAKE_ENVIRONMENT, "")
    if not CMAKE:
        sys.exit(f"{CMAKE_ENVIRONMENT} 가 없다 — cmake 실행 파일 경로를 준다")
    unittest.main()
