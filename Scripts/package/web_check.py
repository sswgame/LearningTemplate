#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file web_check.py
@brief Web-Shipping 묶음을 브라우저에서 띄워 기동 · WebGPU · 오류 없음 · 화면 · IDBFS 영속을 한 번에 본다 — 손 확인을 쌓지 않는 웹 확인 절차.

| 단계 | 하는 일 |
| --- | --- |
| package | `web-package` 와 같은 묶음 + 확인 페이지 `check.html`(배포 페이지 `index.html` 에 보고 스크립트만 더한 것) |
| serve | 교차 출처 격리 헤더를 주는 로컬 서버(`IsolatedRequestHandler`) + 보고 받는 끝점 `POST /__swcheck/<종류>` |
| run 1 | 브라우저(Chrome · Edge — `BROWSER_CANDIDATES`, 임시 프로필)로 `check.html?probe=write` — `--seconds` 동안 돌린 뒤 화면을 PNG 로 받고 IDBFS 에 표지 파일을 쓴다 |
| run 2 | 같은 프로필로 다시 띄워 `check.html?probe=read` — 앱이 IDBFS 를 붙인 뒤 표지를 읽어 run 1 의 값과 대조한다(새 세션에 영속 폴더가 살아 있다) |

판정(모두 지나야 통과): `navigator.gpu` 가 있다, 오류 줄(printErr · 창 오류 · 처리 안 된 거부)이 없다, 앱이 확인 시간 안에 끝나거나 멈추지 않았다, 화면 PNG 를 받았다,
오디오 워클릿이 링에서 샘플을 꺼내기 시작했다(`AUDIO_STARTED_LINE` — 브라우저는 자동 재생 정책을 끄고 띄운다), run 2 가 run 1 의 표지를 읽었다. 로그 · 화면은 `<묶음>/check/` 에 남는다(`run1.log` · `run2.log` · `screen.png`).
입력은 `--scenario` 로 본다 — 배포 빌드에는 시나리오 실행기가 없어 개발 빌드(Web-Release, 시나리오를 담아 쿠킹한 팩)를 한 번 띄우고, 시나리오가 DOM 이벤트(`DispatchDOMEvent`)를 내
감시 값으로 판정한다. 앱의 종료 코드(`Config/Engine/ProcessExitCodes.json`)가 0 이고 오류 줄이 없으면 통과다(건너뜀 13 도 실패 — 웹에서 볼 수 없었다는 뜻이다).
브라우저는 헤드리스(--headless=new --enable-unsafe-webgpu)로 띄운다. 페이지가 WebGPU 어댑터를 받았는지 알리고(`adapter` 보고), 헤드리스에서 못 받는 기계면
창을 띄워(headed) 처음부터 다시 돈다 — 손으로 `--headed` 를 고를 일이 없다(`--headed` 는 처음부터 창을 띄운다).

사용법:
    py -3 -m Scripts web-check --game Playground
    py -3 -m Scripts web-check --game Playground --skip-package --headed --seconds 30
    py -3 -m Scripts web-check --game Playground --scenario game/playground/automation/web/dominput.scenario.xml
