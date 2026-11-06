"""사람이 보면서 시나리오를 돌린다(py -3 Scripts/dev/WatchScenarios.py [--preset 프리셋] [--backend 백엔드] [--stages a,b] [체인…]).

`Scenario.<체인>` 의 ctest 명령에서 -unattended 를 뺀다 — 창이 뜨고 모든 프레임을 내보내며, 단계 판정은 무인 실행과 같다.
숨긴 창은 검증(`VerifyBundle.sh`)이 맡고, 이 명령은 눈으로 볼 때 쓴다. 입력은 시나리오가 잡으므로(input="exclusive") 마우스를 움직여도 판정이 바뀌지 않는다.
기본: Ninja-Release, dx12, editor. --stages 는 engine/automation/editor 의 단계 이름 묶음이다.
"""
import argparse
import pathlib
import re
import shlex
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parents[2]


def find_chain_command(build_dir, chain):
    listing = subprocess.run(['ctest', '-V', '-N', '-R', f'^Scenario\\.{chain}$'], cwd=build_dir, capture_output=True, text=True, encoding='utf-8', errors='replace').stdout
    found = [line for line in listing.splitlines() if 'Test command:' in line and '-scenario=' in line]
    if not found:
        sys.exit(f'Scenario.{chain} 이 {build_dir} 에 없다')
    return shlex.split(re.sub(r'.*Test command: ', '', found[0]).replace('\\', '/'))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--preset', default='Ninja-Release')
    parser.add_argument('--backend', default='dx12')
    parser.add_argument('--stages')
    parser.add_argument('chains', nargs='*', default=['editor'])
    options = parser.parse_args()
    build_dir = REPO / 'build' / options.preset
    code = 0
    for chain in options.chains if options.stages is None else ['editor']:
        command = [a for a in find_chain_command(build_dir, chain) if a not in ('-unattended', '-scenario-backend-split') and not a.startswith(('-scenario-backends=', '-log-file='))]
        if options.stages is not None:
            listed = ','.join(f'engine/automation/editor/{n}.scenario.xml' for n in options.stages.split(','))
            command = [f'-scenario={listed}' if a.startswith('-scenario=') else a for a in command]
        log = build_dir / 'Testing' / 'Logs' / f'Watch.{chain}.log'
        command += [f'-scenario-backends={options.backend}', f'-log-file={log}']
        print(f'{chain}: {options.preset} {options.backend}', flush=True)
        rc = subprocess.run(command, cwd=build_dir / 'Bin').returncode
        print(f'{chain} rc={rc} 로그 {log}', flush=True)
        code = code or rc
    return code


if __name__ == '__main__':
    sys.exit(main())
