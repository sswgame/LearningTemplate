"""
락스텝 2 인 대전 실행(ctest `Net.lockstep`) — Playground 의 RTS 매치 미니게임(starskirmish)을 리슨 호스트(0 번)와 클라이언트(1 번) 두 프로세스로 띄워, 명령만 나누는 락스텝이 같은 판을 같은 걸음으로 돌리는지 본다(K3).

  py -3 -m Scripts lockstep-pair run --preset Ninja-Release

두 프로세스 모두 자동 플레이라 사람 입력은 없다. 값(걸음 수 · 비동기 · 적용된 명령 수)은 두 시나리오(`netpair/lockstep_host` · `lockstep_client`)가 감시 값으로 판정하고,
여기는 두 프로세스의 종료 코드와 로그에 Error · Warning 이 없는지 본다. 판 해시는 세션이 `kChecksumInterval` 틱마다 맞춘다.
"""

from __future__ import annotations

import argparse
import socket

from common.exit_code import ExitCode
from common.log_lines import problem_lines
from common.process_group import DEFAULT_PRESET, ProcessGroup, free_port, report
from net.ctest_items import CtestItem

_HOST_SCENARIO = "game/playground/automation/netpair/lockstep_host.scenario.xml"
_CLIENT_SCENARIO = "game/playground/automation/netpair/lockstep_client.scenario.xml"
_GAME = "Playground"
_TIMEOUT_SECONDS = 900.0  # 시나리오 하나가 끝나기를 기다리는 최대 초(병력이 생기기까지 판의 시간이 실제 시간과 같다)

CTEST_ITEMS = (CtestItem("lockstep", ("lockstep-pair", "run"), 1200, (_HOST_SCENARIO, _CLIENT_SCENARIO)),)


def _run(preset: str) -> int:
    with ProcessGroup("lockstep-pair", preset, "LockstepPairRun") as group:
        list_exe = group.require("App")
        if list_exe is None:
            return int(ExitCode.CHECK_INVALID)
        port = free_port(socket.SOCK_DGRAM)
        common = {"gv_firstMinigame": "starskirmish", "gv_playgroundAutoPlay": 1}
        host = group.start_scenario("host", list_exe[0], _GAME, _HOST_SCENARIO, {**common, "gv_netListenPort": port, "gv_userSettingsFile": group.out_dir / "host.usersettings.json"})
        client = group.start_scenario("client", list_exe[0], _GAME, _CLIENT_SCENARIO,
                                      {**common, "gv_netConnectAddress": f"127.0.0.1:{port}", "gv_userSettingsFile": group.out_dir / "client.usersettings.json"})
        for process in (client, host):
            process.wait(_TIMEOUT_SECONDS)
    problems = []
    for process in (host, client):
        problems += process.scenario_problems() + problem_lines(process.name, process.text())
    return report("lockstep-pair", problems, [host.log, client.log], "both processes advanced the same lockstep match with no desync and the same player 1 command")


def main(list_argument: list[str]) -> int:
    parser = argparse.ArgumentParser(prog="py -3 -m Scripts lockstep-pair", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("mode", choices=("run",), help="호스트와 클라이언트를 띄워 락스텝 대전을 돌리고 판정한다")
    parser.add_argument("--preset", default=DEFAULT_PRESET, help="빌드 프리셋(build/<프리셋>/Bin 의 App 을 쓴다)")
    return _run(parser.parse_args(list_argument).preset)
