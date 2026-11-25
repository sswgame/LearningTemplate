"""
여러 프로세스 온라인 접속 시나리오 — 전용 서버와 클라이언트를 띄워 로그인 → 게임 접속 티켓 → 암호화 UDP 게임 접속을 끝까지 돌린다(OnlineKits F5, ctest `Net.mmo` · `Net.match` · `Net.floor` · `Net.respawn`).

  py -3 -m Scripts net-pair mmo --preset Ninja-Release      # 허브 서버(Playground): 첫 접속 → 나감(서버 저장) → 재접속(같은 진행) → 두 클라이언트 함께(폰 복제 · 예측, 회선 흉내)
  py -3 -m Scripts net-pair match --preset Ninja-Release    # 접속한 허브 → 매칭 배정 → 매치 서버 → 서버가 점수 제출 → 허브 재접속(서버 둘 · 클라이언트 하나)
  py -3 -m Scripts net-pair floor --preset Ninja-Release    # 두 사람이 움직이는 바닥 맵의 매치에서 서로 보고 쏜다(서버 둘 · 클라이언트 둘)
  py -3 -m Scripts net-pair respawn --preset Ninja-Release  # 두 사람이 한 매치 서버에서 동시에 둔다: 서로 쏘아(서버 판정) 쓰러짐 → 같은 매치에서 다시 태어남(서버 둘 · 클라이언트 둘)

준비(`Saved/NetPairRun/` 아래에 쓴다): 서버 설정(빈 게임 포트 · 서비스 포트 · `_bGameConnectTicket`)과 옆 온라인 설정(계정 키트의 개발용 시드 계정 설정 `_listSeedTestAccount`, 게임 접속 조립 `gameConnect`).
서버는 `-server-config=<경로>` 로 그 설정을 읽고, 클라이언트 시나리오는 주소를 `{gv_onlineServerAddress}`(로그인) · `{gv_gameServerAddress}`(게임)로 받는다.
계정 시드는 서버 명령줄 인자가 아니라 온라인 설정의 옵션이다(설정이 정본 — 인자를 하나 더 두면 같은 값이 둘이 된다). Shipping 은 그 옵션을 거절하므로 시드 시설은 Dev 에만 있다.

판정: 값은 클라이언트 시나리오(`game/playground/automation/netpair/`)가 감시 값으로 보고, 두 프로세스를 견줄 값은 시나리오의 `<Record>`(보고 JSON)다.
여기는 종료 코드, 서버 사건의 수(로그 id), 모든 로그에 Error · Warning 이 없는지(종료 코드 0 이어도 경고는 실패)를 본다.
"""

from __future__ import annotations

import argparse
import json
import shutil
import socket
import socketserver
import threading
import time
from pathlib import Path

from common.command import Command, ScriptContext
from common import app_arguments
from common.exit_code import ExitCode
from common.log_lines import problem_lines
from common.process_group import DEFAULT_PRESET, NET_LISTENING_ID, USER_DATA_ROOT_ENVIRONMENT, ChildProcess, ProcessGroup, free_port, report
from net.ctest_items import CtestItem

_GAME = "Playground"
_TICKET_KEY_ENVIRONMENT = "SW_NETPAIR_TICKET_KEY"
_TICKET_KEY = "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff"  # 시나리오 전용 임시 키(32 B) — 개발용 키 경고를 피한다
_FOLDER = "game/playground/automation/netpair"
_TIMEOUT_SECONDS = 60.0  # 서버 기동 · 사건을 기다리는 최대 초
_SCENARIO_TIMEOUT_SECONDS = 400.0  # 시나리오 하나가 끝나기를 기다리는 최대 초
_FLOOR_TOLERANCE = 0.3  # 바닥 위에 선 자리(m) - 절대 자리로 보내면 보간 지연만큼 어긋난다
# 서버 사건의 로그 id
_CONNECTED_ID = "Net.ClientConnected"
_DISCONNECTED_ID = "GameNetSession.NetSessionClientDisconnected"
_HUB_ENTERED_ID = "Hub.Admitted"
_CLIENT_LEFT_ID = "GameNetSession.NetSessionClientDisconnected"
_HUB_SAVED_ID = "Hub.ProfileSaved"
_DOWNED_ID = "ShooterPlayer.YouOverrunWaveAfter"
_BORN_AGAIN_ID = "ShooterPlayer.BornAgain"


def _scenario(name: str) -> str:
    return f"{_FOLDER}/{name}.scenario.xml"


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


