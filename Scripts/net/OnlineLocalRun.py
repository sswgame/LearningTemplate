"""
온라인 로컬 실행 — 전용 서버와 게임 클라이언트를 두 프로세스로 띄워 TLS 서비스 연결 · 로그인 · 우편함 호출 · 매칭 배정 · 순위표 제출과 조회를 끝까지 돌린다(온라인 조사 단계 1, K4).

  py -3 -m Scripts online-local run                                   # 기본: Shooter3D, 메모리 저장소
  py -3 -m Scripts online-local run --server-config <서버 설정 경로>   # 저장소 · 캐시 · 키트 · 제공자를 그 설정이 고른다(sqlite · postgres · resp 도 같은 길)
  py -3 -m Scripts online-local check <서버 로그> <클라이언트 로그>     # 이미 있는 두 로그만 판정한다

설정은 코드가 만들지 않고 파일을 그대로 쓴다(`Config/Server/Online/<게임>.json` 과 옆 `.online.json`). 저장 키트(`GF_Server_SQLStore` · `GF_Server_CacheStore`)는 Shooter3D 매니페스트가 켠다(`_listModuleOverride`).
`Config/Server/Online/Shooter3D.external.json` 이 Postgres · RESP · 영수증 · 푸시를 모두 고른 견본이다 - 끝점과 환경 변수의 값은 운영자가 준다. 없으면 서버가 까닭을 남기고 기동하지 못한다.
매칭은 혼자 서는 모드(`arena_solo`)라 줄을 서면 바로 이 서버(`_publicAddress` 로 디렉터리에 등록한 게임 서버)로 배정된다. 순위표는 `arena_kills` 에 처치 수를 내고 상위 목록을 받는다.
판정: 클라이언트 로그에 로그인 · 우편 목록 · 매칭 배정 · 점수 제출 · 상위 목록(1 항목) 줄이 있고, 두 로그에 허용 밖 `[Error]` · `[Warning]` 가 없고, 서버가 종료 코드 0 으로 끝난다.
허용 줄: 개발 티켓 키 경고(기본 설정은 게임 접속 티켓을 쓰지 않아 주 키를 두지 않는다).
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

from common.ExitCode import ExitCode
from common.HostPlatform import executableName
from common.Paths import getProjectRoot
from net.NetPairRun import _stop, _waitForText

_kDefaultConfig = "Config/Server/Online/Shooter3D.json"
_kListening = re.compile(r"Online services listening on port")
_kLoggedIn = re.compile(r"online login done")
_kMailList = re.compile(r"online mail list: \d+ mails")
_kMatchFound = re.compile(r"online match found")
_kScoreSubmitted = re.compile(r"online score submitted")
_kTopScores = re.compile(r"online top scores: 1 entries")
_kProblemLine = re.compile(r"\[(Error|Warning)\]")
_kAllowedProblem = re.compile(r"no ticketKeySecretEnvironment")


@dataclass
class CheckResult:
    problems: list[str] = field(default_factory=list)

    @property
    def bOk(self) -> bool:
        return not self.problems


def checkLogs(serverText: str, clientText: str) -> CheckResult:
    """서버 · 클라이언트 로그를 판정한다. 규칙은 파일 머리말을 본다."""
    result = CheckResult()
    if _kLoggedIn.search(clientText) is None:
        result.problems.append("the client log has no login line - the account kit call never finished")
    if _kMailList.search(clientText) is None:
        result.problems.append("the client log has no mail list line - the mailbox kit call never finished")
    if _kMatchFound.search(clientText) is None:
        result.problems.append("the client log has no match found line - the matchmaking kit did not assign the solo arena match")
    if _kScoreSubmitted.search(clientText) is None:
        result.problems.append("the client log has no score submitted line - the leaderboard kit did not take the score")
    if _kTopScores.search(clientText) is None:
        result.problems.append("the client log has no top scores line with one entry - the leaderboard kit did not return the submitted score")
    for name, text in (("server", serverText), ("client", clientText)):
        for line in text.splitlines():
            if _kProblemLine.search(line) and _kAllowedProblem.search(line) is None:
                result.problems.append(f"{name} log: {line.strip()}")
    return result


def _send(process: subprocess.Popen, line: str) -> None:
    process.stdin.write((line + "\n").encode("utf-8"))
    process.stdin.flush()


def _run(argument: argparse.Namespace) -> int:
    root = getProjectRoot()
    binDir = root / "build" / argument.preset / "Bin"
    serverExe = binDir / executableName("Server")
    clientExe = binDir / executableName("App")
    for exe in (serverExe, clientExe):
        if not exe.exists():
            print(f"[online-local] FAIL: {exe} does not exist - build the '{argument.preset}' preset first")
            return int(ExitCode.CheckInvalid)
    serverConfig = (root / argument.server_config).resolve()
    servicePort = json.loads(serverConfig.read_text(encoding="utf-8"))["_servicePort"]
    outDir = Path(argument.out) if argument.out else root / "Saved" / "OnlineLocalRun"
    outDir.mkdir(parents=True, exist_ok=True)
    serverLog = outDir / "server.log"
    clientLog = outDir / "client.log"
    serverArgs = [str(serverExe), f"-game={argument.game}", f"-server-config={serverConfig}"]
    clientArgs = [str(clientExe), f"-game={argument.game}", "-gv_standardInputCommands=1", f"-gv_onlineServerAddress=127.0.0.1:{servicePort}"]
    print(f"[online-local] server: {' '.join(serverArgs)}")
    print(f"[online-local] client: {' '.join(clientArgs)}")
    with serverLog.open("wb") as serverOut, clientLog.open("wb") as clientOut:
        server = subprocess.Popen(serverArgs, cwd=str(binDir), stdin=subprocess.PIPE, stdout=serverOut, stderr=subprocess.STDOUT)
        if not _waitForText(serverLog, _kListening, argument.timeout, server):
            print(f"[online-local] FAIL: the server never opened its service port (log: {serverLog})")
            _stop(server, False, 5)
            return int(ExitCode.Violation)
        client = subprocess.Popen(clientArgs, cwd=str(binDir), stdin=subprocess.PIPE, stdout=clientOut, stderr=subprocess.STDOUT)
        time.sleep(argument.client_delay)  # 클라이언트가 게임 인스턴스를 세우고 표준 입력 명령을 받을 때까지
        try:
            _send(client, "online.connect")
            time.sleep(2.0)  # Hello 앞의 요청은 세션이 모았다가 보내지만, 연결 실패를 로그인 실패와 가르기 위해 잠깐 둔다
            _send(client, "shooter.online.login")
            if _waitForText(clientLog, _kLoggedIn, argument.timeout, client):
                _send(client, "shooter.online.listmail")
                _waitForText(clientLog, _kMailList, argument.timeout, client)
                _send(client, "shooter.online.match")
                _waitForText(clientLog, _kMatchFound, argument.timeout, client)
                _send(client, "shooter.online.submitkills 5")
                if _waitForText(clientLog, _kScoreSubmitted, argument.timeout, client):
                    _send(client, "shooter.online.topkills")
                    _waitForText(clientLog, _kTopScores, argument.timeout, client)
            time.sleep(1.0)  # 늦게 나는 경고도 잡는다
        except OSError:
            print(f"[online-local] FAIL: the client exited before it took the commands (log: {clientLog})")
        _stop(client, True, 10)
        _stop(server, True, 20)
    if server.returncode != 0:
        print(f"[online-local] FAIL: the server exited with code {server.returncode} (log: {serverLog})")
        return int(ExitCode.Violation)
    serverText = serverLog.read_text(encoding="utf-8", errors="replace")
    clientText = clientLog.read_text(encoding="utf-8", errors="replace")
    return _print(checkLogs(serverText, clientText), serverLog, clientLog)


def _print(result: CheckResult, serverLog: Path, clientLog: Path) -> int:
    for problem in result.problems:
        print(f"[online-local] FAIL: {problem}")
    print(f"[online-local] logs: {serverLog} {clientLog}")
    if result.bOk:
        print("[online-local] OK: TLS service connection -> login -> mailbox -> match -> leaderboard finished with no unexpected errors or warnings")
        return int(ExitCode.Passed)
    return int(ExitCode.Violation)


def main(listArgument: list[str]) -> int:
    parser = argparse.ArgumentParser(prog="py -3 -m Scripts online-local", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="mode", required=True)
    runParser = sub.add_parser("run", help="서버와 클라이언트를 띄워 로그인 → 우편 목록 → 매칭 → 순위표를 돈다")
    runParser.add_argument("--preset", default="Ninja-Debug", help="빌드 프리셋(build/<프리셋>/Bin 의 Server · App 을 쓴다)")
    runParser.add_argument("--game", default="Shooter3D", help="게임 이름")
    runParser.add_argument("--server-config", default=_kDefaultConfig, help="서버 설정(옆 .online.json 과 함께) — 저장소 · 키트 · 제공자를 고른다")
    runParser.add_argument("--client-delay", type=float, default=8.0, help="클라이언트를 띄운 뒤 명령을 보낼 때까지 기다리는 초")
    runParser.add_argument("--timeout", type=float, default=60.0, help="서버 기동 · 응답을 기다리는 최대 초")
    runParser.add_argument("--out", default="", help="로그를 둘 폴더(기본 Saved/OnlineLocalRun)")
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
