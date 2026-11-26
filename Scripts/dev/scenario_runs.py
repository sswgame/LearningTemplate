"""
시나리오 체인 실행의 보고 읽기 · 진 단계 고르기 · 앞 단계 이분 탐색 — Windows(`scenario-bisect`) · Linux(`linux-scenarios`) · 결정성 검사가 함께 쓴다.
골든 여유 · 골든 수 · 진행 목록처럼 체인을 돌리지 않는 보고도 여기다.
"""

from __future__ import annotations

import datetime
import json
import re
import shutil
import sqlite3
import sys
import pathlib
from typing import Callable, Sequence

from common.git import run_git, run_git_or_raise
from common.paths import get_bin_dir
from dev.run_history import DEFAULT_DATABASE, PASSED_RESULTS, find_report_commits, ingest, open_database, read_reports

REPO = pathlib.Path(__file__).resolve().parents[2]


def record_rerun(run_dir: pathlib.Path) -> None:
    """다시 돌린 보고를 이력에 `rerun` 으로 넣는다 — 흔들림 판정(`test_history.py flaky`)은 검증 실행만 본다. 같은 보고는 다시 들어가지 않아
    다음 검증의 ingest 가 이것을 검증 실행으로 덮지 못한다."""
    ingest(open_database(REPO / DEFAULT_DATABASE), read_reports([run_dir]), run_git_or_raise(['rev-parse', 'HEAD'], cwd=REPO).strip(), 'rerun')


