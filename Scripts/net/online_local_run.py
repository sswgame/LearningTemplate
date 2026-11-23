"""
온라인 로컬 실행 — 전용 서버와 게임 클라이언트를 두 프로세스로 띄워 TLS 서비스 연결 · 공지(로그인 전) · 로그인 · 라이브 이벤트 · 우편함 호출 · 매칭 배정 · 순위표 제출과 조회를 끝까지 돌린다(온라인 조사 단계 1, K4).

  py -3 -m Scripts online-local run                                   # 기본: Playground, 메모리 저장소
  py -3 -m Scripts online-local run --server-config <서버 설정 경로>   # 저장소 · 캐시 · 키트 · 제공자를 그 설정이 고른다(sqlite · postgres · resp 도 같은 길)
  py -3 -m Scripts online-local people                                 # 서버 하나 · 클라이언트 둘: 친구 신청 → 수락, 금화 거래 정산 → 모자란 거래는 실패
  py -3 -m Scripts online-local check <서버 로그> <클라이언트 로그>     # 이미 있는 두 로그만 판정한다

설정은 코드가 만들지 않고 파일을 그대로 쓴다(`Config/Server/Online/<게임>.json` 과 옆 `.online.json`). 저장 키트(`GF_Server_SQLStore` · `GF_Server_CacheStore`)는 Playground 매니페스트가 켠다(`_listModuleOverride`).
`Config/Server/Online/Playground.external.json` 이 Postgres · RESP · 영수증 · 푸시를 모두 고른 견본이다 - 끝점과 환경 변수의 값은 운영자가 준다. 없으면 서버가 까닭을 남기고 기동하지 못한다.
서버는 매치 서버다(`gv_firstMinigame` 이 매치 맵을 첫 씬으로 띄운다). 매칭은 허브에서 매치형 미니게임(`shooter3d_match`)으로 들어가 선다 - 혼자 서는 모드(`arena_solo`)라 줄을 서면 바로 이 서버
(`_publicAddress` 로 디렉터리에 등록한 게임 서버)로 배정되고, 접속 티켓으로 그 게임 포트(암호화 UDP)에 붙은 뒤 매치에 들어간다. 나오면 **서버가** 순위표 `arena_kills` 에 점수를 내고, 클라이언트가 내는 점수는 거절된다.
판정: 클라이언트 로그에 공지 · 로그인 · 라이브 이벤트 · 우편 목록 · 매칭 배정 · 매치 진입 · 상위 목록(1 항목) · 클라이언트 점수 거절 줄이, 서버 로그에 점수 제출 · 저장 줄이 있고, 두 로그에 허용 밖 `[Error]` · `[Warning]` 가 없고, 서버가 종료 코드 0 으로 끝난다.
허용 줄: 개발 티켓 키 경고(기본 설정은 주 키를 두지 않고 개발 키로 티켓을 만든다).
"""

from __future__ import annotations

import argparse
import json
import re
import time
from pathlib import Path

from common import app_arguments
from common.exit_code import ExitCode
from common.paths import get_project_root
from common.process_group import USER_DATA_ROOT_ENVIRONMENT, CheckResult, ChildProcess, ProcessGroup, problem_lines, read_log, report

_DEFAULT_CONFIG = "Config/Server/Online/Playground.json"
_PRESET = "Ninja-Debug"  # build/<프리셋>/Bin 의 Server · App 을 쓴다
_GAME = "Playground"
_MATCH_MINIGAME = "shooter3d_match"  # 서버의 첫 씬이자 클라이언트가 줄을 서는 매치형 미니게임
_CLIENT_DELAY_SECONDS = 8.0  # 클라이언트를 띄운 뒤 명령을 보낼 때까지
_TIMEOUT_SECONDS = 60.0  # 서버 기동 · 응답을 기다리는 최대 초
_LISTENING = re.compile(r"Online services listening on port")
_LOGGED_IN = re.compile(r"online login done")
_SERVICE_STATUS = re.compile(r"online service status: \d+ announcements")
_LIVE_EVENTS = re.compile(r"online live events: \d+ events")
_MAIL_LIST = re.compile(r"online mail list: \d+ mails")
_MATCH_FOUND = re.compile(r"online match found")
_MATCH_ENTERED = re.compile(r"Minigame 'shooter3d_match' is entered")
_SCORE_REPORTED = re.compile(r"match score of account \d+ reported: arena_kills = \d+")
_SCORE_STORED = re.compile(r"the game server's score is stored")
_SCORE_REFUSED = re.compile(r"online score refused")
_TOP_SCORES = re.compile(r"online top scores: 1 entries")
_ALLOWED_PROBLEM = re.compile(r"no ticketKeySecretEnvironment")