def _check_counts(process: ChildProcess, *expected: tuple[str, int]) -> list[str]:
    """@p process 의 로그에 id 마다 정한 수만큼 줄이 있는지."""
    return [f"the {process.name} log has {process.count_log(log_id)} '{log_id}' line(s) - expected {count}" for log_id, count in expected if process.count_log(log_id) != count]


def _check_same_spot(rider: ChildProcess, rider_watches: tuple[str, str], viewer: ChildProcess, viewer_watches: tuple[str, str]) -> list[str]:
    """움직이는 바닥 위에 선 쪽의 자기 자리와 다른 쪽이 본 자리(둘 다 바닥 기준 X · Z)가 같은지."""
    own, seen = rider.records(), viewer.records()
    if any(name not in own for name in rider_watches) or any(name not in seen for name in viewer_watches):
        return []  # 기록 앞에서 진 시나리오는 종료 코드가 까닭을 낸다
    own_spot = tuple(own[name] for name in rider_watches)
    seen_spot = tuple(seen[name] for name in viewer_watches)
    if max(abs(a - b) for a, b in zip(own_spot, seen_spot)) > _FLOOR_TOLERANCE:
        return [f"{rider.name} stands at {own_spot} on the moving floor but {viewer.name} shows it at {seen_spot}"]
    return []


def _finish(list_server: list[ChildProcess], list_client: list[ChildProcess], problems: list[str], ok_message: str) -> int:
    for client in list_client:
        problems += client.scenario_problems()
    for server in list_server:
        if server.return_code != 0:
            problems.append(f"the {server.name} server exited with code {server.return_code} (log: {server.log})")
    for process in (*list_server, *list_client):
        problems += problem_lines(process.name, process.text())
    return report("net-pair", problems, [process.log for process in (*list_server, *list_client)], ok_message)


# Playground 허브 서버와 클라이언트(T10) — 시작 맵이 허브(maps/hub)다. 계정 · 서버 이름은 클라이언트 시나리오의 `net.ticketconnect` 줄과 같아야 한다.
_MMO_SERVER_NAME = "playground_hub"
_MMO_ACCOUNT = ("hub_netpair", "hub_netpair_pw_1")
_MMO_SCENARIOS = (_scenario("hub_first"), _scenario("hub_return"))
# 두 클라이언트를 함께 띄워 폰 복제 · 예측을 보는 시나리오(시나리오, 계정) — 계정 비밀번호는 `<계정>_pw_1` 이다. 회선 흉내(보내는 쪽)는 두 클라이언트 모두에 건다.
_MMO_PAWN_CLIENTS = ((_scenario("hub_walk_north"), "hub_north"), (_scenario("hub_walk_south"), "hub_south"))
_MMO_EMULATION = {"gv_netEmulationLatencyMs": "60", "gv_netEmulationJitterMs": "10", "gv_netEmulationLossPercent": "3"}
_MMO_STATUS_ID = "Hub.Status"  # 첫 시나리오의 `hub.status` — 첫 줄은 서버가 하위 레벨에 들어갈 차례, 둘째 줄은 나올 차례다
_MMO_SUBLEVEL_ENTER, _MMO_SUBLEVEL_LEAVE = "minigame.enter cardtable CardTableGate", "minigame.leave"  # 들어가는 것은 서버뿐이다(클라이언트 월드의 진입은 거절된다)


