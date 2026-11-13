"""체인에서만 지는 단계의 범인(앞 단계)을 이분 탐색한다 — Windows · Linux 공용(py -3 Scripts/dev/scenario_bisect.py <체인> <단계> [옵션]).

체인을 처음부터 다시 돌리지 않고, 진 단계 앞의 단계들 중 뒤쪽 구간만 붙여 부분 체인을 돌려 가며 "어느 단계부터 붙이면 지는가" 를 찾는다.
단계 목록 · 나머지 인자(게임 체인의 <Launch> 값 등)는 `Scenario.<체인>` 의 ctest 명령에서 그대로 가져오고(`common/AppArguments`) `-scenario=` 만 바꾼다 —
단계 이름은 시나리오 파일의 `name` 속성이고, 판정은 로그 문구가 아니라 실행마다 비운 보고 폴더(`build/<프리셋>/Saved/Culprit/run<N>` 에
`-scenario-report=<폴더>/report.json` — 체인 단계마다 `report[.<백엔드>].<단계>.json`)의 JSON `result` 다. 보고가 없는 단계(크래시 · 행)는 짐이다.

  py -3 Scripts/dev/scenario_bisect.py empty surfacefootprint                      # Ninja-Debug, dx12, 무인
  py -3 Scripts/dev/scenario_bisect.py editor focusloss --preset Ninja-Release --show
  py -3 Scripts/dev/scenario_bisect.py --self-check
  py -3 Scripts/dev/scenario_bisect.py --golden-margins [보고 폴더…]                # 흔들리는 골든: 보고의 goldenSamples[] 를 모아 골든별 최대 · 여유
  py -3 Scripts/dev/scenario_bisect.py --golden-coverage                            # 플랫폼 · 백엔드마다 골든 수(체인 백엔드 표와 대조)

Linux 는 `linux_scenarios.py bisect` 가 같은 함수(`find_first_bad_step`)를 자기 실행기(Xvfb · lavapipe)로 부른다.
"""
from __future__ import annotations

import argparse
import pathlib
import re
import subprocess
import sys
from typing import Callable, Sequence

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))   # Scripts
from common import app_arguments  # noqa: E402

REPO = pathlib.Path(__file__).resolve().parents[2]



def build_preset_or_exit(preset: str) -> None:
    """
    시나리오를 돌리기 전에 @p preset 을 증분 빌드한다(바뀐 것이 없으면 몇 초). 짓지 않으면 고친 코드 대신 옛 바이너리가 돌아 고친 단계가 다시 지거나,
    고치기 전 결함을 다시 찾는다(preflight.sh --from 과 같은 틈). 지면 멈춘다 — 반쯤 지은 바이너리로 돌리지 않는다.
    """
    print(f'{preset} 증분 빌드', flush=True)
    result = subprocess.run(['cmake', '--build', '--preset', preset], cwd=REPO, capture_output=True, text=True, encoding='utf-8', errors='replace')
    if result.returncode != 0:
        sys.exit('\n'.join([line for line in result.stdout.splitlines() if 'error' in line or 'FAILED' in line][:30] + [f'{preset} 빌드 실패 — 고친 뒤 다시 돌린다']))


def resolve_target(names: dict[int, str], target: str) -> tuple[int, str]:
    """@p target 은 전체 이름(`game.empty.surfacefootprint`)이나 끝 조각(`surfacefootprint`)."""
    matches = sorted({(index, name) for index, name in names.items() if name == target or name.endswith('.' + target)})
    if len(matches) != 1:
        sys.exit(f'단계 {target!r} 를 체인 목록에서 {"찾지 못했다" if not matches else "하나로 정하지 못했다: " + ", ".join(n for _, n in matches)}')
    return matches[0]



def find_first_bad_step(before: Sequence[str], fails: Callable[[list[str]], bool]) -> int | None:
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


def minimize_stages(stages: Sequence[str], fails: Callable[[list[str]], bool]) -> list[str]:
    """지는 앞 단계 묶음 @p stages 에서 빼도 여전히 지는 단계를 하나씩 버려 남은 묶음을 돌려준다(첫 단계 — 시작 단계 — 는 남긴다).

    시작 단계 하나만으로는 지지 않을 때(뒤 단계와 함께 상태를 만든다) 쓴다. 단계마다 한 번 돈다.
    결함이 확률로 나오면(같은 묶음이 실행마다 갈린다) 결과가 수렴하지 않는다 — 그때는 묶음이 아니라 쌓인 상태 · 타이밍을 본다.
    """
    kept = list(stages)
    index = 1
    while index < len(kept):
        candidate = kept[:index] + kept[index + 1:]
        if fails(candidate):
            kept = candidate
        else:
            index += 1
    return kept


