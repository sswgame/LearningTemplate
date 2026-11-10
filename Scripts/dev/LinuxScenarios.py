"""Linux 검증(py -3 Scripts/dev/LinuxScenarios.py <명령>) — WSL 클론에서 빌드 · 시나리오 · 골든 · 단위 테스트를 돌린다.

Windows 에서 부르면 자기를 WSL 에서 다시 띄운다. 창 관리자 없는 Xvfb 안에서 돌아 사용자 화면을 건드리지 않고,
Windows 의 하드웨어 GPU 테스트와 함께 돌려도 된다. 체인은 저마다 자기 Xvfb 에서 동시에 돈다. 이 명령은 클론당 하나만 돌린다.

명령:
  setup                  Xvfb · 글꼴을 sudo 없이 ~/lt-deps 에 푼다(한 번)
  setup-dzn              GPU Vulkan(dzn — Mesa 의 Vulkan-on-D3D12)을 sudo 없이 ~/lt-deps/dzn 에 짓는다(한 번)
  --gpu dzn              (어느 명령 앞에든) 시나리오를 소프트웨어(lavapipe · llvmpipe) 대신 WSL GPU(dzn Vulkan · d3d12 GL)로 돌린다.
                         골든은 소프트웨어로 뜬 .linux 라 그림 비교는 참고만 한다
  build                  본 저장소 HEAD 를 클론으로 옮겨 Linux-Release 를 짓고 임포트 결과를 복사한다
  chain [--record] 이름… Scenario.<이름> 체인을 돌리고 진 단계와 까닭을 낸다(--record 는 .linux 골든 기록)
  stages 묶음…           쉼표로 이은 단계 묶음을 부분 체인으로 돌린다(예: focusloss,graphaddnode)
  culprit 단계 [체인]    체인에서만 지는 단계의 앞 단계를 이분 탐색한다(체인 기본값 editor, 먼저 chain 을 돌려 로그가 있어야 한다)
  report [체인]          마지막 체인 로그에서 진 단계와 까닭을 다시 낸다
  goldens                클론의 .linux 골든을 본 저장소로 복사하고 한 색인 사진을 알린다
  stop                   돌고 있는 실행을 끝낸다(체인 · 게임 · Xvfb). 게임 프로세스는 이름이 GameThread 로 바뀌어 있다
  all [--record]         build → 모든 체인(병렬) · Release 단위 · 서버 빌드 · 단위. 끝 줄 LINUX_OK=<sha> 가 나와야 그 커밋을 push 한다
"""
import concurrent.futures
import contextlib
import io
import os
import pathlib
import re
import shutil
import signal
import subprocess
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))   # Scripts
from common.HostPlatform import isWindowsHost  # noqa: E402
from common import AppArguments  # noqa: E402
from dev import ScenarioCulprit  # noqa: E402

DISTRO = 'Ubuntu-26.04'
REPO = pathlib.Path(__file__).resolve().parents[2]
# 저장소의 WSL 경로 — WSL 안에서는 이 파일의 자리가 곧 그 경로다(/mnt/<드라이브>/…, 체크아웃 위치를 고정하지 않는다).
# Windows 쪽은 쓰지 않고 다시 띄울 때 `wslpath -a` 로 구해 넘긴다(run_in_wsl). 예전에는 /mnt/d/… 로 고정해 다른 드라이브 · 위치의 클론에서 다른 트리를 가리켰다.
WSL_REPO = None if isWindowsHost() else REPO.as_posix()
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
ALL_CHAINS = ('editor empty abilityarena frontiertown harvestvalley lanternvale meadowvillage nilecity pixeltown shooter3d starskirmish themepark velmora voxelcraft wildwest '
              'abilityarena.editor harvestvalley.editor shooter3d.editor themepark.editor').split()
PARALLEL_CHAINS = 4  # lavapipe 가 코어를 나눠 쓰므로 많이 띄우면 벽시계 시한을 넘긴다
WALL_SCALE = '4'  # 소프트웨어 렌더링이라 벽시계 시한을 늘린다(gv_scenarioWallScale)
PATH_LINUX = '/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin'  # Windows PATH 가 끼면 Windows 도구를 잡는다


def run_in_wsl(argv):
    """Windows 쪽: HEAD 를 refs/linux-verify 에 적고 같은 명령을 WSL 에서 띄운다."""
    if argv and argv[0] in ('build', 'all'):
        head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=REPO, text=True).strip()
        subprocess.check_call(['git', 'update-ref', 'refs/linux-verify', head], cwd=REPO)
    env = dict(os.environ, MSYS_NO_PATHCONV='1', MSYS2_ARG_CONV_EXCL='*')
    wslRepo = to_wsl_path(REPO, env)
    return subprocess.call(['wsl.exe', '-d', DISTRO, '--', 'python3', f'{wslRepo}/Scripts/dev/LinuxScenarios.py'] + argv, env=env)


