"""
지다 만 ninja 재압축 보정 검사 — 옛 기록이 사라졌으면 .recompact 를 제자리로, 새로 열린 .ninja_log 면 완성본을 뒤에 잇고, 남은 .recompact 는 지운다
(cmake/Modules/Build/NinjaBuildLogs.cmake 를 `cmake -P` 로 부른다).

실행: SW_CMAKE_COMMAND=<cmake 실행 파일> python -X utf8 Scripts/setup/tests/test_ninja_build_logs.py (CTest `Scripts.NinjaBuildLogs` 가 환경 변수를 준다).
"""

from __future__ import annotations

import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
MODULE_FILE = ROOT / "cmake/Modules/Build/NinjaBuildLogs.cmake"
CMAKE_ENVIRONMENT = "SW_CMAKE_COMMAND"  # IntegrationTests/CMakeLists.txt 가 스크립트 테스트 모두에 ${CMAKE_COMMAND} 를 준다
CMAKE = ""


def restore(binary_dir: Path) -> None:
    script = binary_dir / "Probe.cmake"
    script.write_text(f'include("{MODULE_FILE.as_posix()}")\nsw_restore_ninja_build_logs("{binary_dir.as_posix()}")\n', encoding="utf-8")
    subprocess.run([CMAKE, "-P", str(script)], capture_output=True, text=True, check=True)


class NinjaBuildLogsTest(unittest.TestCase):
    def setUp(self) -> None:
        self._folder = tempfile.TemporaryDirectory()
        self.addCleanup(self._folder.cleanup)
        self.binary_dir = Path(self._folder.name)

    def write(self, name: str, data: bytes) -> Path:
        path = self.binary_dir / name
        path.write_bytes(data)
        return path

    def test_missing_logs_come_back(self) -> None:
        self.write(".ninja_deps.recompact", b"\x00binary;[deps]")
        self.write(".ninja_log.recompact", b"# ninja log v6\n1\t2\t3\ta.obj\tff\n")
        restore(self.binary_dir)
        self.assertEqual((self.binary_dir / ".ninja_deps").read_bytes(), b"\x00binary;[deps]")
        self.assertTrue((self.binary_dir / ".ninja_log").read_bytes().startswith(b"# ninja log v6"))
        self.assertEqual(list(self.binary_dir.glob("*.recompact")), [])

    def test_new_log_takes_complete_lines(self) -> None:
        self.write(".ninja_log", b"# ninja log v6\n0\t1\t2\tglobs.stamp\taa\n")
        self.write(".ninja_log.recompact", b"# ninja log v6\n" + b"1\t2\t3\tdir;x/[a].obj\tff\n" * 20)
        restore(self.binary_dir)
        lines = (self.binary_dir / ".ninja_log").read_text(encoding="utf-8").splitlines()
        self.assertIn("0\t1\t2\tglobs.stamp\taa", lines)
        self.assertEqual(lines.count("1\t2\t3\tdir;x/[a].obj\tff"), 20)
        self.assertEqual(list(self.binary_dir.glob("*.recompact")), [])

    def test_leftover_beside_a_live_log_is_dropped(self) -> None:
        self.write(".ninja_deps", b"live deps that are longer")
        self.write(".ninja_deps.recompact", b"old")
        self.write(".ninja_log", b"# ninja log v6\n" + b"1\t2\t3\ta.obj\tff\n" * 5)
        self.write(".ninja_log.recompact", b"# ninja")
        restore(self.binary_dir)
        self.assertEqual((self.binary_dir / ".ninja_deps").read_bytes(), b"live deps that are longer")
        self.assertEqual((self.binary_dir / ".ninja_log").read_bytes().count(b"a.obj"), 5)
        self.assertEqual(list(self.binary_dir.glob("*.recompact")), [])


if __name__ == "__main__":
    CMAKE = os.environ.get(CMAKE_ENVIRONMENT, "")
    if not CMAKE:
        print(f"{CMAKE_ENVIRONMENT} is not set")
        sys.exit(1)
    unittest.main()
