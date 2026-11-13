"""App 명령줄 인자를 만드는 · 읽는 한 곳 — 스크립트는 인자 철자를 직접 적지 않고 여기를 거친다.

정본은 C++ 이다: 인자는 `Source/Core/Predefined/ArgumentList.xxx` 의 `SW_REGISTER_ARGUMENT` 줄, 전역 변수(`-gv_…`)는 `SW_*GLOBAL_VARIABLE( 형, 이름, …)`.
여기는 그 표를 읽어 이름이 있는지, 값을 받는 인자인지(기본값이 bool 이면 `-key` 만, 아니면 `-key=value`)를 확인하고 틀리면 멈춘다 —
인자 이름을 바꾸면 스크립트가 엉뚱한 명령을 조용히 만드는 대신(예: 걸러야 할 `-scenario-backends=` 를 못 걸러 백엔드 넷이 돈다) 시작에서 진다.
ctest 의 체인 명령(`Scenario.<체인>`)을 읽는 것도 여기 하나다.
"""
from __future__ import annotations

import functools
import re
import shlex
import subprocess
from pathlib import Path
from typing import Mapping, Sequence

from common.paths import get_project_root

ARGUMENT_TABLE = Path('Source') / 'Core' / 'Predefined' / 'ArgumentList.xxx'
ARGUMENT_LINE = re.compile(r'^SW_REGISTER_ARGUMENT\(\s*\w+\s*,\s*([^,]+?)\s*,\s*\w+\s*,\s*(.+?)\s*\)\s*$', re.MULTILINE)
GLOBAL_VARIABLE = re.compile(r'SW_[A-Z_]*GLOBAL_VARIABLE[A-Z_]*\(\s*[\w:<> ]+?\s*,\s*(gv_\w+)\s*,')
SOURCE_SUFFIX = {'.cpp', '.h', '.inl'}


class AppArgumentError(RuntimeError):
    """표에 없는 인자 · 값 모양이 틀린 인자."""


@functools.cache
def read_argument_table() -> dict[str, bool]:
    """명령줄 철자 → 값을 받는가. 한 줄의 철자 여럿(백엔드 별칭)은 모두 같은 모양이다."""
    text = (get_project_root() / ARGUMENT_TABLE).read_text(encoding='utf-8')
    table: dict[str, bool] = {}
    for default_value, spellings in ARGUMENT_LINE.findall(text):
        takes_value = default_value not in ('false', 'true')
        for spelling in re.findall(r'"([^"]+)"', spellings):
            table[spelling] = takes_value
    if not table:
        raise AppArgumentError(f'{ARGUMENT_TABLE}: SW_REGISTER_ARGUMENT 줄을 하나도 읽지 못했다 — 표 모양이 바뀌었으면 kArgumentLine 을 고친다')
    return table


@functools.cache
def read_global_variable_names() -> frozenset[str]:
    """`Source/` 가 등록하는 전역 변수 이름(`gv_…`)."""
    names: set[str] = set()
    for path in (get_project_root() / 'Source').rglob('*'):
        if path.suffix in SOURCE_SUFFIX:
            text = path.read_text(encoding='utf-8', errors='replace')
            if 'GLOBAL_VARIABLE' in text:
                names.update(GLOBAL_VARIABLE.findall(text))
    return frozenset(names)


def check_argument(name: str, takes_value: bool) -> None:
    table = read_argument_table()
    if name not in table:
        raise AppArgumentError(f'-{name}: {ARGUMENT_TABLE} 에 없는 인자다')
    if table[name] != takes_value:
        raise AppArgumentError(f'-{name}: ' + ('값을 받는 인자다(-key=value)' if table[name] else 'bool 인자라 값을 적지 않는다(-key)'))


def flag(name: str) -> str:
    """bool 인자 `-name`."""
    check_argument(name, False)
    return f'-{name}'


def option(name: str, value: object) -> str:
    """값을 받는 인자 `-name=value`."""
    check_argument(name, True)
    return f'-{name}={value}'


def variable(name: str, value: object) -> str:
    """전역 변수 `-gv_name=value`."""
    if name not in read_global_variable_names():
        raise AppArgumentError(f'-{name}: Source/ 에 등록된 전역 변수가 아니다(SW_*GLOBAL_VARIABLE)')
    return f'-{name}={value}'


def variables(values: Mapping[str, object]) -> list[str]:
    return [variable(name, value) for name, value in values.items()]


def read_name(token: str) -> str | None:
    """`-name` · `-name=value` 의 name. 인자가 아니면 None."""
    if not token.startswith('-'):
        return None
    return token[1:].split('=', 1)[0]


def check_name(name: str) -> None:
    if name not in read_argument_table() and name not in read_global_variable_names():
        raise AppArgumentError(f'-{name}: 인자 표 · 전역 변수 어디에도 없다')


def without(command: Sequence[str], *names: str) -> list[str]:
    """@p command 에서 @p names 인자를 뺀다(값 있는 것 · 없는 것 모두)."""
    for name in names:
        check_name(name)
    return [token for token in command if read_name(token) not in names]


def read_value(command: Sequence[str], name: str) -> str | None:
    check_name(name)
    for token in command:
        if read_name(token) == name and '=' in token:
            return token.split('=', 1)[1]
    return None


def find_chain_command(build_dir: Path, chain: str, env: Mapping[str, str] | None = None) -> list[str]:
    """`Scenario.<chain>` 의 ctest 명령(인자 목록). 없으면 멈춘다."""
    listing = subprocess.run(['ctest', '-V', '-N', '-R', f'^Scenario\\.{re.escape(chain)}$'], cwd=build_dir, env=env, capture_output=True, text=True,
                             encoding='utf-8', errors='replace').stdout
    for line in listing.splitlines():
        if 'Test command:' in line:
            command = shlex.split(re.sub(r'.*Test command: ', '', line).replace('\\', '/'))
            if read_value(command, 'scenario') is not None:
                return command
    raise AppArgumentError(f'Scenario.{chain} 이 {build_dir} 의 ctest 에 없다')


def read_chain_stages(command: Sequence[str]) -> list[str]:
    """체인 명령의 `-scenario=` 경로 목록(체인 순서)."""
    value = read_value(command, 'scenario')
    if value is None:
        raise AppArgumentError('명령에 -scenario= 가 없다')
    return value.split(',')


def self_check() -> int:
    assert read_argument_table()['scenario'] is True and read_argument_table()['unattended'] is False
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
    print('self-check ok')
    return 0


if __name__ == '__main__':
    raise SystemExit(self_check())
