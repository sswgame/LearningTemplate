"""
시한 정본(`Config/Dev/TimeLimits.json`) → C++ 상수 헤더 생성기(`time_limits.py`) 회귀 검사 — 칸 이름이 상수 이름으로 바뀌고, 스크립트 ctest 시한이
App 의 체인 프로세스 시한보다 짧으면 configure 가 멈춘다. 지금 저장소의 표로 헤더를 만든다.

실행: python -X utf8 Scripts/generate/tests/test_time_limits.py (CTest `Scripts.TimeLimits`).
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.paths import get_project_root  # noqa: E402
from common.time_limits import get_chain_kill_seconds, read_time_limit_sections  # noqa: E402
from generate.generator_output import GeneratorError  # noqa: E402
from generate.time_limits import make_header_text, require_script_limits, to_constant_name  # noqa: E402


class TimeLimitsTest(unittest.TestCase):
    def test_constant_name(self) -> None:
        self.assertEqual(to_constant_name("_processStallSeconds"), "kProcessStallSeconds")
        with self.assertRaises(GeneratorError):
            to_constant_name("processStallSeconds")

    def test_real_table_writes_header(self) -> None:
        root = get_project_root()
        sections = read_time_limit_sections(root)
        require_script_limits(sections, root)
        text = make_header_text(sections)
        self.assertIn(f"inline constexpr uint32 kProcessKillSeconds = {sections['_scenario']['_processKillSeconds']}u;", text)
        self.assertNotIn("_scripts", text)

    def test_ctest_limit_shorter_than_process_kill_is_configure_error(self) -> None:
        root = get_project_root()
        sections = read_time_limit_sections(root)
        sections["_scripts"] = dict(sections["_scripts"], _singleScenarioCtestSeconds=get_chain_kill_seconds(1, root))
        with self.assertRaises(GeneratorError):
            require_script_limits(sections, root)


if __name__ == "__main__":
    unittest.main()
