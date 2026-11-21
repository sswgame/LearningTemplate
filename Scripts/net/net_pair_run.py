"""
두 프로세스 온라인 접속 시나리오 — 전용 서버와 클라이언트를 띄워 로그인 → 게임 접속 티켓 → 암호화 UDP 게임 접속을 끝까지 돌린다(OnlineKits F5).

  py -3 -m Scripts net-pair run --preset Ninja-Debug      # 서버 · 클라이언트를 띄워 접속이 열리는지 본다
  py -3 -m Scripts net-pair check <서버 로그> <클라이언트 로그>   # 이미 있는 두 로그만 판정한다
  py -3 -m Scripts net-pair mmo --preset Ninja-Debug      # LanternVale 액션 MMO: 첫 접속 시나리오 → 나감(서버 저장) → 재접속 시나리오(같은 진행)

준비(`run` 이 `Saved/NetPairRun/` 아래에 쓴다): 서버 설정(`NetPair.json` — 게임 포트 · 서비스 포트 · `_bGameConnectTicket`)과 옆 온라인 설정(`NetPair.online.json` —
계정 키트의 개발용 시드 계정 설정 `_listSeedTestAccount`, 게임 접속 조립 `gameConnect`). 서버는 `-server-config=<경로>` 로 그 설정을 읽는다.
계정 시드는 서버 명령줄 인자가 아니라 온라인 설정의 옵션이다(설정이 정본 — 인자를 하나 더 두면 같은 값이 둘이 된다). Shipping 은 그 옵션을 거절하므로 시드 시설은 Dev 에만 있다.

판정: 서버 로그에 `Net session client <n> connected`(암호화 UDP 접속이 열림)가 있고, 두 로그에 `[Error]` · `[Warning]` 가 없고(종료 코드 0 이어도 경고는 실패), 서버가 종료 코드 0 으로 끝난다.
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import time
from pathlib import Path

from common import app_arguments
from common.exit_code import ExitCode
from common.process_group import USER_DATA_ROOT_ENVIRONMENT, CheckResult, ProcessGroup, problem_lines, read_log, report

_GAME = "Playground"
_SERVER_NAME = "netpair"
_ACCOUNT = "netpair_bot"
_PASSWORD = "netpair_pw_1"
_TICKET_KEY_ENVIRONMENT = "SW_NETPAIR_TICKET_KEY"
_TICKET_KEY = "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff"  # 시나리오 전용 임시 키(32 B) — 개발용 키 경고를 피한다
_CONNECTED = re.compile(r"Net session client \d+ connected")
_LISTENING = re.compile(r"Net session listening on UDP port")
_GAME_PORT = 17777  # 서버 게임(UDP) 포트
_SERVICE_PORT = 17780  # 서버 서비스(TCP · 로그인) 포트
_CLIENT_DELAY_SECONDS = 5.0  # 클라이언트를 띄운 뒤 명령을 보낼 때까지
_TIMEOUT_SECONDS = 60.0  # 서버 기동 · 접속을 기다리는 최대 초(mmo 의 클라이언트 하나는 이 네 배까지)


def check_logs(server_text: str, client_text: str) -> CheckResult:
    """서버 · 클라이언트 로그를 판정한다. 규칙은 파일 머리말을 본다."""
    result = CheckResult()
    if _CONNECTED.search(server_text) is None:
        result.problems.append("the server log has no 'Net session client <n> connected' line - the ticket connection never opened")
    result.problems += problem_lines("server", server_text) + problem_lines("client", client_text)
    return result


def write_configs(out_dir: Path, game_port: int, service_port: int, account: str = _ACCOUNT, password: str = _PASSWORD,
                 server_name: str = _SERVER_NAME) -> Path:
    """서버 설정과 온라인 설정을 쓴다. 서버 설정 경로를 돌려준다."""
    server_config = {
        "_listenAddress": "127.0.0.1",
        "_gamePort": game_port,
        "_servicePort": service_port,
        "_bGameConnectTicket": True,
        "_bConsoleInput": True,  # 표준 입력 `quit` 로 드레인 없이 정상 종료한다
        "_listStore": [{"_name": "local", "_driver": "memory"}],  # 드라이버는 필수다 - 메모리도 적는다
        "_listCache": [{"_name": "local", "_driver": "memory"}],
    }
    online_config = {
        "_storeName": "local",
        "_cacheName": "local",
        # 기반 표 TTL 은 필수다(코드에 기본값이 없다). 짧은 짝 실행이라 지우지 않는다(기간 0).
        **{name: {"_expireAfterDays": 0, "_maxScanPerSweep": 1, "_maxDeletePerSweep": 1}
           for name in ("_auditRetention", "_ledgerRetention", "_closedReportTTL", "_mailSentTTL", "_scheduleRunTTL")},
        "_ttlSweepMinuteOfDay": 300,
        "_ttlBackfillRolloutHours": 0,
        "_maxPendingCallsPerService": 1024,
        "_listTrustedProxy": [],
        "_seatReservationSeconds": 30,
        "_listKit": [
            {"_name": "account", "_settings": {
                "_expiredSessionTTL": {"_expireAfterDays": 30, "_maxScanPerSweep": 1000, "_maxDeletePerSweep": 500},
                "_maxConcurrentPasswordHashes": 2,
                "_maxQueuedPasswordHashes": 64,
                "_sharedAttemptLimit": 1000,
                "_sharedAttemptWindowSeconds": 60,
                "_loginNonceLifetimeSeconds": 300,
                "_ticketKeySecretEnvironment": _TICKET_KEY_ENVIRONMENT,
                "_bLightPasswordHash": True,
                "_listSeedTestAccount": [{"_loginName": account, "_password": password}],
            }},
            {"_name": "gameConnect", "_settings": {
                "_ticketKeySecretEnvironment": _TICKET_KEY_ENVIRONMENT,
                "_serverName": server_name,
            }},
        ],
    }
    server_path = out_dir / "NetPair.json"
    server_path.write_text(json.dumps(server_config, indent=4), encoding="utf-8")
    (out_dir / "NetPair.online.json").write_text(json.dumps(online_config, indent=4), encoding="utf-8")
    return server_path


def _run(argument: argparse.Namespace) -> int:
    with ProcessGroup("net-pair", argument.preset, "NetPairRun") as group:
        list_exe = group.require("Server", "App")
        if list_exe is None:
            return int(ExitCode.CHECK_INVALID)
        server_exe, client_exe = list_exe
        server_config = write_configs(group.out_dir, _GAME_PORT, _SERVICE_PORT)
        server = group.start("server", [server_exe, app_arguments.option("game", _GAME), app_arguments.option("server-config", server_config)],
                             environment={_TICKET_KEY_ENVIRONMENT: _TICKET_KEY})
        if not server.wait_for(_LISTENING, _TIMEOUT_SECONDS):
            return group.fail(f"the server never opened its game port (log: {server.log})")
        client = group.start("client", [client_exe, app_arguments.option("game", _GAME), app_arguments.variable("gv_standardInputCommands", "1")])
        time.sleep(_CLIENT_DELAY_SECONDS)  # 클라이언트가 게임 인스턴스를 세우고 표준 입력 명령을 받을 때까지
        try:
            client.send(f"net.ticketconnect 127.0.0.1:{_SERVICE_PORT} {_ACCOUNT} {_PASSWORD} {_SERVER_NAME} 127.0.0.1:{_GAME_PORT}")
        except OSError:
            server.stop(timeout_seconds=10)
            return group.fail(f"the client exited before it took the command (log: {client.log})")
        if server.wait_for(_CONNECTED, _TIMEOUT_SECONDS):
            time.sleep(1.0)  # 접속 뒤 잠깐 두어 늦게 나는 경고도 잡는다
        client.stop(timeout_seconds=10)
        server.stop(timeout_seconds=20)
    if server.return_code != 0:
        return group.fail(f"the server exited with code {server.return_code} (log: {server.log})")
    return _print(check_logs(server.text(), client.text()), server.log, client.log)


# LanternVale 액션 MMO 두 프로세스(T10). 계정 · 서버 이름 · 포트는 클라이언트 시나리오의 `net.ticketconnect` 줄과 같아야 한다(game/lanternvale/automation/netpair/).
_MMO_GAME = "LanternVale"
_MMO_ACCOUNT = "vale_netpair"
_MMO_PASSWORD = "vale_netpair_pw_1"
_MMO_SERVER_NAME = "lanternvale"
_MMO_GAME_PORT = 17877
_MMO_SERVICE_PORT = 17880
_MMO_SCENARIOS = ("game/lanternvale/automation/netpair/first.scenario.xml", "game/lanternvale/automation/netpair/return.scenario.xml")
_MMO_ACCEPTED = re.compile(r"entered the vale as avatar \d+ on connection \d+ \(level (\d+), (new account|stored progress)\)")
_MMO_SAVED = re.compile(r"\[LanternVale\] profile of account \d+ saved")


def check_mmo_logs(server_text: str, list_client_text: list[str]) -> CheckResult:
    """MMO 두 프로세스 로그를 판정한다 — 서버가 첫 접속을 새 계정으로, 재접속을 저장된 진행(레벨 2)으로 들였고 그 사이 진행을 저장했으며, 어느 로그에도 오류 · 경고가 없다."""
    result = CheckResult()
    list_admitted = _MMO_ACCEPTED.findall(server_text)
    if len(list_admitted) != 2:
        result.problems.append(f"the server admitted {len(list_admitted)} sessions - expected the first login and the return")
    else:
        if list_admitted[0][1] != "new account":
            result.problems.append(f"the first session was admitted with '{list_admitted[0][1]}' - the run must start from a fresh in-memory profile")
        if list_admitted[1] != ("2", "stored progress"):
            result.problems.append(f"the return was admitted at level {list_admitted[1][0]} with '{list_admitted[1][1]}' - expected level 2 from the stored progress")
    if _MMO_SAVED.search(server_text) is None:
        result.problems.append("the server log has no 'profile of account N saved' line - the session end did not save")
    result.problems += problem_lines("server", server_text)
    for index, text in enumerate(list_client_text):
        result.problems += problem_lines(f"client {index + 1}", text)
    return result


def _run_mmo(argument: argparse.Namespace) -> int:
    with ProcessGroup("net-pair", argument.preset, "NetPairRun/mmo") as group:
        list_exe = group.require("Server", "App")
        if list_exe is None:
            return int(ExitCode.CHECK_INVALID)
        server_exe, client_exe = list_exe
        server_config = write_configs(group.out_dir, _MMO_GAME_PORT, _MMO_SERVICE_PORT, _MMO_ACCOUNT, _MMO_PASSWORD, _MMO_SERVER_NAME)
        # 서버 진행 저장소는 서버 설정의 온라인 저장소(`_listStore` 의 memory — 저장소 계약 `ServiceStore`)라 실행마다 새 진행으로 시작한다
        # (한 서버 실행 안의 저장 → 재접속 읽기는 같은 저장소를 지난다). 사용자 데이터 폴더도 실행마다 빈 폴더로 옮겨 다른 서버 파일이 섞이지 않게 한다.
        server_user_data = group.out_dir / "server_userdata"
        shutil.rmtree(server_user_data, ignore_errors=True)
        server_user_data.mkdir(parents=True)
        server = group.start("server", [server_exe, app_arguments.option("game", _MMO_GAME), app_arguments.option("server-config", server_config)],
                             environment={_TICKET_KEY_ENVIRONMENT: _TICKET_KEY, USER_DATA_ROOT_ENVIRONMENT: str(server_user_data)})
        if not server.wait_for(_LISTENING, _TIMEOUT_SECONDS):
            return group.fail(f"the server never opened its game port (log: {server.log})")
        list_client = []
        for index, scenario in enumerate(_MMO_SCENARIOS):
            client = group.start(f"client{index + 1}", [client_exe, app_arguments.option("game", _MMO_GAME), app_arguments.option("scenario", scenario),
                                                        app_arguments.flag("unattended")], stdin=False)
            list_client.append(client)
            client.wait(_TIMEOUT_SECONDS * 4)
            if client.return_code != 0:
                server.stop()
                return group.fail(f"client {index + 1} ({scenario}) exited with code {client.return_code} (log: {client.log})")
            time.sleep(1.0)  # 서버가 나간 연결을 알아채고 진행을 저장할 틈
        server.stop()
    if server.return_code != 0:
        return group.fail(f"the server exited with code {server.return_code} (log: {server.log})")
    result = check_mmo_logs(server.text(), [client.text() for client in list_client])
    return report("net-pair", result.problems, [server.log, *(client.log for client in list_client)],
                  "LanternVale login -> combat/gathering -> level 2 -> leave (saved) -> return with the same progress, no errors or warnings")


def _print(result: CheckResult, server_log: Path, client_log: Path) -> int:
    return report("net-pair", result.problems, [server_log, client_log], "login -> game ticket -> encrypted UDP connection opened with no errors or warnings")


def main(list_argument: list[str]) -> int:
    parser = argparse.ArgumentParser(prog="py -3 -m Scripts net-pair", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="mode", required=True)
    run_parser = sub.add_parser("run", help="서버와 클라이언트를 띄워 로그인 → 티켓 → 게임 접속을 돈다")
    run_parser.add_argument("--preset", default="Ninja-Debug", help="빌드 프리셋(build/<프리셋>/Bin 의 Server · App 을 쓴다)")
    mmo_parser = sub.add_parser("mmo", help="LanternVale 서버와 클라이언트 둘(첫 접속 → 재접속)을 차례로 띄워 서버 권위 행동 · 저장 후 재접속을 본다")
    mmo_parser.add_argument("--preset", default="Ninja-Debug", help="빌드 프리셋(build/<프리셋>/Bin 의 Server · App 을 쓴다)")
    check_parser = sub.add_parser("check", help="이미 있는 서버 · 클라이언트 로그를 판정한다")
    check_parser.add_argument("server_log")
    check_parser.add_argument("client_log")
    argument = parser.parse_args(list_argument)
    if argument.mode == "check":
        server_log, client_log = Path(argument.server_log), Path(argument.client_log)
        return _print(check_logs(read_log(server_log), read_log(client_log)), server_log, client_log)
    if argument.mode == "mmo":
        return _run_mmo(argument)
    return _run(argument)
