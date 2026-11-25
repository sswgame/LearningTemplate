"""
Linux 검증 입구(`linux_scenarios.py`)의 판정 회귀 검사 — 퍼저 로그 · 체인 단계 줄 · 끝 줄(SCENARIO_RESULT=).

실행: python -X utf8 Scripts/dev/tests/test_linux_scenarios.py (CTest `Scripts.LinuxScenarios`). 빌드 · WSL 없이 돈다.
"""

from __future__ import annotations

import contextlib
import io
import subprocess
import sys
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from dev import linux_scenarios  # noqa: E402
from dev.linux_scenarios import ACTIONS, evaluate_fuzz_log, parse_chain_stage, read_result_line  # noqa: E402


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


    def run_action(self, action: str, arguments: list[str], **fakes: object) -> tuple[int | str, str]:
        """(종료 코드 — 예외면 그 이름, 마지막 줄). 자식 · 클론은 가짜다."""
        defaults = {"build": 0, "run_job": 0, "chain": 0, "configure_and_build": 0, "shell": subprocess.CompletedProcess("", 0), "sync_clone": None, "make_configure_command": "",
                    "run_git_or_raise": "abc"}
        output = io.StringIO()
        with contextlib.ExitStack() as stack, contextlib.redirect_stdout(output):
            for name, value in {**defaults, **fakes}.items():
                kind = {"side_effect": value} if isinstance(value, (list, BaseException)) else {"return_value": value}
                stack.enter_context(mock.patch.object(linux_scenarios, name, **kind))
            try:
                code: int | str = ACTIONS[action](arguments)
            except (RuntimeError, SystemExit) as error:
                code = type(error).__name__
        return code, output.getvalue().splitlines()[-1]

    def test_every_failure_ends_with_the_final_line(self) -> None:
        failed = subprocess.CompletedProcess("", 5)
        for action, arguments, fakes, want in (
            ("all", [], {}, (0, "LINUX_OK=abc")),
            ("all", [], {"build": 7}, (7, "LINUX_FAILED")),
            ("all", [], {"run_job": [0, 4]}, (4, "LINUX_FAILED")),
            ("all", [], {"chain": 3}, (3, "LINUX_FAILED")),
            ("all", [], {"build": RuntimeError("git")}, ("RuntimeError", "LINUX_FAILED")),
            ("all", [], {"chain": SystemExit("no build")}, ("SystemExit", "LINUX_FAILED")),
            ("sanitize", ["thread"], {}, (0, "SANITIZE_OK=thread")),
            ("sanitize", [], {"run_fuzz": (0, "FUZZ_OK")}, (0, "SANITIZE_OK=address")),
            ("sanitize", [], {"run_fuzz": (1, "FUZZ_FAILED=A")}, (1, "SANITIZE_FAILED=fuzz")),
            ("sanitize", [], {"configure_and_build": 1}, (1, "SANITIZE_FAILED=address")),
            ("sanitize", ["thread"], {"shell": failed}, (5, f"SANITIZE_FAILED=thread - {linux_scenarios.VERIFY_LOGS}/lt.sanitize.log")),
            ("sanitize", ["memory"], {}, (2, "sanitize 는 address 또는 thread 다: memory")),
            ("sanitize", [], {"sync_clone": RuntimeError("git")}, ("RuntimeError", "SANITIZE_FAILED")),
            ("fuzz", ["A"], {"configure_and_build": 1}, (1, "FUZZ_FAILED")),
            ("fuzz", ["A"], {"sync_clone": RuntimeError("git")}, ("RuntimeError", "FUZZ_FAILED")),
        ):
            with self.subTest(action=action, fakes=fakes):
                self.assertEqual(self.run_action(action, arguments, **fakes), want)


if __name__ == "__main__":
    unittest.main()