def _run_mmo(argument: argparse.Namespace) -> int:
    with ProcessGroup("net-pair", argument.preset, "NetPairRun/mmo") as group:
        list_exe = group.require("Server", "App")
        if list_exe is None:
            return int(ExitCode.CHECK_INVALID)
        server_exe, client_exe = list_exe
        game_port, service_port = free_port(socket.SOCK_DGRAM), free_port(socket.SOCK_STREAM)
        accounts = (_MMO_ACCOUNT, *((account, f"{account}_pw_1") for _, account in _MMO_PAWN_CLIENTS))
        online_config = make_online_config([make_account_kit(accounts), make_game_connect_kit(_MMO_SERVER_NAME)])
        server_config = write_server(group.out_dir, "Hub", game_port, service_port, online_config, {"_driver": "memory"}, {"_driver": "memory"})
        # 서버 진행 저장소는 서버 설정의 온라인 저장소(`_listStore` 의 memory — 저장소 계약 `ServiceStore`)라 실행마다 새 진행으로 시작한다
        # (한 서버 실행 안의 저장 → 재접속 읽기는 같은 저장소를 지난다). 사용자 데이터 폴더도 실행마다 빈 폴더로 옮겨 다른 서버 파일이 섞이지 않게 한다.
        server_user_data = group.out_dir / "server_userdata"
        shutil.rmtree(server_user_data, ignore_errors=True)
        server_user_data.mkdir(parents=True)
        server = group.start("server", [server_exe, app_arguments.option("game", _GAME), app_arguments.option("server-config", server_config)],
                             environment={_TICKET_KEY_ENVIRONMENT: _TICKET_KEY, USER_DATA_ROOT_ENVIRONMENT: str(server_user_data)})
        if not server.wait_for_log(NET_LISTENING_ID, _TIMEOUT_SECONDS):
            return group.fail(f"the server never opened its game port (log: {server.log})")
        addresses = {"gv_onlineServerAddress": f"127.0.0.1:{service_port}", "gv_gameServerAddress": f"127.0.0.1:{game_port}"}
        list_client = []
        for index, scenario in enumerate(_MMO_SCENARIOS):
            client = group.start_scenario(f"client{index + 1}", client_exe, _GAME, scenario, addresses)
            list_client.append(client)
            if index == 0:
                # 하위 레벨은 서버가 들어가고, 클라이언트가 같은 것을 세웠다고 답한 뒤 나온다. 줄이 나오지 않으면 시나리오가 져서 종료 코드가 알린다.
                for count, line in ((1, _MMO_SUBLEVEL_ENTER), (2, _MMO_SUBLEVEL_LEAVE)):
                    if client.wait_for_log(_MMO_STATUS_ID, _SCENARIO_TIMEOUT_SECONDS, count):
                        server.send_if_running(line)
            client.wait(_SCENARIO_TIMEOUT_SECONDS)
            if client.return_code != 0:
                server.stop()
                return _finish([server], list_client, [], "")
            # 재접속은 앞 진행이 저장된 뒤에 읽는다 — 서버가 나간 연결을 알아챈 사건과 저장한 사건을 기다린다.
            # 두 번째 클라이언트는 진행을 바꾸지 않아 서버가 다시 쓰지 않는다(`HubProfileStore::requestSave` 가 같은 바이트를 건너뛴다) — 저장은 첫 번째 것만 본다.
            for log_id, count in ((_CLIENT_LEFT_ID, index + 1), (_HUB_SAVED_ID, 1)):
                if not server.wait_for_log(log_id, _TIMEOUT_SECONDS, count):
                    server.stop()
                    return _finish([server], list_client, [f"the server log has no '{log_id}' (x{count}) after {client.name} left"], "")
        # 두 클라이언트를 함께 — 서로의 폰이 걷는 것을 보고, 회선 흉내 아래에서 어긋난 예측이 되돌아온다.
        north, south = (group.start_scenario(account, client_exe, _GAME, scenario, {**addresses, **_MMO_EMULATION}) for scenario, account in _MMO_PAWN_CLIENTS)
        for client in (north, south):
            client.wait(_SCENARIO_TIMEOUT_SECONDS)
        server.stop()
    # 접속 넷(처음 · 재접속 · 걷는 둘) — 첫 접속이 새 계정이고 재접속이 저장된 진행이라는 것은 시나리오가 값(레벨 · 경험)으로 본다.
    problems = _check_counts(server, (_HUB_ENTERED_ID, 2 + len(_MMO_PAWN_CLIENTS)))
    problems += _check_same_spot(south, ("Hub.PawnFloorX", "Hub.PawnFloorZ"), north, ("Hub.RemotePawnFloorX", "Hub.RemotePawnFloorZ"))
    return _finish([server], [*list_client, north, south], problems,
                   "hub server login -> combat/gathering -> level 2 -> leave (saved) -> return with the same progress -> two predicted pawns see each other, no errors or warnings")


# 접속한 허브 → 매치 서버 → 허브(서버 둘). 두 서버는 저장소(SQLite 파일 하나)와 캐시(아래 RESP 서버)를 나눠 가진다 - 배정 명단 · 서버 디렉터리 · 순위가 캐시를 지난다.
# 계정 · 허브 이름은 온라인 흐름의 개발 계정 · gamesettings onlineHubServerID 와 같아야 한다.
_MATCH_ACCOUNTS = (("playground_dev", "playground_dev_pw"), ("playground_dev2", "playground_dev2_pw"))
_MATCH_SERVER_ID = 2
_MATCH_MINIGAME = "shooter3d_match"
_MATCH_SCENARIO = _scenario("match_roundtrip")
_FLOOR_MINIGAME = "shooter3d_floormatch"  # 움직이는 바닥 맵의 매치 - 매칭 모드(floor_solo)의 서버 종류가 아레나와 다르다
_FLOOR_SERVER_KIND = "floor"
_RESPAWN_SCENARIOS = (_scenario("match_respawn_first"), _scenario("match_respawn_second"))
_FLOOR_SCENARIOS = (_scenario("match_floor_first"), _scenario("match_floor_second"))

