"""
락스텝 2 인 대전 실행 — StarSkirmish 를 리슨 호스트(0 번)와 클라이언트(1 번) 두 프로세스로 띄워, 명령만 나누는 락스텝이 같은 판을 같은 걸음으로 돌리는지 본다(K3).

  py -3 -m Scripts lockstep-pair run                 # 기본: Ninja-Debug, 자동 플레이(두 AI 가 양쪽 프로세스에서 같은 판 규칙으로 돈다)
  py -3 -m Scripts lockstep-pair run --seconds 90

두 프로세스 모두 자동 플레이라 사람 입력은 없다. 클라이언트(1 번)에 개발 명령 `skirmish.order` 로 병력 공격 이동을 한 번 내 사람 명령이 락스텝 입력으로 건너가는지 본다.
판정: 두 로그에 락스텝 시작 줄이 있고, 끝의 `skirmish.lockstep` 상태 줄에서 두 프로세스가 `--min-ticks` 틱 넘게 진행했고 비동기(desynced)가 0 이며,
      1 번이 낸 명령이 두 프로세스에서 같은 수로 적용됐고(0 보다 크다), 두 로그에 허용 밖 `[Error]` · `[Warning]` 이 없다. 판 해시는 세션이 `kChecksumInterval` 틱마다 맞춘다.
"""

from __future__ import annotations

import argparse
import re
import socket
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

from common import AppArguments
from common.ExitCode import ExitCode
from common.HostPlatform import executableName
from common.Paths import getProjectRoot
from net.NetPairRun import _stop, _waitForText

_kOpened = re.compile(r"\[Skirmish\] lockstep match - you are player (\d)")
_kStatus = re.compile(r"skirmish lockstep: player (\d) tick (\d+) stalls (\d+) desynced (\d) applied (\d+) (\d+)")
_kProblemLine = re.compile(r"\[(Error|Warning)\]")


@dataclass
class LockstepStatus:
    player: int
    tick: int
    desynced: int
    applied: tuple[int, int]


@dataclass
class CheckResult:
    problems: list[str] = field(default_factory=list)

    @property
    def bOk(self) -> bool:
        return not self.problems


def _findStatus(text: str) -> LockstepStatus | None:
    listMatch = list(_kStatus.finditer(text))
    if not listMatch:
        return None
    match = listMatch[-1]
    return LockstepStatus(int(match.group(1)), int(match.group(2)), int(match.group(4)), (int(match.group(5)), int(match.group(6))))


def checkLogs(hostText: str, clientText: str, minTicks: int) -> CheckResult:
    """두 로그를 판정한다. 규칙은 파일 머리말을 본다."""
    result = CheckResult()
    listStatus: list[LockstepStatus] = []
    for name, text, expectedPlayer in (("host", hostText, 0), ("client", clientText, 1)):
        opened = _kOpened.search(text)
        if opened is None or int(opened.group(1)) != expectedPlayer:
            result.problems.append(f"{name} log has no lockstep start line as player {expectedPlayer}")
        status = _findStatus(text)
        if status is None:
            result.problems.append(f"{name} log has no 'skirmish.lockstep' status line")
            continue
        listStatus.append(status)
        if status.tick < minTicks:
            result.problems.append(f"{name} advanced only {status.tick} ticks (< {minTicks}) - the lockstep stalled")
        if status.desynced != 0:
            result.problems.append(f"{name} reports a desync - the two matches differ")
        for line in text.splitlines():
            if _kProblemLine.search(line):
                result.problems.append(f"{name} log: {line.strip()}")
    if len(listStatus) == 2:
        host, client = listStatus
        if host.applied[1] != client.applied[1]:
            result.problems.append(f"player 1 commands applied {host.applied[1]} times on the host but {client.applied[1]} on the client")
        if client.applied[1] == 0:
            result.problems.append("the player 1 attack-move was never applied - no army yet or the command did not cross the lockstep input")
    return result


def _freePort() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def _send(process: subprocess.Popen, line: str) -> None:
    process.stdin.write((line + "\n").encode("utf-8"))
    process.stdin.flush()


