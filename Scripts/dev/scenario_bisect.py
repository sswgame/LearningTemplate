"""체인에서만 지는 단계의 범인(앞 단계)을 이분 탐색한다 — Windows · Linux 공용(py -3 Scripts/dev/scenario_bisect.py <체인> <단계> [옵션]).

체인을 처음부터 다시 돌리지 않고, 진 단계 앞의 단계들 중 뒤쪽 구간만 붙여 부분 체인을 돌려 가며 "어느 단계부터 붙이면 지는가" 를 찾는다.
단계 목록 · 나머지 인자(게임 체인의 <Launch> 값 등)는 `Scenario.<체인>` 의 ctest 명령에서 그대로 가져오고(`common/AppArguments`) `-scenario=` 만 바꾼다 —
단계 이름은 시나리오 파일의 `name` 속성이고, 판정은 로그 문구가 아니라 실행마다 비운 보고 폴더(`build/<프리셋>/Saved/Culprit/run<N>` 에
`-scenario-report=<폴더>/report.json` — 체인 단계마다 `report[.<백엔드>].<단계>.json`)의 JSON `result` 다. 보고가 없는 단계(크래시 · 행)는 짐이다.

  py -3 Scripts/dev/scenario_bisect.py empty surfacefootprint                      # Ninja-Debug, dx12, 무인
  py -3 Scripts/dev/scenario_bisect.py editor focusloss --preset Ninja-Release --show
  py -3 Scripts/dev/scenario_bisect.py playground.shooter3d --rerun-failed --preset Ninja-Release   # 마지막 체인에서 진 단계: 혼자 → 앞을 붙여 → 이분 탐색
  py -3 Scripts/dev/scenario_bisect.py playground.shooter3d --from-stage shooter3d_enemydeath --backend vk     # 그 단계부터 끝까지만
  py -3 Scripts/dev/scenario_bisect.py playground.shooter3d --advances                              # 진행(AdvanceSimulation) 목록(condition · photo · other, render)
  py -3 Scripts/dev/scenario_bisect.py playground wildwest_mount --determinism --preset Ninja-Release   # 혼자 · 앞 단계 하나 · 두 번 이어 돌린 사진을 바이트로 견준다
  py -3 Scripts/dev/scenario_bisect.py --golden-margins [보고 폴더…]                # 흔들리는 골든: 보고의 goldenSamples[] 를 모아 골든별 최대 · 여유
  py -3 Scripts/dev/scenario_bisect.py --golden-coverage                            # 플랫폼 · 백엔드마다 골든 수(체인 백엔드 표와 대조)

Linux 는 `linux_scenarios.py bisect` 가 같은 함수(`find_first_bad_step`)를 자기 실행기(Xvfb · lavapipe)로 부른다.
"""

from __future__ import annotations

import argparse
import sys
import subprocess
import pathlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))   # Scripts

from common import app_arguments  # noqa: E402
from common.command import Command, ScriptContext  # noqa: E402
from common.paths import get_build_dir  # noqa: E402
from common.run_process import build_preset_or_exit  # noqa: E402
from dev.scenario_determinism import check_determinism, make_runner  # noqa: E402
from dev.run_history import DEFAULT_DATABASE, open_database  # noqa: E402
from dev.scenario_runs import REPO, collect_expected_stages, describe_code_change, describe_stage_failures, find_first_bad_step, find_latest_per_backend_folder, make_run_report_folder, minimize_stages, print_advance_steps, print_golden_coverage, print_golden_margins, read_backend_chain_order, read_backend_reports, read_run_reports, read_scenario_name, record_rerun, resolve_target, retry_failed_stages, select_failed_stages, stage_failed_in_reports  # noqa: E402


