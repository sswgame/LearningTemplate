"""Linux 검증(py -3 Scripts/dev/LinuxScenarios.py <명령>) — WSL 클론에서 빌드 · 시나리오 · 골든 · 단위 테스트를 돌린다.

Windows 에서 부르면 자기를 WSL 에서 다시 띄운다. 창 관리자 없는 Xvfb 안에서 돌아 사용자 화면을 건드리지 않고,
Windows 의 하드웨어 GPU 테스트와 함께 돌려도 된다. 체인은 저마다 자기 Xvfb 에서 동시에 돈다. 이 명령은 클론당 하나만 돌린다.

명령:
  setup                  Xvfb · 글꼴을 sudo 없이 ~/lt-deps 에 푼다(한 번)
  build                  본 저장소 HEAD 를 클론으로 옮겨 Linux-Release 를 짓고 임포트 결과를 복사한다
  chain [--record] 이름… Scenario.<이름> 체인을 돌리고 진 단계와 까닭을 낸다(--record 는 .linux 골든 기록)
  stages 묶음…           쉼표로 이은 단계 묶음을 부분 체인으로 돌린다(예: focusloss,graphaddnode)
  culprit 단계 [체인]    체인에서만 지는 단계의 앞 단계를 이분 탐색한다(체인 기본값 editor, 먼저 chain 을 돌려 로그가 있어야 한다)
  report [체인]          마지막 체인 로그에서 진 단계와 까닭을 다시 낸다
  goldens                클론의 .linux 골든을 본 저장소로 복사하고 한 색인 사진을 알린다
  all [--record]         build → 모든 체인(병렬) · Release 단위 · 서버 빌드 · 단위. 끝 줄 LINUX_OK=<sha> 가 나와야 그 커밋을 push 한다
"""
import concurrent.futures
import contextlib
import io
import os
import pathlib
import re
import shutil
import subprocess
import sys
import time

DISTRO = 'Ubuntu-26.04'
WINDOWS_REPO = pathlib.Path(__file__).resolve().parents[2]
WSL_REPO = '/mnt/d/Projects/Personal/LearningTemplate'
HOME = os.path.expanduser('~')
CLONE = f'{HOME}/LT-linux'
RELEASE = f'{CLONE}/build/Linux-Release'
LOGS = f'{RELEASE}/Testing/Logs'
XVFB_ROOT = f'{HOME}/lt-deps/xvfb/root'
FONT_DIR = f'{HOME}/.local/share/fonts'
FONT_FILE = 'LiberationSans-Regular.ttf'  # 카탈로그가 찾는 Linux 글꼴(Content/engine/fonts/fontcatalog.xml)
XVFB_DEBS = ('git-lfs xvfb xserver-common x11-xkb-utils xkb-data libxfont2 libfontenc1 libunwind8 libxkbfile1 libxtst6 libgl1 libgl1-mesa-dri '
             'libtirpc3t64 libxshmfence1 libpixman-1-0 libxcvt0 libglvnd0 libgbm1 libepoxy0 libxau6 libxdmcp6').split()
SCREEN = '2560x1440x24'  # 체인의 큰 창이 화면 안에 들어오고, 실제 포인터가 창 밖 가운데에 머문다
ALL_CHAINS = ('editor empty abilityarena frontiertown harvestvalley meadowvillage nilecity pixeltown shooter3d starskirmish themepark voxelcraft wildwest '
              'abilityarena.editor harvestvalley.editor shooter3d.editor themepark.editor').split()
PARALLEL_CHAINS = 4  # lavapipe 가 코어를 나눠 쓰므로 많이 띄우면 벽시계 시한을 넘긴다
WALL_SCALE = '4'  # 소프트웨어 렌더링이라 벽시계 시한을 늘린다(gv_scenarioWallScale)
PATH_LINUX = '/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin'  # Windows PATH 가 끼면 Windows 도구를 잡는다