"""

from __future__ import annotations

import argparse
import functools
import http.server
import json
import os
import secrets
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common import app_arguments  # noqa: E402
from common.exit_code import ExitCode  # noqa: E402
from common.host_platform import is_windows_host  # noqa: E402
from common.paths import get_build_dir, get_project_root  # noqa: E402
from package.pack import PACK_FOLDER_NAME  # noqa: E402
from package.web_package import IsolatedRequestHandler, WebPackageError, INDEX_HTML, PACK_LOADER_NAME, PRESET_NAME, make_web_package  # noqa: E402

#: Chrome, 없으면 Edge(둘 다 Chromium 이라 같은 DevTools 프로토콜) — 임시 프로필로 띄운다.
BROWSER_CANDIDATES = (
    r"%ProgramFiles%\Google\Chrome\Application\chrome.exe",
    r"%ProgramFiles(x86)%\Google\Chrome\Application\chrome.exe",
    r"%LOCALAPPDATA%\Google\Chrome\Application\chrome.exe",
    r"%ProgramFiles(x86)%\Microsoft\Edge\Application\msedge.exe",
    r"%ProgramFiles%\Microsoft\Edge\Application\msedge.exe",
)

CHECK_PAGE_NAME = "check.html"
REPORT_PREFIX = "/__swcheck/"
DEFAULT_PORT = 8090
DEFAULT_SECONDS = 20
#: 표지 파일 — AppEntry.cpp 의 kWebPersistentRoot(IDBFS 마운트) 아래다. 두 곳이 같은 자리를 쓴다.
PROBE_PATH = "/persistent/.swwebcheck"
#: 브라우저가 페이지를 열고 보고를 마칠 때까지의 여유(확인 시간 위에 더한다) — 묶음 내려받기 · wasm 컴파일 · IDBFS 읽기.
LOAD_ALLOWANCE_SECONDS = 60
#: 웹 오디오 백엔드(Engine/Audio/Web/WebAudioBackend.js)가 워클릿이 처음 샘플을 꺼냈을 때 Module.print 로 내는 줄의 앞머리 — 믹서 스레드 → 링 → 워클릿이 흐른다는 판정이다.
AUDIO_STARTED_LINE = "O [WebAudio] output started"
STOP_TIMEOUT_SECONDS = 10
#: 시나리오 실행은 개발 빌드다 — 배포 빌드(`PRESET_NAME`)에는 실행기도 시나리오 팩도 없다.
SCENARIO_PRESET_NAME = "Web-Release"
#: 시나리오가 끝나기까지의 상한(초) — 시나리오의 프레임 시한이 먼저 진다. 이것은 앱이 멈춘 때의 끝이다.
SCENARIO_SECONDS = 180
#: 확인 페이지의 보고 스크립트. `probe` 쿼리가 run 1(write) · run 2(read)를 가른다. Module.FS 는 Web.cmake 의 EXPORTED_RUNTIME_METHODS 가 내보낸다.
REPORTER_SCRIPT = """<script>
(() => {{
    const probe = new URLSearchParams(location.search).get("probe");
    const post = (kind, body) => fetch("{prefix}" + kind, {{ method: "POST", body: body }});
    const print = Module.print;
    const printErr = Module.printErr;
    Module.print = (text) => {{ print(text); post("log", "O " + text); }};
    Module.printErr = (text) => {{ printErr(text); post("log", "E " + text); }};
    window.addEventListener("error", (event) => post("log", "E window: " + event.message));
    window.addEventListener("unhandledrejection", (event) => post("log", "E rejection: " + event.reason));
    Module.onExit = (code) => post("exit", "exit " + code);
    Module.onAbort = (what) => post("exit", "abort " + what);
    post("gpu", navigator.gpu ? "yes" : "no");
    (navigator.gpu ? navigator.gpu.requestAdapter() : Promise.resolve(null)).then((adapter) => post("adapter", adapter ? "yes" : "no"), () => post("adapter", "no"));
    setTimeout(async () => {{
        const blob = await new Promise((resolve) => Module.canvas.toBlob(resolve, "image/png"));
        if (blob) await post("screen", blob);
        if (probe === "write") {{
            const token = "{token}";
            Module.FS.writeFile("{probePath}", token);
            await new Promise((resolve) => Module.FS.syncfs(false, (error) => {{ post("probe", error ? "E " + error : "written " + token).then(resolve); }}));
        }} else {{
            let text = "missing";
            try {{ text = "read " + Module.FS.readFile("{probePath}", {{ encoding: "utf8" }}); }} catch (error) {{ }}
            await post("probe", text);
        }}
        post("done", "");
    }}, {seconds} * 1000);
}})();
</script>
"""


#: 시나리오 확인 페이지의 보고 스크립트 — 앱 인자를 넘기고, 앱이 끝나면(종료 코드) 보고를 마친다.
SCENARIO_REPORTER_SCRIPT = """<script>
(() => {{
    const post = (kind, body) => fetch("{prefix}" + kind, {{ method: "POST", body: body }});
    const print = Module.print;
    const printErr = Module.printErr;
    Module.arguments = {arguments};
    Module.print = (text) => {{ print(text); post("log", "O " + text); }};
    Module.printErr = (text) => {{ printErr(text); post("log", "E " + text); }};
    window.addEventListener("error", (event) => post("log", "E window: " + event.message));
    window.addEventListener("unhandledrejection", (event) => post("log", "E rejection: " + event.reason));
    Module.onExit = (code) => post("exit", "exit " + code).then(() => post("done", ""));
    Module.onAbort = (what) => post("exit", "abort " + what).then(() => post("done", ""));
    post("gpu", navigator.gpu ? "yes" : "no");
    (navigator.gpu ? navigator.gpu.requestAdapter() : Promise.resolve(null)).then((adapter) => post("adapter", adapter ? "yes" : "no"), () => post("adapter", "no"));
}})();
</script>
"""


def make_scenario_arguments(scenario: str) -> list[str]:
    """시나리오 하나를 돌리는 앱 인자다. 웹의 사용자 폴더는 페이지의 가상 폴더(임시 프로필)라 자동화 폴더 밖이어도 사람의 상태를 건드리지 않는다."""
    return [app_arguments.option("scenario", scenario), app_arguments.flag("unattended"), app_arguments.variable("gv_automationAllowOutsideUserData", "true")]


def find_browser() -> str | None:
    for candidate in BROWSER_CANDIDATES:
        path = os.path.expandvars(candidate)
        if os.path.isfile(path):
            return path
    return None


@dataclass
class RunReport:
    """한 번의 브라우저 실행이 보낸 보고입니다."""

    list_line: list[str] = field(default_factory=list)
    gpu: str = ""
    adapter: str = ""
    exit: str = ""
    probe: str = ""
    screen: bytes = b""
    done: threading.Event = field(default_factory=threading.Event)


class CheckRequestHandler(IsolatedRequestHandler):
    """정적 파일 + 보고 끝점 — 보고는 지금 실행의 `RunReport` 에 쌓는다."""

    report: RunReport = RunReport()

    def do_POST(self) -> None:  # noqa: N802 — http.server 의 이름
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length)
        kind = self.path.removeprefix(REPORT_PREFIX) if self.path.startswith(REPORT_PREFIX) else ""
        report = CheckRequestHandler.report
        text = body.decode("utf-8", errors="replace")
        if kind == "log":
            report.list_line.append(text)
        elif kind == "gpu":
            report.gpu = text
        elif kind == "adapter":
            report.adapter = text
        elif kind == "exit":
            report.exit = text
        elif kind == "probe":
            report.probe = text
        elif kind == "screen":
            report.screen = body
        elif kind == "done":
            report.done.set()
        else:
            self.send_error(404)
            return
        self.send_response(204)
        self.end_headers()

    def log_message(self, format: str, *args: object) -> None:  # noqa: A002 — 요청마다의 접근 로그는 보고 줄을 가린다
        return


def write_check_page(folder: Path, game: str, token: str, seconds: int) -> None:
    reporter = REPORTER_SCRIPT.format(prefix=REPORT_PREFIX, token=token, probePath=PROBE_PATH, seconds=seconds)
    (folder / CHECK_PAGE_NAME).write_text(INDEX_HTML.format(title=f"{game} check", loader=PACK_LOADER_NAME, reporter=reporter), encoding="utf-8", newline="\n")


def write_scenario_page(folder: Path, game: str, scenario: str) -> None:
    reporter = SCENARIO_REPORTER_SCRIPT.format(prefix=REPORT_PREFIX, arguments=json.dumps(make_scenario_arguments(scenario)))
    (folder / CHECK_PAGE_NAME).write_text(INDEX_HTML.format(title=f"{game} scenario", loader=PACK_LOADER_NAME, reporter=reporter), encoding="utf-8", newline="\n")


def stop_process_tree(process: subprocess.Popen[bytes]) -> None:
    """띄운 브라우저와 그 자식(렌더러 · GPU 프로세스)을 PID 로 끝냅니다 — 자식이 남으면 다음 실행이 같은 프로필을 열지 못한다."""
    if is_windows_host():
        subprocess.run(["taskkill", "/T", "/F", "/PID", str(process.pid)], capture_output=True, check=False)
    else:
        process.terminate()
    try:
        process.wait(timeout=STOP_TIMEOUT_SECONDS)
    except subprocess.TimeoutExpired:
        process.kill()


def run_browser(browser: str, profile_dir: Path, url: str, headed: bool, timeout_seconds: int) -> RunReport:
    """브라우저 하나를 띄워 `done` 보고(또는 시한)까지 기다린 뒤 그 프로세스(PID)를 끝냅니다."""
    report = RunReport()
    CheckRequestHandler.report = report
    # 자동 재생 정책을 끈다 — 사용자 몸짓 없이도 AudioContext 가 돌아야 오디오 판정을 할 수 있다.
    command = [browser, f"--user-data-dir={profile_dir}", "--no-first-run", "--no-default-browser-check", "--enable-unsafe-webgpu", "--autoplay-policy=no-user-gesture-required"]
    if headed is False:
        command.append("--headless=new")
    process = subprocess.Popen([*command, url])
    try:
        report.done.wait(timeout_seconds)
    finally:
        stop_process_tree(process)
    return report


def evaluate_run(name: str, report: RunReport) -> list[str]:
    """한 실행의 실패 까닭 목록입니다(비면 통과)."""
    list_failure: list[str] = []
    if report.done.is_set() is False:
        list_failure.append(f"{name}: the page did not finish the check in time")
    if report.gpu != "yes":
        list_failure.append(f"{name}: navigator.gpu is missing ({report.gpu or 'no report'})")
    elif report.adapter != "yes":
        list_failure.append(f"{name}: navigator.gpu.requestAdapter() gave no adapter ({report.adapter or 'no report'})")
    if report.exit:
        list_failure.append(f"{name}: the app ended during the check ({report.exit})")
    if any(line.startswith(AUDIO_STARTED_LINE) for line in report.list_line) is False:
        list_failure.append(f"{name}: the audio worklet never pulled samples from the mixer ring ('{AUDIO_STARTED_LINE[2:]}' missing)")
    list_failure.extend(f"{name}: {line[2:]}" for line in report.list_line if line.startswith("E "))
    return list_failure


def evaluate_scenario(report: RunReport) -> list[str]:
    """시나리오 실행의 실패 까닭 목록입니다(비면 통과)."""
    list_failure: list[str] = []
    if report.done.is_set() is False:
        list_failure.append("scenario: the app did not end in time")
    if report.gpu != "yes" or report.adapter != "yes":
        list_failure.append(f"scenario: no WebGPU adapter (navigator.gpu {report.gpu or 'no report'}, adapter {report.adapter or 'no report'})")
    if report.exit != "exit 0":
        list_failure.append(f"scenario: the app ended with '{report.exit or 'no report'}' (Config/Engine/ProcessExitCodes.json)")
    list_failure.extend(f"scenario: {line[2:]}" for line in report.list_line if line.startswith("E "))
    return list_failure


def print_result(game: str, list_failure: list[str], out_dir: Path) -> int:
    for failure in list_failure:
        print(f"[web-check] {failure}")
    status = "FAILED" if list_failure else "PASSED"
    print(f"[web-check] {status} {game} — logs in {out_dir}")
    return ExitCode.VIOLATION if list_failure else ExitCode.PASSED


def run_until_adapter(browser: str, profile: Path, url: str, headed: bool, timeout_seconds: int) -> tuple[RunReport, bool]:
    """한 번 띄우고, 헤드리스에서 어댑터를 못 받는 기계면 창을 띄워 다시 돕니다. 실제로 쓴 방식(창을 띄웠나)도 돌려줍니다."""
    report = run_browser(browser, profile, url, headed, timeout_seconds)
    if headed is False and report.gpu == "yes" and report.adapter == "no":
        print("[web-check] headless WebGPU gave no adapter on this machine - running again with a browser window")
        headed = True
        time.sleep(1)  # 앞 브라우저가 프로필 잠금을 놓을 틈
        report = run_browser(browser, profile, url, headed, timeout_seconds)
    return report, headed


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="py -3 -m Scripts web-check", description="Web-Shipping 묶음을 브라우저에서 띄워 기동 · WebGPU · 화면 · IDBFS 영속을 본다")
    parser.add_argument("--game", required=True, help="게임 프리셋 이름(Config/Game/<게임>.json)")
    parser.add_argument("--skip-package", action="store_true", help="묶음을 다시 만들지 않는다(build/Web-Shipping/Web/<게임> 을 그대로 쓴다)")
    parser.add_argument("--seconds", type=int, default=DEFAULT_SECONDS, help="실행마다 앱을 돌리는 시간(초) — 그 뒤 화면 · 표지를 본다")
    parser.add_argument("--scenario", default="", help="시나리오 경로 — 개발 빌드(Web-Release)로 이 시나리오 하나를 돌려 종료 코드로 판정한다(입력)")
    parser.add_argument("--headed", action="store_true", help="처음부터 브라우저 창을 띄운다(헤드리스가 어댑터를 못 받으면 알아서 창으로 다시 돈다)")
    args = parser.parse_args(argv)
    project_root = get_project_root()
    build_dir = get_build_dir(SCENARIO_PRESET_NAME if args.scenario else PRESET_NAME, project_root).resolve()
    folder = build_dir / "Web" / args.game
    if args.skip_package is False:
        try:
            make_web_package(args.game, build_dir, build_dir / "Bin" / PACK_FOLDER_NAME, folder, project_root)
        except WebPackageError as error:
            print(f"[web-check] FAILED {error}")
            return ExitCode.VIOLATION
    elif (folder / "App.wasm").is_file() is False:
        print(f"[web-check] FAILED {folder} has no package - run without --skip-package")
        return ExitCode.CHECK_INVALID
    browser = find_browser()
    if browser is None:
        print("[web-check] FAILED Chrome or Edge not found (BROWSER_CANDIDATES)")
        return ExitCode.CHECK_INVALID

    token = secrets.token_hex(8)
    if args.scenario:
        write_scenario_page(folder, args.game, args.scenario)
    else:
        write_check_page(folder, args.game, token, args.seconds)
    out_dir = folder / "check"
    shutil.rmtree(out_dir, ignore_errors=True)
    out_dir.mkdir(parents=True)
    handler = functools.partial(CheckRequestHandler, directory=str(folder))
    server = http.server.ThreadingHTTPServer(("127.0.0.1", DEFAULT_PORT), handler)
    server_thread = threading.Thread(target=server.serve_forever, daemon=True)
    server_thread.start()
    list_failure: list[str] = []
    if args.scenario:
        try:
            with tempfile.TemporaryDirectory(prefix="swwebcheck-") as profile:
                report, _ = run_until_adapter(browser, Path(profile), f"http://localhost:{DEFAULT_PORT}/{CHECK_PAGE_NAME}", args.headed, SCENARIO_SECONDS)
        finally:
            server.shutdown()
            server.server_close()
        (out_dir / "scenario.log").write_text("\n".join(report.list_line) + "\n", encoding="utf-8", newline="\n")
        return print_result(args.game, evaluate_scenario(report), out_dir)
    try:
        with tempfile.TemporaryDirectory(prefix="swwebcheck-") as profile:
            timeout_seconds = args.seconds + LOAD_ALLOWANCE_SECONDS
            base_url = f"http://localhost:{DEFAULT_PORT}/{CHECK_PAGE_NAME}"
            first, headed = run_until_adapter(browser, Path(profile), f"{base_url}?probe=write", args.headed, timeout_seconds)
            time.sleep(1)  # 앞 브라우저가 프로필 잠금을 놓을 틈
            second = run_browser(browser, Path(profile), f"{base_url}?probe=read", headed, timeout_seconds)
    finally:
        server.shutdown()
        server.server_close()

    for name, report in (("run1", first), ("run2", second)):
        (out_dir / f"{name}.log").write_text("\n".join(report.list_line) + "\n", encoding="utf-8", newline="\n")
        list_failure.extend(evaluate_run(name, report))
    if first.screen:
        (out_dir / "screen.png").write_bytes(first.screen)
    else:
        list_failure.append("run1: no screen image (canvas.toBlob gave nothing)")
    if first.probe != f"written {token}":
        list_failure.append(f"run1: IDBFS write failed ({first.probe or 'no report'})")
    if second.probe != f"read {token}":
        list_failure.append(f"run2: IDBFS did not keep the marker across sessions ({second.probe or 'no report'})")

    return print_result(args.game, list_failure, out_dir)


if __name__ == "__main__":
    sys.exit(main())