def check_logs(server_text: str, client_text: str) -> CheckResult:
    """서버 · 클라이언트 로그를 판정한다. 규칙은 파일 머리말을 본다."""
    result = CheckResult()
    if _LOGGED_IN.search(client_text) is None:
        result.problems.append("the client log has no login line - the account kit call never finished")
    if _SERVICE_STATUS.search(client_text) is None:
        result.problems.append("the client log has no service status line - the server directory kit call never finished")
    if _LIVE_EVENTS.search(client_text) is None:
        result.problems.append("the client log has no live events line - the live-ops kit call never finished")
    if _MAIL_LIST.search(client_text) is None:
        result.problems.append("the client log has no mail list line - the mailbox kit call never finished")
    if _MATCH_FOUND.search(client_text) is None:
        result.problems.append("the client log has no match found line - the matchmaking kit did not assign the solo arena match")
    if _MATCH_ENTERED.search(client_text) is None:
        result.problems.append("the client log has no line that the match map was entered - the assigned match minigame never started")
    if _SCORE_REPORTED.search(server_text) is None or _SCORE_STORED.search(server_text) is None:
        result.problems.append("the server log has no reported and stored match score - the match server did not submit the score of the player who left")
    if _SCORE_REFUSED.search(client_text) is None:
        result.problems.append("the client log has no refused score line - the board must not take a client's score")
    if _TOP_SCORES.search(client_text) is None:
        result.problems.append("the client log has no top scores line with one entry - the leaderboard kit did not return the submitted score")
    result.problems += problem_lines("server", server_text, _ALLOWED_PROBLEM) + problem_lines("client", client_text, _ALLOWED_PROBLEM)
    return result


def _run(argument: argparse.Namespace) -> int:
    with ProcessGroup("online-local", _PRESET, "OnlineLocalRun") as group:
        list_exe = group.require("Server", "App")
        if list_exe is None:
            return int(ExitCode.CHECK_INVALID)
        server_exe, client_exe = list_exe
        server_config = (get_project_root() / argument.server_config).resolve()
        service_port = json.loads(server_config.read_text(encoding="utf-8"))["_servicePort"]
        server = group.start("server", [server_exe, app_arguments.option("game", _GAME), app_arguments.option("server-config", server_config),
                                        app_arguments.variable("gv_firstMinigame", _MATCH_MINIGAME)])
        if not server.wait_for(_LISTENING, _TIMEOUT_SECONDS):
            return group.fail(f"the server never opened its service port (log: {server.log})")
        client = group.start("client", [client_exe, app_arguments.option("game", _GAME), app_arguments.variable("gv_standardInputCommands", "1"),
                                        app_arguments.variable("gv_onlineServerAddress", f"127.0.0.1:{service_port}")])
        time.sleep(_CLIENT_DELAY_SECONDS)  # 클라이언트가 게임 인스턴스를 세우고 표준 입력 명령을 받을 때까지
        try:
            client.send("online.connect")
            time.sleep(2.0)  # Hello 앞의 요청은 세션이 모았다가 보내지만, 연결 실패를 로그인 실패와 가르기 위해 잠깐 둔다
            client.send("playground.online.servicestatus")  # 로그인 전에 되는 호출이다
            client.wait_for(_SERVICE_STATUS, _TIMEOUT_SECONDS)
            client.send("playground.online.login")
            if client.wait_for(_LOGGED_IN, _TIMEOUT_SECONDS):
                client.send("playground.online.liveevents")
                client.wait_for(_LIVE_EVENTS, _TIMEOUT_SECONDS)
                client.send("playground.online.listmail")
                client.wait_for(_MAIL_LIST, _TIMEOUT_SECONDS)
                client.send(f"minigame.enter {_MATCH_MINIGAME}")
                client.wait_for(_MATCH_FOUND, _TIMEOUT_SECONDS)
                client.wait_for(_MATCH_ENTERED, _TIMEOUT_SECONDS)
                time.sleep(2.0)  # 서버가 조종 연결을 정할 틈
                client.send("minigame.quit")  # 나가면 매치 서버가 점수를 낸다
                if server.wait_for(_SCORE_STORED, _TIMEOUT_SECONDS):
                    client.send("playground.online.topkills")
                    client.wait_for(_TOP_SCORES, _TIMEOUT_SECONDS)
                client.send("playground.online.submitkills 999")
                client.wait_for(_SCORE_REFUSED, _TIMEOUT_SECONDS)
            time.sleep(1.0)  # 늦게 나는 경고도 잡는다
        except OSError:
            print(f"[online-local] FAIL: the client exited before it took the commands (log: {client.log})")
        client.stop(timeout_seconds=10)
        server.stop(timeout_seconds=20)
    if server.return_code != 0:
        return group.fail(f"the server exited with code {server.return_code} (log: {server.log})")
    return _print(check_logs(server.text(), client.text()), server.log, client.log)