def run_in_wsl(argv):
    """Windows 쪽: HEAD 를 refs/linux-verify 에 적고 같은 명령을 WSL 에서 띄운다."""
    if argv and argv[0] in ('build', 'all'):
        head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=WINDOWS_REPO, text=True).strip()
        subprocess.check_call(['git', 'update-ref', 'refs/linux-verify', head], cwd=WINDOWS_REPO)
    env = dict(os.environ, MSYS_NO_PATHCONV='1', MSYS2_ARG_CONV_EXCL='*')
    return subprocess.call(['wsl.exe', '-d', DISTRO, '--', 'python3', f'{WSL_REPO}/Scripts/dev/LinuxScenarios.py'] + argv, env=env)


def shell(command, **kwargs):
    return subprocess.run(command, shell=True, executable='/bin/bash', env=dict(os.environ, PATH=PATH_LINUX), **kwargs)


def scenario_env(display):
    env = dict(os.environ, PATH=PATH_LINUX, DISPLAY=f':{display}', LIBGL_ALWAYS_SOFTWARE='1', GALLIUM_DRIVER='llvmpipe',
               VK_ICD_FILENAMES='/usr/share/vulkan/icd.d/lvp_icd.json',
               VK_ADD_LAYER_PATH=f'{HOME}/lt-deps/vl/usr/share/vulkan/explicit_layer.d',
               LD_LIBRARY_PATH=f'{HOME}/lt-deps/vl/usr/lib/x86_64-linux-gnu')
    env.pop('WAYLAND_DISPLAY', None)
    return env


class Xvfb:
    """체인 동안 화면을 띄운다. 실행 파일에 박힌 xkbcomp 경로를 /tmp/xkb 로 고친 사본을 쓴다(setup)."""

    def __init__(self, display=99):
        self.display = display

    def __enter__(self):
        if not os.path.exists(f'{XVFB_ROOT}/usr/bin/Xvfb.p'):
            sys.exit('Xvfb 가 없다 - setup 을 먼저 돌린다')
        if not os.path.exists(f'{FONT_DIR}/{FONT_FILE}'):
            sys.exit('글꼴이 없다 - setup 을 먼저 돌린다')
        if not os.path.lexists('/tmp/xkb'):
            os.symlink(f'{XVFB_ROOT}/usr/bin', '/tmp/xkb')
        env = dict(os.environ, PATH=f'{XVFB_ROOT}/usr/bin:{PATH_LINUX}', LD_LIBRARY_PATH=f'{XVFB_ROOT}/usr/lib/x86_64-linux-gnu')
        with open(f'/tmp/xvfb{self.display}.log', 'w') as log:
            self.process = subprocess.Popen([f'{XVFB_ROOT}/usr/bin/Xvfb.p', f':{self.display}', '-screen', '0', SCREEN, '-xkbdir', f'{XVFB_ROOT}/usr/share/X11/xkb', '-nolisten', 'tcp'],
                                            env=env, stdout=log, stderr=log)
        time.sleep(2)
        if self.process.poll() is not None:
            sys.exit(f'Xvfb 가 뜨지 못했다 - /tmp/xvfb{self.display}.log')
        return self

    def __exit__(self, *_):
        self.process.kill()
        self.process.wait()


