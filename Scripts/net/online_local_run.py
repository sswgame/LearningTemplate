"""
온라인 로컬 실행(ctest `Net.onlineRun` · `Net.people`) — 전용 서버와 게임 클라이언트를 여러 프로세스로 띄워 TLS 서비스 연결 · 공지(로그인 전) · 로그인 · 라이브 이벤트 · 우편함 호출 · 매칭 배정 · 순위표 제출과 조회를 끝까지 돌린다(온라인 조사 단계 1, K4).

  py -3 -m Scripts online-local run                                   # 기본: Playground, 메모리 저장소
  py -3 -m Scripts online-local run --server-config <서버 설정 경로>   # 저장소 · 캐시 · 키트 · 제공자를 그 설정이 고른다(sqlite · postgres · resp 도 같은 길)
  py -3 -m Scripts online-local people                                 # 서버 하나 · 클라이언트 둘: 친구 신청 → 수락, 금화 거래 정산 → 모자란 거래는 잠글 때 거절

설정은 코드가 만들지 않고 파일을 쓴다(`Config/Server/Online/<게임>.json` 과 옆 `.online.json`) — 포트만 빈 것으로 바꾼 사본을 출력 폴더에 쓴다. 저장 키트(`GF_Server_SQLStore` · `GF_Server_CacheStore`)는 Playground 매니페스트가 켠다(`_listModuleOverride`).
`Config/Server/Online/Playground.external.json` 이 Postgres · RESP · 영수증 · 푸시를 모두 고른 견본이다 - 끝점과 환경 변수의 값은 운영자가 준다. 없으면 서버가 까닭을 남기고 기동하지 못한다.
서버는 매치 서버다(`gv_firstMinigame` 이 매치 맵을 첫 씬으로 띄운다). 매칭은 허브에서 매치형 미니게임(`shooter3d_match`)으로 들어가 선다 - 혼자 서는 모드(`arena_solo`)라 줄을 서면 바로 이 서버
(`_publicAddress` 로 디렉터리에 등록한 게임 서버)로 배정되고, 접속 티켓으로 그 게임 포트(암호화 UDP)에 붙은 뒤 매치에 들어간다. 나오면 **서버가** 순위표 `arena_kills` 에 점수를 내고, 클라이언트가 내는 점수는 거절된다.
판정: 값은 클라이언트 시나리오(`netpair/online_run` · `people_first` · `people_second`)가 감시 값으로 본다. 여기는 종료 코드, 서버가 점수를 내고 저장한 사건(로그 id), 허용 밖 Error · Warning 줄을 본다.
허용: 개발 티켓 키 경고(기본 설정은 주 키를 두지 않고 개발 키로 티켓을 만든다).
"""

from __future__ import annotations

import argparse
import json
import socket
from pathlib import Path

from common.command import Command, ScriptContext
from common import app_arguments
from common.exit_code import ExitCode
from common.log_lines import problem_lines
from common.paths import get_project_root
from common.process_group import DEFAULT_PRESET, ChildProcess, ProcessGroup, free_port, report
from net.ctest_items import CtestItem

_DEFAULT_CONFIG = "Config/Server/Online/Playground.json"
_GAME = "Playground"
_FOLDER = "game/playground/automation/netpair"
_RUN_SCENARIO = f"{_FOLDER}/online_run.scenario.xml"
_PEOPLE_SCENARIOS = (f"{_FOLDER}/people_first.scenario.xml", f"{_FOLDER}/people_second.scenario.xml")
_MATCH_MINIGAME = "shooter3d_match"  # 서버의 첫 씬이자 클라이언트가 줄을 서는 매치형 미니게임
_TIMEOUT_SECONDS = 60.0  # 서버 기동 · 사건을 기다리는 최대 초
_SCENARIO_TIMEOUT_SECONDS = 400.0  # 시나리오 하나가 끝나기를 기다리는 최대 초
_ALLOWED_IDS = frozenset({"LoginTicketKey.AccountKitNoTicketKeySecretEnvironment"})

CTEST_ITEMS = (
    CtestItem("onlineRun", ("online-local", "run"), 900, (_RUN_SCENARIO,)),
    CtestItem("people", ("online-local", "people"), 900, _PEOPLE_SCENARIOS),
)


def write_server_config(out_dir: Path, server_config: Path) -> tuple[Path, int]:
    """@p server_config 와 옆 `.online.json` 을 @p out_dir 에 베끼며 게임 · 서비스 포트를 빈 것으로 바꾼다. (사본 경로, 서비스 포트)."""
    config = json.loads(server_config.read_text(encoding="utf-8"))
    config["_gamePort"] = free_port(socket.SOCK_DGRAM)
    config["_servicePort"] = free_port(socket.SOCK_STREAM)
    copied = out_dir / server_config.name
    copied.write_text(json.dumps(config, indent=4), encoding="utf-8")
    online = server_config.with_suffix(".online.json")
    (out_dir / online.name).write_bytes(online.read_bytes())
    return copied, config["_servicePort"]


