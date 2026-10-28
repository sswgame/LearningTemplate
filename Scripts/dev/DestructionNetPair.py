"""
파괴 복제의 실제 세션 확인 — 전용 서버와 클라이언트를 한 기계에서 두 프로세스로 띄워 구조 해시 로그를 맞댄다.

  py -3 -m Scripts destruction-net run --preset Ninja-Release          # 서버 + 클라이언트를 띄우고 로그를 맞댄다
  py -3 -m Scripts destruction-net compare <서버 로그> <클라이언트 로그>   # 이미 있는 두 로그만 맞댄다

해시 로그 한 줄은 `DestructionNetSession::formatHashLine` 의 모양이다:
  `[DestructionHash] side=<server|client> id=<번호> events=<사건 수> hash=<16 자리> name=<이름>`
같은 그래프에 같은 사건을 같은 순서로 적용하면 같은 해시이므로, 서버와 클라이언트가 같은 (번호, 사건 수) 에서 같은 해시를 남겨야 한다.
클라이언트가 스냅숏으로 건너뛴 사건 수는 그 줄이 없다 — 서버에만 있는 (번호, 사건 수) 는 틀린 것이 아니다.
틀린 것: (1) 같은 (번호, 사건 수) 의 해시가 다름, (2) 클라이언트에만 있는 (번호, 사건 수), (3) 맞댄 쌍이 하나도 없음.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

from common.ExitCode import ExitCode
from common.Paths import getProjectRoot

_kHashLine = re.compile(r"\[DestructionHash\]\s+side=(?P<side>server|client)\s+id=(?P<id>\d+)\s+events=(?P<events>\d+)\s+hash=(?P<hash>[0-9a-fA-F]+)\s+name=(?P<name>.*)$")
_kScene = "game/empty/maps/destructionnet.scene.xml"
_kGame = "Empty"


@dataclass
class HashRecords:
    """한쪽 로그의 해시 기록 — `(번호, 사건 수) → (해시, 이름)`."""

    byKey: dict[tuple[int, int], tuple[str, str]] = field(default_factory=dict)
    conflictingKeys: list[tuple[int, int]] = field(default_factory=list)


def parseHashLog(text: str, side: str) -> HashRecords:
    """`side` 줄만 읽는다. 같은 (번호, 사건 수) 가 다른 해시로 두 번 나오면 그 쪽 로그가 스스로 어긋난 것이라 따로 모은다."""
    records = HashRecords()
    for line in text.splitlines():
        match = _kHashLine.search(line)
        if match is None or match.group("side") != side:
            continue
        key = (int(match.group("id")), int(match.group("events")))
        value = (match.group("hash").lower(), match.group("name").strip())
        previous = records.byKey.get(key)
        if previous is not None and previous[0] != value[0]:
            records.conflictingKeys.append(key)
        records.byKey[key] = value
    return records


@dataclass
class CompareResult:
    matched: int = 0
    problems: list[str] = field(default_factory=list)

    @property
    def bOk(self) -> bool:
        return self.matched > 0 and not self.problems


def compareLogs(serverText: str, clientText: str) -> CompareResult:
    """서버 로그와 클라이언트 로그를 맞댄다. 규칙은 파일 머리말을 본다."""
    server = parseHashLog(serverText, "server")
    client = parseHashLog(clientText, "client")
    result = CompareResult()
    for key in server.conflictingKeys:
        result.problems.append(f"server log repeats id={key[0]} events={key[1]} with different hashes")
    for key in client.conflictingKeys:
        result.problems.append(f"client log repeats id={key[0]} events={key[1]} with different hashes")
    for key in sorted(client.byKey):
        clientHash, name = client.byKey[key]
        serverEntry = server.byKey.get(key)
        if serverEntry is None:
            result.problems.append(f"client has id={key[0]} events={key[1]} ({name}) that the server never logged")
        elif serverEntry[0] != clientHash:
            result.problems.append(f"hash mismatch id={key[0]} events={key[1]} ({name}): server {serverEntry[0]} client {clientHash}")
        else:
            result.matched += 1
    if result.matched == 0:
        result.problems.append("no (id, events) pair was logged by both sides - nothing was compared")
    return result


def _readLog(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def _printResult(result: CompareResult) -> int:
    for problem in result.problems:
        print(f"[destruction-net] FAIL: {problem}")
    if result.bOk:
        print(f"[destruction-net] OK: {result.matched} (id, events) pairs matched between server and client")
        return int(ExitCode.Passed)
    return int(ExitCode.Violation)


def _runPair(argument: argparse.Namespace) -> int:
    root = getProjectRoot()
    binDir = root / "build" / argument.preset / "Bin"
    serverExe = binDir / ("Server.exe" if sys.platform == "win32" else "Server")
    clientExe = binDir / ("App.exe" if sys.platform == "win32" else "App")
    for exe in (serverExe, clientExe):
        if not exe.exists():
            print(f"[destruction-net] FAIL: {exe} does not exist - build the '{argument.preset}' preset first")
            return int(ExitCode.Violation)
    outDir = Path(argument.out) if argument.out else root / "Saved" / "DestructionNetPair"
    outDir.mkdir(parents=True, exist_ok=True)
    serverLog = outDir / "server.log"
    clientLog = outDir / "client.log"
    port = argument.port
    serverArgs = [str(serverExe), f"-game={_kGame}", f"-gv_firstScene={_kScene}", f"-gv_netListenPort={port}",
                  f"-gv_serverExitAfterTicks={argument.server_ticks}"]
    clientArgs = [str(clientExe), f"-game={_kGame}", f"-gv_firstScene={_kScene}", f"-gv_netConnectAddress=127.0.0.1:{port}"]
    print(f"[destruction-net] server: {' '.join(serverArgs)}")
    print(f"[destruction-net] client: {' '.join(clientArgs)}")
    with serverLog.open("wb") as serverOut, clientLog.open("wb") as clientOut:
        server = subprocess.Popen(serverArgs, cwd=str(binDir), stdout=serverOut, stderr=subprocess.STDOUT)
        time.sleep(argument.client_delay)  # 서버가 먼저 서야 한다(클라이언트는 시간 초과면 다시 시도하지만, 첫 연결을 빨리 잡는다)
        client = subprocess.Popen(clientArgs, cwd=str(binDir), stdout=clientOut, stderr=subprocess.STDOUT)
        try:
            server.wait(timeout=argument.timeout)  # 서버는 틱 상한에 스스로 끝난다
        except subprocess.TimeoutExpired:
            print(f"[destruction-net] FAIL: the server did not finish within {argument.timeout} s")
            server.kill()
            client.kill()
            return int(ExitCode.Violation)
        # 클라이언트는 자동 종료 인자가 없어 서버가 끝난 뒤 닫는다.
        client.terminate()
        try:
            client.wait(timeout=10)
        except subprocess.TimeoutExpired:
            client.kill()
    if server.returncode != 0:
        print(f"[destruction-net] FAIL: the server exited with code {server.returncode} (log: {serverLog})")
        return int(ExitCode.Violation)
    clientText = _readLog(clientLog)
    # 클라이언트가 콘솔이 아니라 파일 로그만 쓰는 구성이면 `--client-log` 로 그 파일을 준다.
    if argument.client_log:
        clientText += "\n" + _readLog(Path(argument.client_log))
    result = compareLogs(_readLog(serverLog), clientText)
    print(f"[destruction-net] logs: {serverLog} {clientLog}")
    return _printResult(result)


def main(listArgument: list[str]) -> int:
    parser = argparse.ArgumentParser(prog="py -3 -m Scripts destruction-net", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="mode", required=True)
    runParser = sub.add_parser("run", help="서버와 클라이언트를 띄워 해시를 맞댄다")
    runParser.add_argument("--preset", default="Ninja-Release", help="빌드 프리셋(build/<프리셋>/Bin 의 Server · App 을 쓴다)")
    runParser.add_argument("--port", type=int, default=7777, help="서버 UDP 포트")
    runParser.add_argument("--server-ticks", type=int, default=1800, help="서버가 이 틱 수를 돈 뒤 끝난다(기본 30 틱/초 기준 60 초)")
    runParser.add_argument("--client-delay", type=float, default=2.0, help="서버를 띄운 뒤 클라이언트를 띄울 때까지 기다리는 초")
    runParser.add_argument("--timeout", type=float, default=180.0, help="서버가 끝나기를 기다리는 최대 초")
    runParser.add_argument("--out", default="", help="로그를 둘 폴더(기본 Saved/DestructionNetPair)")
    runParser.add_argument("--client-log", default="", help="클라이언트가 파일로만 남기는 로그 파일(콘솔 출력에 더해 읽는다)")
    compareParser = sub.add_parser("compare", help="이미 있는 서버 · 클라이언트 로그를 맞댄다")
    compareParser.add_argument("serverLog")
    compareParser.add_argument("clientLog")
    argument = parser.parse_args(listArgument)
    if argument.mode == "compare":
        return _printResult(compareLogs(_readLog(Path(argument.serverLog)), _readLog(Path(argument.clientLog))))
    return _runPair(argument)