def setup():
    os.makedirs(f'{HOME}/lt-deps/xvfb', exist_ok=True)
    for package in XVFB_DEBS:
        if shell(f'cd ~/lt-deps/xvfb && apt-get download {package}', stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode != 0:
            print('내려받지 못함:', package)
    shell('cd ~/lt-deps/xvfb && mkdir -p root && for d in *.deb; do dpkg -x "$d" root; done', check=True)
    binary = pathlib.Path(f'{XVFB_ROOT}/usr/bin/Xvfb').read_bytes()
    patched = pathlib.Path(f'{XVFB_ROOT}/usr/bin/Xvfb.p')
    patched.write_bytes(binary.replace(b'/usr/bin\x00', b'/tmp/xkb\x00'))
    patched.chmod(0o755)
    os.makedirs(FONT_DIR, exist_ok=True)
    os.makedirs(f'{HOME}/lt-deps/fontdeb', exist_ok=True)
    shell('cd ~/lt-deps/fontdeb && apt-get download fonts-liberation fonts-noto-cjk && mkdir -p root && for d in *.deb; do dpkg -x "$d" root; done', check=True, stdout=subprocess.DEVNULL)
    shell(f'find ~/lt-deps/fontdeb/root \\( -name "LiberationSans-*.ttf" -o -name "NotoSansCJK-Regular.ttc" \\) -exec cp -f {{}} {FONT_DIR}/ \\; && fc-cache -f', check=True)
    print(sorted(os.listdir(FONT_DIR)))
    return 0


def sync_clone():
    """클론을 refs/linux-verify 로 옮긴다. 옮기지 못하면 옛 빌드를 돌리지 않고 멈춘다."""
    shell(f'cd {CLONE} && git fetch -q {WSL_REPO} refs/linux-verify && git reset -q --hard FETCH_HEAD', check=True)
    print('클론:', shell(f'git -C {CLONE} log -1 --format=%h', capture_output=True, text=True).stdout.strip())


def build():
    sync_clone()
    cache = pathlib.Path(f'{CLONE}/build/Linux-Debug/CMakeCache.txt').read_text(errors='replace')

    def cached(key):
        return re.search(rf'^{key}:[^=]*=(.*)$', cache, re.M).group(1)

    configure = (f'cmake -S {CLONE} -B {RELEASE} -G Ninja -DCMAKE_BUILD_TYPE=Release -DSW_GAMES=ALL -DSW_BUILD_UNUSED_KITS=ON '
                 f'-DCMAKE_C_COMPILER="{cached("CMAKE_C_COMPILER")}" -DCMAKE_CXX_COMPILER="{cached("CMAKE_CXX_COMPILER")}" '
                 f'-DCMAKE_TOOLCHAIN_FILE="{cached("CMAKE_TOOLCHAIN_FILE")}" -DVCPKG_INSTALLED_DIR="{cached("VCPKG_INSTALLED_DIR")}"')
    if shell(f'{configure} > {RELEASE}.configure.log 2>&1').returncode != 0:
        print(f'구성 실패 - {RELEASE}.configure.log')
        return 1
    built = shell(f'ninja -C {RELEASE} -k 0 > {RELEASE}.build.log 2>&1').returncode
    log = pathlib.Path(f'{RELEASE}.build.log').read_text(errors='replace').splitlines()
    for line in [l for l in log if 'error:' in l or 'FAILED:' in l][:40]:
        print(line[:240])
    print('경고', sum('warning:' in l for l in log), '빌드', built)
    if built != 0:
        return built
    # Windows 에서 이미 임포트한 결과를 옮긴다 - 스탬프가 바이트 해시라 다시 임포트하지 않는다(Linux 임포트는 느리다).
    copy = (f"cd {WSL_REPO}/Content && find . -type f \\( -path '*/textures/*' -o -path '*/models/*' -o -path '*/heightfields/*' -o -name import.stamp \\) "
            f"-not -path '*/textures_raw/*.png' -not -path '*/models_raw/*' -printf '%P\\n' > /tmp/lcopy.list && rsync -a --files-from=/tmp/lcopy.list . {CLONE}/Content/")
    if shell(copy).returncode != 0:
        print('임포트 결과 복사 실패')
        return 1
    return shell(f"cd {RELEASE} && ctest -R '^Import\\.' 2>&1 | tail -n 1").returncode


def report(pack):
    """진 단계와 그 까닭(자동화 판정 경고)을 낸다. 그 밖의 경고 · 오류 분류는 로그를 직접 읽는다."""
    path = f'{LOGS}/Scenario.{pack}.vk.log'
    return report_file(path)


def report_file(path):
    if not os.path.exists(path):
        print('로그 없음:', path)
        return 1
    failed = 0
    reasons = []
    for line in open(path, errors='replace'):
        if 'Automation.JudgeWarning' in line:
            reasons.append(line[line.find('[Scenario]') + 11:].strip()[:190])
        elif 'AutomationSession.ChainStage' in line:
            stage = re.search(r'chain stage (\d+)/(\d+) (\w+) \(([\w.]+)\)', line)
            if stage and stage.group(3) not in ('Passed', 'Skipped'):
                failed += 1
                print(f'  {stage.group(1)}/{stage.group(2)} {stage.group(3)} {stage.group(4)}')
                for reason in reasons[:6]:
                    print('     ', reason)
            reasons = []
    # 단계 하나만 돌리면 체인 줄이 없다 - 남은 판정 경고가 그 단계의 까닭이다.
    for reason in reasons[:6]:
        print('     ', reason)
    failed += 1 if reasons else 0
    print('  진 단계', failed)
    return 1 if failed else 0


def run_chain(name, record, display):
    """체인 하나를 자기 Xvfb 에서 돌리고 (이름, 코드, 보고 글)을 돌려줍니다."""
    with Xvfb(display):
        env = scenario_env(display)
        listing = subprocess.run(['ctest', '-V', '-N', '-R', f'^Scenario\\.{name}$'], cwd=RELEASE, env=env, capture_output=True, text=True).stdout
        found = [l for l in listing.splitlines() if 'Test command:' in l and '-scenario=' in l]
        if not found:
            return name, 1, '체인 없음'
        command = re.sub(r'.*Test command: ', '', found[0])
        # 백엔드마다 프로세스를 나누는 대신 Vulkan 하나만 돈다(GL 교체 단계는 체인 안에서 돈다).
        command = command.replace('"-scenario-backend-split" ', '').replace('-scenario-backends=all', '-scenario-backends=vk')
        command += f' -WinX=0 -WinY=0 -gv_scenarioWallScale={WALL_SCALE}' + (' -scenario-update-golden' if record else '')
        started = time.time()
        result = subprocess.run(f'timeout 3000 {command}', shell=True, executable='/bin/bash', cwd=f'{RELEASE}/Bin', env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            reported = report(name)
        return name, result.returncode or reported, f'{name} rc={result.returncode} {int(time.time() - started)}초\n{output.getvalue()}'


def chain(arguments):
    record = '--record' in arguments
    names = [a for a in arguments if a != '--record']
    if not names:
        sys.exit('체인 이름을 적는다 (예: chain editor shooter3d)')
    code = 0
    # 표시 번호를 한 줄에 하나씩 받아 Xvfb 가 겹치지 않는다. 표시 번호 = 99 + 작업 순번
    with concurrent.futures.ThreadPoolExecutor(PARALLEL_CHAINS) as pool:
        for _, rc, text in pool.map(lambda item: run_chain(item[1], record, 99 + item[0]), enumerate(names)):
            print(text, flush=True)
            code = code or rc
    return code


def run_stage_set(names, env, log_name):
    """단계 이름 묶음을 부분 체인으로 돌린다. 건너뜀 · 실패가 있어도 끝까지 간다 - 없으면 뒤 단계가 돌지 않아 통과로 착각한다."""
    listed = ','.join(f'engine/automation/editor/{n}.scenario.xml' for n in names)
    return subprocess.run(['timeout', '1200', './App', '-EnableEditor', f'-scenario={listed}', '-scenario-keep-going', '-unattended', '-scenario-backends=vk',
                           '-WinX=0', '-WinY=0', f'-gv_scenarioWallScale={WALL_SCALE}', f'-log-file={log_name}.log'],
                          cwd=f'{RELEASE}/Bin', env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode


def stages(arguments):
    code = 0
    with Xvfb():
        env = scenario_env(99)
        for stage_set in arguments:
            rc = run_stage_set(stage_set.split(','), env, 'x_stages')
            print(f'[{stage_set}] rc={rc}')
            code = report_file(f'{RELEASE}/Bin/x_stages.vk.log') or code
    return code


def culprit(arguments):
    target = arguments[0]
    pack = arguments[1] if len(arguments) > 1 else 'editor'
    text = open(f'{LOGS}/Scenario.{pack}.vk.log', errors='replace').read()
    names = [m.group(1) for m in re.finditer(r'chain stage \d+/\d+ \w+ \(editor\.(\w+)\)', text)]
    if target not in names:
        sys.exit(f'{target} 이 체인 로그에 없다 - 먼저 chain {pack}')
    before = names[:names.index(target)]
    with Xvfb():
        env = scenario_env(99)

        def fails(prefix):
            run_stage_set(prefix + [target], env, 'x_culprit')
            log = open(f'{RELEASE}/Bin/x_culprit.vk.log', errors='replace').read()
            bad = re.search(rf'chain stage \d+/\d+ Failed \(editor\.{target}\)', log) is not None
            print(len(prefix), prefix[0] if prefix else '-', '짐' if bad else '통과', flush=True)
            return bad

        if not fails(before):
            print('재현 안 됨 - 체인의 다른 인자(--record) · 앞 체인 상태를 본다')
            return 1
        low, high = 0, len(before)  # before[low:] 는 지고 before[high:] 는 통과한다
        while high - low > 1:
            middle = (low + high) // 2
            if fails(before[middle:]):
                low = middle
            else:
                high = middle
        print('시작 단계:', before[low])
        print('단독으로 지는가(아니면 뒤 단계와 함께 만드는 상태다):')
        fails([before[low]])
    return 0


def read_ppm_colors(path):
    """P6 사진의 크기와 표본 색 수입니다."""
    data = pathlib.Path(path).read_bytes()
    tokens, offset = [], 0
    while len(tokens) < 4:
        end = offset
        while data[end] not in b' \n\t\r':
            end += 1
        tokens.append(data[offset:end])
        offset = end + 1
    pixels = data[offset:]
    return int(tokens[1]), int(tokens[2]), len({pixels[i:i + 3] for i in range(0, len(pixels), 21)})


def goldens():
    count = 0
    flat = []
    for root, _, files in os.walk(f'{CLONE}/Content'):
        for name in files:
            if name.endswith('.linux.ppm'):
                source = os.path.join(root, name)
                target = source.replace(f'{CLONE}/Content', f'{WSL_REPO}/Content')
                os.makedirs(os.path.dirname(target), exist_ok=True)
                shutil.copyfile(source, target)
                count += 1
                width, height, colors = read_ppm_colors(source)
                if colors <= 3:
                    flat.append(f'{name} {width}x{height} {colors}색')
    print('복사', count)
    print('한 색에 가까운 사진(빈 화면을 기록했을 수 있다 - Windows 골든과 견준다):')
    for line in flat:
        print(' ', line)
    return 0


def verify_all(arguments):
    """묶음 끝 Linux 검증 한 번. Debug 클라이언트는 짓지 않는다 - Release 단위가 키트까지 덮고 단언은 Windows Debug 가 본다. 서버 구성은 렌더러를 빼 여기서만 드러나는 층 위반이 있어 남긴다."""
    code = build()
    if code != 0:
        return code
    head = shell(f'git -C {CLONE} rev-parse HEAD', capture_output=True, text=True).stdout.strip()
    server_dir = f'{CLONE}/build/Linux-Server'
    steps = [('Release 단위', f'cd {RELEASE} && ./Bin/App --test -unattended > /tmp/lt.unit.log 2>&1; rc=$?; grep -E "case run|FAILED" /tmp/lt.unit.log | tail -n 5 | cut -c1-160; exit $rc'),
             ('서버 빌드', f'cmake --build {server_dir} -j8 > /tmp/srv.build.log 2>&1; rc=$?; grep -E "error:|FAILED:|undefined reference" /tmp/srv.build.log | head -n 20 | cut -c1-220; exit $rc'),
             ('서버 기동', f'cd {server_dir} && timeout 120 ./Bin/Server -gv_serverExitAfterTicks=60 > /tmp/srv.run.log 2>&1'),
             ('서버 단위', f'cd {server_dir} && timeout 400 ./Bin/Server --test > /tmp/srv.unit.log 2>&1; rc=$?; grep -E "case run|NO TESTS" /tmp/srv.unit.log | cut -c1-140; exit $rc')]
    for title, command in steps:
        rc = shell(command).returncode
        print(title, rc, flush=True)
        if rc != 0:
            return rc
    # 빠른 실패: 단위 · 서버가 지면 긴 체인을 돌리지 않는다.
    code = chain(arguments + ALL_CHAINS)
    if code != 0:
        print('LINUX_FAILED')
        return code
    print(f'LINUX_OK={head}')
    return 0


def main(argv):
    if os.name == 'nt':
        return run_in_wsl(argv)
    command = argv[0] if argv else ''
    arguments = argv[1:]
    if command == 'setup':
        return setup()
    if command == 'build':
        return build()
    if command == 'chain':
        return chain(arguments)
    if command == 'stages' and arguments:
        return stages(arguments)
    if command == 'culprit' and arguments:
        return culprit(arguments)
    if command == 'report':
        return report(arguments[0] if arguments else 'editor')
    if command == 'goldens':
        return goldens()
    if command == 'all':
        return verify_all(arguments)
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
