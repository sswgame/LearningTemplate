"""핫 리로드 뒤 디버거 브레이크포인트가 새 모듈에서 잡히는지 자동으로 본다(py -3 Scripts/dev/hot_reload_debug_check.py [--preset 프리셋] [--variant code|layout]).

lldb(Tools/LLVM — `Scripts/setup/llvm.py` 가 둔다) 아래에서 Playground 의 확인 시나리오(`game/playground/automation/debugger/hotreload<변형>.scenario.xml`)를 띄우고:
1. `HotReloadTestComponent::computeVersion` 브레이크포인트가 처음 멈추면(리로드 전 모듈) 소스를 바꿔 SWGame 만 다시 짓는다 — 디버거가 PDB 를 쥔 채 링크가 되는지.
   변형 code 는 `.cpp` 의 판 숫자만, layout 은 헤더에 PROPERTY 칸 하나와 그 생성자 값 7 을 더한다(리로드가 리플렉션 칸으로 상태를 옮기는지 — 시나리오가 본다).
2. 이어서 시나리오가 `module.reload SWGame` 을 하고, 같은 브레이크포인트가 **다른 모듈 파일**(새 섀도 복사본)에서 멈추고 소스 줄이 풀리는지 본다.
3. 시나리오 종료 코드(0 = 통과)를 함께 보고, 끝나면 소스를 되돌려 다시 짓는다.
VS 의 vsdbg 는 DAP 시작 때 핸드셰이크 서명을 요구하고 "VS Code · Visual Studio 와 함께만" 이라는 라이선스라 쓰지 않는다.
"""
import argparse
import json
import pathlib
import sys

from common.command import Command, ScriptContext
from common import app_arguments
from common.paths import get_bin_dir, get_build_dir
from common.run_process import run_cmake, run_process

REPO = pathlib.Path(__file__).resolve().parents[2]
PROBE_H = REPO / 'Source/Games/Playground/Dev/ScenarioTest/HotReloadTestComponent.h'
PROBE_CPP = REPO / 'Source/Games/Playground/Dev/ScenarioTest/HotReloadTestComponent.cpp'
FUNCTION = 'sw::HotReloadTestComponent::computeVersion'

# lldb 안에서 도는 쪽(lldb 의 Python — 임베드 3.10). 결과는 JSON 한 줄로 낸다.
DRIVER = r'''
import json, subprocess, lldb
cfg = json.loads(CONFIG)
dbg = lldb.debugger
dbg.SetAsync(False)
target = dbg.CreateTarget(cfg["app"])
bp = target.BreakpointCreateByName(cfg["function"])
info = lldb.SBLaunchInfo(cfg["args"])
info.SetWorkingDirectory(cfg["bin"])
# 새 SBLaunchInfo 는 환경이 비어 있다 — SystemRoot 등이 없으면 D3D · COM(XAudio2)이 올라오지 않는다. lldb 의 환경(곧 이 스크립트의 부모)을 그대로 넘긴다.
import os
info.SetEnvironmentEntries([f"{key}={value}" for key, value in os.environ.items()], False)
error = lldb.SBError()
process = target.Launch(info, error)
result = {"launch_error": error.GetCString(), "hits": [], "build_rc": None, "exit_code": None}
while process.IsValid() and process.GetState() == lldb.eStateStopped:
    for thread in process:
        if thread.GetStopReason() != lldb.eStopReasonBreakpoint:
            continue
        frame = thread.GetFrameAtIndex(0)
        line = frame.GetLineEntry()
        result["hits"].append({"module": frame.GetModule().GetFileSpec().fullpath, "file": line.GetFileSpec().fullpath, "line": line.GetLine()})
        if len(result["hits"]) == 1:
            exec(cfg["patch"])
            result["build_rc"] = subprocess.run(cfg["build"], cwd=cfg["repo"]).returncode
    process.Continue()
if process.IsValid() and process.GetState() == lldb.eStateExited:
    result["exit_code"] = process.GetExitStatus()
print("HOTRELOAD_RESULT " + json.dumps(result))
'''