def _start_server(group: ProcessGroup, server_exe: Path, server_config: str, *extra: str) -> tuple[ChildProcess, int] | None:
    copied, service_port = write_server_config(group.out_dir, (get_project_root() / server_config).resolve())
    server = group.start("server", [server_exe, app_arguments.option("game", _GAME), app_arguments.option("server-config", copied), *extra])
    if not server.wait_for_log("OnlineServerRuntime.OnlineServicesListeningPort", _TIMEOUT_SECONDS):
        group.fail(f"the server never opened its service port (log: {server.log})")
        return None
    return server, service_port


def _finish(server: ChildProcess, list_client: list[ChildProcess], problems: list[str], ok_message: str) -> int:
    for client in list_client:
        problems += client.scenario_problems()
    if server.return_code != 0:
        problems.append(f"the server exited with code {server.return_code} (log: {server.log})")
    for process in (server, *list_client):
        problems += problem_lines(process.name, process.text(), _ALLOWED_IDS)
    return report("online-local", problems, [server.log, *(client.log for client in list_client)], ok_message)


def _run(argument: argparse.Namespace) -> int:
    problems: list[str] = []
    with ProcessGroup("online-local", argument.preset, "OnlineLocalRun") as group:
        list_exe = group.require("Server", "App")
        if list_exe is None:
            return int(ExitCode.CHECK_INVALID)
        started = _start_server(group, list_exe[0], argument.server_config, app_arguments.variable("gv_firstMinigame", _MATCH_MINIGAME))
        if started is None:
            return int(ExitCode.VIOLATION)
        server, service_port = started
        client = group.start_scenario("client", list_exe[1], _GAME, _RUN_SCENARIO, {"gv_onlineServerAddress": f"127.0.0.1:{service_port}"}, stdin=True)
        # 서버가 점수를 저장한 뒤라야 순위표에 있다 — 그 사건을 보고 순위표를 읽게 한다(값은 시나리오가 본다). 클라이언트가 먼저 끝났으면 기다리지 않는다.
        if server.wait_for_log("Leaderboard.ServerScoreStored", _SCENARIO_TIMEOUT_SECONDS, while_running=client):
            client.send_if_running("playground.online.topkills")
        else:
            problems.append("the match server did not store the score of the player who left")
        client.wait(_SCENARIO_TIMEOUT_SECONDS)
        server.stop()
    if server.count_log("Shooter.MatchScoreReported") == 0:
        problems.append("the match server did not submit the score of the player who left")
    return _finish(server, [client], problems,
                   "TLS service connection -> service status -> login -> live events -> mailbox -> match server -> server score -> leaderboard finished with no unexpected errors or warnings")


def _run_people(argument: argparse.Namespace) -> int:
    problems: list[str] = []
    with ProcessGroup("online-local", argument.preset, "OnlineLocalRun/people") as group:
        list_exe = group.require("Server", "App")
        if list_exe is None:
            return int(ExitCode.CHECK_INVALID)
        started = _start_server(group, list_exe[0], argument.server_config)
        if started is None:
            return int(ExitCode.VIOLATION)
        server, service_port = started
        variables = {"gv_onlineServerAddress": f"127.0.0.1:{service_port}"}
        first = group.start_scenario("client1", list_exe[1], _GAME, _PEOPLE_SCENARIOS[0], variables, stdin=True)
        second = group.start_scenario("client2", list_exe[1], _GAME, _PEOPLE_SCENARIOS[1], variables)
        # 금화 우편은 접속한 계정에게만 간다 — 로그인을 보고 서버 콘솔로 넣고, 저장된 뒤 받는 쪽이 목록을 다시 읽게 한다(값은 시나리오가 본다).
        if first.wait_for_log("Playground.OnlineLoggedIn", _SCENARIO_TIMEOUT_SECONDS):
            server.send_if_running("playground.online.sendmail 100 playground_dev")
            if server.wait_for_log("Playground.OnlineSelfMailSent", _TIMEOUT_SECONDS):
                first.send_if_running("playground.online.listmail")
            else:
                problems.append("the server did not store the gold mail")
        for client in (first, second):
            client.wait(_SCENARIO_TIMEOUT_SECONDS)
        server.stop()
    return _finish(server, [first, second], problems, "friend request -> accept -> list, gold trade settled, short trade refused, no unexpected errors or warnings")


class OnlineLocalCommand(Command):
    name = "online-local"
    help = "온라인 로컬 실행 — 전용 서버와 게임 클라이언트를 띄워 로그인 → 키트 호출까지 돈다. 저장소 · 키트 · 제공자는 --server-config 가 고른다(run · people)"

    def add_arguments(self, parser: argparse.ArgumentParser) -> None:
        parser.add_argument("mode", choices=("run", "people"), help="run: 로그인 → 우편 목록 → 매칭 → 순위표, people: 클라이언트 둘의 친구 · 거래")
        parser.add_argument("--server-config", default=_DEFAULT_CONFIG, help="서버 설정(옆 .online.json 과 함께) — 저장소 · 키트 · 제공자를 고른다(people 은 시드 계정 둘이 있어야 한다)")
        parser.add_argument("--preset", default=DEFAULT_PRESET, help="빌드 프리셋(build/<프리셋>/Bin 의 Server · App 을 쓴다)")

    def run(self, context: ScriptContext, args: argparse.Namespace) -> int:
        return _run_people(args) if args.mode == "people" else _run(args)