def to_wsl_path(windowsPath, env):
    """Windows 경로를 이 배포판의 WSL 경로로 바꾼다(`wslpath -a`). 못 바꾸면 오류로 멈춘다 — 고정 경로로 대신하지 않는다."""
    result = subprocess.run(['wsl.exe', '-d', DISTRO, '--', 'wslpath', '-a', windowsPath.as_posix()], env=env, capture_output=True, text=True)
    converted = result.stdout.strip()
    if result.returncode != 0 or not converted.startswith('/'):
        sys.exit(f'LinuxScenarios: wslpath could not convert {windowsPath} ({result.stderr.strip()})')
    return converted


def shell(command, **kwargs):
    return subprocess.run(command, shell=True, executable='/bin/bash', env=dict(os.environ, PATH=PATH_LINUX), **kwargs)


# 시나리오 드라이버. 기본은 소프트웨어(lavapipe · llvmpipe — 결정적이고 .linux 골든이 이것으로 떴다). `--gpu dzn` 은 WSL 의 GPU 로 돈다:
# Vulkan 은 직접 지은 dzn(시스템 Mesa 에 없다 — setup-dzn), GL 은 시스템 Mesa 의 d3d12 gallium. dzn 은 Vulkan 1.2 까지다(엔진 요구 1.2).
GPU = 'software'
DZN = f'{HOME}/lt-deps/dzn'
DZN_MESA_VERSION = '26.0.8'
DZN_DEBS = ('directx-headers-dev libdrm-dev libdrm2 libpciaccess-dev libpciaccess0 libx11-dev libx11-xcb-dev libx11-xcb1 libxau-dev libxcb-dri2-0-dev '
            'libxcb-dri2-0 libxcb-dri3-0 libxcb-dri3-dev libxcb-glx0-dev libxcb-glx0 libxcb-keysyms1-dev libxcb-keysyms1 libxcb-present-dev '
            'libxcb-present0 libxcb-randr0-dev libxcb-randr0 libxcb-render0-dev libxcb-render0 libxcb-shape0-dev libxcb-shape0 libxcb-shm0-dev '
            'libxcb-shm0 libxcb-sync-dev libxcb-sync1 libxcb-xfixes0-dev libxcb-xfixes0 libxcb1-dev libxdmcp-dev libxext-dev libxext6 libxfixes-dev '
            'libxfixes3 libxrandr-dev libxrandr2 libxrender-dev libxrender1 libxshmfence-dev libxshmfence1 libxxf86vm-dev libxxf86vm1 meson '
            'python3-mako python3-markupsafe python3-packaging python3-yaml vulkan-tools x11proto-dev').split()
DZN_MESON_OPTIONS = ('-Dvulkan-drivers=microsoft-experimental -Dgallium-drivers= -Dplatforms=x11 -Dopengl=false -Dgles1=disabled -Dgles2=disabled '
                     '-Degl=disabled -Dglx=disabled -Dgbm=disabled -Dllvm=disabled -Dvideo-codecs= -Dxmlconfig=disabled -Dzstd=disabled '
                     '-Dbuild-tests=false -Dvalgrind=disabled -Dlibunwind=disabled -Dlmsensors=disabled -Dlibdir=lib -Dbuildtype=release')


def scenario_env(display):
    if GPU == 'dzn':
        # dzn 은 libxcb-keysyms 등 시스템에 없는 xcb 라이브러리를 ~/lt-deps/dzn/root 에서 찾는다 — 빠지면 ICD 가 조용히 안 올라와 로더 확장 5 개만 보인다.
        env = dict(os.environ, PATH=PATH_LINUX, DISPLAY=f':{display}', GALLIUM_DRIVER='d3d12',
                   VK_ICD_FILENAMES=f'{DZN}/install/share/vulkan/icd.d/dzn_icd.x86_64.json',
                   LD_LIBRARY_PATH=f'/usr/lib/wsl/lib:{DZN}/root/usr/lib/x86_64-linux-gnu')
    else:
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
        try:
            os.symlink(f'{XVFB_ROOT}/usr/bin', '/tmp/xkb')
        except FileExistsError:
            pass  # 앞 실행이나 함께 뜨는 체인이 이미 만들었다
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