# 두 계정의 친구 · 거래. 계정은 `Playground.online.json` 의 시드 둘이다.
_PEER_NAME = "playground_dev2"
_FRIEND_REQUESTED = re.compile(r"friend request sent")
_FRIEND_ACCEPTED = re.compile(r"friend request accepted")
_ONE_FRIEND = re.compile(r"friend list: 1 friends")
_MAIL_STORED = re.compile(r"self mail is stored")
_TRADE_INVITED = re.compile(r"trade \d+ is Invited")
_TRADE_OPEN = re.compile(r"trade \d+ is Open")
_TRADE_DONE = re.compile(r"trade \d+ is Settled")
_TRADE_FAILED = re.compile(r"trade \d+ is Failed")
_ONE_MAIL = re.compile(r"online mail list: 1 mails")


def _send(client: ChildProcess, line: str) -> None:
    client.send(line)
    time.sleep(0.5)  # 다음 명령이 앞 명령의 답(제시 판 · 잠금) 뒤에 서버에 닿게 한다


def _trade(first: ChildProcess, second: ChildProcess, gold: int, result: re.Pattern[str]) -> list[str]:
    """첫째가 금화 @p gold 를 내놓는 거래 하나 — 신청 → 수락 → 제시 → 잠금 → 확정. 두 쪽 모두 @p result 로 끝나야 한다."""
    _send(first, f"playground.online.trade invite {_PEER_NAME}")
    if not second.wait_for(_TRADE_INVITED, _TIMEOUT_SECONDS):
        return ["the second client never got the trade invite"]
    _send(second, "playground.online.trade accept")
    if not first.wait_for(_TRADE_OPEN, _TIMEOUT_SECONDS):
        return ["the trade never opened"]
    _send(first, f"playground.online.trade offer {gold}")
    for verb in ("lock", "confirm"):
        for client in (first, second):
            _send(client, f"playground.online.trade {verb}")
    return [f"the {client.name} log never saw the trade end as expected ({result.pattern})" for client in (first, second) if not client.wait_for(result, _TIMEOUT_SECONDS)]


