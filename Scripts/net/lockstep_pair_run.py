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

from common import app_arguments
from common.exit_code import ExitCode
from common.host_platform import executable_name
from common.paths import get_project_root
from net.net_pair_run import _stop, _wait_for_text

_OPENED = re.compile(r"\[Skirmish\] lockstep match - you are player (\d)")
_STATUS = re.compile(r"skirmish lockstep: player (\d) tick (\d+) stalls (\d+) desynced (\d) applied (\d+) (\d+)")
_PROBLEM_LINE = re.compile(r"\[(Error|Warning)\]")


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
    def ok(self) -> bool:
        return not self.problems


def _find_status(text: str) -> LockstepStatus | None:
    list_match = list(_STATUS.finditer(text))
    if not list_match:
        return None
    match = list_match[-1]
    return LockstepStatus(int(match.group(1)), int(match.group(2)), int(match.group(4)), (int(match.group(5)), int(match.group(6))))


def check_logs(host_text: str, client_text: str, min_ticks: int) -> CheckResult:
    """두 로그를 판정한다. 규칙은 파일 머리말을 본다."""
    result = CheckResult()
    list_status: list[LockstepStatus] = []
    for name, text, expected_player in (("host", host_text, 0), ("client", client_text, 1)):
        opened = _OPENED.search(text)
        if opened is None or int(opened.group(1)) != expected_player:
            result.problems.append(f"{name} log has no lockstep start line as player {expected_player}")
        status = _find_status(text)
        if status is None:
            result.problems.append(f"{name} log has no 'skirmish.lockstep' status line")
            continue
        list_status.append(status)
        if status.tick < min_ticks:
            result.problems.append(f"{name} advanced only {status.tick} ticks (< {min_ticks}) - the lockstep stalled")
        if status.desynced != 0:
            result.problems.append(f"{name} reports a desync - the two matches differ")
        for line in text.splitlines():
            if _PROBLEM_LINE.search(line):
                result.problems.append(f"{name} log: {line.strip()}")
    if len(list_status) == 2:
        host, client = list_status
        if host.applied[1] != client.applied[1]:
            result.problems.append(f"player 1 commands applied {host.applied[1]} times on the host but {client.applied[1]} on the client")
        if client.applied[1] == 0:
            result.problems.append("the player 1 attack-move was never applied - no army yet or the command did not cross the lockstep input")
    return result


def _free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def _send(process: subprocess.Popen, line: str) -> None:
    process.stdin.write((line + "\n").encode("utf-8"))
    process.stdin.flush()


def _run(argument: argparse.Namespace) -> int:
    root = get_project_root()
    bin_dir = root / "build" / argument.preset / "Bin"
    client_exe = bin_dir / executable_name("App")
    if not client_exe.exists():
        print(f"[lockstep-pair] FAIL: {client_exe} does not exist - build the '{argument.preset}' preset first")
        return int(ExitCode.CHECK_INVALID)
    out_dir = Path(argument.out) if argument.out else root / "Saved" / "LockstepPairRun"
    out_dir.mkdir(parents=True, exist_ok=True)
    port = _free_port()
    # -unattended: 무인 실행이라 세이브 · 사용자 데이터는 프로세스마다 Saved/Automation/UserData 아래로 간다(사람의 폴더를 쓰지 않는다 — AutomationUserDataRoot).
    common = [app_arguments.option("game", argument.game), app_arguments.flag("unattended"), app_arguments.variable("gv_standardInputCommands", "1"), app_arguments.variable("gv_skirmishAutoPlay", "1")]
    host_args = [str(client_exe), *common, app_arguments.variable("gv_netListenPort", port), app_arguments.variable("gv_userSettingsFile", out_dir / 'host.usersettings.json')]
    client_args = [str(client_exe), *common, app_arguments.variable("gv_netConnectAddress", f"127.0.0.1:{port}"), app_arguments.variable("gv_userSettingsFile", out_dir / 'client.usersettings.json')]
    host_log = out_dir / "host.log"
    client_log = out_dir / "client.log"
    print(f"[lockstep-pair] host: {' '.join(host_args)}")
    print(f"[lockstep-pair] client: {' '.join(client_args)}")
    with host_log.open("wb") as host_out, client_log.open("wb") as client_out:
        host = subprocess.Popen(host_args, cwd=str(bin_dir), stdin=subprocess.PIPE, stdout=host_out, stderr=subprocess.STDOUT)
        client = subprocess.Popen(client_args, cwd=str(bin_dir), stdin=subprocess.PIPE, stdout=client_out, stderr=subprocess.STDOUT)
        try:
            is_opened = _wait_for_text(host_log, _OPENED, argument.timeout, host) and _wait_for_text(client_log, _OPENED, argument.timeout, client)
            if is_opened:
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
    host_text = host_log.read_text(encoding="utf-8", errors="replace")
    client_text = client_log.read_text(encoding="utf-8", errors="replace")
    result = check_logs(host_text, client_text, argument.min_ticks)
    for problem in result.problems:
        print(f"[lockstep-pair] FAIL: {problem}")
    print(f"[lockstep-pair] logs: {host_log} {client_log}")
    if result.ok:
        print("[lockstep-pair] OK: both processes advanced the same lockstep match with no desync and the same player 1 commands")
        return int(ExitCode.PASSED)
    return int(ExitCode.VIOLATION)


def main(list_argument: list[str]) -> int:
    parser = argparse.ArgumentParser(prog="py -3 -m Scripts lockstep-pair", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="mode", required=True)
    run_parser = sub.add_parser("run", help="호스트와 클라이언트를 띄워 락스텝 대전을 돌리고 판정한다")
    run_parser.add_argument("--preset", default="Ninja-Debug", help="빌드 프리셋(build/<프리셋>/Bin 의 App 을 쓴다)")
    run_parser.add_argument("--game", default="StarSkirmish", help="게임 이름")
    run_parser.add_argument("--seconds", type=float, default=60.0, help="락스텝이 선 뒤 판을 돌리는 초(반에 1 번 명령을 낸다)")
    run_parser.add_argument("--min-ticks", type=int, default=600, help="두 프로세스가 넘어야 할 락스텝 틱 수(20 Hz)")
    run_parser.add_argument("--timeout", type=float, default=90.0, help="락스텝 시작 줄을 기다리는 최대 초")
    run_parser.add_argument("--out", default="", help="로그를 둘 폴더(기본 Saved/LockstepPairRun)")
    return _run(parser.parse_args(list_argument))
