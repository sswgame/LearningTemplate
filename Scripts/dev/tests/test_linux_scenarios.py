"""
Linux 검증 입구(`linux_scenarios.py`)의 판정 회귀 검사 — 퍼저 로그 · 체인 단계 줄 · 끝 줄(SCENARIO_RESULT=).

실행: python -X utf8 Scripts/dev/tests/test_linux_scenarios.py (CTest `Scripts.LinuxScenarios`). 빌드 · WSL 없이 돈다.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from dev.linux_scenarios import evaluate_fuzz_log, parse_chain_stage, read_result_line  # noqa: E402


class LinuxScenariosTest(unittest.TestCase):
    def test_log_checks(self) -> None:
        assert evaluate_fuzz_log('INFO: Loaded 3 modules\n==1==ERROR: AddressSanitizer: heap-buffer-overflow') == 'ERROR: AddressSanitizer'
        assert evaluate_fuzz_log('INFO: Loaded 1 modules   (10 inline 8-bit counters)').startswith('coverage reaches only')
        assert evaluate_fuzz_log('INFO: Loaded 4 modules   (99 inline 8-bit counters)\nDone 2000 runs') is None
        # 체인 단계 끝 줄 — 앞머리는 그대로, 진 단계는 판정 값 한 줄(`<종류> - <까닭>`)이 붙는다. `: <Pass/>` 로 끝나는 실패 줄은 없다.
        failed = '[Error] [Scenario] chain stage 12/94 Failed (game.playground.vfxsort): UnhandledLog - [Warning] (id=Shadow.AtlasFull) atlas full (+2) - 3 later stage(s) skipped'
        assert parse_chain_stage(failed) == (12, 94, 'Failed', 'game.playground.vfxsort', 'UnhandledLog - [Warning] (id=Shadow.AtlasFull) atlas full (+2)'), parse_chain_stage(failed)
        assert parse_chain_stage('[Info] [Scenario] chain stage 1/2 Passed (editor.focus)') == (1, 2, 'Passed', 'editor.focus', '')
        assert parse_chain_stage('chain stage 3/3 Skipped (a.b): no window manager') == (3, 3, 'Skipped', 'a.b', 'no window manager')
        assert parse_chain_stage('[Scenario] chain Failed (3 stage(s), 1 failed, 0 skipped, 1 not run)') is None
        # 끝 줄 — 마지막 것을 읽고, 없으면 None(크래시는 통과가 아니다).
        assert read_result_line('x\n[Info] [Scenario] SCENARIO_RESULT=FAILED stages=4 failed=1 notRun=2\n') == 'SCENARIO_RESULT=FAILED stages=4 failed=1 notRun=2'
        assert read_result_line('TEST_RESULT=FAILED run=1 failed=1 skipped=0\nTEST_RESULT=PASSED run=3 failed=0 skipped=0') == 'TEST_RESULT=PASSED run=3 failed=0 skipped=0'
        assert read_result_line('[==========] 3 case run(s)') is None


if __name__ == "__main__":
    unittest.main()
