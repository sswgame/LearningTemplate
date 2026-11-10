"""체인에서만 지는 단계의 범인(앞 단계)을 이분 탐색한다 — Windows · Linux 공용(py -3 Scripts/dev/ScenarioCulprit.py <체인> <단계> [옵션]).

체인을 처음부터 다시 돌리지 않고, 진 단계 앞의 단계들 중 뒤쪽 구간만 붙여 부분 체인을 돌려 가며 "어느 단계부터 붙이면 지는가" 를 찾는다.
단계 목록 · 나머지 인자(게임 체인의 <Launch> 값 등)는 `Scenario.<체인>` 의 ctest 명령에서 그대로 가져오고(`common/AppArguments`) `-scenario=` 만 바꾼다 —
예전 Linux 판은 `engine/automation/editor/` 경로를 고정해 에디터 체인만 됐다. 단계 이름은 마지막 체인 로그(`chain stage i/n … (<이름>)`)의 순번으로
ctest 목록의 경로와 짝짓는다(이름 속성은 경로에서 알 수 없다).

  py -3 Scripts/dev/ScenarioCulprit.py empty surfacefootprint                      # Ninja-Debug, dx12, 무인
  py -3 Scripts/dev/ScenarioCulprit.py editor focusloss --preset Ninja-Release --show
  py -3 Scripts/dev/ScenarioCulprit.py --self-check
  py -3 Scripts/dev/ScenarioCulprit.py --golden-margins [보고 폴더…]                # 흔들리는 골든: 보고의 goldenSamples[] 를 모아 골든별 최대 · 여유

Linux 는 `LinuxScenarios.py culprit` 가 같은 함수(`findCulprit`)를 자기 실행기(Xvfb · lavapipe)로 부른다.
"""
from __future__ import annotations

import argparse
import pathlib
import re
import subprocess
import sys
from typing import Callable, Sequence

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))   # Scripts
from common import AppArguments  # noqa: E402

REPO = pathlib.Path(__file__).resolve().parents[2]
kStagePattern = re.compile(r'chain stage (\d+)/\d+ \w+ \(([^)]+)\)')


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


kGoldenAxes = (('differingFraction', 'maxDifferingFraction'), ('meanDistance', 'maxMeanDistance'), ('largestDeepRegionPixels', 'maxDeepRegionPixels'))


def summarizeGoldenMargins(listReport: Sequence[dict]) -> list[dict]:
    """보고들(`goldenSamples[]`)을 골든마다 모아 축마다 최대값 · 상한 · 여유(상한 / 최대값, 최대가 0 이면 inf)를 낸다. 여유가 작은 골든부터.

    흔들리는 골든을 고칠 때 보는 표다 — 같은 골든을 여러 번 돌린 보고(체인 여러 번 · 백엔드마다)를 넣으면 실행 사이 흔들림이 최대값에 드러난다.
    """
    byGolden: dict[str, dict] = {}
    for report in listReport:
        for sample in report.get('goldenSamples', []):
            row = byGolden.setdefault(sample['golden'], {'golden': sample['golden'], 'runs': 0})
            row['runs'] += 1
            for valueKey, limitKey in kGoldenAxes:
                row[valueKey] = max(row.get(valueKey, 0), sample[valueKey])
                row[limitKey] = sample[limitKey]   # 상한은 실행마다 같다(정책 · 단계 속성) — 마지막 것을 쓴다
    for row in byGolden.values():
        row['margin'] = min((row[limitKey] / row[valueKey]) if row[valueKey] > 0 else float('inf') for valueKey, limitKey in kGoldenAxes)
    return sorted(byGolden.values(), key=lambda row: (row['margin'], row['golden']))


