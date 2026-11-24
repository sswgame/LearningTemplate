"""
락스텝 2 인 대전 실행 — Playground 의 RTS 매치 미니게임(starskirmish)을 리슨 호스트(0 번)와 클라이언트(1 번) 두 프로세스로 띄워, 명령만 나누는 락스텝이 같은 판을 같은 걸음으로 돌리는지 본다(K3).

  py -3 -m Scripts lockstep-pair run                 # 기본: Ninja-Debug, 자동 플레이(두 AI 가 양쪽 프로세스에서 같은 판 규칙으로 돈다)
  py -3 -m Scripts lockstep-pair run --seconds 90

두 프로세스 모두 자동 플레이라 사람 입력은 없다. 1 번에게 병력이 생기면 클라이언트(1 번)에 개발 명령 `skirmish.order` 로 병력 공격 이동을 한 번 내 사람 명령이 락스텝 입력으로 건너가는지 본다.
판정: 두 로그에 락스텝 시작 줄이 있고, 끝의 `skirmish.lockstep` 상태 줄에서 두 프로세스가 `--min-ticks` 틱 넘게 진행했고 비동기(desynced)가 0 이며,
      1 번이 낸 명령이 두 프로세스에서 같은 수로 적용됐고(0 보다 크다), 두 로그에 허용 밖 `[Error]` · `[Warning]` 이 없다. 판 해시는 세션이 `kChecksumInterval` 틱마다 맞춘다.
"""

from __future__ import annotations

import argparse
import re
import socket
import time
from dataclasses import dataclass

from common import app_arguments
from common.exit_code import ExitCode
from common.process_group import CheckResult, ProcessGroup, problem_lines, report

_OPENED = re.compile(r"\[Skirmish\] lockstep match - you are player (\d)")
_STATUS = re.compile(r"skirmish lockstep: player (\d) tick (\d+) stalls (\d+) desynced (\d) applied (\d+) (\d+)")
_PRESET = "Ninja-Debug"  # build/<프리셋>/Bin 의 App 을 쓴다
_ARMY = re.compile(r"\[Skirmish\] t=\d+s .*\| p1 workers \d+ army [1-9]")  # 1 번에게 병력이 생긴 판 상태 줄(`SkirmishMatch` 가 30 초마다 남긴다)
_ARMY_TIMEOUT_SECONDS = 420.0  # 병력이 생기기를 기다리는 최대 초(판의 시간은 실제 시간과 같다)
_GAME = "Playground"
_MINIGAME = "starskirmish"
_TIMEOUT_SECONDS = 90.0  # 락스텝 시작 줄을 기다리는 최대 초


@dataclass
class LockstepStatus:
    player: int
    tick: int
    desynced: int
    applied: tuple[int, int]


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
        result.problems += problem_lines(name, text)
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


def _run(argument: argparse.Namespace) -> int:
    with ProcessGroup("lockstep-pair", _PRESET, "LockstepPairRun") as group:
        list_exe = group.require("App")
        if list_exe is None:
            return int(ExitCode.CHECK_INVALID)
        app_exe = list_exe[0]
        port = _free_port()
        # -unattended: 무인 실행이라 세이브 · 사용자 데이터는 프로세스마다 Saved/Automation/UserData 아래로 간다(사람의 폴더를 쓰지 않는다 — AutomationUserDataRoot).
        common = [app_exe, app_arguments.option("game", _GAME), app_arguments.flag("unattended"), app_arguments.variable("gv_standardInputCommands", "1"),
                  app_arguments.variable("gv_firstMinigame", _MINIGAME), app_arguments.variable("gv_playgroundAutoPlay", "1")]
        host = group.start("host", [*common, app_arguments.variable("gv_netListenPort", port),
                                    app_arguments.variable("gv_userSettingsFile", group.out_dir / "host.usersettings.json")])
        client = group.start("client", [*common, app_arguments.variable("gv_netConnectAddress", f"127.0.0.1:{port}"),
                                        app_arguments.variable("gv_userSettingsFile", group.out_dir / "client.usersettings.json")])
        try:
            if host.wait_for(_OPENED, _TIMEOUT_SECONDS) and client.wait_for(_OPENED, _TIMEOUT_SECONDS):
                # 병력은 AI 가 건물을 짓고 뽑아야 생긴다 - 시각이 아니라 판의 상태로 기다린다(병력 없이 낸 명령은 실을 것이 없다)
                if not client.wait_for(_ARMY, _ARMY_TIMEOUT_SECONDS):
                    return group.fail(f"player 1 never had an army (logs: {host.log} {client.log})")
                client.send("skirmish.order 32 32")
                time.sleep(argument.seconds)
                host.send("skirmish.lockstep")
                client.send("skirmish.lockstep")
                time.sleep(2.0)
        except OSError:
            print("[lockstep-pair] FAIL: a process exited before it took the commands")
        client.stop()
        host.stop()
    result = check_logs(host.text(), client.text(), argument.min_ticks)
    return report("lockstep-pair", result.problems, [host.log, client.log],
                  "both processes advanced the same lockstep match with no desync and the same player 1 commands")


def main(list_argument: list[str]) -> int:
    parser = argparse.ArgumentParser(prog="py -3 -m Scripts lockstep-pair", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="mode", required=True)
    run_parser = sub.add_parser("run", help="호스트와 클라이언트를 띄워 락스텝 대전을 돌리고 판정한다")
    run_parser.add_argument("--seconds", type=float, default=30.0, help="1 번이 명령을 낸 뒤 판을 더 돌리는 초")
    run_parser.add_argument("--min-ticks", type=int, default=600, help="두 프로세스가 넘어야 할 락스텝 틱 수(20 Hz)")
    return _run(parser.parse_args(list_argument))
