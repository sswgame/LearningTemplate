"""
온라인 로컬 실행 — 전용 서버와 게임 클라이언트를 두 프로세스로 띄워 TLS 서비스 연결 · 공지(로그인 전) · 로그인 · 라이브 이벤트 · 우편함 호출 · 매칭 배정 · 순위표 제출과 조회를 끝까지 돌린다(온라인 조사 단계 1, K4).

  py -3 -m Scripts online-local run                                   # 기본: Shooter3D, 메모리 저장소
  py -3 -m Scripts online-local run --server-config <서버 설정 경로>   # 저장소 · 캐시 · 키트 · 제공자를 그 설정이 고른다(sqlite · postgres · resp 도 같은 길)
  py -3 -m Scripts online-local check <서버 로그> <클라이언트 로그>     # 이미 있는 두 로그만 판정한다

설정은 코드가 만들지 않고 파일을 그대로 쓴다(`Config/Server/Online/<게임>.json` 과 옆 `.online.json`). 저장 키트(`GF_Server_SQLStore` · `GF_Server_CacheStore`)는 Shooter3D 매니페스트가 켠다(`_listModuleOverride`).
`Config/Server/Online/Shooter3D.external.json` 이 Postgres · RESP · 영수증 · 푸시를 모두 고른 견본이다 - 끝점과 환경 변수의 값은 운영자가 준다. 없으면 서버가 까닭을 남기고 기동하지 못한다.
매칭은 혼자 서는 모드(`arena_solo`)라 줄을 서면 바로 이 서버(`_publicAddress` 로 디렉터리에 등록한 게임 서버)로 배정된다. 순위표는 `arena_kills` 에 처치 수를 내고 상위 목록을 받는다.
판정: 클라이언트 로그에 공지 · 로그인 · 라이브 이벤트 · 우편 목록 · 매칭 배정 · 점수 제출 · 상위 목록(1 항목) 줄이 있고, 두 로그에 허용 밖 `[Error]` · `[Warning]` 가 없고, 서버가 종료 코드 0 으로 끝난다.
허용 줄: 개발 티켓 키 경고(기본 설정은 게임 접속 티켓을 쓰지 않아 주 키를 두지 않는다).
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
from common.process_group import CheckResult, ProcessGroup, problem_lines, read_log, report

_DEFAULT_CONFIG = "Config/Server/Online/Shooter3D.json"
_PRESET = "Ninja-Debug"  # build/<프리셋>/Bin 의 Server · App 을 쓴다
_GAME = "Shooter3D"
_CLIENT_DELAY_SECONDS = 8.0  # 클라이언트를 띄운 뒤 명령을 보낼 때까지
_TIMEOUT_SECONDS = 60.0  # 서버 기동 · 응답을 기다리는 최대 초
_LISTENING = re.compile(r"Online services listening on port")
_LOGGED_IN = re.compile(r"online login done")
_SERVICE_STATUS = re.compile(r"online service status: \d+ announcements")
_LIVE_EVENTS = re.compile(r"online live events: \d+ events")
_MAIL_LIST = re.compile(r"online mail list: \d+ mails")
_MATCH_FOUND = re.compile(r"online match found")
_SCORE_SUBMITTED = re.compile(r"online score submitted")
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
    if _SCORE_SUBMITTED.search(client_text) is None:
        result.problems.append("the client log has no score submitted line - the leaderboard kit did not take the score")
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
        server = group.start("server", [server_exe, app_arguments.option("game", _GAME), app_arguments.option("server-config", server_config)])
        if not server.wait_for(_LISTENING, _TIMEOUT_SECONDS):
            return group.fail(f"the server never opened its service port (log: {server.log})")
        client = group.start("client", [client_exe, app_arguments.option("game", _GAME), app_arguments.variable("gv_standardInputCommands", "1"),
                                        app_arguments.variable("gv_onlineServerAddress", f"127.0.0.1:{service_port}")])
        time.sleep(_CLIENT_DELAY_SECONDS)  # 클라이언트가 게임 인스턴스를 세우고 표준 입력 명령을 받을 때까지
        try:
            client.send("online.connect")
            time.sleep(2.0)  # Hello 앞의 요청은 세션이 모았다가 보내지만, 연결 실패를 로그인 실패와 가르기 위해 잠깐 둔다
            client.send("shooter.online.servicestatus")  # 로그인 전에 되는 호출이다
            client.wait_for(_SERVICE_STATUS, _TIMEOUT_SECONDS)
            client.send("shooter.online.login")
            if client.wait_for(_LOGGED_IN, _TIMEOUT_SECONDS):
                client.send("shooter.online.liveevents")
                client.wait_for(_LIVE_EVENTS, _TIMEOUT_SECONDS)
                client.send("shooter.online.listmail")
                client.wait_for(_MAIL_LIST, _TIMEOUT_SECONDS)
                client.send("shooter.online.match")
                client.wait_for(_MATCH_FOUND, _TIMEOUT_SECONDS)
                client.send("shooter.online.submitkills 5")
                if client.wait_for(_SCORE_SUBMITTED, _TIMEOUT_SECONDS):
                    client.send("shooter.online.topkills")
                    client.wait_for(_TOP_SCORES, _TIMEOUT_SECONDS)
            time.sleep(1.0)  # 늦게 나는 경고도 잡는다
        except OSError:
            print(f"[online-local] FAIL: the client exited before it took the commands (log: {client.log})")
        client.stop(timeout_seconds=10)
        server.stop(timeout_seconds=20)
    if server.return_code != 0:
        return group.fail(f"the server exited with code {server.return_code} (log: {server.log})")
    return _print(check_logs(server.text(), client.text()), server.log, client.log)


def _print(result: CheckResult, server_log: Path, client_log: Path) -> int:
    return report("online-local", result.problems, [server_log, client_log],
                  "TLS service connection -> service status -> login -> live events -> mailbox -> match -> leaderboard finished with no unexpected errors or warnings")


def main(list_argument: list[str]) -> int:
    parser = argparse.ArgumentParser(prog="py -3 -m Scripts online-local", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="mode", required=True)
    run_parser = sub.add_parser("run", help="서버와 클라이언트를 띄워 로그인 → 우편 목록 → 매칭 → 순위표를 돈다")
    run_parser.add_argument("--server-config", default=_DEFAULT_CONFIG, help="서버 설정(옆 .online.json 과 함께) — 저장소 · 키트 · 제공자를 고른다")
    check_parser = sub.add_parser("check", help="이미 있는 서버 · 클라이언트 로그를 판정한다")
    check_parser.add_argument("server_log")
    check_parser.add_argument("client_log")
    argument = parser.parse_args(list_argument)
    if argument.mode == "check":
        server_log, client_log = Path(argument.server_log), Path(argument.client_log)
        return _print(check_logs(read_log(server_log), read_log(client_log)), server_log, client_log)
    return _run(argument)