def printGoldenMargins(folders: Sequence[str], preset: str) -> int:
    import json
    roots = [pathlib.Path(folder) for folder in folders] or [REPO / 'build' / preset / 'Bin' / 'Saved' / 'Automation']
    listReport = []
    for root in roots:
        for path in sorted(root.rglob('*.json')) if root.is_dir() else [root]:
            try:
                document = json.loads(path.read_text(encoding='utf-8'))
            except (OSError, ValueError):
                continue   # 보고가 아닌 JSON(설정 · 메타) · 쓰다 만 보고는 건너뛴다
            if isinstance(document, dict) and 'goldenSamples' in document:
                listReport.append(document)
    rows = summarizeGoldenMargins(listReport)
    if not rows:
        print(f'goldenSamples 가 있는 보고가 없다: {", ".join(str(root) for root in roots)} — 기록 모드가 아닌 실행의 보고를 넣는다')
        return 1
    print(f'보고 {len(listReport)} · 골든 {len(rows)} — 여유 = 상한 / 실행 중 최대값(가장 작은 축), 1 미만이면 진 실행이 있다')
    print(f'{"margin":>7} {"runs":>4} {"frac%":>7} {"mean":>8} {"region":>6}  golden')
    for row in rows:
        print(f'{row["margin"]:7.2f} {row["runs"]:4d} {row["differingFraction"] * 100:7.3f} {row["meanDistance"]:8.5f} {row["largestDeepRegionPixels"]:6d}  {row["golden"]}')
    regions = sorted(row['largestDeepRegionPixels'] for row in rows)
    print(f'덩어리 최대 {regions[-1]} px · p99 {regions[min(len(regions) - 1, int(0.99 * len(regions)))]} px (상한 {rows[0]["maxDeepRegionPixels"]} px)')
    return 0


def selfCheck() -> int:
    reports = [
        {'goldenSamples': [{'golden': 'a', 'differingFraction': 0.002, 'meanDistance': 0.0001, 'largestDeepRegionPixels': 4, 'maxDifferingFraction': 0.01, 'maxMeanDistance': 0.005, 'maxDeepRegionPixels': 16}]},
        {'goldenSamples': [{'golden': 'a', 'differingFraction': 0.001, 'meanDistance': 0.0001, 'largestDeepRegionPixels': 8, 'maxDifferingFraction': 0.01, 'maxMeanDistance': 0.005, 'maxDeepRegionPixels': 16},
                           {'golden': 'b', 'differingFraction': 0.0, 'meanDistance': 0.0, 'largestDeepRegionPixels': 0, 'maxDifferingFraction': 0.01, 'maxMeanDistance': 0.005, 'maxDeepRegionPixels': 16}]},
        {'records': []},
    ]
    rows = summarizeGoldenMargins(reports)
    assert [row['golden'] for row in rows] == ['a', 'b'], rows
    assert rows[0]['runs'] == 2 and rows[0]['largestDeepRegionPixels'] == 8 and rows[0]['differingFraction'] == 0.002, rows[0]
    assert rows[0]['margin'] == 2.0, rows[0]   # 덩어리 16 / 8 이 가장 작은 여유
    assert rows[1]['margin'] == float('inf'), rows[1]

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
    parser.add_argument('--golden-margins', nargs='*', metavar='보고 폴더 · 파일',
                        help='체인 대신: 시나리오 보고(goldenSamples[])를 모아 골든마다 실행 사이 최대 차이와 여유를 낸다(기본 build/<preset>/Bin/Saved/Automation)')
    options = parser.parse_args(argv)
    if options.self_check:
        return selfCheck()
    if options.golden_margins is not None:
        return printGoldenMargins(options.golden_margins, options.preset)
    if not options.chain or not options.stage:
        parser.error('체인과 단계를 적는다 (예: empty surfacefootprint)')

    buildDir = REPO / 'build' / options.preset
    logDir = buildDir / 'Testing' / 'Logs'
    chainLog = logDir / f'Scenario.{options.chain}.{options.backend}.log'
    if not chainLog.is_file():
        sys.exit(f'{chainLog} 이 없다 — 그 백엔드로 체인을 먼저 돌린다')
    command = AppArguments.findChainCommand(buildDir, options.chain)
    paths = AppArguments.readChainStages(command)
    targetIndex, targetName = resolveTarget(readStageNames(chainLog.read_text(encoding='utf-8', errors='replace')), options.stage)
    if targetIndex >= len(paths):
        sys.exit(f'로그의 순번 {targetIndex + 1} 이 ctest 목록({len(paths)} 단계)을 넘는다 — 체인 로그가 지금 빌드와 다르다')
    dropped = ('unattended',) if options.show else ()
    base = AppArguments.without(command, 'scenario-backend-split', 'scenario-keep-going', 'scenario-backends', 'log-file', 'scenario', *dropped)
    runLog = logDir / 'Culprit.log'

    def fails(prefix: list[str]) -> bool:
        listed = ','.join(prefix + [paths[targetIndex]])
        subprocess.run(base + [AppArguments.option('scenario', listed), AppArguments.flag('scenario-keep-going'), AppArguments.option('scenario-backends', options.backend),
                               AppArguments.option('log-file', runLog)], cwd=buildDir / 'Bin',
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
