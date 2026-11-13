"""사람이 보면서 시나리오를 돌린다(py -3 Scripts/dev/watch_scenarios.py [--preset 프리셋] [--backend 백엔드] [--stages a,b] [--speed N] [체인…]).

`Scenario.<체인>` 의 ctest 명령에서 -unattended 를 뺀다 — 창이 뜨고, 단계 판정은 무인 실행과 같다. 시나리오 실행은 VSync 를 끄고 돈다(`-vsync` 를 적지 않으면).
--speed N 은 그린 프레임 N 개마다 하나만 화면에 내보낸다(`gv_scenarioPresentInterval` — 그리기 · 사진 · 판정은 매 프레임 그대로라 결과는 같고 내보내기 대기만 준다).
숨긴 창은 검증(`preflight.sh`)이 맡고, 이 명령은 눈으로 볼 때 쓴다. 입력은 시나리오가 잡으므로(input="exclusive") 마우스를 움직여도 판정이 바뀌지 않는다.
기본: Ninja-Release, dx12, editor. --stages 는 engine/automation/editor 의 단계 이름 묶음이다.
"""
import argparse
import pathlib
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))   # Scripts
from common import app_arguments  # noqa: E402

REPO = pathlib.Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--preset', default='Ninja-Release')
    parser.add_argument('--backend', default='dx12')
    parser.add_argument('--stages')
    parser.add_argument('--speed', type=int, default=1, help='그린 프레임 N 개마다 하나를 내보낸다(1 = 매 프레임)')
    parser.add_argument('chains', nargs='*', default=['editor'])
    options = parser.parse_args()
    if options.speed < 1:
        parser.error('--speed 는 1 이상이다')
    build_dir = REPO / 'build' / options.preset
    code = 0
    for chain in options.chains if options.stages is None else ['editor']:
        command = app_arguments.without(app_arguments.find_chain_command(build_dir, chain), 'unattended', 'scenario-backend-split', 'scenario-backends', 'log-file')
        if options.stages is not None:
            listed = ','.join(f'engine/automation/editor/{n}.scenario.xml' for n in options.stages.split(','))
            command = app_arguments.without(command, 'scenario') + [app_arguments.option('scenario', listed)]
        log = build_dir / 'Testing' / 'Logs' / f'Watch.{chain}.log'
        command += [app_arguments.option('scenario-backends', options.backend), app_arguments.option('log-file', log), app_arguments.variable('gv_scenarioPresentInterval', options.speed)]
        print(f'{chain}: {options.preset} {options.backend} x{options.speed}', flush=True)
        rc = subprocess.run(command, cwd=build_dir / 'Bin').returncode
        print(f'{chain} rc={rc} 로그 {log}', flush=True)
        code = code or rc
    return code


if __name__ == '__main__':
    sys.exit(main())