SCENARIO_NAME = re.compile(r'<Scenario\b[^>]*\bname="([^"]+)"')
PASSED_RESULTS = frozenset({'Passed', 'Skipped'})


def read_backend_chain_order(log_text: str) -> list[str]:
    """백엔드 체인 로그(`Scenario.<체인>.<백엔드>.log`)가 실제로 돈 단계 순서 — 시나리오를 백엔드에 나눠 주는 체인(`-scenario-backend-split`)은
    백엔드마다 ctest 명령의 일부만 돈다. 로그의 `loaded from <경로>` 줄이 그 순서다(같은 경로가 다시 나오면 처음 것만)."""
    list_path: list[str] = []
    for match in re.finditer(r'loaded from (\S+\.scenario\.xml)', log_text):
        if match.group(1) not in list_path:
            list_path.append(match.group(1))
    return list_path


def read_scenario_name(path: str) -> str:
    """시나리오 리소스 id(`game/x/automation/y.scenario.xml`)의 `name` 속성. 없으면 멈춘다(체인 목록이 지금 Content 와 다르다)."""
    match = SCENARIO_NAME.search((REPO / 'Content' / path).read_text(encoding='utf-8'))
    if match is None:
        sys.exit(f'{path}: <Scenario name="…"> 이 없다')
    return match.group(1)


def make_run_report_folder(build_dir: pathlib.Path, run_number: int) -> pathlib.Path:
    """부분 체인 실행 하나의 보고 폴더(`build/<프리셋>/Saved/Culprit/run<N>`) — 실행마다 새로 비워, 앞 실행의 보고를 다음 판정이 읽지 않는다."""
    import shutil
    run_dir = build_dir / 'Saved' / 'Culprit' / f'run{run_number}'
    shutil.rmtree(run_dir, ignore_errors=True)
    run_dir.mkdir(parents=True)
    return run_dir


def read_run_reports(run_dir: pathlib.Path) -> list[dict]:
    """실행 폴더 아래 단계 보고(`report[.<백엔드>].<단계>.json` — name · result 가 있는 것)를 모은다."""
    import json
    list_report = []
    for path in sorted(run_dir.rglob('*.json')):
        try:
            document = json.loads(path.read_text(encoding='utf-8'))
        except (OSError, ValueError):
            continue
        if isinstance(document, dict) and 'name' in document and 'result' in document:
            list_report.append(document)
    return list_report


def stage_failed_in_reports(list_report: Sequence[dict], stage_name: str) -> bool:
    """그 단계의 보고가 통과 · 건너뜀이 아니면 짐. 보고가 없으면(크래시 · 행으로 못 씀) 짐이다 — 통과로 치지 않는다."""
    list_result = [report['result'] for report in list_report if report['name'] == stage_name]
    return not list_result or any(result not in PASSED_RESULTS for result in list_result)


GOLDEN_AXES = (('differingFraction', 'maxDifferingFraction'), ('meanDistance', 'maxMeanDistance'), ('largestDeepRegionPixels', 'maxDeepRegionPixels'))


def summarize_golden_margins(list_report: Sequence[dict]) -> list[dict]:
    """보고들(`goldenSamples[]`)을 골든마다 모아 축마다 최대값 · 상한 · 여유(상한 / 최대값, 최대가 0 이면 inf)를 낸다. 여유가 작은 골든부터.

    흔들리는 골든을 고칠 때 보는 표다 — 같은 골든을 여러 번 돌린 보고(체인 여러 번 · 백엔드마다)를 넣으면 실행 사이 흔들림이 최대값에 드러난다.
    """
    by_golden: dict[str, dict] = {}
    for report in list_report:
        for sample in report.get('goldenSamples', []):
            row = by_golden.setdefault(sample['golden'], {'golden': sample['golden'], 'runs': 0})
            row['runs'] += 1
            for value_key, limit_key in GOLDEN_AXES:
                row[value_key] = max(row.get(value_key, 0), sample[value_key])
                row[limit_key] = sample[limit_key]   # 상한은 실행마다 같다(정책 · 단계 속성) — 마지막 것을 쓴다
    for row in by_golden.values():
        row['margin'] = min((row[limit_key] / row[value_key]) if row[value_key] > 0 else float('inf') for value_key, limit_key in GOLDEN_AXES)
    return sorted(by_golden.values(), key=lambda row: (row['margin'], row['golden']))