PATCH_CODE = r'''
import pathlib
p = pathlib.Path(cfg["probe_cpp"]); t = p.read_text(encoding="utf-8")
p.write_text(t.replace("kTestVersion = 100;", "kTestVersion = 101;"), encoding="utf-8", newline="")
'''
PATCH_LAYOUT = PATCH_CODE + r'''
h = pathlib.Path(cfg["probe_h"]); t = h.read_text(encoding="utf-8")
h.write_text(t.replace("        // HOTRELOAD-LAYOUT\n", "        PROPERTY( Category = \"Probe\", Tooltip = \"Field the layout check adds\" )\n        int32 _addedValue;\n        // HOTRELOAD-LAYOUT\n"), encoding="utf-8", newline="")
c = pathlib.Path(cfg["probe_cpp"]); t = c.read_text(encoding="utf-8")
t = t.replace("    // HOTRELOAD-LAYOUT-INIT\n", "        , _addedValue{ 7 }\n    // HOTRELOAD-LAYOUT-INIT\n").replace("return 0; // HOTRELOAD-LAYOUT-READ", "return _addedValue; // HOTRELOAD-LAYOUT-READ")
c.write_text(t, encoding="utf-8", newline="")
'''


class HotReloadDebugCheckCommand(Command):
    name = "hot-reload-debug-check"
    help = "핫 리로드 디버그 확인 — 디버거(lldb)가 붙은 채 모듈을 다시 지어 올리고 중단점이 새 모듈에서 멈추는지 본다(--variant code · layout)"

    def add_arguments(self, parser: argparse.ArgumentParser) -> None:
        parser.add_argument('--preset', default='Ninja-Debug')
        parser.add_argument('--variant', choices=('code', 'layout'), default='code')

    def run(self, context: ScriptContext, args: argparse.Namespace) -> int:
        bin_dir = get_bin_dir(args.preset)
        lldb = REPO / 'Tools/LLVM/bin/lldb.exe'
        if not lldb.is_file():
            sys.exit(f'{lldb} 이 없다 — py -3 Scripts/setup/llvm.py 로 둔다')
        scenario = 'game/playground/automation/debugger/hotreloadbreakpoint.scenario.xml' if args.variant == 'code' else 'game/playground/automation/debugger/hotreloadlayout.scenario.xml'
        log_path = get_build_dir(args.preset) / 'Testing' / 'Logs' / f'HotReloadDebug.{args.variant}.log'
        build = ['cmake', '--build', '--preset', args.preset, '--target', 'SWGame_Playground']
        originals = {path: path.read_bytes() for path in (PROBE_H, PROBE_CPP)}
        # 리로드 전 모듈이 지금 소스와 같아야 한다(확인 컴포넌트가 없는 옛 SWGame 으로 띄우면 씬이 읽히지 않는다).
        if run_cmake(build[1:], cwd=REPO, capture=False).return_code != 0:
            sys.exit('SWGame 을 짓지 못했다')
        config = {
            'app': str(bin_dir / 'App.exe'), 'bin': str(bin_dir), 'repo': str(REPO), 'function': FUNCTION,
            'args': [app_arguments.option('scenario', scenario), app_arguments.flag('unattended'), app_arguments.option('log-file', log_path)],
            'build': build, 'probe_h': str(PROBE_H), 'probe_cpp': str(PROBE_CPP),
            'patch': PATCH_CODE if args.variant == 'code' else PATCH_LAYOUT,
        }
        driver_path = get_build_dir(args.preset) / 'HotReloadDebugDriver.py'
        driver_path.write_text(f'CONFIG = {json.dumps(json.dumps(config))}\n' + DRIVER, encoding='utf-8')
        try:
            run = run_process([lldb, '--batch', '-o', f'command script import "{driver_path}"'])
        finally:
            for path, data in originals.items():
                path.write_bytes(data)
            restore = run_cmake(build[1:], cwd=REPO, capture=False).return_code
        line = next((l for l in run.stdout.splitlines() if l.startswith('HOTRELOAD_RESULT ')), None)
        if line is None:
            print(run.stdout[-4000:], run.stderr[-4000:])
            sys.exit('lldb 가 결과를 내지 않았다')
        result = json.loads(line[len('HOTRELOAD_RESULT '):])
        hits = result['hits']
        checks = {
            '링크(디버거가 PDB 를 쥔 채)': result['build_rc'] == 0,
            '리로드 전 멈춤': len(hits) >= 1,
            '리로드 뒤 새 모듈에서 멈춤': len(hits) >= 2 and hits[-1]['module'] != hits[0]['module'],
            '소스 줄이 풀림': bool(hits) and all(h['file'].replace('\\', '/').endswith('HotReloadTestComponent.cpp') for h in hits),
            '시나리오 통과': result['exit_code'] == 0,
            '원래 소스로 다시 지음': restore == 0,
        }
        print(json.dumps(result, ensure_ascii=False, indent=1))
        for name, ok in checks.items():
            print(f"{'OK  ' if ok else 'FAIL'} {name}")
        print(f'로그 {log_path}')
        return 0 if all(checks.values()) else 1
