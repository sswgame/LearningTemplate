"""
빌드 시간 보고(`build_time_report.py`) 회귀 검사 — 생성 TU 비중과 마지막 ninja 실행 한 번의 단계 종류별 합(묶음 검증이 부르는 `summarize_last_build`).

실행: python -X utf8 Scripts/dev/tests/test_build_time_report.py. 파일은 임시 폴더에 만든다.
"""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/dev
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

import build_time_report  # noqa: E402


class BuildTimeReportTest(unittest.TestCase):
    def test_self_check(self) -> None:
        build_time_report.self_check()

    def test_last_build_reads_the_build_folder(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            build_dir = Path(folder)
            (build_dir / ".ninja_log").write_text(
                "# ninja log v6\n0\t500\t1\ta.obj\th\n0\t100\t1\ta.obj\th\n100\t130\t1\tApp.exe\th\n", encoding="utf-8")
            (build_dir / "build.ninja").write_text(
                "build a.obj: CXX_COMPILER__App_Release a.cpp\nbuild App.exe: CXX_EXECUTABLE_LINKER__App_Release a.obj\n", encoding="utf-8")
            times = build_time_report.summarize_last_build(build_dir)
        self.assertEqual((times.compile_seconds, times.link_seconds, times.wall_seconds, times.step_count), (0.1, 0.03, 0.13, 2))


if __name__ == "__main__":
    unittest.main()