CTEST_ITEMS = (
    CtestItem("mmo", ("net-pair", "mmo"), 1500, (*_MMO_SCENARIOS, *(scenario for scenario, _ in _MMO_PAWN_CLIENTS))),
    CtestItem("match", ("net-pair", "match"), 900, (_MATCH_SCENARIO,)),
    CtestItem("floor", ("net-pair", "floor"), 1200, _FLOOR_SCENARIOS),
    CtestItem("respawn", ("net-pair", "respawn"), 1200, _RESPAWN_SCENARIOS),
)


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


def _start_match_servers(group: ProcessGroup, server_exe: Path, cache_server: _CacheServer, accounts: tuple[tuple[str, str], ...], match_arguments: tuple[str, ...] = (),
                         minigame: str = _MATCH_MINIGAME, server_kind: str = "game") -> tuple[list[ChildProcess], dict[str, str]] | None:
    """허브 서버와 매치 서버를 띄운다(저장소 SQLite 파일 하나 · 캐시 하나를 나눠 가진다). (서버들, 클라이언트에 줄 주소 변수)이고, 게임 포트를 열지 못하면 까닭을 찍고 None."""
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

    hub_port, hub_service_port = free_port(socket.SOCK_DGRAM), free_port(socket.SOCK_STREAM)
    # 허브는 배정받는 서버가 아니다(`_publicAddress` 없음) - 매치 서버만 디렉터리에 선다.
    hub_online = make_online_config([make_account_kit(accounts), make_game_connect_kit(_MMO_SERVER_NAME), directory, leaderboard, matchmaking(False)], topology="MultiServer")
    hub_config = write_server(group.out_dir, "Hub", hub_port, hub_service_port, hub_online, store, cache)
    match_online = make_online_config([make_account_kit(()), make_game_connect_kit(f"{_MATCH_SERVER_ID:016x}"), directory, leaderboard, matchmaking(True)], _MATCH_SERVER_ID, "MultiServer")
    match_online.update({"_publicAddress": "127.0.0.1", "_serverKind": server_kind})
    match_config = write_server(group.out_dir, "Match", free_port(socket.SOCK_DGRAM), free_port(socket.SOCK_STREAM), match_online, store, cache)
    list_server = []
    for name, config, list_extra in (("hub", hub_config, []), ("match", match_config, [app_arguments.variable("gv_firstMinigame", minigame), *match_arguments])):
        user_data = group.out_dir / f"{name}_userdata"
        user_data.mkdir()
        server = group.start(name, [server_exe, app_arguments.option("game", _GAME), app_arguments.option("server-config", config), *list_extra],
                             environment={_TICKET_KEY_ENVIRONMENT: _TICKET_KEY, USER_DATA_ROOT_ENVIRONMENT: str(user_data)})
        if not server.wait_for_log(NET_LISTENING_ID, _TIMEOUT_SECONDS):
            group.fail(f"the {name} server never opened its game port (log: {server.log})")
            return None
        list_server.append(server)
    return list_server, {"gv_onlineServerAddress": f"127.0.0.1:{hub_service_port}", "gv_gameServerAddress": f"127.0.0.1:{hub_port}"}


def _stop_servers(list_server: list[ChildProcess]) -> None:
    """모든 클라이언트가 나간 뒤 부른다 — 서버가 접속마다 끊김을 알아챈(진행 · 점수를 저장하는 자리) 뒤에 멈춘다."""
    for server in list_server:
        server.wait_for_log(_DISCONNECTED_ID, _TIMEOUT_SECONDS, server.count_log(_CONNECTED_ID))
        server.stop()


def _run_match(argument: argparse.Namespace) -> int:
    with _CacheServer() as cache_server, ProcessGroup("net-pair", argument.preset, "NetPairRun/match", clean=True) as group:
        list_exe = group.require("Server", "App")
        if list_exe is None:
            return int(ExitCode.CHECK_INVALID)
        started = _start_match_servers(group, list_exe[0], cache_server, _MATCH_ACCOUNTS[:1])
        if started is None:
            return int(ExitCode.VIOLATION)
        list_server, addresses = started
        client = group.start_scenario("client", list_exe[1], _GAME, _MATCH_SCENARIO, addresses)
        client.wait(_SCENARIO_TIMEOUT_SECONDS)
        _stop_servers(list_server)
    hub, match = list_server
    problems = _check_counts(hub, (_HUB_ENTERED_ID, 2))  # 처음 · 매치에서 돌아옴
    problems += _check_counts(match, (_CONNECTED_ID, 1))
    problems += [f"the match server log has no '{log_id}' line" for log_id in ("Shooter.MatchScoreReported", "Leaderboard.ServerScoreStored") if match.count_log(log_id) == 0]  # 점수의 값(처치 1 이상)은 시나리오가 순위표에서 본다
    return _finish(list_server, [client], problems, "hub join -> queue -> match server -> server-submitted score -> back in the hub, no errors or warnings")