def ensure_vcpkg_manifest(installedDir):
    """
    공유 설치본(Linux-Debug 의 vcpkg_installed)이 지금 매니페스트로 깔렸는지 본다. 매니페스트(vcpkg.json)가 바뀌었으면 Linux-Debug 를
    -DSW_VCPKG_FORCE_INSTALL=ON 으로 한 번 다시 구성해 새 포트를 깐다(클론은 혼자 쓰므로 공유 설치본 보호 장치를 넘는다 — linux-verify 스킬 "클론 환경").
    표시는 설치본 안의 매니페스트 해시 파일이다. 깔지 못하면 False(구성 로그 경로를 남긴다).
    """
    stamp = f'{installedDir}/.sw-manifest.sha256'
    current = shell(f'sha256sum {CLONE}/vcpkg.json | cut -d" " -f1', capture_output=True, text=True).stdout.strip()
    previous = shell(f'cat {stamp} 2>/dev/null', capture_output=True, text=True).stdout.strip()
    if current == previous:
        return True
    debugDir = f'{CLONE}/build/Linux-Debug'
    print(f'vcpkg 매니페스트가 바뀌었다 - {debugDir} 를 강제 설치로 다시 구성한다(오래 걸릴 수 있다)')
    if shell(f'cmake -S {CLONE} -B {debugDir} -DSW_VCPKG_FORCE_INSTALL=ON > {debugDir}.vcpkg.log 2>&1').returncode != 0:
        print(f'vcpkg 설치 실패 - {debugDir}.vcpkg.log')
        return False
    shell(f'echo {current} > {stamp}')
    return True


def build():
    sync_clone()
    cache = pathlib.Path(f'{CLONE}/build/Linux-Debug/CMakeCache.txt').read_text(errors='replace')

    def cached(key):
        return re.search(rf'^{key}:[^=]*=(.*)$', cache, re.M).group(1)

    if ensure_vcpkg_manifest(cached('VCPKG_INSTALLED_DIR')) is False:
        return 1
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


def window_arguments():
    """Xvfb 화면 왼쪽 위에 창을 두고, 소프트웨어 렌더링만큼 벽시계 시한을 늘린다."""
    return [AppArguments.option('WinX', 0), AppArguments.option('WinY', 0), AppArguments.variable('gv_scenarioWallScale', WALL_SCALE)]


