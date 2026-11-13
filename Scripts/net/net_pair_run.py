"""
두 프로세스 온라인 접속 시나리오 — 전용 서버와 클라이언트를 띄워 로그인 → 게임 접속 티켓 → 암호화 UDP 게임 접속을 끝까지 돌린다(OnlineKits F5).

  py -3 -m Scripts net-pair run --preset Ninja-Debug      # 서버 · 클라이언트를 띄워 접속이 열리는지 본다
  py -3 -m Scripts net-pair check <서버 로그> <클라이언트 로그>   # 이미 있는 두 로그만 판정한다
  py -3 -m Scripts net-pair mmo --preset Ninja-Debug      # LanternVale 액션 MMO: 첫 접속 시나리오 → 나감(서버 저장) → 재접속 시나리오(같은 진행)
  py -3 -m Scripts net-pair mmo --preset Ninja-Debug      # LanternVale 액션 MMO: 첫 접속 시나리오 → 나감(서버 저장) → 재접속 시나리오(같은 진행)

준비(`run` 이 `Saved/NetPairRun/` 아래에 쓴다): 서버 설정(`NetPair.json` — 게임 포트 · 서비스 포트 · `_bGameConnectTicket`)과 옆 온라인 설정(`NetPair.online.json` —
계정 키트의 개발용 시드 계정 설정 `_listSeedTestAccount`, 게임 접속 조립 `gameConnect`). 서버는 `-server-config=<경로>` 로 그 설정을 읽는다.
계정 시드는 서버 명령줄 인자가 아니라 온라인 설정의 옵션이다(설정이 정본 — 인자를 하나 더 두면 같은 값이 둘이 된다). Shipping 은 그 옵션을 거절하므로 시드 시설은 Dev 에만 있다.

판정: 서버 로그에 `Net session client <n> connected`(암호화 UDP 접속이 열림)가 있고, 두 로그에 `[Error]` · `[Warning]` 가 없고(종료 코드 0 이어도 경고는 실패), 서버가 종료 코드 0 으로 끝난다.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

from common import app_arguments
from common.exit_code import ExitCode
from common.host_platform import executable_name
from common.paths import get_project_root

_GAME = "Empty"
_SERVER_NAME = "netpair"
_ACCOUNT = "netpair_bot"
_PASSWORD = "netpair_pw_1"
_TICKET_KEY_ENVIRONMENT = "SW_NETPAIR_TICKET_KEY"
_USER_DATA_ROOT_ENVIRONMENT = "SW_USER_DATA_ROOT"  # Core/File/UserDataPath.h 의 kUserDataRootVariable — 사용자 데이터 폴더를 옮긴다
_TICKET_KEY = "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff"  # 시나리오 전용 임시 키(32 B) — 개발용 키 경고를 피한다
_CONNECTED = re.compile(r"Net session client \d+ connected")
_LISTENING = re.compile(r"Net session listening on UDP port")
_PROBLEM_LINE = re.compile(r"\[(Error|Warning)\]")


@dataclass
class CheckResult:
    problems: list[str] = field(default_factory=list)

    @property
    def ok(self) -> bool:
        return not self.problems


def check_logs(server_text: str, client_text: str) -> CheckResult:
    """서버 · 클라이언트 로그를 판정한다. 규칙은 파일 머리말을 본다."""
    result = CheckResult()
    if _CONNECTED.search(server_text) is None:
        result.problems.append("the server log has no 'Net session client <n> connected' line - the ticket connection never opened")
    for name, text in (("server", server_text), ("client", client_text)):
        for line in text.splitlines():
            if _PROBLEM_LINE.search(line):
                result.problems.append(f"{name} log: {line.strip()}")
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
        "_listKit": [
            {"_name": "account", "_settings": {
                "_expiredSessionTTL": {"_expireAfterDays": 30, "_maxScanPerSweep": 1000, "_maxDeletePerSweep": 500},
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


def _wait_for_text(log_path: Path, pattern: re.Pattern[str], timeout: float, process: subprocess.Popen) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if pattern.search(log_path.read_text(encoding="utf-8", errors="replace")):
            return True
        if process.poll() is not None:
            return False
        time.sleep(0.25)
    return False


def _stop(process: subprocess.Popen, quit_line: bool, timeout: float) -> None:
    if process.poll() is not None:
        return
    try:
        if quit_line and process.stdin is not None:
            process.stdin.write(b"quit\n")
            process.stdin.flush()
        process.wait(timeout=timeout)
    except (subprocess.TimeoutExpired, OSError):
        process.kill()


def _run(argument: argparse.Namespace) -> int:
    root = get_project_root()
    bin_dir = root / "build" / argument.preset / "Bin"
    server_exe = bin_dir / executable_name("Server")
    client_exe = bin_dir / executable_name("App")
    for exe in (server_exe, client_exe):
        if not exe.exists():
            print(f"[net-pair] FAIL: {exe} does not exist - build the '{argument.preset}' preset first")
            return int(ExitCode.CHECK_INVALID)
    out_dir = Path(argument.out) if argument.out else root / "Saved" / "NetPairRun"
    out_dir.mkdir(parents=True, exist_ok=True)
    server_config = write_configs(out_dir, argument.game_port, argument.service_port)
    server_log = out_dir / "server.log"
    client_log = out_dir / "client.log"
    server_args = [str(server_exe), app_arguments.option("game", _GAME), app_arguments.option("server-config", server_config)]
    client_args = [str(client_exe), app_arguments.option("game", _GAME), app_arguments.variable("gv_standardInputCommands", "1")]
    ticket_command = (f"net.ticketconnect 127.0.0.1:{argument.service_port} {_ACCOUNT} {_PASSWORD} {_SERVER_NAME} "
                     f"127.0.0.1:{argument.game_port}\n")
    print(f"[net-pair] server: {' '.join(server_args)}")
    print(f"[net-pair] client: {' '.join(client_args)}")
    with server_log.open("wb") as server_out, client_log.open("wb") as client_out:
        server_environment = {**os.environ, _TICKET_KEY_ENVIRONMENT: _TICKET_KEY}
        server = subprocess.Popen(server_args, cwd=str(bin_dir), env=server_environment, stdin=subprocess.PIPE, stdout=server_out, stderr=subprocess.STDOUT)
        if not _wait_for_text(server_log, _LISTENING, argument.timeout, server):
            print(f"[net-pair] FAIL: the server never opened its game port (log: {server_log})")
            _stop(server, False, 5)
            return int(ExitCode.VIOLATION)
        client = subprocess.Popen(client_args, cwd=str(bin_dir), stdin=subprocess.PIPE, stdout=client_out, stderr=subprocess.STDOUT)
        time.sleep(argument.client_delay)  # 클라이언트가 게임 인스턴스를 세우고 표준 입력 명령을 받을 때까지
        try:
            client.stdin.write(ticket_command.encode("utf-8"))
            client.stdin.flush()
        except OSError:
            print(f"[net-pair] FAIL: the client exited before it took the command (log: {client_log})")
            _stop(server, True, 10)
            return int(ExitCode.VIOLATION)
        connected = _wait_for_text(server_log, _CONNECTED, argument.timeout, server)
        if connected:
            time.sleep(1.0)  # 접속 뒤 잠깐 두어 늦게 나는 경고도 잡는다
        _stop(client, True, 10)
        _stop(server, True, 20)
    if server.returncode != 0:
        print(f"[net-pair] FAIL: the server exited with code {server.returncode} (log: {server_log})")
        return int(ExitCode.VIOLATION)
    server_text = server_log.read_text(encoding="utf-8", errors="replace")
    client_text = client_log.read_text(encoding="utf-8", errors="replace")
    return _print(check_logs(server_text, client_text), server_log, client_log)


# LanternVale 액션 MMO 두 프로세스(T10). 계정 · 서버 이름 · 포트는 클라이언트 시나리오의 `net.ticketconnect` 줄과 같아야 한다(game/lanternvale/automation/netpair/).
_MMO_GAME = "LanternVale"
_MMO_ACCOUNT = "vale_netpair"
_MMO_PASSWORD = "vale_netpair_pw_1"
_MMO_SERVER_NAME = "lanternvale"
_MMO_GAME_PORT = 17877
_MMO_SERVICE_PORT = 17880
_MMO_SCENARIOS = ("game/lanternvale/automation/netpair/first.scenario.xml", "game/lanternvale/automation/netpair/return.scenario.xml")
_MMO_ADMITTED = re.compile(r"entered the vale as avatar \d+ on connection \d+ \(level (\d+), (new account|stored progress)\)")
_MMO_SAVED = re.compile(r"\[LanternVale\] profile of account \d+ saved")


def check_mmo_logs(server_text: str, list_client_text: list[str]) -> CheckResult:
    """MMO 두 프로세스 로그를 판정한다 — 서버가 첫 접속을 새 계정으로, 재접속을 저장된 진행(레벨 2)으로 들였고 그 사이 진행을 저장했으며, 어느 로그에도 오류 · 경고가 없다."""
    result = CheckResult()
    list_admitted = _MMO_ADMITTED.findall(server_text)
    if len(list_admitted) != 2:
        result.problems.append(f"the server admitted {len(list_admitted)} sessions - expected the first login and the return")
    else:
        if list_admitted[0][1] != "new account":
            result.problems.append(f"the first session was admitted with '{list_admitted[0][1]}' - the run must start from a fresh in-memory profile")
        if list_admitted[1] != ("2", "stored progress"):
            result.problems.append(f"the return was admitted at level {list_admitted[1][0]} with '{list_admitted[1][1]}' - expected level 2 from the stored progress")
    if _MMO_SAVED.search(server_text) is None:
        result.problems.append("the server log has no 'profile of account N saved' line - the session end did not save")
    for name, text in [("server", server_text)] + [(f"client {index + 1}", text) for index, text in enumerate(list_client_text)]:
        for line in text.splitlines():
            if _PROBLEM_LINE.search(line):
                result.problems.append(f"{name} log: {line.strip()}")
    return result


def _run_mmo(argument: argparse.Namespace) -> int:
    root = get_project_root()
    bin_dir = root / "build" / argument.preset / "Bin"
    server_exe = bin_dir / executable_name("Server")
    client_exe = bin_dir / executable_name("App")
    for exe in (server_exe, client_exe):
        if not exe.exists():
            print(f"[net-pair] FAIL: {exe} does not exist - build the '{argument.preset}' preset first")
            return int(ExitCode.CHECK_INVALID)
    out_dir = Path(argument.out) if argument.out else root / "Saved" / "NetPairRun" / "mmo"
    out_dir.mkdir(parents=True, exist_ok=True)
    server_config = write_configs(out_dir, _MMO_GAME_PORT, _MMO_SERVICE_PORT, _MMO_ACCOUNT, _MMO_PASSWORD, _MMO_SERVER_NAME)
    server_log = out_dir / "server.log"
    # 서버 진행 저장소는 배포와 같은 파일 저장소다. 실행마다 새 진행으로 시작하도록 서버의 사용자 데이터 폴더를 이 실행의 빈 폴더로 옮긴다
    # (한 서버 실행 안의 저장 → 재접속 읽기는 같은 저장소 계약을 지난다).
    server_user_data = out_dir / "server_userdata"
    shutil.rmtree(server_user_data, ignore_errors=True)
    server_user_data.mkdir(parents=True)
    server_args = [str(server_exe), app_arguments.option("game", _MMO_GAME), app_arguments.option("server-config", server_config)]
    print(f"[net-pair] server: {' '.join(server_args)}")
    list_client_log: list[Path] = []
    with server_log.open("wb") as server_out:
        server_environment = {**os.environ, _TICKET_KEY_ENVIRONMENT: _TICKET_KEY, _USER_DATA_ROOT_ENVIRONMENT: str(server_user_data)}
        server = subprocess.Popen(server_args, cwd=str(bin_dir), env=server_environment, stdin=subprocess.PIPE, stdout=server_out, stderr=subprocess.STDOUT)
        if not _wait_for_text(server_log, _LISTENING, argument.timeout, server):
            print(f"[net-pair] FAIL: the server never opened its game port (log: {server_log})")
            _stop(server, False, 5)
            return int(ExitCode.VIOLATION)
        for index, scenario in enumerate(_MMO_SCENARIOS):
            client_log = out_dir / f"client{index + 1}.log"
            list_client_log.append(client_log)
            client_args = [str(client_exe), app_arguments.option("game", _MMO_GAME), app_arguments.option("scenario", scenario), app_arguments.flag("unattended")]
            print(f"[net-pair] client {index + 1}: {' '.join(client_args)}")
            with client_log.open("wb") as client_out:
                client = subprocess.Popen(client_args, cwd=str(bin_dir), stdout=client_out, stderr=subprocess.STDOUT)
                try:
                    client.wait(timeout=argument.timeout * 4)
                except subprocess.TimeoutExpired:
                    client.kill()
                    client.wait()
            if client.returncode != 0:
                print(f"[net-pair] FAIL: client {index + 1} ({scenario}) exited with code {client.returncode} (log: {client_log})")
                _stop(server, True, 20)
                return int(ExitCode.VIOLATION)
            time.sleep(1.0)  # 서버가 나간 연결을 알아채고 진행을 저장할 틈
        _stop(server, True, 20)
    if server.returncode != 0:
        print(f"[net-pair] FAIL: the server exited with code {server.returncode} (log: {server_log})")
        return int(ExitCode.VIOLATION)
    server_text = server_log.read_text(encoding="utf-8", errors="replace")
    list_client_text = [log.read_text(encoding="utf-8", errors="replace") for log in list_client_log]
    result = check_mmo_logs(server_text, list_client_text)
    for problem in result.problems:
        print(f"[net-pair] FAIL: {problem}")
    print(f"[net-pair] logs: {server_log} {' '.join(str(log) for log in list_client_log)}")
    if result.ok:
        print("[net-pair] OK: LanternVale login -> combat/gathering -> level 2 -> leave (saved) -> return with the same progress, no errors or warnings")
        return int(ExitCode.PASSED)
    return int(ExitCode.VIOLATION)


def _print(result: CheckResult, server_log: Path, client_log: Path) -> int:
    for problem in result.problems:
        print(f"[net-pair] FAIL: {problem}")
    print(f"[net-pair] logs: {server_log} {client_log}")
    if result.ok:
        print("[net-pair] OK: login -> game ticket -> encrypted UDP connection opened with no errors or warnings")
        return int(ExitCode.PASSED)
    return int(ExitCode.VIOLATION)


def main(list_argument: list[str]) -> int:
    parser = argparse.ArgumentParser(prog="py -3 -m Scripts net-pair", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="mode", required=True)
    run_parser = sub.add_parser("run", help="서버와 클라이언트를 띄워 로그인 → 티켓 → 게임 접속을 돈다")
    run_parser.add_argument("--preset", default="Ninja-Debug", help="빌드 프리셋(build/<프리셋>/Bin 의 Server · App 을 쓴다)")
    run_parser.add_argument("--game-port", type=int, default=17777, help="서버 게임(UDP) 포트")
    run_parser.add_argument("--service-port", type=int, default=17780, help="서버 서비스(TCP · 로그인) 포트")
    run_parser.add_argument("--client-delay", type=float, default=5.0, help="클라이언트를 띄운 뒤 명령을 보낼 때까지 기다리는 초")
    run_parser.add_argument("--timeout", type=float, default=60.0, help="서버 기동 · 접속을 기다리는 최대 초")
    run_parser.add_argument("--out", default="", help="설정 · 로그를 둘 폴더(기본 Saved/NetPairRun)")
    mmo_parser = sub.add_parser("mmo", help="LanternVale 서버와 클라이언트 둘(첫 접속 → 재접속)을 차례로 띄워 서버 권위 행동 · 저장 후 재접속을 본다")
    mmo_parser.add_argument("--preset", default="Ninja-Debug", help="빌드 프리셋(build/<프리셋>/Bin 의 Server · App 을 쓴다)")
    mmo_parser.add_argument("--timeout", type=float, default=60.0, help="서버 기동을 기다리는 최대 초(클라이언트 하나는 이 네 배까지)")
    mmo_parser.add_argument("--out", default="", help="설정 · 로그를 둘 폴더(기본 Saved/NetPairRun/mmo)")
    check_parser = sub.add_parser("check", help="이미 있는 서버 · 클라이언트 로그를 판정한다")
    check_parser.add_argument("server_log")
    check_parser.add_argument("client_log")
    argument = parser.parse_args(list_argument)
    if argument.mode == "check":
        server_log, client_log = Path(argument.server_log), Path(argument.client_log)
        server_text = server_log.read_text(encoding="utf-8", errors="replace")
        client_text = client_log.read_text(encoding="utf-8", errors="replace")
        return _print(check_logs(server_text, client_text), server_log, client_log)
    if argument.mode == "mmo":
        return _run_mmo(argument)
    return _run(argument)