def _run_two_players(argument: argparse.Namespace, mode: str, scenarios: tuple[str, str], minigame: str, server_kind: str, match_arguments: tuple[str, ...],
                     match_counts: tuple[tuple[str, int], ...], first_watches: tuple[str, str] | None, second_watches: tuple[str, str] | None, ok_message: str) -> int:
    """허브 · 매치 서버와 두 클라이언트를 돌린다 — 첫 사람이 매치에 들어간 뒤 둘째가 같은 서버의 빈 자리로 들어간다. 바닥 감시 값을 주면 첫 사람의 자기 자리와 둘째가 본 자리를 견준다."""
    with _CacheServer() as cache_server, ProcessGroup("net-pair", argument.preset, f"NetPairRun/{mode}", clean=True) as group:
        list_exe = group.require("Server", "App")
        if list_exe is None:
            return int(ExitCode.CHECK_INVALID)
        started = _start_match_servers(group, list_exe[0], cache_server, _MATCH_ACCOUNTS, match_arguments, minigame, server_kind)
        if started is None:
            return int(ExitCode.VIOLATION)
        list_server, addresses = started
        first = group.start_scenario("first", list_exe[1], _GAME, scenarios[0], addresses)
        list_client = [first]
        if first.wait_for_log("MinigameFlow.Entered", _SCENARIO_TIMEOUT_SECONDS):
            list_client.append(group.start_scenario("second", list_exe[1], _GAME, scenarios[1], addresses))
        for client in list_client:
            client.wait(_SCENARIO_TIMEOUT_SECONDS)
        _stop_servers(list_server)
    if len(list_client) == 1:
        return _finish(list_server, list_client, ["the first player did not enter the match server"], "")
    hub, match = list_server
    problems = _check_counts(hub, (_HUB_ENTERED_ID, 4)) + _check_counts(match, (_CONNECTED_ID, 2), *match_counts)  # 허브: 두 사람의 들어옴 · 돌아옴
    if first_watches is not None and second_watches is not None:
        problems += _check_same_spot(list_client[0], first_watches, list_client[1], second_watches)
    return _finish(list_server, list_client, problems, ok_message)


def _run_respawn(argument: argparse.Namespace) -> int:
    return _run_two_players(argument, "respawn", _RESPAWN_SCENARIOS, _MATCH_MINIGAME, "game", (app_arguments.variable("gv_shooterNoEnemies", 1),),
                            ((_DOWNED_ID, 2), (_BORN_AGAIN_ID, 2)), None, None,
                            "two players in one match server shoot each other down -> born again in the same match -> hub, no errors or warnings")


def _run_floor(argument: argparse.Namespace) -> int:
    return _run_two_players(argument, "floor", _FLOOR_SCENARIOS, _FLOOR_MINIGAME, _FLOOR_SERVER_KIND, (),
                            ((_DOWNED_ID, 1), (_BORN_AGAIN_ID, 1)), ("Shooter.FloorX", "Shooter.FloorZ"), ("Shooter.RemoteFloorX", "Shooter.RemoteFloorZ"),
                            "two players ride a moving floor, see each other at the same spot on it, and one shoots the other down while riding, no errors or warnings")


_MODES = {"mmo": _run_mmo, "match": _run_match, "floor": _run_floor, "respawn": _run_respawn}


class NetPairCommand(Command):
    name = "net-pair"
    help = "두 프로세스 온라인 접속 시나리오 — 서버 · 클라이언트로 로그인 → 게임 접속 티켓 → 암호화 UDP 접속(mmo · match · floor · respawn)"

    def add_arguments(self, parser: argparse.ArgumentParser) -> None:
        parser.add_argument("mode", choices=tuple(_MODES))
        parser.add_argument("--preset", default=DEFAULT_PRESET, help="빌드 프리셋(build/<프리셋>/Bin 의 Server · App 을 쓴다)")

    def run(self, context: ScriptContext, args: argparse.Namespace) -> int:
        return _MODES[args.mode](args)