def _run_people(argument: argparse.Namespace) -> int:
    with ProcessGroup("online-local", _PRESET, "OnlineLocalRun/people") as group:
        list_exe = group.require("Server", "App")
        if list_exe is None:
            return int(ExitCode.CHECK_INVALID)
        server_exe, client_exe = list_exe
        server_config = (get_project_root() / argument.server_config).resolve()
        service_port = json.loads(server_config.read_text(encoding="utf-8"))["_servicePort"]
        server = group.start("server", [server_exe, app_arguments.option("game", _GAME), app_arguments.option("server-config", server_config)])
        if not server.wait_for(_LISTENING, _TIMEOUT_SECONDS):
            return group.fail(f"the server never opened its service port (log: {server.log})")
        problems: list[str] = []
        list_client = []
        for number in (1, 2):
            user_data = group.out_dir / f"client{number}_userdata"
            user_data.mkdir(parents=True, exist_ok=True)  # 같은 파일을 쓰지 않게 사용자 데이터 폴더를 가른다
            list_client.append(group.start(f"client{number}", [client_exe, app_arguments.option("game", _GAME), app_arguments.variable("gv_standardInputCommands", "1"),
                                                              app_arguments.variable("gv_onlineServerAddress", f"127.0.0.1:{service_port}")],
                                           environment={USER_DATA_ROOT_ENVIRONMENT: str(user_data)}))
        time.sleep(_CLIENT_DELAY_SECONDS)
        first, second = list_client
        try:
            for number, client in enumerate(list_client, start=1):
                _send(client, "online.connect")
                client.send(f"playground.online.login {number}")
                if not client.wait_for(_LOGGED_IN, _TIMEOUT_SECONDS):
                    problems.append(f"client {number} never logged in")
            # 친구 — 신청 → 수락 → 목록
            _send(first, f"playground.online.friend request {_PEER_NAME}")
            if not first.wait_for(_FRIEND_REQUESTED, _TIMEOUT_SECONDS):
                problems.append("the friend request was not taken")
            _send(second, "playground.online.friend accept")
            if not second.wait_for(_FRIEND_ACCEPTED, _TIMEOUT_SECONDS):
                problems.append("the friend request was not accepted")
            for client in list_client:
                _send(client, "playground.online.friend list")
                if not client.wait_for(_ONE_FRIEND, _TIMEOUT_SECONDS):
                    problems.append(f"the {client.name} friend list never showed the one friend")
            # 거래 — 금화를 우편으로 받고, 가진 만큼은 정산되며 모자란 거래는 아무것도 옮기지 않고 실패한다
            server.send("playground.online.sendmail 100 playground_dev")
            if not server.wait_for(_MAIL_STORED, _TIMEOUT_SECONDS):
                problems.append("the server did not store the gold mail")
            _send(first, "playground.online.listmail")
            if not first.wait_for(_ONE_MAIL, _TIMEOUT_SECONDS):
                problems.append("the gold mail never reached the first client")
            _send(first, "playground.online.claimall")
            time.sleep(1.0)
            problems += _trade(first, second, 40, _TRADE_DONE)
            problems += _trade(first, second, 100, _TRADE_FAILED)
            time.sleep(1.0)  # 늦게 나는 경고도 잡는다
        except OSError:
            return group.fail("a client exited before it took the commands")
        for client in list_client:
            client.stop(timeout_seconds=10)
        server.stop(timeout_seconds=20)
    if server.return_code != 0:
        return group.fail(f"the server exited with code {server.return_code} (log: {server.log})")
    for process in (server, *list_client):
        problems += problem_lines(process.name, process.text(), _ALLOWED_PROBLEM)
    return report("online-local", problems, [server.log, *(client.log for client in list_client)],
                  "friend request -> accept -> list, gold trade settled, short trade failed, no unexpected errors or warnings")


def _print(result: CheckResult, server_log: Path, client_log: Path) -> int:
    return report("online-local", result.problems, [server_log, client_log],
                  "TLS service connection -> service status -> login -> live events -> mailbox -> match server -> server score -> leaderboard finished with no unexpected errors or warnings")


def main(list_argument: list[str]) -> int:
    parser = argparse.ArgumentParser(prog="py -3 -m Scripts online-local", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="mode", required=True)
    run_parser = sub.add_parser("run", help="서버와 클라이언트를 띄워 로그인 → 우편 목록 → 매칭 → 순위표를 돈다")
    run_parser.add_argument("--server-config", default=_DEFAULT_CONFIG, help="서버 설정(옆 .online.json 과 함께) — 저장소 · 키트 · 제공자를 고른다")
    people_parser = sub.add_parser("people", help="서버 하나와 클라이언트 둘을 띄워 친구 신청 · 수락과 금화 거래를 돈다")
    people_parser.add_argument("--server-config", default=_DEFAULT_CONFIG, help="서버 설정(옆 .online.json 과 함께) — 시드 계정 둘이 있어야 한다")
    check_parser = sub.add_parser("check", help="이미 있는 서버 · 클라이언트 로그를 판정한다")
    check_parser.add_argument("server_log")
    check_parser.add_argument("client_log")
    argument = parser.parse_args(list_argument)
    if argument.mode == "check":
        server_log, client_log = Path(argument.server_log), Path(argument.client_log)
        return _print(check_logs(read_log(server_log), read_log(client_log)), server_log, client_log)
    return _run_people(argument) if argument.mode == "people" else _run(argument)