def _run(argument: argparse.Namespace) -> int:
    root = getProjectRoot()
    binDir = root / "build" / argument.preset / "Bin"
    clientExe = binDir / executableName("App")
    if not clientExe.exists():
        print(f"[lockstep-pair] FAIL: {clientExe} does not exist - build the '{argument.preset}' preset first")
        return int(ExitCode.CheckInvalid)
    outDir = Path(argument.out) if argument.out else root / "Saved" / "LockstepPairRun"
    outDir.mkdir(parents=True, exist_ok=True)
    port = _freePort()
    # -unattended: 무인 실행이라 세이브 · 사용자 데이터는 프로세스마다 Saved/Automation/UserData 아래로 간다(사람의 폴더를 쓰지 않는다 — AutomationUserDataRoot).
    common = [AppArguments.option("game", argument.game), AppArguments.flag("unattended"), AppArguments.variable("gv_standardInputCommands", "1"), AppArguments.variable("gv_skirmishAutoPlay", "1")]
    hostArgs = [str(clientExe), *common, AppArguments.variable("gv_netListenPort", port), AppArguments.variable("gv_userSettingsFile", outDir / 'host.usersettings.json')]
    clientArgs = [str(clientExe), *common, AppArguments.variable("gv_netConnectAddress", f"127.0.0.1:{port}"), AppArguments.variable("gv_userSettingsFile", outDir / 'client.usersettings.json')]
    hostLog = outDir / "host.log"
    clientLog = outDir / "client.log"
    print(f"[lockstep-pair] host: {' '.join(hostArgs)}")
    print(f"[lockstep-pair] client: {' '.join(clientArgs)}")
    with hostLog.open("wb") as hostOut, clientLog.open("wb") as clientOut:
        host = subprocess.Popen(hostArgs, cwd=str(binDir), stdin=subprocess.PIPE, stdout=hostOut, stderr=subprocess.STDOUT)
        client = subprocess.Popen(clientArgs, cwd=str(binDir), stdin=subprocess.PIPE, stdout=clientOut, stderr=subprocess.STDOUT)
        try:
            bOpened = _waitForText(hostLog, _kOpened, argument.timeout, host) and _waitForText(clientLog, _kOpened, argument.timeout, client)
            if bOpened:
                time.sleep(argument.seconds / 2)  # AI 가 병력을 모을 때까지
                _send(client, "skirmish.order 32 32")
                time.sleep(argument.seconds / 2)
                _send(host, "skirmish.lockstep")
                _send(client, "skirmish.lockstep")
                time.sleep(2.0)
        except OSError:
            print("[lockstep-pair] FAIL: a process exited before it took the commands")
        _stop(client, True, 20)
        _stop(host, True, 20)
    hostText = hostLog.read_text(encoding="utf-8", errors="replace")
    clientText = clientLog.read_text(encoding="utf-8", errors="replace")
    result = checkLogs(hostText, clientText, argument.min_ticks)
    for problem in result.problems:
        print(f"[lockstep-pair] FAIL: {problem}")
    print(f"[lockstep-pair] logs: {hostLog} {clientLog}")
    if result.bOk:
        print("[lockstep-pair] OK: both processes advanced the same lockstep match with no desync and the same player 1 commands")
        return int(ExitCode.Passed)
    return int(ExitCode.Violation)


def main(listArgument: list[str]) -> int:
    parser = argparse.ArgumentParser(prog="py -3 -m Scripts lockstep-pair", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="mode", required=True)
    runParser = sub.add_parser("run", help="호스트와 클라이언트를 띄워 락스텝 대전을 돌리고 판정한다")
    runParser.add_argument("--preset", default="Ninja-Debug", help="빌드 프리셋(build/<프리셋>/Bin 의 App 을 쓴다)")
    runParser.add_argument("--game", default="StarSkirmish", help="게임 이름")
    runParser.add_argument("--seconds", type=float, default=60.0, help="락스텝이 선 뒤 판을 돌리는 초(반에 1 번 명령을 낸다)")
    runParser.add_argument("--min-ticks", type=int, default=600, help="두 프로세스가 넘어야 할 락스텝 틱 수(20 Hz)")
    runParser.add_argument("--timeout", type=float, default=90.0, help="락스텝 시작 줄을 기다리는 최대 초")
    runParser.add_argument("--out", default="", help="로그를 둘 폴더(기본 Saved/LockstepPairRun)")
    return _run(parser.parse_args(listArgument))
