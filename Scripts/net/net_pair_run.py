"""
두 프로세스 온라인 접속 시나리오 — 전용 서버와 클라이언트를 띄워 로그인 → 게임 접속 티켓 → 암호화 UDP 게임 접속을 끝까지 돌린다(OnlineKits F5).

  py -3 -m Scripts net-pair run --preset Ninja-Debug      # 서버 · 클라이언트를 띄워 접속이 열리는지 본다
  py -3 -m Scripts net-pair check <서버 로그> <클라이언트 로그>   # 이미 있는 두 로그만 판정한다
  py -3 -m Scripts net-pair mmo --preset Ninja-Debug      # 허브 서버(Playground): 첫 접속 → 나감(서버 저장) → 재접속(같은 진행) → 두 클라이언트 함께(폰 복제 · 예측, 회선 흉내)
  py -3 -m Scripts net-pair match --preset Ninja-Debug    # 접속한 허브 → 매칭 배정 → 매치 서버 → 서버가 점수 제출 → 허브 재접속(서버 둘 · 클라이언트 하나)
  py -3 -m Scripts net-pair respawn --preset Ninja-Debug  # 두 사람이 한 매치 서버에서 동시에 둔다: 서로 쏘아(서버 판정) 쓰러짐 → 같은 매치에서 다시 태어남(서버 둘 · 클라이언트 둘)

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
import socketserver
import threading
import time
from pathlib import Path

from common import app_arguments
from common.exit_code import ExitCode
from common.process_group import USER_DATA_ROOT_ENVIRONMENT, CheckResult, ChildProcess, ProcessGroup, problem_lines, read_log, report

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


def make_online_config(list_kit: list[dict], server_id: int = 1, topology: str = "SingleServer") -> dict:
    """온라인 설정의 공통 칸(짧은 짝 실행이라 기반 표를 지우지 않는다)에 키트 목록을 더한다. 서버 여럿이 한 저장소 · 캐시를 나누면 `MultiServer` 와 서로 다른 @p server_id 를 준다."""
    return {
        "_topology": topology,
        "_serverID": server_id,
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
        "_listKit": list_kit,
    }


def make_account_kit(accounts: tuple[tuple[str, str], ...]) -> dict:
    """계정 키트 설정(@p accounts 는 시드 계정의 이름 · 비밀번호)."""
    return {"_name": "account", "_settings": {
        "_expiredSessionTTL": {"_expireAfterDays": 30, "_maxScanPerSweep": 1000, "_maxDeletePerSweep": 500},
        "_maxConcurrentPasswordHashes": 2,
        "_maxQueuedPasswordHashes": 64,
        "_sharedAttemptLimit": 1000,
        "_sharedAttemptWindowSeconds": 60,
        "_loginNonceLifetimeSeconds": 300,
        "_ticketKeySecretEnvironment": _TICKET_KEY_ENVIRONMENT,
        "_bLightPasswordHash": True,
        "_listSeedTestAccount": [{"_loginName": account, "_password": password} for account, password in accounts],
    }}


def make_game_connect_kit(server_name: str) -> dict:
    return {"_name": "gameConnect", "_settings": {"_ticketKeySecretEnvironment": _TICKET_KEY_ENVIRONMENT, "_serverName": server_name}}


def write_server(out_dir: Path, name: str, game_port: int, service_port: int, online_config: dict, store: dict, cache: dict) -> Path:
    """서버 설정 `<name>.json` 과 옆 온라인 설정을 쓴다. 서버 설정 경로를 돌려준다."""
    server_config = {
        "_listenAddress": "127.0.0.1",
        "_gamePort": game_port,
        "_servicePort": service_port,
        "_bGameConnectTicket": True,
        "_bConsoleInput": True,  # 표준 입력 `quit` 로 드레인 없이 정상 종료한다
        "_listStore": [{"_name": "local", **store}],  # 드라이버는 필수다 - 메모리도 적는다
        "_listCache": [{"_name": "local", **cache}],
    }
    server_path = out_dir / f"{name}.json"
    server_path.write_text(json.dumps(server_config, indent=4), encoding="utf-8")
    (out_dir / f"{name}.online.json").write_text(json.dumps(online_config, indent=4), encoding="utf-8")
    return server_path


def write_configs(out_dir: Path, game_port: int, service_port: int, accounts: tuple[tuple[str, str], ...] = ((_ACCOUNT, _PASSWORD),),
                 server_name: str = _SERVER_NAME) -> Path:
    """메모리 저장소 하나짜리 서버 설정과 온라인 설정을 쓴다(@p accounts 는 시드 계정의 이름 · 비밀번호). 서버 설정 경로를 돌려준다."""
    online_config = make_online_config([make_account_kit(accounts), make_game_connect_kit(server_name)])
    return write_server(out_dir, "NetPair", game_port, service_port, online_config, {"_driver": "memory"}, {"_driver": "memory"})


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


# Playground 허브 서버 두 프로세스(T10) — 시작 맵이 허브(maps/hub)다. 계정 · 서버 이름 · 포트는 클라이언트 시나리오의 `net.ticketconnect` 줄과 같아야 한다(game/playground/automation/netpair/).
_MMO_GAME = _GAME
_MMO_ACCOUNT = "hub_netpair"
_MMO_PASSWORD = "hub_netpair_pw_1"
_MMO_SERVER_NAME = "playground_hub"
_MMO_GAME_PORT = 17877
_MMO_SERVICE_PORT = 17880
_MMO_SCENARIOS = ("game/playground/automation/netpair/hub_first.scenario.xml", "game/playground/automation/netpair/hub_return.scenario.xml")
# 두 클라이언트를 함께 띄워 폰 복제 · 예측을 보는 시나리오(이름, 계정) — 계정 비밀번호는 `<계정>_pw_1` 이다. 회선 흉내(보내는 쪽)는 두 클라이언트 모두에 건다.
_MMO_PAWN_SCENARIOS = (("game/playground/automation/netpair/hub_walk_north.scenario.xml", "hub_north"),
                       ("game/playground/automation/netpair/hub_walk_south.scenario.xml", "hub_south"))
_MMO_PAWN_EMULATION = {"gv_netEmulationLatencyMs": "60", "gv_netEmulationJitterMs": "10", "gv_netEmulationLossPercent": "3"}
_MMO_ACCEPTED = re.compile(r"entered the hub as avatar \d+ on connection \d+ \(level (\d+), (new account|stored progress)\)")
_MMO_SAVED = re.compile(r"\[Hub\] profile of account \d+ saved")


def check_mmo_logs(server_text: str, list_client_text: list[str]) -> CheckResult:
    """MMO 두 프로세스 로그를 판정한다 — 서버가 첫 접속을 새 계정으로, 재접속을 저장된 진행(레벨 2)으로 들였고 그 사이 진행을 저장했으며, 어느 로그에도 오류 · 경고가 없다."""
    result = CheckResult()
    list_admitted = _MMO_ACCEPTED.findall(server_text)
    expected = 2 + len(_MMO_PAWN_SCENARIOS)
    if len(list_admitted) != expected:
        result.problems.append(f"the server admitted {len(list_admitted)} sessions - expected {expected} (the first login, the return and the walkers)")
    else:
        if list_admitted[0][1] != "new account":
            result.problems.append(f"the first session was admitted with '{list_admitted[0][1]}' - the run must start from a fresh in-memory profile")
        if list_admitted[1] != ("2", "stored progress"):
            result.problems.append(f"the return was admitted at level {list_admitted[1][0]} with '{list_admitted[1][1]}' - expected level 2 from the stored progress")
        result.problems += [f"a walker was admitted with '{entry[1]}' - expected a new account" for entry in list_admitted[2:] if entry[1] != "new account"]
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
        accounts = ((_MMO_ACCOUNT, _MMO_PASSWORD), *((account, f"{account}_pw_1") for _, account in _MMO_PAWN_SCENARIOS))
        server_config = write_configs(group.out_dir, _MMO_GAME_PORT, _MMO_SERVICE_PORT, accounts, _MMO_SERVER_NAME)
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
        # 두 클라이언트를 함께 — 서로의 폰이 걷는 것을 보고, 회선 흉내 아래에서 어긋난 예측이 되돌아온다. 같은 파일을 쓰지 않게 사용자 데이터 폴더를 가른다.
        list_walker = []
        for scenario, account in _MMO_PAWN_SCENARIOS:
            user_data = group.out_dir / f"{account}_userdata"
            user_data.mkdir(parents=True, exist_ok=True)
            list_walker.append(group.start(account, [client_exe, app_arguments.option("game", _MMO_GAME), app_arguments.option("scenario", scenario), app_arguments.flag("unattended"),
                                                     *app_arguments.variables(_MMO_PAWN_EMULATION)],
                                           environment={USER_DATA_ROOT_ENVIRONMENT: str(user_data)}, stdin=False))
        for walker in list_walker:
            walker.wait(_TIMEOUT_SECONDS * 4)
            if walker.return_code != 0:
                server.stop()
                return group.fail(f"{walker.name} exited with code {walker.return_code} (log: {walker.log})")
        list_client += list_walker
        server.stop()
    if server.return_code != 0:
        return group.fail(f"the server exited with code {server.return_code} (log: {server.log})")
    result = check_mmo_logs(server.text(), [client.text() for client in list_client])
    return report("net-pair", result.problems, [server.log, *(client.log for client in list_client)],
                  "hub server login -> combat/gathering -> level 2 -> leave (saved) -> return with the same progress -> two predicted pawns see each other, no errors or warnings")


# 접속한 허브 → 매치 서버 → 허브(서버 둘). 두 서버는 저장소(SQLite 파일 하나)와 캐시(아래 RESP 서버)를 나눠 가진다 - 배정 명단 · 서버 디렉터리 · 순위가 캐시를 지난다.
# 계정 · 포트 · 허브 이름은 클라이언트 시나리오(game/playground/automation/netpair/match_roundtrip) · 온라인 흐름의 개발 계정 · gamesettings onlineHubServerID 와 같아야 한다.
_MATCH_ACCOUNT = ("playground_dev", "playground_dev_pw")
_MATCH_HUB_GAME_PORT, _MATCH_HUB_SERVICE_PORT = 17977, 17980
_MATCH_GAME_PORT, _MATCH_SERVICE_PORT = 17987, 17990
_MATCH_SERVER_ID = 2
_MATCH_MINIGAME = "shooter3d_match"
_MATCH_SCENARIO = "game/playground/automation/netpair/match_roundtrip.scenario.xml"
_MATCH_HUB_ENTERED = re.compile(r"entered the hub as avatar \d+ on connection \d+")
_MATCH_SCORE_REPORTED = re.compile(r"match score of account \d+ reported: arena_kills = [1-9]\d*")
_MATCH_SCORE_STORED = re.compile(r"the game server's score is stored")
_MATCH_RESPAWN_ACCOUNTS = (_MATCH_ACCOUNT, ("playground_dev2", "playground_dev2_pw"))  # 온라인 흐름의 개발 계정 둘
_MATCH_ENTERED = re.compile(r"Minigame 'shooter3d_match' is entered")
_MATCH_DOWNED = re.compile(r"\[Shooter\] you were overrun")
_MATCH_BORN_AGAIN = re.compile(r"\[Shooter\] a player is born again")


class _CacheState:
    """RESP 서버의 값 - 모든 연결이 나눠 가진다(명령 하나가 잠금 하나 안에서 끝난다)."""

    def __init__(self) -> None:
        self.lock = threading.RLock()
        self.values: dict[bytes, bytes] = {}
        self.scores: dict[bytes, dict[bytes, float]] = {}
        self.expires: dict[bytes, float] = {}
        self.channels: dict[bytes, set[_CacheHandler]] = {}
        self.watchers: dict[bytes, set[_CacheHandler]] = {}

    def drop_expired(self, key: bytes) -> None:
        if key in self.expires and time.monotonic() >= self.expires[key]:
            self.remove(key)

    def remove(self, key: bytes) -> bool:
        self.expires.pop(key, None)
        return (self.values.pop(key, None) is not None) | (self.scores.pop(key, None) is not None)

    def touch(self, key: bytes) -> None:
        for handler in self.watchers.pop(key, set()):
            handler.watch_broken = True


class _CacheHandler(socketserver.StreamRequestHandler):
    """RESP2 연결 하나 - `RESPEphemeralStore` 가 보내는 명령만 안다(모르는 명령은 오류로 답해 판정에 드러난다)."""

    state: _CacheState

    def setup(self) -> None:
        super().setup()
        self.watch_broken = False
        self.queue: list[list[bytes]] | None = None
        self.send_lock = threading.Lock()

    def handle(self) -> None:
        try:
            while True:
                command = self._read_command()
                if command is None:
                    return
                with self.state.lock:
                    reply = self._answer(command)
                self.write(reply)
        except (ConnectionError, OSError, ValueError):
            return
        finally:
            with self.state.lock:
                for group in (*self.state.channels.values(), *self.state.watchers.values()):
                    group.discard(self)

    def write(self, data: bytes) -> None:
        with self.send_lock:
            self.wfile.write(data)

    def _read_command(self) -> list[bytes] | None:
        head = self.rfile.readline()
        if not head:
            return None
        list_part = []
        for _ in range(int(head[1:])):
            size = int(self.rfile.readline()[1:])
            list_part.append(self.rfile.read(size + 2)[:size])
        return list_part

    @staticmethod
    def _bulk(value: bytes | None) -> bytes:
        return b"$-1\r\n" if value is None else b"$%d\r\n%s\r\n" % (len(value), value)

    @staticmethod
    def _array(list_item: list[bytes]) -> bytes:
        return b"*%d\r\n" % len(list_item) + b"".join(list_item)

    @staticmethod
    def _number(value: float) -> bytes:
        return b"%d" % int(value)

    def _answer(self, command: list[bytes]) -> bytes:
        name = command[0].upper()
        if self.queue is not None and name not in (b"EXEC", b"MULTI"):
            self.queue.append(command)
            return b"+QUEUED\r\n"
        if name == b"MULTI":
            self.queue = []
            return b"+OK\r\n"
        if name == b"EXEC":
            list_queued, self.queue = self.queue or [], None
            broken, self.watch_broken = self.watch_broken, False
            self._unwatch()
            return b"*-1\r\n" if broken else self._array([self._run(queued) for queued in list_queued])
        if name == b"WATCH":
            self.state.drop_expired(command[1])
            self.state.watchers.setdefault(command[1], set()).add(self)
            return b"+OK\r\n"
        if name == b"UNWATCH":
            self.watch_broken = False
            self._unwatch()
            return b"+OK\r\n"
        return self._run(command)

    def _unwatch(self) -> None:
        for group in self.state.watchers.values():
            group.discard(self)

    def _run(self, command: list[bytes]) -> bytes:
        state, name = self.state, command[0].upper()
        if name == b"PING":
            return self._array([self._bulk(b"pong"), self._bulk(b"")])
        if name in (b"SUBSCRIBE", b"UNSUBSCRIBE"):
            group = state.channels.setdefault(command[1], set())
            group.add(self) if name == b"SUBSCRIBE" else group.discard(self)
            return self._array([self._bulk(name.lower()), self._bulk(command[1]), b":%d\r\n" % len(group)])
        if name == b"PUBLISH":
            list_handler = list(state.channels.get(command[1], set()))
            for handler in list_handler:
                try:
                    handler.write(self._array([self._bulk(b"message"), self._bulk(command[1]), self._bulk(command[2])]))
                except OSError:
                    state.channels[command[1]].discard(handler)
            return b":%d\r\n" % len(list_handler)
        key = command[1]
        state.drop_expired(key)
        if name == b"GET":
            return self._bulk(state.values.get(key))
        if name == b"SET":
            list_option = [option.upper() for option in command[3:]]
            exists = key in state.values or key in state.scores
            if (b"NX" in list_option and exists) or (b"XX" in list_option and not exists):
                return b"$-1\r\n"
            state.remove(key)
            state.values[key] = command[2]
            if b"PX" in list_option:
                state.expires[key] = time.monotonic() + int(command[3 + list_option.index(b"PX") + 1]) / 1000.0
            state.touch(key)
            return b"+OK\r\n"
        if name == b"DEL":
            removed = state.remove(key)
            state.touch(key)
            return b":%d\r\n" % removed
        if name == b"PEXPIRE":
            exists = key in state.values or key in state.scores
            if exists:
                state.expires[key] = time.monotonic() + int(command[2]) / 1000.0
            return b":%d\r\n" % exists
        if name == b"INCRBY":
            value = int(state.values.get(key, b"0")) + int(command[2])
            state.values[key] = b"%d" % value
            state.touch(key)
            return b":%d\r\n" % value
        table = state.scores.get(key, {})
        order = sorted(table, key=lambda member: (table[member], member), reverse=True)  # Valkey 의 ZREV* 차례(같은 점수는 멤버 역순)
        if name in (b"ZADD", b"ZINCRBY"):
            score, member = int(command[2]), command[3]
            added = member not in table
            table[member] = score if name == b"ZADD" else table.get(member, 0) + score
            state.scores[key] = table
            state.touch(key)
            return b":%d\r\n" % added if name == b"ZADD" else self._bulk(self._number(table[member]))
        if name == b"ZREM":
            removed = table.pop(command[2], None) is not None
            if not table:
                state.remove(key)
            state.touch(key)
            return b":%d\r\n" % removed
        if name == b"ZREVRANK":
            return b":%d\r\n" % order.index(command[2]) if command[2] in table else b"$-1\r\n"
        if name == b"ZSCORE":
            return self._bulk(self._number(table[command[2]]) if command[2] in table else None)
        if name == b"ZREVRANGE":
            first, last = int(command[2]), int(command[3])
            list_member = order[first:] if last < 0 else order[first:last + 1]
            return self._array([part for member in list_member for part in (self._bulk(member), self._bulk(self._number(table[member])))])
        return b"-ERR unknown command '%s'\r\n" % name


class _CacheServer(socketserver.ThreadingTCPServer):
    """짝 실행의 캐시 서버(RESP) - 서버 프로세스 둘이 `resp` 드라이버로 나눠 가진다. 블록을 나가면 닫는다."""

    daemon_threads = True

    def __init__(self) -> None:
        super().__init__(("127.0.0.1", 0), type("_BoundCacheHandler", (_CacheHandler,), {"state": _CacheState()}))
        threading.Thread(target=self.serve_forever, daemon=True).start()

    def __exit__(self, *exc: object) -> None:
        self.shutdown()
        super().__exit__(*exc)


def check_match_logs(hub_text: str, match_text: str, client_text: str) -> CheckResult:
    """매치 왕복 로그를 판정한다 - 허브가 같은 계정을 두 번 들였고(처음 · 돌아옴), 매치 서버가 처치가 있는 점수를 내 저장했으며, 어느 로그에도 오류 · 경고가 없다."""
    result = CheckResult()
    if len(_MATCH_HUB_ENTERED.findall(hub_text)) != 2:
        result.problems.append(f"the hub admitted {len(_MATCH_HUB_ENTERED.findall(hub_text))} sessions - expected 2 (the first join and the return from the match)")
    if _CONNECTED.search(match_text) is None:
        result.problems.append("the match server log has no 'Net session client <n> connected' line - the assigned ticket connection never opened")
    if _MATCH_SCORE_REPORTED.search(match_text) is None or _MATCH_SCORE_STORED.search(match_text) is None:
        result.problems.append("the match server log has no reported and stored score with a kill - the server did not submit the player's score")
    result.problems += problem_lines("hub", hub_text) + problem_lines("match", match_text) + problem_lines("client", client_text)
    return result


def _start_match_servers(group: ProcessGroup, server_exe: Path, cache_server: _CacheServer, accounts: tuple[tuple[str, str], ...],
                         match_arguments: tuple[str, ...] = ()) -> list[ChildProcess] | None:
    """허브 서버와 매치 서버를 띄운다(저장소 SQLite 파일 하나 · 캐시 하나를 나눠 가진다). 게임 포트를 열지 못하면 까닭을 찍고 None."""
    store = {"_driver": "sqlite", "_connection": (group.out_dir / "match.sqlite").as_posix()}
    cache = {"_driver": "resp", "_endpoint": f"127.0.0.1:{cache_server.server_address[1]}?prefix=netpair:"}
    content = "game/playground/data/playground"
    directory = {"_name": "directory", "_settings": {"_endedAnnouncementTTL": {"_expireAfterDays": 30, "_maxScanPerSweep": 1000, "_maxDeletePerSweep": 500}}}
    leaderboard = {"_name": "leaderboard", "_settings": {
        "_pastScoreTTL": {"_expireAfterDays": 30, "_maxScanPerSweep": 1000, "_maxDeletePerSweep": 500},
        "_seasonResultTTL": {"_expireAfterDays": 365, "_maxScanPerSweep": 1000, "_maxDeletePerSweep": 500},
        "_catalog": f"{content}.leaderboards.xml"}}

    def matchmaking(game_server: bool) -> dict:
        return {"_name": "matchmaking", "_settings": {"_catalog": f"{content}.matchmodes.xml", "_reconnectGraceMs": 60000, "_cancelConfirmWaitMs": 3000, "_bGameServer": game_server}}

    # 허브는 배정받는 서버가 아니다(`_publicAddress` 없음) - 매치 서버만 디렉터리에 선다.
    hub_online = make_online_config([make_account_kit(accounts), make_game_connect_kit(_MMO_SERVER_NAME), directory, leaderboard, matchmaking(False)], topology="MultiServer")
    hub_config = write_server(group.out_dir, "Hub", _MATCH_HUB_GAME_PORT, _MATCH_HUB_SERVICE_PORT, hub_online, store, cache)
    match_online = make_online_config([make_account_kit(()), make_game_connect_kit(f"{_MATCH_SERVER_ID:016x}"), directory, leaderboard, matchmaking(True)], _MATCH_SERVER_ID, "MultiServer")
    match_online.update({"_publicAddress": "127.0.0.1"})
    match_config = write_server(group.out_dir, "Match", _MATCH_GAME_PORT, _MATCH_SERVICE_PORT, match_online, store, cache)
    list_server = []
    for name, config, list_extra in (("hub", hub_config, []), ("match", match_config, [app_arguments.variable("gv_firstMinigame", _MATCH_MINIGAME), *match_arguments])):
        user_data = group.out_dir / f"{name}_userdata"
        user_data.mkdir()
        server = group.start(name, [server_exe, app_arguments.option("game", _GAME), app_arguments.option("server-config", config), *list_extra],
                             environment={_TICKET_KEY_ENVIRONMENT: _TICKET_KEY, USER_DATA_ROOT_ENVIRONMENT: str(user_data)})
        if not server.wait_for(_LISTENING, _TIMEOUT_SECONDS):
            group.fail(f"the {name} server never opened its game port (log: {server.log})")
            return None
        list_server.append(server)
    return list_server


def check_respawn_logs(hub_text: str, match_text: str, list_client_text: list[str]) -> CheckResult:
    """두 사람 대전 로그를 판정한다 - 매치 서버에 둘이 붙어 한 번씩 쓰러지고 한 번씩 다시 태어났고, 허브가 네 번(들어옴 · 돌아옴) 들였으며, 어느 로그에도 오류 · 경고가 없다."""
    result = CheckResult()
    counts = (("match server connections", len(_CONNECTED.findall(match_text)), 2), ("downed players", len(_MATCH_DOWNED.findall(match_text)), 2),
              ("players born again", len(_MATCH_BORN_AGAIN.findall(match_text)), 2), ("hub admissions", len(_MATCH_HUB_ENTERED.findall(hub_text)), 4))
    result.problems += [f"{name}: {count} - expected {expected}" for name, count, expected in counts if count != expected]
    result.problems += problem_lines("hub", hub_text) + problem_lines("match", match_text)
    for index, text in enumerate(list_client_text):
        result.problems += problem_lines(f"client {index + 1}", text)
    return result


def _run_respawn(argument: argparse.Namespace) -> int:
    with _CacheServer() as cache_server, ProcessGroup("net-pair", argument.preset, "NetPairRun/respawn", clean=True) as group:
        list_exe = group.require("Server", "App")
        if list_exe is None:
            return int(ExitCode.CHECK_INVALID)
        server_exe, client_exe = list_exe
        list_server = _start_match_servers(group, server_exe, cache_server, _MATCH_RESPAWN_ACCOUNTS, (app_arguments.variable("gv_shooterNoEnemies", "1"),))
        if list_server is None:
            return int(ExitCode.VIOLATION)
        hub, match = list_server

        def start_client(name: str) -> ChildProcess:
            user_data = group.out_dir / f"{name}_userdata"
            user_data.mkdir()  # 두 클라이언트가 같은 파일을 쓰지 않게 가른다
            scenario = f"game/playground/automation/netpair/match_respawn_{name}.scenario.xml"
            return group.start(name, [client_exe, app_arguments.option("game", _GAME), app_arguments.option("scenario", scenario), app_arguments.flag("unattended"),
                                      app_arguments.variable("gv_onlineServerAddress", f"127.0.0.1:{_MATCH_HUB_SERVICE_PORT}")],
                               environment={USER_DATA_ROOT_ENVIRONMENT: str(user_data)}, stdin=False)

        first = start_client("first")
        steps_done = first.wait_for(_MATCH_ENTERED, _TIMEOUT_SECONDS * 3)
        if steps_done:
            second = start_client("second")  # 첫 사람이 자리를 쥔 채 둘째가 같은 서버의 빈 자리로 들어간다
            first.wait(_TIMEOUT_SECONDS * 4)
            second.wait(_TIMEOUT_SECONDS * 4)
            time.sleep(1.0)  # 서버가 나간 연결을 알아챌 틈
        for server in list_server:
            server.stop()
    if not steps_done:
        return group.fail(f"the first player did not enter the match server (logs: {group.out_dir})")
    for process in (first, second, *list_server):
        if process.return_code != 0:
            return group.fail(f"{process.name} exited with code {process.return_code} (log: {process.log})")
    result = check_respawn_logs(hub.text(), match.text(), [first.text(), second.text()])
    return report("net-pair", result.problems, [hub.log, match.log, first.log, second.log],
                  "two players in one match server shoot each other down -> born again in the same match -> hub, no errors or warnings")


def _run_match(argument: argparse.Namespace) -> int:
    with _CacheServer() as cache_server, ProcessGroup("net-pair", argument.preset, "NetPairRun/match", clean=True) as group:
        list_exe = group.require("Server", "App")
        if list_exe is None:
            return int(ExitCode.CHECK_INVALID)
        server_exe, client_exe = list_exe
        list_server = _start_match_servers(group, server_exe, cache_server, (_MATCH_ACCOUNT,))
        if list_server is None:
            return int(ExitCode.VIOLATION)
        client = group.start("client", [client_exe, app_arguments.option("game", _GAME), app_arguments.option("scenario", _MATCH_SCENARIO), app_arguments.flag("unattended"),
                                        app_arguments.variable("gv_onlineServerAddress", f"127.0.0.1:{_MATCH_HUB_SERVICE_PORT}")], stdin=False)
        client.wait(_TIMEOUT_SECONDS * 6)
        time.sleep(1.0)  # 서버가 나간 연결을 알아채고 저장할 틈
        for server in list_server:
            server.stop()
    if client.return_code != 0:
        return group.fail(f"the client exited with code {client.return_code} (log: {client.log})")
    for server in list_server:
        if server.return_code != 0:
            return group.fail(f"the {server.name} server exited with code {server.return_code} (log: {server.log})")
    hub, match = list_server
    result = check_match_logs(hub.text(), match.text(), client.text())
    return report("net-pair", result.problems, [hub.log, match.log, client.log],
                  "hub join -> queue -> match server -> server-submitted score -> back in the hub, no errors or warnings")


def _print(result: CheckResult, server_log: Path, client_log: Path) -> int:
    return report("net-pair", result.problems, [server_log, client_log], "login -> game ticket -> encrypted UDP connection opened with no errors or warnings")


def main(list_argument: list[str]) -> int:
    parser = argparse.ArgumentParser(prog="py -3 -m Scripts net-pair", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="mode", required=True)
    run_parser = sub.add_parser("run", help="서버와 클라이언트를 띄워 로그인 → 티켓 → 게임 접속을 돈다")
    run_parser.add_argument("--preset", default="Ninja-Debug", help="빌드 프리셋(build/<프리셋>/Bin 의 Server · App 을 쓴다)")
    mmo_parser = sub.add_parser("mmo", help="허브 서버(Playground)와 클라이언트(첫 접속 → 재접속 → 둘 함께)를 띄워 서버 권위 행동 · 저장 후 재접속 · 폰 복제와 예측을 본다")
    mmo_parser.add_argument("--preset", default="Ninja-Debug", help="빌드 프리셋(build/<프리셋>/Bin 의 Server · App 을 쓴다)")
    match_parser = sub.add_parser("match", help="허브 서버 · 매치 서버 · 클라이언트를 띄워 접속한 허브 → 매치 → 서버 점수 제출 → 허브 재접속을 본다")
    match_parser.add_argument("--preset", default="Ninja-Debug", help="빌드 프리셋(build/<프리셋>/Bin 의 Server · App 을 쓴다)")
    respawn_parser = sub.add_parser("respawn", help="허브 서버 · 매치 서버 · 클라이언트 둘을 띄워 동시 대전의 쓰러짐 → 다시 태어남을 본다")
    respawn_parser.add_argument("--preset", default="Ninja-Debug", help="빌드 프리셋(build/<프리셋>/Bin 의 Server · App 을 쓴다)")
    check_parser = sub.add_parser("check", help="이미 있는 서버 · 클라이언트 로그를 판정한다")
    check_parser.add_argument("server_log")
    check_parser.add_argument("client_log")
    argument = parser.parse_args(list_argument)
    if argument.mode == "check":
        server_log, client_log = Path(argument.server_log), Path(argument.client_log)
        return _print(check_logs(read_log(server_log), read_log(client_log)), server_log, client_log)
    if argument.mode == "mmo":
        return _run_mmo(argument)
    if argument.mode == "match":
        return _run_match(argument)
    if argument.mode == "respawn":
        return _run_respawn(argument)
    return _run(argument)