class ScenarioBisectCommand(Command):
    name = "scenario-bisect"
    help = "시나리오 체인의 진 단계를 혼자 → 앞 단계를 붙여 → 이분 탐색으로 다시 돈다(--rerun-failed 는 마지막 보고에서 고른다)"

    def add_arguments(self, parser: argparse.ArgumentParser) -> None:
        parser.add_argument('chain', nargs='?')
        parser.add_argument('stage', nargs='?')
        parser.add_argument('--preset', default='Ninja-Debug')
        parser.add_argument('--backend', default='dx12')
        parser.add_argument('--show', action='store_true', help='창을 띄워 돈다(-unattended 를 뺀다 — 판정은 같다)')
        parser.add_argument('--golden-margins', nargs='*', metavar='보고 폴더 · 파일',
                            help='체인 대신: 시나리오 보고(goldenSamples[])를 모아 골든마다 실행 사이 최대 차이와 여유를 낸다(기본 build/<preset>/Bin/Saved/Automation)')
        parser.add_argument('--golden-coverage', action='store_true', help='체인 대신: 플랫폼 · 백엔드마다 저장소 골든 수(체인 백엔드 표와 대조, 체인 밖 백엔드의 골든 0 이 드러난다)')
        parser.add_argument('--rerun-failed', action='store_true',
                            help='단계 대신: 마지막 체인 실행(Bin/Saved/Automation 의 per-backend 보고)에서 진 단계를 그 백엔드로 혼자 → 앞 단계를 붙여 → 이분 탐색으로 다시 돈다(보고 뒤에 코드가 바뀌었으면 혼자만)')
        parser.add_argument('--from-stage', metavar='단계', help='단계 대신: 그 단계부터 체인 끝까지만 --backend 로 돌고 진 단계를 낸다')
        parser.add_argument('--determinism', action='store_true',
                            help='이분 탐색 대신: 그 단계를 혼자 · 앞 단계 하나 · 두 번 이어 --backend 로 돌려 찍은 사진이 바이트까지 같은지 본다(scenario_determinism)')
        parser.add_argument('--advances', action='store_true', help='단계 대신: 체인의 진행(<AdvanceSimulation>)마다 프레임 수 · render · 다음 단계 · 분류(condition · photo · other)')

    def run(self, context: ScriptContext, args: argparse.Namespace) -> int:
        if args.golden_coverage:
            return print_golden_coverage()
        if args.golden_margins is not None:
            return print_golden_margins(args.golden_margins, args.preset)
        chain_mode = args.rerun_failed or args.from_stage or args.advances
        if not args.chain or (not args.stage and not chain_mode):
            self.parser.error('체인과 단계를 적는다 (예: empty surfacefootprint) — 또는 체인과 --rerun-failed · --from-stage · --advances')

        build_dir = get_build_dir(args.preset)
        log_dir = build_dir / 'Testing' / 'Logs'
        command = app_arguments.find_chain_command(build_dir, args.chain)
        paths = app_arguments.read_chain_stages(command)
        if args.advances:
            return print_advance_steps(paths)
        build_preset_or_exit(args.preset, cwd=REPO)
        dropped = ('unattended',) if args.show else ()
        # 셰이더 요청 기록은 체인 전체의 것이라 단계 몇 개로 덮지 않는다(preflight shader warm 이 그 몫만 데우게 된다).
        base = app_arguments.without(command, 'scenario-backend-split', 'scenario-keep-going', 'scenario-backends', 'log-file', 'scenario', 'scenario-report', 'shader-request-log', *dropped)
        run_log = log_dir / 'Culprit.log'
        run_counter = [0]

        def run_part(stage_paths: list[str], backend: str) -> pathlib.Path:
            run_counter[0] += 1
            run_dir = make_run_report_folder(build_dir, run_counter[0])
            subprocess.run(base + [app_arguments.option('scenario', ','.join(stage_paths)), app_arguments.flag('scenario-keep-going'), app_arguments.option('scenario-backends', backend),
                                   app_arguments.option('scenario-report', run_dir / 'report.json'), app_arguments.option('log-file', run_log)], cwd=build_dir / 'Bin',
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            return run_dir

        def run_stage(stage_paths: list[str], target_name: str, backend: str) -> bool:
            run_dir = run_part(stage_paths, backend)
            list_report = read_run_reports(run_dir)
            bad = stage_failed_in_reports(list_report, target_name)
            cause = f' — {describe_stage_failures(list_report, target_name)}' if bad else ''
            print(f'{backend} {len(stage_paths):3d} 단계 (첫 {stage_paths[0]}) → {target_name} {"짐" if bad else "통과"}{cause}  ({run_dir})', flush=True)
            record_rerun(run_dir)
            return bad

        if args.rerun_failed:
            names = [read_scenario_name(path) for path in paths]
            automation_dir = build_dir / 'Bin' / 'Saved' / 'Automation'
            folder = find_latest_per_backend_folder(automation_dir, set(names), log_dir / f'Scenario.{args.chain}.log')
            if folder is None:
                self.parser.error(f'{args.chain} 의 체인 보고가 {automation_dir} 에 없다 — 체인을 먼저 돌린다')
            split = any('scenario-backend-split' in part for part in command)
            list_report = read_backend_reports(folder)
            failed = select_failed_stages(collect_expected_stages(folder, log_dir, args.chain, names, split), list_report)
            code_change = describe_code_change([report for _, report in list_report], REPO, open_database(REPO / DEFAULT_DATABASE))
            print(f'{args.chain}: {folder} 에서 진 단계 {len(failed)} 개{f" — 코드가 바뀌었다: {code_change}" if code_change else ""}', flush=True)
            return retry_failed_stages(paths, failed, run_stage, code_change)
        if args.from_stage:
            names = [read_scenario_name(path) for path in paths]
            start_index, _ = resolve_target(dict(enumerate(names)), args.from_stage)
            run_dir = run_part(paths[start_index:], args.backend)
            failed = select_failed_stages({args.backend: names[start_index:]}, [(args.backend, report) for report in read_run_reports(run_dir)])
            print(f'{args.chain}: {names[start_index]} 부터 {len(paths) - start_index} 단계 · {args.backend} → 진 단계 {len(failed)} 개  ({run_dir})')
            for _, stage in failed:
                print('  짐:', stage)
            return 1 if failed else 0
        if any('scenario-backend-split' in part for part in command):
            # 나눠 주는 체인은 백엔드마다 다른 부분만 돌았다 — 그 백엔드의 체인 로그가 실제 순서의 정본이다(없으면 체인을 먼저 돌린다, 짐작하지 않는다).
            backend_log = log_dir / f'Scenario.{args.chain}.{args.backend}.log'
            if not backend_log.is_file():
                self.parser.error(f'{backend_log} 가 없다 — 나눠 주는 체인은 그 백엔드로 체인을 한 번 돌려 실제 순서를 남긴 뒤 찾는다')
            paths = read_backend_chain_order(backend_log.read_text(encoding='utf-8', errors='replace'))
        # 단계 이름은 시나리오 파일의 name 속성이 정본이다(로그 문구를 읽지 않는다 — 체인을 먼저 돌려 둘 필요도 없다).
        target_index, target_name = resolve_target({index: read_scenario_name(path) for index, path in enumerate(paths)}, args.stage)
        if args.determinism:
            runner = make_runner(command, target_name, REPO / 'Content', args.backend, build_dir, build_dir / 'Bin')
            list_problem = check_determinism(paths, target_index, target_name, build_dir / 'Bin' / 'Saved' / 'Automation', runner)
            for problem in list_problem:
                print(f'{args.backend} {target_name}: {problem}')
            print(f'{args.backend} {target_name}: {"앞 단계에 따라 다르다" if list_problem else "세 실행의 사진이 바이트까지 같다"}')
            return 1 if list_problem else 0

        def fails(prefix: list[str]) -> bool:
            return run_stage(prefix + [paths[target_index]], target_name, args.backend)

        before = paths[:target_index]
        print(f'{args.chain}: {target_name} (단계 {target_index + 1}/{len(paths)}) · {args.preset} {args.backend}', flush=True)
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
    sys.exit(ScenarioBisectCommand.main())
