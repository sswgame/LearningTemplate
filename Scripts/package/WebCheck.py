#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file WebCheck.py
@brief Web-Shipping 묶음을 브라우저에서 띄워 기동 · WebGPU · 오류 없음 · 화면 · IDBFS 영속을 한 번에 본다 — 손 확인을 쌓지 않는 웹 확인 절차.

| 단계 | 하는 일 |
| --- | --- |
| package | `web-package` 와 같은 묶음 + 확인 페이지 `check.html`(배포 페이지 `index.html` 에 보고 스크립트만 더한 것) |
| serve | 교차 출처 격리 헤더를 주는 로컬 서버(`IsolatedRequestHandler`) + 보고 받는 끝점 `POST /__swcheck/<종류>` |
| run 1 | 브라우저(Chrome · Edge — `browser-mcp` 와 같은 후보, 임시 프로필)로 `check.html?probe=write` — `--seconds` 동안 돌린 뒤 화면을 PNG 로 받고 IDBFS 에 표지 파일을 쓴다 |
| run 2 | 같은 프로필로 다시 띄워 `check.html?probe=read` — 앱이 IDBFS 를 붙인 뒤 표지를 읽어 run 1 의 값과 대조한다(새 세션에 영속 폴더가 살아 있다) |

판정(모두 지나야 통과): `navigator.gpu` 가 있다, 오류 줄(printErr · 창 오류 · 처리 안 된 거부)이 없다, 앱이 확인 시간 안에 끝나거나 멈추지 않았다, 화면 PNG 를 받았다,
오디오 워클릿이 링에서 샘플을 꺼내기 시작했다(`kAudioStartedLine` — 브라우저는 자동 재생 정책을 끄고 띄운다), run 2 가 run 1 의 표지를 읽었다. 로그 · 화면은 `<묶음>/check/` 에 남는다(`run1.log` · `run2.log` · `screen.png`).
보지 않는 것: 입력 — 합성 이벤트를 넣어도 앱의 반응을 페이지에서 읽을 길이 없다(배포 빌드에는 시나리오 실행기가 없다). 입력 판정은 쿠킹한 웹 시나리오가 생길 때 더한다.
shortcut: 브라우저는 헤드리스(--headless=new --enable-unsafe-webgpu)로 띄운다, 헤드리스 WebGPU 가 그 기계에서 어댑터를 못 받으면 `--headed` 로 창을 띄운다.

사용법:
    py -3 -m Scripts web-check --game Empty
    py -3 -m Scripts web-check --game Empty --skip-package --headed --seconds 30
"""

from __future__ import annotations

import argparse
import functools
import http.server
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
from common.ExitCode import ExitCode  # noqa: E402
from common.HostPlatform import isWindowsHost  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402
from package.Pack import kPackFolderName  # noqa: E402
from package.WebPackage import IsolatedRequestHandler, WebPackageError, kIndexHtml, kPackLoaderName, kPresetName, makeWebPackage  # noqa: E402
from setup.BrowserMcp import findBrowser  # noqa: E402

kCheckPageName = "check.html"
kReportPrefix = "/__swcheck/"
kDefaultPort = 8090
kDefaultSeconds = 20
#: 표지 파일 — AppEntry.cpp 의 kWebPersistentRoot(IDBFS 마운트) 아래다. 두 곳이 같은 자리를 쓴다.
kProbePath = "/persistent/.swwebcheck"
#: 브라우저가 페이지를 열고 보고를 마칠 때까지의 여유(확인 시간 위에 더한다) — 묶음 내려받기 · wasm 컴파일 · IDBFS 읽기.
kLoadAllowanceSeconds = 60
#: 웹 오디오 백엔드(Engine/Audio/Web/WebAudioBackend.js)가 워클릿이 처음 샘플을 꺼냈을 때 Module.print 로 내는 줄의 앞머리 — 믹서 스레드 → 링 → 워클릿이 흐른다는 판정이다.
kAudioStartedLine = "O [WebAudio] output started"
kStopTimeoutSeconds = 10
#: 확인 페이지의 보고 스크립트. `probe` 쿼리가 run 1(write) · run 2(read)를 가른다. Module.FS 는 Web.cmake 의 EXPORTED_RUNTIME_METHODS 가 내보낸다.
kReporterScript = """<script>
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


@dataclass
class RunReport:
    """한 번의 브라우저 실행이 보낸 보고입니다."""

    listLine: list[str] = field(default_factory=list)
    gpu: str = ""
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
        kind = self.path.removeprefix(kReportPrefix) if self.path.startswith(kReportPrefix) else ""
        report = CheckRequestHandler.report
        text = body.decode("utf-8", errors="replace")
        if kind == "log":
            report.listLine.append(text)
        elif kind == "gpu":
            report.gpu = text
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


def writeCheckPage(folder: Path, game: str, token: str, seconds: int) -> None:
    reporter = kReporterScript.format(prefix=kReportPrefix, token=token, probePath=kProbePath, seconds=seconds)
    (folder / kCheckPageName).write_text(kIndexHtml.format(title=f"{game} check", loader=kPackLoaderName, reporter=reporter), encoding="utf-8", newline="\n")


def stopProcessTree(process: subprocess.Popen[bytes]) -> None:
    """띄운 브라우저와 그 자식(렌더러 · GPU 프로세스)을 PID 로 끝냅니다 — 자식이 남으면 다음 실행이 같은 프로필을 열지 못한다."""
    if isWindowsHost():
        subprocess.run(["taskkill", "/T", "/F", "/PID", str(process.pid)], capture_output=True, check=False)
    else:
        process.terminate()
    try:
        process.wait(timeout=kStopTimeoutSeconds)
    except subprocess.TimeoutExpired:
        process.kill()


