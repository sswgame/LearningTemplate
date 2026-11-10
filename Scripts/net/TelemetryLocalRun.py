"""
텔레메트리 · 크래시 업로드 로컬 실행 — 루프백 HTTP 수신기를 띄우고 게임을 두 번 띄워, 동의한 텔레메트리 스풀 파일과 보낼 줄의 크래시 묶음이 실제 HTTP 로 오는지 본다(H1).

  py -3 -m Scripts telemetry-local run                 # 기본: Shooter3D, Ninja-Debug
  py -3 -m Scripts telemetry-local run --game WildWest

끝점은 gamesettings 칸(`_telemetryUploadEndpoint` · `_crashUploadEndpoint`)이고 저장소의 팩은 적지 않는다 - 이 스크립트는 `-gv_telemetryUploadEndpoint` ·
`-gv_crashUploadEndpoint` 로 수신기를 가리킨다. 사용자 설정은 `-gv_userSettingsFile` 로 출력 폴더에 따로 두어 사람의 동의 설정을 바꾸지 않는다.
1 회: 동의를 켜고(`settings.apply telemetry.enabled=true`) 끝낸다. 종료가 업로더를 떼며 스풀 파일을 닫아 보낸다(`TelemetryService::releaseUploader`).
     로그의 크래시 보고 줄에서 묶음 폴더를 읽는다.
2 회: 그 폴더에 보낼 줄(queued)의 가짜 묶음을 하나 두고 띄운다. 기동이 전용 스레드로 묶음을 올린다(`TelemetryUploadLink`).
판정: 수신기가 `/telemetry` 로 문맥 줄과 `session.start` 를 담은 ndjson 을, `/crash` 로 그 묶음 세션의 multipart 를 받았고, 두 로그에 허용 밖 `[Error]` · `[Warning]` 이 없다.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import threading
import time
from dataclasses import dataclass, field
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from common import AppArguments
from common.ExitCode import ExitCode
from common.HostPlatform import executableName
from common.Paths import getProjectRoot
from net.NetPairRun import _kUserDataRootEnvironment, _stop, _waitForText

_kTelemetryLinked = re.compile(r"\[TelemetryUpload\] telemetry uploads go to")
_kCrashSent = re.compile(r"\[TelemetryUpload\] crash reports: (\d+) sent from '([^']+)'")
_kProblemLine = re.compile(r"\[(Error|Warning)\]")
_kFakeSession = "telemetrylocalrun"


@dataclass
class ReceivedRequest:
    path: str
    headers: dict[str, str]
    body: bytes


@dataclass
class Sink:
    """루프백 수신기가 받은 요청입니다. 수신 스레드가 붙이고 판정이 읽는다."""

    listRequest: list[ReceivedRequest] = field(default_factory=list)
    lock: threading.Lock = field(default_factory=threading.Lock)

    def add(self, request: ReceivedRequest) -> None:
        with self.lock:
            self.listRequest.append(request)

    def find(self, path: str) -> list[ReceivedRequest]:
        with self.lock:
            return [request for request in self.listRequest if request.path == path]


def _makeHandler(sink: Sink) -> type[BaseHTTPRequestHandler]:
    class Handler(BaseHTTPRequestHandler):
        def do_POST(self) -> None:  # noqa: N802 - http.server 의 이름이다
            length = int(self.headers.get("Content-Length", "0"))
            sink.add(ReceivedRequest(self.path, {key: value for key, value in self.headers.items()}, self.rfile.read(length)))
            self.send_response(200)
            self.send_header("Content-Length", "0")
            self.end_headers()

        def log_message(self, format: str, *args: object) -> None:  # noqa: A002 - 기반 서명이다
            return

    return Handler


@dataclass
class CheckResult:
    problems: list[str] = field(default_factory=list)

    @property
    def bOk(self) -> bool:
        return not self.problems


def checkRun(sink: Sink, listLogText: list[str]) -> CheckResult:
    """받은 요청과 두 로그를 판정한다. 규칙은 파일 머리말을 본다."""
    result = CheckResult()
    listTelemetry = sink.find("/telemetry")
    if not listTelemetry:
        result.problems.append("the sink got no telemetry POST - the uploader was not linked or the spool file was not closed at shutdown")
    elif not any(b"session.start" in request.body for request in listTelemetry):
        result.problems.append("no telemetry POST carried session.start - consent did not reach the telemetry service")
    listCrash = sink.find("/crash")
    if not any(_kFakeSession.encode("utf-8") in request.body for request in listCrash):
        result.problems.append(f"the sink got no crash POST for the queued bundle '{_kFakeSession}'")
    for index, text in enumerate(listLogText):
        if _kTelemetryLinked.search(text) is None:
            result.problems.append(f"run {index + 1}: no telemetry link line - TelemetryUploadLink did not take the endpoint")
        for line in text.splitlines():
            if _kProblemLine.search(line):
                result.problems.append(f"run {index + 1} log: {line.strip()}")
    return result


def _send(process: subprocess.Popen, line: str) -> None:
    process.stdin.write((line + "\n").encode("utf-8"))
    process.stdin.flush()


def _writeFakeBundle(reportsFolder: Path) -> Path:
    """보낼 줄(queued)의 묶음 하나를 둔다. 매니페스트 모양은 `CrashReportService::createBundle` 과 같다(파일은 글 하나)."""
    bundle = reportsFolder / f"crash_{_kFakeSession}"
    bundle.mkdir(parents=True, exist_ok=True)
    (bundle / "crash.txt").write_text("reason : TelemetryLocalRun fake crash\n", encoding="utf-8")
    manifest = {
        "schema": 1,
        "session": _kFakeSession,
        "crashTime": 0,
        "reason": "TelemetryLocalRun fake crash",
        "files": [{"name": "crash.txt", "bytes": (bundle / "crash.txt").stat().st_size}],
        "breadcrumbCount": 0,
        "attempts": 0,
        "state": "queued",
    }
    (bundle / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    return bundle


def _runOnce(argument: argparse.Namespace, clientExe: Path, outDir: Path, port: int, index: int, bConsent: bool, environment: dict[str, str]) -> Path:
    log = outDir / f"run{index}.log"
    clientArgs = [
        str(clientExe),
        AppArguments.option("game", argument.game),
        AppArguments.flag("unattended"),
        AppArguments.variable("gv_standardInputCommands", "1"),
        AppArguments.variable("gv_userSettingsFile", outDir / 'usersettings.json'),
        AppArguments.variable("gv_telemetryUploadEndpoint", f"http://127.0.0.1:{port}/telemetry"),
        AppArguments.variable("gv_crashUploadEndpoint", f"http://127.0.0.1:{port}/crash"),
    ]
    print(f"[telemetry-local] run {index}: {' '.join(clientArgs)}")
    with log.open("wb") as clientOut:
        client = subprocess.Popen(clientArgs, cwd=str(clientExe.parent), stdin=subprocess.PIPE, stdout=clientOut, stderr=subprocess.STDOUT, env=environment)
        _waitForText(log, _kCrashSent, argument.timeout, client)
        time.sleep(argument.client_delay)  # 게임 인스턴스가 서고 표준 입력 명령을 받을 때까지
        try:
            if bConsent:
                _send(client, "settings.apply telemetry.enabled=true")
                time.sleep(2.0)  # 동의가 session.start 를 남길 때까지
        except OSError:
            print(f"[telemetry-local] FAIL: run {index} exited before it took the commands (log: {log})")
        _stop(client, True, 20)
    return log


def _run(argument: argparse.Namespace) -> int:
    root = getProjectRoot()
    clientExe = root / "build" / argument.preset / "Bin" / executableName("App")
    if not clientExe.exists():
        print(f"[telemetry-local] FAIL: {clientExe} does not exist - build the '{argument.preset}' preset first")
        return int(ExitCode.CheckInvalid)
    outDir = Path(argument.out) if argument.out else root / "Saved" / "TelemetryLocalRun"
    if outDir.exists():
        shutil.rmtree(outDir)
    outDir.mkdir(parents=True)
    # 무인 실행(-unattended)의 사용자 데이터 뿌리 — 두 실행이 같은 크래시 묶음 폴더를 봐야 하므로 프로세스마다 새 폴더(기본)가 아니라 하나를 함께 쓴다.
    # 뿌리는 `<Bin>/Saved/Automation/UserData/` 아래여야 한다(App 이 밖이면 거절한다 `AutomationUserDataRoot`, 순서 시나리오의 `RunStateDirPair.cmake` 와 같다).
    userDataRoot = clientExe.parent / "Saved" / "Automation" / "UserData" / "telemetry-local"
    if userDataRoot.exists():
        shutil.rmtree(userDataRoot)
    userDataRoot.mkdir(parents=True)
    environment = {**os.environ, _kUserDataRootEnvironment: str(userDataRoot)}
    sink = Sink()
    server = ThreadingHTTPServer(("127.0.0.1", 0), _makeHandler(sink))
    serverThread = threading.Thread(target=server.serve_forever, daemon=True)
    serverThread.start()
    port = server.server_address[1]
    listLog: list[Path] = []
    bundle: Path | None = None
    try:
        listLog.append(_runOnce(argument, clientExe, outDir, port, 1, True, environment))
        match = _kCrashSent.search(listLog[0].read_text(encoding="utf-8", errors="replace"))
        if match is None:
            print(f"[telemetry-local] FAIL: run 1 logged no crash report line - the crash uploader was not linked (log: {listLog[0]})")
            return int(ExitCode.Violation)
        bundle = _writeFakeBundle(Path(match.group(2)))
        listLog.append(_runOnce(argument, clientExe, outDir, port, 2, False, environment))
    finally:
        server.shutdown()
        if bundle is not None:
            shutil.rmtree(bundle, ignore_errors=True)
    result = checkRun(sink, [log.read_text(encoding="utf-8", errors="replace") for log in listLog])
    for problem in result.problems:
        print(f"[telemetry-local] FAIL: {problem}")
    print(f"[telemetry-local] logs: {' '.join(str(log) for log in listLog)}")
    if result.bOk:
        print("[telemetry-local] OK: telemetry spool file and queued crash bundle reached the loopback sink over HTTP")
        return int(ExitCode.Passed)
    return int(ExitCode.Violation)


def main(listArgument: list[str]) -> int:
    parser = argparse.ArgumentParser(prog="py -3 -m Scripts telemetry-local", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="mode", required=True)
    runParser = sub.add_parser("run", help="수신기를 띄우고 게임을 두 번 띄워 텔레메트리 · 크래시 업로드를 본다")
    runParser.add_argument("--preset", default="Ninja-Debug", help="빌드 프리셋(build/<프리셋>/Bin 의 App 을 쓴다)")
    runParser.add_argument("--game", default="Shooter3D", help="게임 이름")
    runParser.add_argument("--client-delay", type=float, default=8.0, help="기동 줄을 본 뒤 명령을 보낼 때까지 기다리는 초")
    runParser.add_argument("--timeout", type=float, default=60.0, help="기동 줄을 기다리는 최대 초")
    runParser.add_argument("--out", default="", help="로그 · 사용자 설정을 둘 폴더(기본 Saved/TelemetryLocalRun, 매번 비운다)")
    return _run(parser.parse_args(listArgument))
