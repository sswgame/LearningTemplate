"""
두 프로세스 온라인 접속 시나리오 — 전용 서버와 클라이언트를 띄워 로그인 → 게임 접속 티켓 → 암호화 UDP 게임 접속을 끝까지 돌린다(OnlineKits F5).

  py -3 -m Scripts net-pair run --preset Ninja-Debug      # 서버 · 클라이언트를 띄워 접속이 열리는지 본다
  py -3 -m Scripts net-pair check <서버 로그> <클라이언트 로그>   # 이미 있는 두 로그만 판정한다

준비(`run` 이 `Saved/NetPairRun/` 아래에 쓴다): 서버 설정(`NetPair.json` — 게임 포트 · 서비스 포트 · `_bGameConnectTicket`)과 옆 온라인 설정(`NetPair.online.json` —
계정 키트의 개발용 시드 계정 옵션 `seedTestAccounts`, 게임 접속 조립 `gameConnect`). 서버는 `-server-config=<경로>` 로 그 설정을 읽는다.
계정 시드는 서버 명령줄 인자가 아니라 온라인 설정의 옵션이다(설정이 정본 — 인자를 하나 더 두면 같은 값이 둘이 된다). Shipping 은 그 옵션을 거절하므로 시드 시설은 Dev 에만 있다.

판정: 서버 로그에 `Net session client <n> connected`(암호화 UDP 접속이 열림)가 있고, 두 로그에 `[Error]` · `[Warning]` 가 없고(종료 코드 0 이어도 경고는 실패), 서버가 종료 코드 0 으로 끝난다.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

from common.ExitCode import ExitCode
from common.Paths import getProjectRoot

_kGame = "Empty"
_kServerName = "netpair"
_kAccount = "netpair_bot"
_kPassword = "netpair_pw_1"
_kTicketKeyEnvironment = "SW_NETPAIR_TICKET_KEY"
_kTicketKey = "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff"  # 시나리오 전용 임시 키(32 B) — 개발용 키 경고를 피한다
_kConnected = re.compile(r"Net session client \d+ connected")
_kListening = re.compile(r"Net session listening on UDP port")
_kProblemLine = re.compile(r"\[(Error|Warning)\]")


@dataclass
class CheckResult:
    problems: list[str] = field(default_factory=list)

    @property
    def bOk(self) -> bool:
        return not self.problems


def checkLogs(serverText: str, clientText: str) -> CheckResult:
    """서버 · 클라이언트 로그를 판정한다. 규칙은 파일 머리말을 본다."""
    result = CheckResult()
    if _kConnected.search(serverText) is None:
        result.problems.append("the server log has no 'Net session client <n> connected' line - the ticket connection never opened")
    for name, text in (("server", serverText), ("client", clientText)):
        for line in text.splitlines():
            if _kProblemLine.search(line):
                result.problems.append(f"{name} log: {line.strip()}")
    return result


def writeConfigs(outDir: Path, gamePort: int, servicePort: int) -> Path:
    """서버 설정과 온라인 설정을 쓴다. 서버 설정 경로를 돌려준다."""
    serverConfig = {
        "_listenAddress": "127.0.0.1",
        "_gamePort": gamePort,
        "_servicePort": servicePort,
        "_bGameConnectTicket": True,
        "_bConsoleInput": True,  # 표준 입력 `quit` 로 드레인 없이 정상 종료한다
    }
    onlineConfig = {
        "_listKit": [
            {"_name": "account", "_listOption": [
                {"_key": "ticketKeySecretEnvironment", "_value": _kTicketKeyEnvironment},
                {"_key": "lightPasswordHash", "_value": "1"},
                {"_key": "seedTestAccounts", "_value": f"{_kAccount}:{_kPassword}"},
            ]},
            {"_name": "gameConnect", "_listOption": [
                {"_key": "ticketKeySecretEnvironment", "_value": _kTicketKeyEnvironment},
                {"_key": "serverName", "_value": _kServerName},
            ]},
        ],
    }
    serverPath = outDir / "NetPair.json"
    serverPath.write_text(json.dumps(serverConfig, indent=4), encoding="utf-8")
    (outDir / "NetPair.online.json").write_text(json.dumps(onlineConfig, indent=4), encoding="utf-8")
    return serverPath


def _waitForText(logPath: Path, pattern: re.Pattern[str], timeout: float, process: subprocess.Popen) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if pattern.search(logPath.read_text(encoding="utf-8", errors="replace")):
            return True
        if process.poll() is not None:
            return False
        time.sleep(0.25)
    return False


def _stop(process: subprocess.Popen, quitLine: bool, timeout: float) -> None:
    if process.poll() is not None:
        return
    try:
        if quitLine and process.stdin is not None:
            process.stdin.write(b"quit\n")
            process.stdin.flush()
        process.wait(timeout=timeout)
    except (subprocess.TimeoutExpired, OSError):
        process.kill()


def _run(argument: argparse.Namespace) -> int:
    root = getProjectRoot()
    binDir = root / "build" / argument.preset / "Bin"
    serverExe = binDir / ("Server.exe" if sys.platform == "win32" else "Server")
    clientExe = binDir / ("App.exe" if sys.platform == "win32" else "App")
    for exe in (serverExe, clientExe):
        if not exe.exists():
            print(f"[net-pair] FAIL: {exe} does not exist - build the '{argument.preset}' preset first")
            return int(ExitCode.CheckInvalid)
    outDir = Path(argument.out) if argument.out else root / "Saved" / "NetPairRun"
    outDir.mkdir(parents=True, exist_ok=True)
    serverConfig = writeConfigs(outDir, argument.game_port, argument.service_port)
    serverLog = outDir / "server.log"
    clientLog = outDir / "client.log"
    serverArgs = [str(serverExe), f"-game={_kGame}", f"-server-config={serverConfig}"]
    clientArgs = [str(clientExe), f"-game={_kGame}", "-gv_standardInputCommands=1"]
    ticketCommand = (f"net.ticketconnect 127.0.0.1:{argument.service_port} {_kAccount} {_kPassword} {_kServerName} "
                     f"127.0.0.1:{argument.game_port}\n")
    print(f"[net-pair] server: {' '.join(serverArgs)}")
    print(f"[net-pair] client: {' '.join(clientArgs)}")
    with serverLog.open("wb") as serverOut, clientLog.open("wb") as clientOut:
        serverEnvironment = {**os.environ, _kTicketKeyEnvironment: _kTicketKey}
        server = subprocess.Popen(serverArgs, cwd=str(binDir), env=serverEnvironment, stdin=subprocess.PIPE, stdout=serverOut, stderr=subprocess.STDOUT)
        if not _waitForText(serverLog, _kListening, argument.timeout, server):
            print(f"[net-pair] FAIL: the server never opened its game port (log: {serverLog})")
            _stop(server, False, 5)
            return int(ExitCode.Violation)
        client = subprocess.Popen(clientArgs, cwd=str(binDir), stdin=subprocess.PIPE, stdout=clientOut, stderr=subprocess.STDOUT)
        time.sleep(argument.client_delay)  # 클라이언트가 게임 인스턴스를 세우고 표준 입력 명령을 받을 때까지
        try:
            client.stdin.write(ticketCommand.encode("utf-8"))
            client.stdin.flush()
        except OSError:
            print(f"[net-pair] FAIL: the client exited before it took the command (log: {clientLog})")
            _stop(server, True, 10)
            return int(ExitCode.Violation)
        bConnected = _waitForText(serverLog, _kConnected, argument.timeout, server)
        if bConnected:
            time.sleep(1.0)  # 접속 뒤 잠깐 두어 늦게 나는 경고도 잡는다
        _stop(client, True, 10)
        _stop(server, True, 20)
    if server.returncode != 0:
        print(f"[net-pair] FAIL: the server exited with code {server.returncode} (log: {serverLog})")
        return int(ExitCode.Violation)
    serverText = serverLog.read_text(encoding="utf-8", errors="replace")
    clientText = clientLog.read_text(encoding="utf-8", errors="replace")
    return _print(checkLogs(serverText, clientText), serverLog, clientLog)


def _print(result: CheckResult, serverLog: Path, clientLog: Path) -> int:
    for problem in result.problems:
        print(f"[net-pair] FAIL: {problem}")
    print(f"[net-pair] logs: {serverLog} {clientLog}")
    if result.bOk:
        print("[net-pair] OK: login -> game ticket -> encrypted UDP connection opened with no errors or warnings")
        return int(ExitCode.Passed)
    return int(ExitCode.Violation)


def main(listArgument: list[str]) -> int:
    parser = argparse.ArgumentParser(prog="py -3 -m Scripts net-pair", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="mode", required=True)
    runParser = sub.add_parser("run", help="서버와 클라이언트를 띄워 로그인 → 티켓 → 게임 접속을 돈다")
    runParser.add_argument("--preset", default="Ninja-Debug", help="빌드 프리셋(build/<프리셋>/Bin 의 Server · App 을 쓴다)")
    runParser.add_argument("--game-port", type=int, default=17777, help="서버 게임(UDP) 포트")
    runParser.add_argument("--service-port", type=int, default=17780, help="서버 서비스(TCP · 로그인) 포트")
    runParser.add_argument("--client-delay", type=float, default=5.0, help="클라이언트를 띄운 뒤 명령을 보낼 때까지 기다리는 초")
    runParser.add_argument("--timeout", type=float, default=60.0, help="서버 기동 · 접속을 기다리는 최대 초")
    runParser.add_argument("--out", default="", help="설정 · 로그를 둘 폴더(기본 Saved/NetPairRun)")
    checkParser = sub.add_parser("check", help="이미 있는 서버 · 클라이언트 로그를 판정한다")
    checkParser.add_argument("serverLog")
    checkParser.add_argument("clientLog")
    argument = parser.parse_args(listArgument)
    if argument.mode == "check":
        serverLog, clientLog = Path(argument.serverLog), Path(argument.clientLog)
        serverText = serverLog.read_text(encoding="utf-8", errors="replace")
        clientText = clientLog.read_text(encoding="utf-8", errors="replace")
        return _print(checkLogs(serverText, clientText), serverLog, clientLog)
    return _run(argument)