def print_golden_margins(folders: Sequence[str], preset: str) -> int:
    import json
    roots = [pathlib.Path(folder) for folder in folders] or [REPO / 'build' / preset / 'Bin' / 'Saved' / 'Automation']
    list_report = []
    for root in roots:
        for path in sorted(root.rglob('*.json')) if root.is_dir() else [root]:
            try:
                document = json.loads(path.read_text(encoding='utf-8'))
            except (OSError, ValueError):
                continue   # 보고가 아닌 JSON(설정 · 메타) · 쓰다 만 보고는 건너뛴다
            if isinstance(document, dict) and 'goldenSamples' in document:
                list_report.append(document)
    rows = summarize_golden_margins(list_report)
    if not rows:
        print(f'goldenSamples 가 있는 보고가 없다: {", ".join(str(root) for root in roots)} — 기록 모드가 아닌 실행의 보고를 넣는다')
        return 1
    print(f'보고 {len(list_report)} · 골든 {len(rows)} — 여유 = 상한 / 실행 중 최대값(가장 작은 축), 1 미만이면 진 실행이 있다')
    print(f'{"margin":>7} {"runs":>4} {"frac%":>7} {"mean":>8} {"region":>6}  golden')
    for row in rows:
        print(f'{row["margin"]:7.2f} {row["runs"]:4d} {row["differingFraction"] * 100:7.3f} {row["meanDistance"]:8.5f} {row["largestDeepRegionPixels"]:6d}  {row["golden"]}')
    regions = sorted(row['largestDeepRegionPixels'] for row in rows)
    print(f'덩어리 최대 {regions[-1]} px · p99 {regions[min(len(regions) - 1, int(0.99 * len(regions)))]} px (상한 {rows[0]["maxDeepRegionPixels"]} px)')
    return 0


#: 골든 이름의 플랫폼 꼬리(`AutomationGoldenUtil::getPlatformTag`) — 골든을 뜨는 플랫폼은 이 둘이다(Android · 웹은 아직 체인이 없다).
GOLDEN_PLATFORM_SUFFIX = {'Windows': '', 'Linux': '.linux'}


def count_golden_coverage(list_golden_path: Sequence[str], tag_by_backend: dict[str, str]) -> tuple[dict[tuple[str, str], int], int, list[str]]:
    """골든 파일 경로들(`<폴더>/golden/<시나리오>.<이름>.<꼬리>[.linux].ppm`)을 (플랫폼, 백엔드)마다 센다.

    돌려주는 것: (플랫폼, 백엔드) → 골든 수, 골든 열쇠(폴더 · 시나리오 · 이름) 수, 쿠킹 표에 없는 꼬리의 파일(지운 백엔드의 골든).
    """
    backend_by_tag = {tag: backend for backend, tag in tag_by_backend.items()}
    counts: dict[tuple[str, str], int] = {}
    keys: set[str] = set()
    unknown: list[str] = []
    for path in list_golden_path:
        stem = path[:-len('.ppm')]
        platform = 'Linux' if stem.endswith('.linux') else 'Windows'
        stem = stem[:-len('.linux')] if platform == 'Linux' else stem
        key, _, tag = stem.rpartition('.')
        if tag not in backend_by_tag:
            unknown.append(path)
            continue
        keys.add(key)
        counts[(platform, backend_by_tag[tag])] = counts.get((platform, backend_by_tag[tag]), 0) + 1
    return counts, len(keys), unknown


def print_golden_coverage() -> int:
    """플랫폼 · 만들 수 있는 백엔드마다 골든 수를 낸다 — 체인 백엔드 집합(쿠킹 표 `scenario_backends`, 한 곳)과 골든 폴더를 대조한다.

    체인 밖 백엔드(데스크톱 WebGPU)의 골든 0 이 여기서 드러난다. 체인 백엔드가 모든 열쇠를 갖췄는지는 단위 테스트 `ScenarioGoldensTest` 가
    `<SkipUnless>` 를 보아 판정한다 — 여기는 집계와, 쿠킹 표에 없는 꼬리(지운 백엔드의 골든)를 실패로 내는 일만 한다.
    """
    from common.cook_table import CookContractSpec
    from common.git import run_git
    spec = CookContractSpec.load(REPO)
    tag_by_backend = {backend.name: backend.command_line_name for backend in spec.list_backend}
    listed = run_git(['ls-files', '*/golden/*.ppm'], cwd=REPO)
    if not listed.succeeded:
        print(f'git ls-files 실패: {listed.stderr.strip()}')
        return 1
    counts, key_count, unknown = count_golden_coverage([line.strip() for line in listed.stdout.splitlines() if line.strip()], tag_by_backend)
    print(f'골든 열쇠(폴더 · 시나리오 · 이름) {key_count} — * 는 검증 체인 백엔드(Config/Engine/CookContract.json scenario_backends)')
    for platform, suffix in GOLDEN_PLATFORM_SUFFIX.items():
        chain = spec.map_scenario_backends_by_platform[platform]
        for backend in spec.map_backend_order_by_platform[platform]:
            count = counts.get((platform, backend), 0)
            print(f'  {platform:8} {"*" if backend in chain else " "} {backend:10} {tag_by_backend[backend] + suffix:10} {count:5d}{"  ← 골든 없음" if count == 0 else ""}')
    for path in unknown:
        print(f'  [쿠킹 표에 없는 백엔드 꼬리] {path}')
    return 1 if unknown else 0


