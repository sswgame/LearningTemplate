"""
App 명령줄 인자 만들기(`app_arguments.py`) 회귀 검사 — 인자 표에 없는 인자 · 값 없는 인자를 거절하고, 체인 명령에서 빼고 읽는다.

실행: python -X utf8 Scripts/common/tests/test_app_arguments.py (CTest `Scripts.AppArguments`). 저장소의 인자 표를 읽는다.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.app_arguments import (  # noqa: E402
    AppArgumentError, flag, option, read_argument_table, read_chain_stages, read_global_variable_names, read_shipping_rejected_arguments, variable, without,
)


class AppArgumentsTest(unittest.TestCase):
    def test_arguments(self) -> None:
        assert read_argument_table()['scenario'] is True and read_argument_table()['unattended'] is False
        rejected = read_shipping_rejected_arguments()
        assert '-test' in rejected and '-EnableEditor' in rejected and '-unattended' not in rejected and '-log-file=x' not in rejected
        assert flag('scenario-keep-going') == '-scenario-keep-going'
        assert option('scenario', 'a,b') == '-scenario=a,b'
        assert 'gv_scenarioWallScale' in read_global_variable_names()
        for bad in (lambda: flag('scenario'), lambda: option('unattended', 1), lambda: flag('no-such-argument'), lambda: variable('gv_noSuchVariable', 1)):
            try:
                bad()
            except AppArgumentError:
                continue
            raise AssertionError('잘못된 인자가 통과했다')
        command = ['App', '-scenario=a,b', '-unattended', '-scenario-backends=all', '-log-file=x']
        assert without(command, 'unattended', 'scenario-backends') == ['App', '-scenario=a,b', '-log-file=x']
        assert read_chain_stages(command) == ['a', 'b']


if __name__ == "__main__":
    unittest.main()
