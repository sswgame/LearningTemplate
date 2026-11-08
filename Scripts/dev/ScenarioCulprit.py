"""체인에서만 지는 단계의 범인(앞 단계)을 이분 탐색한다 — Windows · Linux 공용(py -3 Scripts/dev/ScenarioCulprit.py <체인> <단계> [옵션]).

체인을 처음부터 다시 돌리지 않고, 진 단계 앞의 단계들 중 뒤쪽 구간만 붙여 부분 체인을 돌려 가며 "어느 단계부터 붙이면 지는가" 를 찾는다.
단계 목록 · 나머지 인자(게임 체인의 <Launch> 값 등)는 `Scenario.<체인>` 의 ctest 명령에서 그대로 가져오고 `-scenario=` 만 바꾼다 —
예전 Linux 판은 `engine/automation/editor/` 경로를 고정해 에디터 체인만 됐다. 단계 이름은 마지막 체인 로그(`chain stage i/n … (<이름>)`)의 순번으로
ctest 목록의 경로와 짝짓는다(이름 속성은 경로에서 알 수 없다).

  py -3 Scripts/dev/ScenarioCulprit.py empty surfacefootprint                      # Ninja-Debug, dx12, 무인
  py -3 Scripts/dev/ScenarioCulprit.py editor focusloss --preset Ninja-Release --show
  py -3 Scripts/dev/ScenarioCulprit.py --self-check

Linux 는 `LinuxScenarios.py culprit` 가 같은 함수(`findCulprit`)를 자기 실행기(Xvfb · lavapipe)로 부른다.
"""
from __future__ import annotations

import argparse
import pathlib
import re
import shlex
import subprocess
import sys
from typing import Callable, Sequence

REPO = pathlib.Path(__file__).resolve().parents[2]
kStagePattern = re.compile(r'chain stage (\d+)/\d+ \w+ \(([^)]+)\)')


def findChainCommand(buildDir: pathlib.Path, chain: str) -> list[str]:
    """`Scenario.<chain>` 의 ctest 명령(인자 목록)."""
    listing = subprocess.run(['ctest', '-V', '-N', '-R', f'^Scenario\\.{re.escape(chain)}$'], cwd=buildDir, capture_output=True, text=True, encoding='utf-8', errors='replace').stdout
    found = [line for line in listing.splitlines() if 'Test command:' in line and '-scenario=' in line]
    if not found:
        sys.exit(f'Scenario.{chain} 이 {buildDir} 에 없다')
    return shlex.split(re.sub(r'.*Test command: ', '', found[0]).replace('\\', '/'))


def readChainStages(command: Sequence[str]) -> list[str]:
    """ctest 명령의 `-scenario=` 경로 목록(체인 순서)."""
    for argument in command:
        if argument.startswith('-scenario='):
            return argument[len('-scenario='):].split(',')
    sys.exit('ctest 명령에 -scenario= 가 없다')


def readStageNames(logText: str) -> dict[int, str]:
    """체인 로그의 순번(0 부터) → 단계 이름. 백엔드 교체로 같은 단계가 또 찍히면 처음 것을 쓴다."""
    names: dict[int, str] = {}
    for match in kStagePattern.finditer(logText):
        names.setdefault(int(match.group(1)) - 1, match.group(2))
    return names


def resolveTarget(names: dict[int, str], target: str) -> tuple[int, str]:
    """@p target 은 전체 이름(`game.empty.surfacefootprint`)이나 끝 조각(`surfacefootprint`)."""
    matches = sorted({(index, name) for index, name in names.items() if name == target or name.endswith('.' + target)})
    if len(matches) != 1:
        sys.exit(f'단계 {target!r} 를 체인 로그에서 {"찾지 못했다" if not matches else "하나로 정하지 못했다: " + ", ".join(n for _, n in matches)}')
    return matches[0]


def stageFailed(logText: str, stageName: str) -> bool:
    return re.search(rf'chain stage \d+/\d+ Failed \({re.escape(stageName)}\)', logText) is not None