def run_chain(name, record, display):
    """체인 하나를 자기 Xvfb 에서 돌리고 (이름, 코드, 보고 글)을 돌려줍니다."""
    with Xvfb(display):
        env = scenario_env(display)
        try:
            chain_command = AppArguments.findChainCommand(pathlib.Path(RELEASE), name, env)
        except AppArguments.AppArgumentError:
            return name, 1, '체인 없음'
        # 백엔드마다 프로세스를 나누는 대신 Vulkan 하나만 돈다(GL 교체 단계는 체인 안에서 돈다).
        command = AppArguments.without(chain_command, 'scenario-backend-split', 'scenario-backends') + [AppArguments.option('scenario-backends', 'vk'), *window_arguments()]
        if record:
            command.append(AppArguments.flag('scenario-update-golden'))
        started = time.time()
        result = subprocess.run(['timeout', '3000', *command], cwd=f'{RELEASE}/Bin', env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
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
    return subprocess.run(['timeout', '1200', './App', AppArguments.flag('EnableEditor'), AppArguments.option('scenario', listed), AppArguments.flag('scenario-keep-going'),
                           AppArguments.flag('unattended'), AppArguments.option('scenario-backends', 'vk'), *window_arguments(), AppArguments.option('log-file', f'{log_name}.log')],
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
    """이분 탐색은 ScenarioCulprit.findCulprit(Windows 와 같은 함수) — 여기는 Xvfb · lavapipe 실행기와 ctest 목록만 댄다."""
    target = arguments[0]
    pack = arguments[1] if len(arguments) > 1 else 'editor'
    with Xvfb():
        env = scenario_env(99)
        command = AppArguments.findChainCommand(pathlib.Path(RELEASE), pack, env)
        paths = AppArguments.readChainStages(command)
        # 단계 이름은 시나리오 파일의 name 속성, 판정은 실행마다 비운 보고 폴더의 JSON(ScenarioCulprit 과 같은 함수) — 로그 문구를 읽지 않는다.
        target_index, target_name = ScenarioCulprit.resolveTarget({index: ScenarioCulprit.readScenarioName(path) for index, path in enumerate(paths)}, target)
        base = AppArguments.without(command, 'scenario-backend-split', 'scenario-keep-going', 'scenario-backends', 'log-file', 'scenario', 'scenario-report')
        run_counter = [0]

        def fails(prefix):
            listed = ','.join(prefix + [paths[target_index]])
            run_counter[0] += 1
            run_dir = ScenarioCulprit.makeRunReportFolder(pathlib.Path(RELEASE), run_counter[0])
            subprocess.run(['timeout', '1200', *base, AppArguments.option('scenario', listed), AppArguments.flag('scenario-keep-going'), AppArguments.option('scenario-backends', 'vk'),
                            AppArguments.option('scenario-report', run_dir / 'report.json'),
                            *window_arguments(), AppArguments.option('log-file', 'x_culprit.log')], cwd=f'{RELEASE}/Bin', env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            bad = ScenarioCulprit.stageFailedInReports(ScenarioCulprit.readRunReports(run_dir), target_name)
            print(len(prefix), prefix[0] if prefix else '-', '짐' if bad else '통과', flush=True)
            return bad

        before = paths[:target_index]
        found_index = ScenarioCulprit.findCulprit(before, fails)
        if found_index is None:
            print('재현 안 됨 - 체인의 다른 인자(--record) · 앞 체인 상태를 본다')
            return 1
        print('시작 단계:', before[found_index])
        print('단독으로 지는가(아니면 뒤 단계와 함께 만드는 상태다):')
        fails([before[found_index]])
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


def find_run_processes():
    """이 도구가 띄운 프로세스의 pid 입니다. 명령줄을 직접 읽는다 - `pkill -f` 는 그 문구를 품은 자기 셸까지 끝낸다."""
    own = os.getpid()
    found = []
    for name in os.listdir('/proc'):
        if not name.isdigit() or int(name) == own:
            continue
        try:
            command = pathlib.Path(f'/proc/{name}/cmdline').read_bytes().replace(b'\0', b' ').decode(errors='replace')
        except OSError:
            continue
        if 'LinuxScenarios.py' in command or 'Xvfb.p' in command or f'{RELEASE}/Bin/' in command or re.match(r'\./(App|Server) ', command):
            found.append(int(name))
    return found


def stop():
    """돌고 있는 실행을 끝낸다. SIGTERM 을 받은 실행은 Xvfb 를 닫지 못하므로 함께 끝낸다."""
    for sig in (signal.SIGTERM, signal.SIGKILL):
        for pid in find_run_processes():
            try:
                os.kill(pid, sig)
            except OSError:
                pass  # 그 사이 끝났다
        time.sleep(2)
    left = find_run_processes()
    print('남은 프로세스:', left if left else '없음')
    return 1 if left else 0


def setup_dzn():
    """dzn(Mesa microsoft-experimental Vulkan)을 sudo 없이 짓는다: 빌드 의존 deb 를 풀고 Mesa 소스를 받아 meson 으로 ~/lt-deps/dzn/install 에 설치한다."""
    os.makedirs(f'{DZN}/deb', exist_ok=True)
    for package in DZN_DEBS:
        if shell(f'cd {DZN}/deb && apt-get download {package}', stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode != 0:
            print('내려받지 못함:', package)
    shell(f'cd {DZN}/deb && mkdir -p {DZN}/root && for d in *.deb; do dpkg -x "$d" {DZN}/root; done', check=True)
    source = f'{DZN}/src/mesa-{DZN_MESA_VERSION}'
    if not os.path.isdir(source):
        shell(f'mkdir -p {DZN}/src && cd {DZN}/src && curl -sSLO https://archive.mesa3d.org/mesa-{DZN_MESA_VERSION}.tar.xz && tar xf mesa-{DZN_MESA_VERSION}.tar.xz', check=True)
    root = f'{DZN}/root/usr'
    env = dict(os.environ, PATH=f'{root}/bin:{PATH_LINUX}', PYTHONPATH=f'{root}/lib/python3/dist-packages',
               PKG_CONFIG_PATH=f'{root}/lib/x86_64-linux-gnu/pkgconfig:{root}/share/pkgconfig', CPATH=f'{root}/include',
               LIBRARY_PATH=f'{root}/lib/x86_64-linux-gnu', LD_LIBRARY_PATH=f'{root}/lib/x86_64-linux-gnu')
    subprocess.run(f'meson setup --wipe {DZN}/build {source} {DZN_MESON_OPTIONS} -Dprefix={DZN}/install || meson setup {DZN}/build {source} {DZN_MESON_OPTIONS} -Dprefix={DZN}/install',
                   shell=True, executable='/bin/bash', env=env, check=True)
    subprocess.run(f'ninja -C {DZN}/build install', shell=True, executable='/bin/bash', env=env, check=True)
    print('dzn:', f'{DZN}/install/share/vulkan/icd.d/dzn_icd.x86_64.json')
    return 0


def main(argv):
    if isWindowsHost():
        return run_in_wsl(argv)
    global GPU
    if argv[:1] == ['--gpu'] and len(argv) > 1:
        GPU = argv[1]
        argv = argv[2:]
        if GPU not in ('dzn', 'software'):
            sys.exit(f'--gpu 는 dzn 또는 software 다: {GPU}')
    command = argv[0] if argv else ''
    arguments = argv[1:]
    if command == 'setup':
        return setup()
    if command == 'setup-dzn':
        return setup_dzn()
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
    if command == 'stop':
        return stop()
    if command == 'all':
        return verify_all(arguments)
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