def self_check() -> int:
    coverage, key_count, unknown = count_golden_coverage(['a/golden/s.n.dx12.ppm', 'a/golden/s.n.vk.ppm', 'a/golden/s.n.vk.linux.ppm', 'a/golden/s.m.dx12.ppm', 'a/golden/s.m.old.ppm'],
                                                      {'DirectX12': 'dx12', 'Vulkan': 'vk', 'WebGPU': 'wgpu'})
    assert coverage == {('Windows', 'DirectX12'): 2, ('Windows', 'Vulkan'): 1, ('Linux', 'Vulkan'): 1}, coverage
    assert key_count == 2 and unknown == ['a/golden/s.m.old.ppm'], (key_count, unknown)
    reports = [
        {'goldenSamples': [{'golden': 'a', 'differingFraction': 0.002, 'meanDistance': 0.0001, 'largestDeepRegionPixels': 4, 'maxDifferingFraction': 0.01, 'maxMeanDistance': 0.005, 'maxDeepRegionPixels': 16}]},
        {'goldenSamples': [{'golden': 'a', 'differingFraction': 0.001, 'meanDistance': 0.0001, 'largestDeepRegionPixels': 8, 'maxDifferingFraction': 0.01, 'maxMeanDistance': 0.005, 'maxDeepRegionPixels': 16},
                           {'golden': 'b', 'differingFraction': 0.0, 'meanDistance': 0.0, 'largestDeepRegionPixels': 0, 'maxDifferingFraction': 0.01, 'maxMeanDistance': 0.005, 'maxDeepRegionPixels': 16}]},
        {'records': []},
    ]
    rows = summarize_golden_margins(reports)
    assert [row['golden'] for row in rows] == ['a', 'b'], rows
    assert rows[0]['runs'] == 2 and rows[0]['largestDeepRegionPixels'] == 8 and rows[0]['differingFraction'] == 0.002, rows[0]
    assert rows[0]['margin'] == 2.0, rows[0]   # 덩어리 16 / 8 이 가장 작은 여유
    assert rows[1]['margin'] == float('inf'), rows[1]

    stages = [f's{i}' for i in range(10)]
    for first_bad in range(10):
        calls: list[list[str]] = []

        def fails(prefix: list[str], first_bad: int = first_bad) -> bool:
            calls.append(prefix)
            return stages[first_bad] in prefix
        assert find_first_bad_step(stages, fails) == first_bad, first_bad
        assert len(calls) <= 1 + 4, len(calls)   # log2(10) 올림 + 처음 한 번
    assert find_first_bad_step(stages, lambda prefix: False) is None
    sample_log = '\n'.join(['x loaded from engine/a.scenario.xml (3 step(s))', 'y loaded from engine/b.scenario.xml', 'z loaded from engine/a.scenario.xml'])
    assert read_backend_chain_order(sample_log) == ['engine/a.scenario.xml', 'engine/b.scenario.xml']
    assert minimize_stages(stages, lambda prefix: 's0' in prefix and 's4' in prefix and 's7' in prefix) == ['s0', 's4', 's7']
    assert stage_failed_in_reports([{'name': 'a', 'result': 'Passed'}], 'a') is False
    assert stage_failed_in_reports([{'name': 'a', 'result': 'Failed'}], 'a') is True
    assert stage_failed_in_reports([{'name': 'b', 'result': 'Passed'}], 'a') is True, '보고 없는 단계는 짐'
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
    parser.add_argument('--start', help='이분 탐색을 건너뛰고 이 단계(시나리오 이름 또는 경로)부터 줄이기만 한다 — 앞 실행이 시작 단계를 찾았을 때')
    parser.add_argument('--golden-margins', nargs='*', metavar='보고 폴더 · 파일',
                        help='체인 대신: 시나리오 보고(goldenSamples[])를 모아 골든마다 실행 사이 최대 차이와 여유를 낸다(기본 build/<preset>/Bin/Saved/Automation)')
    parser.add_argument('--golden-coverage', action='store_true', help='체인 대신: 플랫폼 · 백엔드마다 저장소 골든 수(체인 백엔드 표와 대조, 체인 밖 백엔드의 골든 0 이 드러난다)')
    options = parser.parse_args(argv)
    if options.self_check:
        return self_check()
    if options.golden_coverage:
        return print_golden_coverage()
    if options.golden_margins is not None:
        return print_golden_margins(options.golden_margins, options.preset)
    if not options.chain or not options.stage:
        parser.error('체인과 단계를 적는다 (예: empty surfacefootprint)')

    build_preset_or_exit(options.preset)
    build_dir = REPO / 'build' / options.preset
    log_dir = build_dir / 'Testing' / 'Logs'
    command = app_arguments.find_chain_command(build_dir, options.chain)
    paths = app_arguments.read_chain_stages(command)
    if any('scenario-backend-split' in part for part in command):
        # 나눠 주는 체인은 백엔드마다 다른 부분만 돌았다 — 그 백엔드의 체인 로그가 실제 순서의 정본이다(없으면 체인을 먼저 돌린다, 짐작하지 않는다).
        backend_log = log_dir / f'Scenario.{options.chain}.{options.backend}.log'
        if not backend_log.is_file():
            parser.error(f'{backend_log} 가 없다 — 나눠 주는 체인은 그 백엔드로 체인을 한 번 돌려 실제 순서를 남긴 뒤 찾는다')
        paths = read_backend_chain_order(backend_log.read_text(encoding='utf-8', errors='replace'))
    # 단계 이름은 시나리오 파일의 name 속성이 정본이다(로그 문구를 읽지 않는다 — 체인을 먼저 돌려 둘 필요도 없다).
    target_index, target_name = resolve_target({index: read_scenario_name(path) for index, path in enumerate(paths)}, options.stage)
    dropped = ('unattended',) if options.show else ()
    base = app_arguments.without(command, 'scenario-backend-split', 'scenario-keep-going', 'scenario-backends', 'log-file', 'scenario', 'scenario-report', *dropped)
    run_log = log_dir / 'Culprit.log'
    run_counter = [0]

    def fails(prefix: list[str]) -> bool:
        listed = ','.join(prefix + [paths[target_index]])
        run_counter[0] += 1
        run_dir = make_run_report_folder(build_dir, run_counter[0])
        subprocess.run(base + [app_arguments.option('scenario', listed), app_arguments.flag('scenario-keep-going'), app_arguments.option('scenario-backends', options.backend),
                               app_arguments.option('scenario-report', run_dir / 'report.json'), app_arguments.option('log-file', run_log)], cwd=build_dir / 'Bin',
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        bad = stage_failed_in_reports(read_run_reports(run_dir), target_name)
        print(f'앞 {len(prefix):3d} 단계 (첫 {prefix[0] if prefix else "-"}) → {"짐" if bad else "통과"}  ({run_dir})', flush=True)
        return bad

    before = paths[:target_index]
    print(f'{options.chain}: {target_name} (단계 {target_index + 1}/{len(paths)}) · {options.preset} {options.backend}', flush=True)
    if options.start:
        first_bad = next((index for index, path in enumerate(before) if path == options.start or read_scenario_name(path) == options.start), None)
        if first_bad is None:
            parser.error(f'{options.start} 는 {target_name} 앞 단계가 아니다')
    else:
        first_bad = find_first_bad_step(before, fails)
    if first_bad is None:
        print('재현 안 됨 — 단독으로 지는지(단계만 -scenario= 로) · 다른 백엔드 · 체인 밖 상태를 본다')
        return 1
    print('시작 단계:', before[first_bad])
    print('단독 쌍으로도 지는가(아니면 뒤 단계들과 함께 만드는 상태다):')
    if fails([before[first_bad]]):
        return 0
    print('뒤 단계와 함께 만드는 상태다 — 빼도 지는 단계를 하나씩 버린다:')
    kept = minimize_stages(before[first_bad:], fails)
    print('지는 최소 묶음:', ', '.join(kept))
    return 0


if __name__ == '__main__':
    sys.exit(main())