def findCulprit(before: Sequence[str], fails: Callable[[list[str]], bool]) -> int | None:
    """@p before (진 단계 앞의 단계들) 중 범인의 순번. @p fails(앞 단계 묶음) 는 그 묶음 + 진 단계를 돌려 졌는지 돌려준다.

    before[low:] 는 지고 before[high:] 는 통과하는 경계를 좁힌다. 앞 단계 전부를 붙여도 지지 않으면 None(재현 안 됨).
    """
    if not fails(list(before)):
        return None
    low, high = 0, len(before)
    while high - low > 1:
        middle = (low + high) // 2
        if fails(list(before[middle:])):
            low = middle
        else:
            high = middle
    return low


def selfCheck() -> int:
    stages = [f's{i}' for i in range(10)]
    for culprit in range(10):
        calls: list[list[str]] = []

        def fails(prefix: list[str], culprit: int = culprit) -> bool:
            calls.append(prefix)
            return stages[culprit] in prefix
        assert findCulprit(stages, fails) == culprit, culprit
        assert len(calls) <= 1 + 4, len(calls)   # log2(10) 올림 + 처음 한 번
    assert findCulprit(stages, lambda prefix: False) is None
    print('self-check ok')
    return 0


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description='체인에서만 지는 단계의 앞 단계를 이분 탐색한다')
    parser.add_argument('chain', nargs='?')
    parser.add_argument('stage', nargs='?')
    parser.add_argument('--preset', default='Ninja-Debug')
    parser.add_argument('--backend', default='dx12')
    parser.add_argument('--show', action='store_true', help='창을 띄워 돈다(-unattended 를 뺀다 — 판정은 같다)')
    parser.add_argument('--self-check', action='store_true')
    options = parser.parse_args(argv)
    if options.self_check:
        return selfCheck()
    if not options.chain or not options.stage:
        parser.error('체인과 단계를 적는다 (예: empty surfacefootprint)')

    buildDir = REPO / 'build' / options.preset
    logDir = buildDir / 'Testing' / 'Logs'
    chainLog = logDir / f'Scenario.{options.chain}.{options.backend}.log'
    if not chainLog.is_file():
        sys.exit(f'{chainLog} 이 없다 — 그 백엔드로 체인을 먼저 돌린다')
    command = findChainCommand(buildDir, options.chain)
    paths = readChainStages(command)
    targetIndex, targetName = resolveTarget(readStageNames(chainLog.read_text(encoding='utf-8', errors='replace')), options.stage)
    if targetIndex >= len(paths):
        sys.exit(f'로그의 순번 {targetIndex + 1} 이 ctest 목록({len(paths)} 단계)을 넘는다 — 체인 로그가 지금 빌드와 다르다')
    dropped = ('-unattended',) if options.show else ()
    base = [a for a in command if a not in ('-scenario-backend-split', '-scenario-keep-going') + dropped and not a.startswith(('-scenario-backends=', '-log-file=', '-scenario='))]
    runLog = logDir / 'Culprit.log'

    def fails(prefix: list[str]) -> bool:
        listed = ','.join(prefix + [paths[targetIndex]])
        subprocess.run(base + [f'-scenario={listed}', '-scenario-keep-going', f'-scenario-backends={options.backend}', f'-log-file={runLog}'], cwd=buildDir / 'Bin',
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        text = (logDir / f'Culprit.{options.backend}.log').read_text(encoding='utf-8', errors='replace')
        bad = stageFailed(text, targetName)
        print(f'앞 {len(prefix):3d} 단계 (첫 {prefix[0] if prefix else "-"}) → {"짐" if bad else "통과"}', flush=True)
        return bad

    before = paths[:targetIndex]
    print(f'{options.chain}: {targetName} (단계 {targetIndex + 1}/{len(paths)}) · {options.preset} {options.backend}', flush=True)
    culprit = findCulprit(before, fails)
    if culprit is None:
        print('재현 안 됨 — 단독으로 지는지(단계만 -scenario= 로) · 다른 백엔드 · 체인 밖 상태를 본다')
        return 1
    print('시작 단계:', before[culprit])
    print('단독 쌍으로도 지는가(아니면 뒤 단계들과 함께 만드는 상태다):')
    fails([before[culprit]])
    return 0


if __name__ == '__main__':
    sys.exit(main())