def resolve_target(names: dict[int, str], target: str) -> tuple[int, str]:
    """@p target 은 전체 이름(`game.playground.surfacefootprint`)이나 끝 조각(`surfacefootprint`)."""
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
    """지는 앞 단계 묶음 @p stages 에서 빼도 여전히 지는 단계를 하나씩 버려 남은 묶음을 돌려준다(첫 단계인 시작 단계는 남긴다).

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


def describe_result(report: dict) -> str:
    """보고 하나의 판정 한 줄: 실행기의 판정 값(`failure`: 종류 · 까닭)이 지은 `<종류> - <까닭>`(통과면 결과 이름). 판정 칸이 없는 옛 보고는 `result - reason`."""
    failure = report.get('failure')
    if isinstance(failure, dict) and failure.get('kind'):
        return f"{failure['kind']} - {failure.get('reason', '')}"
    reason = report.get('reason', '')
    return f"{report.get('result', '?')} - {reason}" if reason else str(report.get('result', '?'))


def describe_stage_failures(list_report: Sequence[dict], stage_name: str) -> str:
    """그 단계 보고들의 판정 한 줄(진 것만, 보고가 없으면 그 사실) — 진 단계 줄에 붙인다."""
    lines = [describe_result(report) for report in list_report if report.get('name') == stage_name and report.get('result') not in PASSED_RESULTS]
    if not any(report.get('name') == stage_name for report in list_report):
        return '보고 없음(크래시 · 행 — 자식 로그를 본다)'
    return '; '.join(dict.fromkeys(lines))


def stage_failed_in_reports(list_report: Sequence[dict], stage_name: str) -> bool:
    """그 단계의 보고가 통과 · 건너뜀이 아니면 짐. 보고가 없으면(크래시 · 행으로 못 씀) 짐이다 — 통과로 치지 않는다."""
    list_result = [report['result'] for report in list_report if report['name'] == stage_name]
    return not list_result or any(result not in PASSED_RESULTS for result in list_result)


# ------------------------------------------------------------------------------
# 진 단계 다시 돌기(--rerun-failed) · 단계부터 끝까지(--from-stage) · 진행 목록(--advances)
# ------------------------------------------------------------------------------

def find_latest_per_backend_folder(automation_dir: pathlib.Path, stage_names: set[str], chain_log: pathlib.Path) -> pathlib.Path | None:
    """마지막 체인 실행의 백엔드 보고 폴더 — `-scenario-report` 없는 체인은 `<Saved/Automation>/<시나리오>/per-backend-<pid>/` 에
    단계 보고 `<백엔드>[.<번호>].json` 을 남긴다(AutomationPerBackend — 폴더 이름의 시나리오는 나눠 주는 체인이면 첫 단계가 아니다).
    체인 로그(`Scenario.<체인>.log`)보다 늦지 않고 보고가 모두 이 체인의 단계인 것 중 가장 새것. 없으면 None.
    체인 뒤에 혼자 다시 돌린 실행은 로그보다 늦어 빠지고, 앞선 실행은 체인 폴더보다 오래돼 고르지 않는다."""
    latest_mtime = chain_log.stat().st_mtime + 5.0 if chain_log.is_file() else float('inf')   # 체인 로그는 자식 보고 뒤에 닫힌다 — 몇 초 여유
    candidates = sorted((path for path in automation_dir.glob('*/per-backend-*') if path.is_dir() and path.stat().st_mtime <= latest_mtime),
                        key=lambda path: path.stat().st_mtime, reverse=True)
    for folder in candidates:
        names = [report.get('name') for _, report in read_backend_reports(folder)]
        if names and all(name in stage_names for name in names):
            return folder
    return None


def read_backend_reports(folder: pathlib.Path) -> list[tuple[str, dict]]:
    """폴더의 단계 보고를 (백엔드, 보고)로 — 백엔드는 파일 이름의 첫 조각(`vk.3.json` → vk)이다."""
    import json
    list_report = []
    for path in sorted(folder.glob('*.json')):
        try:
            document = json.loads(path.read_text(encoding='utf-8'))
        except (OSError, ValueError):
            continue
        if isinstance(document, dict) and 'name' in document and 'result' in document:
            list_report.append((path.name.split('.')[0], document))
    return list_report


def select_failed_stages(expected_by_backend: dict[str, list[str]], list_report: Sequence[tuple[str, dict]]) -> list[tuple[str, str]]:
    """백엔드마다 돌았어야 할 단계 중 진 것(통과 · 건너뜀이 아니거나 보고가 없는 것 — 크래시 · 행)을 (백엔드, 단계) 체인 순서로 돌려준다."""
    failed = []
    for backend, list_stage in expected_by_backend.items():
        reports = [report for report_backend, report in list_report if report_backend == backend]
        for stage in list_stage:
            if stage_failed_in_reports(reports, stage):
                failed.append((backend, stage))
    return failed


def collect_expected_stages(folder: pathlib.Path, log_dir: pathlib.Path, chain: str, chain_names: list[str], split: bool) -> dict[str, list[str]]:
    """보고 폴더에 보고를 낸 백엔드마다 그 백엔드가 돌았어야 할 단계 — 나눠 주는 체인은 그 백엔드 체인 로그(`Scenario.<체인>.<백엔드>.log`,
    `read_backend_chain_order`)가 실은 순서, 아니면 체인 전부. 백엔드 로그는 보고 폴더가 아니라 Testing/Logs 에 있다."""
    expected: dict[str, list[str]] = {}
    for backend in sorted({backend for backend, _ in read_backend_reports(folder)}):
        if split:
            log = log_dir / f'Scenario.{chain}.{backend}.log'
            if not log.is_file():
                sys.exit(f'{log} 가 없다 — 나눠 주는 체인의 {backend} 순서를 알 수 없다(짐작하지 않는다)')
            expected[backend] = [read_scenario_name(path) for path in read_backend_chain_order(log.read_text(encoding='utf-8', errors='replace'))]
        else:
            expected[backend] = list(chain_names)
    return expected


StageRunner = Callable[[list[str], str, str], bool]   # (돌릴 단계 경로들, 판정할 단계 이름, 백엔드) → 졌는가


def describe_code_change(list_report: Sequence[dict], project_root: pathlib.Path, connection: sqlite3.Connection | None = None) -> str | None:
    """
    체인 보고(@p list_report)가 돈 뒤 코드가 바뀌었으면 그 까닭, 같은 코드면 None. 보고의 커밋은 이력(@p connection — 묶음 검증이 붙인 것)에서, 없으면 그 시각의 HEAD(reflog)에서
    읽어 지금 HEAD 와 견준다. 커밋이 같아도 보고 뒤에 고친 추적 파일이 있으면 바뀐 것이다(지금 빌드는 그 파일을 담는다).
    """
    started_ms = min(int(report['startedAtUnixMs']) for report in list_report)
    head = run_git_or_raise(['rev-parse', 'HEAD'], cwd=project_root).strip()
    started_at = datetime.datetime.fromtimestamp(started_ms / 1000, datetime.timezone.utc).strftime('%Y-%m-%d %H:%M:%S +0000')
    commits = (find_report_commits(connection, list_report) if connection is not None else set()) or {run_git_or_raise(['rev-parse', f'HEAD@{{{started_at}}}'], cwd=project_root).strip()}
    if commits != {head}:
        return f'보고는 {", ".join(sorted(commit[:10] for commit in commits))}, 지금은 {head[:10]}'
    for relative in run_git_or_raise(['diff', '--name-only', 'HEAD'], cwd=project_root).splitlines():
        path = project_root / relative
        if not path.exists() or path.stat().st_mtime * 1000 > started_ms:
            return f'보고 뒤에 고친 파일이 있다({relative})'
    return None


def retry_failed_stages(paths: list[str], failed: Sequence[tuple[str, str]], run: StageRunner, code_change: str | None) -> int:
    """진 단계마다: 혼자 돌려 지면 "단독으로 진다", 통과하면 "체인에서만 진다" — 앞 단계를 모두 붙여 다시 돌리고, 지면 이분 탐색으로 넘어간다.
    보고 뒤에 코드가 바뀌었으면(@p code_change, `describe_code_change`) 혼자 통과한 단계는 고쳐졌을 수 있으므로 앞 단계를 붙이지 않는다(고친 뒤 확인이 단계마다 앞 160 여 개를 다시 돌았다).
    Windows(`main`)와 Linux(`linux_scenarios.py rerun`)가 자기 실행기(@p run)로 같은 함수를 부른다. 진 단계가 하나라도 남으면 1."""
    names = [read_scenario_name(path) for path in paths]
    still_failing = 0
    for backend, stage in failed:
        index = names.index(stage)
        if run([paths[index]], stage, backend):
            print(f'{backend} {stage}: 단독으로 진다 — scenario-debug 의 단독 재현으로 본다', flush=True)
            still_failing += 1
            continue
        if code_change is not None:
            print(f'{backend} {stage}: 고쳤을 수 있다 — 혼자 통과({code_change}). 체인에서만 지는지는 체인을 다시 돌려 본다', flush=True)
            continue
        print(f'{backend} {stage}: 체인에서만 진다 — 앞 {index} 단계를 붙여 다시 돈다', flush=True)
        if not run(paths[:index + 1], stage, backend):
            print(f'{backend} {stage}: 앞 단계를 붙여도 통과 — 흔들림(같은 체인을 한 번 더 돌려 확인한다)', flush=True)
            continue
        still_failing += 1
        first_bad = find_first_bad_step(paths[:index], lambda prefix: run(prefix + [paths[index]], stage, backend))
        print(f'{backend} {stage}: 시작 단계 {paths[first_bad] if first_bad is not None else "재현 안 됨"}', flush=True)
    return 1 if still_failing else 0


WAIT_PHOTO_STEPS = frozenset({'Screenshot', 'ExpectImage', 'ExpectImagesDiffer', 'CaptureEditorUI'})
STEP_TAG = re.compile(r'<!--.*?-->|<([A-Za-z]\w*)\b([^>]*)>', re.S)


def list_advance_steps(scenario_text: str) -> list[dict]:
    """시나리오 글의 진행(`<AdvanceSimulation frames="N" [render="true"]/>`)마다 프레임 수 · render · 다음 단계 종류 · 분류를 낸다.
    분류: condition(바로 뒤가 `<Expect watch>`. 조건 대기로 바꿀 후보이거나 "아무 일도 안 일어난다" 확인), photo(바로 뒤가 사진, 사진 앞 진행 시간), other.
    render 는 모든 프레임을 그리는 진행이다(측정 창 · 비교 기준) — 그리는 비용이 그대로 든다."""
    steps = [(match.group(1), match.group(2)) for match in STEP_TAG.finditer(scenario_text) if match.group(1) and match.group(1) != 'Scenario']
    rows = []
    for index, (tag, attributes) in enumerate(steps):
        frames = re.search(r'\bframes="(\d+)"', attributes)
        if tag != 'AdvanceSimulation' or frames is None:
            continue
        next_tag, next_attributes = steps[index + 1] if index + 1 < len(steps) else ('-', '')
        kind = 'condition' if next_tag == 'Expect' and re.search(r'\bwatch=', next_attributes) else ('photo' if next_tag in WAIT_PHOTO_STEPS else 'other')
        rows.append({'frames': int(frames.group(1)), 'render': re.search(r'\brender="true"', attributes) is not None, 'next': next_tag, 'kind': kind})
    return rows


def print_advance_steps(paths: list[str]) -> int:
    """체인의 진행 목록과 분류별 프레임 합 — 조건 대기로 바꿀 후보(condition) · 사진 앞 진행 시간(photo) · 모두 그리는 진행(render)을 가른다."""
    totals: dict[str, int] = {}
    for path in paths:
        for row in list_advance_steps((REPO / 'Content' / path).read_text(encoding='utf-8')):
            kind = row['kind'] + (' render' if row['render'] else '')
            totals[kind] = totals.get(kind, 0) + row['frames']
            print(f'{kind:16s} {row["frames"]:5d}  → {row["next"]:18s} {path}')
    print('프레임 합:', ', '.join(f'{kind} {frames}' for kind, frames in sorted(totals.items())) or '없음')
    return 0


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
    roots = [pathlib.Path(folder) for folder in folders] or [get_bin_dir(preset) / 'Saved' / 'Automation']
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