def runBrowser(browser: str, profileDir: Path, url: str, bHeaded: bool, timeoutSeconds: int) -> RunReport:
    """브라우저 하나를 띄워 `done` 보고(또는 시한)까지 기다린 뒤 그 프로세스(PID)를 끝냅니다."""
    report = RunReport()
    CheckRequestHandler.report = report
    # 자동 재생 정책을 끈다 — 사용자 몸짓 없이도 AudioContext 가 돌아야 오디오 판정을 할 수 있다.
    command = [browser, f"--user-data-dir={profileDir}", "--no-first-run", "--no-default-browser-check", "--enable-unsafe-webgpu", "--autoplay-policy=no-user-gesture-required"]
    if bHeaded is False:
        command.append("--headless=new")
    process = subprocess.Popen([*command, url])
    try:
        report.done.wait(timeoutSeconds)
    finally:
        stopProcessTree(process)
    return report


def judgeRun(name: str, report: RunReport) -> list[str]:
    """한 실행의 실패 까닭 목록입니다(비면 통과)."""
    listFailure: list[str] = []
    if report.done.is_set() is False:
        listFailure.append(f"{name}: the page did not finish the check in time")
    if report.gpu != "yes":
        listFailure.append(f"{name}: navigator.gpu is missing ({report.gpu or 'no report'})")
    if report.exit:
        listFailure.append(f"{name}: the app ended during the check ({report.exit})")
    if any(line.startswith(kAudioStartedLine) for line in report.listLine) is False:
        listFailure.append(f"{name}: the audio worklet never pulled samples from the mixer ring ('{kAudioStartedLine[2:]}' missing)")
    listFailure.extend(f"{name}: {line[2:]}" for line in report.listLine if line.startswith("E "))
    return listFailure


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="py -3 -m Scripts web-check", description="Web-Shipping 묶음을 브라우저에서 띄워 기동 · WebGPU · 화면 · IDBFS 영속을 본다")
    parser.add_argument("--game", required=True, help="게임 프리셋 이름(Config/Game/<게임>.json)")
    parser.add_argument("--build-dir", default=f"build/{kPresetName}", help="Web-Shipping 빌드 폴더(저장소 기준)")
    parser.add_argument("--skip-package", action="store_true", help="묶음을 다시 만들지 않는다(<build-dir>/Web/<게임> 을 그대로 쓴다)")
    parser.add_argument("--seconds", type=int, default=kDefaultSeconds, help="실행마다 앱을 돌리는 시간(초) — 그 뒤 화면 · 표지를 본다")
    parser.add_argument("--port", type=int, default=kDefaultPort, help="로컬 서버 포트")
    parser.add_argument("--headed", action="store_true", help="브라우저 창을 띄운다(헤드리스 WebGPU 가 어댑터를 못 받는 기계)")
    args = parser.parse_args(argv)
    projectRoot = getProjectRoot()
    buildDir = (projectRoot / args.build_dir).resolve()
    folder = buildDir / "Web" / args.game
    if args.skip_package is False:
        try:
            makeWebPackage(args.game, buildDir, buildDir / "Bin" / kPackFolderName, folder, projectRoot)
        except WebPackageError as error:
            print(f"[web-check] FAILED {error}")
            return ExitCode.Violation
    elif (folder / "App.wasm").is_file() is False:
        print(f"[web-check] FAILED {folder} has no package - run without --skip-package")
        return ExitCode.CheckInvalid
    browser = findBrowser()
    if browser is None:
        print("[web-check] FAILED Chrome or Edge not found (the same candidates as browser-mcp)")
        return ExitCode.CheckInvalid

    token = secrets.token_hex(8)
    writeCheckPage(folder, args.game, token, args.seconds)
    outDir = folder / "check"
    shutil.rmtree(outDir, ignore_errors=True)
    outDir.mkdir(parents=True)
    handler = functools.partial(CheckRequestHandler, directory=str(folder))
    server = http.server.ThreadingHTTPServer(("127.0.0.1", args.port), handler)
    serverThread = threading.Thread(target=server.serve_forever, daemon=True)
    serverThread.start()
    listFailure: list[str] = []
    try:
        with tempfile.TemporaryDirectory(prefix="swwebcheck-") as profile:
            timeoutSeconds = args.seconds + kLoadAllowanceSeconds
            baseUrl = f"http://localhost:{args.port}/{kCheckPageName}"
            first = runBrowser(browser, Path(profile), f"{baseUrl}?probe=write", args.headed, timeoutSeconds)
            time.sleep(1)  # 앞 브라우저가 프로필 잠금을 놓을 틈
            second = runBrowser(browser, Path(profile), f"{baseUrl}?probe=read", args.headed, timeoutSeconds)
    finally:
        server.shutdown()
        server.server_close()

    for name, report in (("run1", first), ("run2", second)):
        (outDir / f"{name}.log").write_text("\n".join(report.listLine) + "\n", encoding="utf-8", newline="\n")
        listFailure.extend(judgeRun(name, report))
    if first.screen:
        (outDir / "screen.png").write_bytes(first.screen)
    else:
        listFailure.append("run1: no screen image (canvas.toBlob gave nothing)")
    if first.probe != f"written {token}":
        listFailure.append(f"run1: IDBFS write failed ({first.probe or 'no report'})")
    if second.probe != f"read {token}":
        listFailure.append(f"run2: IDBFS did not keep the marker across sessions ({second.probe or 'no report'})")

    for failure in listFailure:
        print(f"[web-check] {failure}")
    status = "FAILED" if listFailure else "PASSED"
    print(f"[web-check] {status} {args.game} — logs and screen in {outDir}")
    return ExitCode.Violation if listFailure else ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
