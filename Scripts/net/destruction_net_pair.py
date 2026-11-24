"""
파괴 복제의 실제 세션 확인 — 전용 서버와 클라이언트를 한 기계에서 두 프로세스로 띄워 구조 해시 로그를 맞댄다.

  py -3 -m Scripts destruction-net run --preset Ninja-Release          # 서버 + 클라이언트를 띄우고 로그를 맞댄다
  py -3 -m Scripts destruction-net compare <서버 로그> <클라이언트 로그>   # 이미 있는 두 로그만 맞댄다

해시 로그 한 줄은 `DestructionNetSession::formatHashLine` 의 모양이다:
  `[DestructionHash] side=<server|client> id=<번호> events=<사건 수> hash=<16 자리> name=<이름>`
같은 그래프에 같은 사건을 같은 순서로 적용하면 같은 해시이므로, 서버와 클라이언트가 같은 (번호, 사건 수) 에서 같은 해시를 남겨야 한다.
줄은 갱신마다 하나다 — 서버는 한 틱에 난 사건 여럿을 모두 적용한 뒤 한 줄을 남기고, 클라이언트는 그 사건들을 패킷 · 프레임이 가른 만큼 나눠 적용한다. 그래서 한쪽에만 있는 중간 사건 수는 틀린 것이 아니다.
틀린 것: (1) 같은 (번호, 사건 수) 의 해시가 다름, (2) 클라이언트가 서버가 낸 적 없는 사건을 적용함(그 번호의 서버 마지막 사건 수를 넘거나 서버에 없는 번호), (3) 클라이언트가 아는 번호의 마지막 상태가 서버와 다름
(서버의 마지막 사건 수에 닿지 않았다), (4) 맞댄 쌍이 하나도 없음.
"""

from __future__ import annotations

import argparse
import re
import time
from dataclasses import dataclass, field
from pathlib import Path

from common import app_arguments
from common.exit_code import ExitCode
from common.process_group import ProcessGroup, read_log, report

_HASH_LINE = re.compile(r"\[DestructionHash\]\s+side=(?P<side>server|client)\s+id=(?P<id>\d+)\s+events=(?P<events>\d+)\s+hash=(?P<hash>[0-9a-fA-F]+)\s+name=(?P<name>.*)$")
_SCENE = "game/playground/maps/destructionnet.scene.xml"
_GAME = "Playground"
_PORT = 7777  # 서버 UDP 포트
_SERVER_TICKS = 1800  # 서버가 이 틱 수를 돈 뒤 끝난다(30 틱/초 기준 60 초)
_CLIENT_DELAY_SECONDS = 2.0  # 서버를 띄운 뒤 클라이언트를 띄울 때까지
_TIMEOUT_SECONDS = 180.0  # 서버가 끝나기를 기다리는 최대 초


@dataclass
class HashRecords:
    """한쪽 로그의 해시 기록 — `(번호, 사건 수) → (해시, 이름)`."""

    by_key: dict[tuple[int, int], tuple[str, str]] = field(default_factory=dict)
    conflicting_keys: list[tuple[int, int]] = field(default_factory=list)


def parse_hash_log(text: str, side: str) -> HashRecords:
    """`side` 줄만 읽는다. 같은 (번호, 사건 수) 가 다른 해시로 두 번 나오면 그 쪽 로그가 스스로 어긋난 것이라 따로 모은다."""
    records = HashRecords()
    for line in text.splitlines():
        match = _HASH_LINE.search(line)
        if match is None or match.group("side") != side:
            continue
        key = (int(match.group("id")), int(match.group("events")))
        value = (match.group("hash").lower(), match.group("name").strip())
        previous = records.by_key.get(key)
        if previous is not None and previous[0] != value[0]:
            records.conflicting_keys.append(key)
        records.by_key[key] = value
    return records


def last_event_counts(records: HashRecords) -> dict[int, int]:
    """번호마다 마지막(가장 큰) 사건 수."""
    last: dict[int, int] = {}
    for net_id, events in records.by_key:
        last[net_id] = max(last.get(net_id, -1), events)
    return last


@dataclass
class CompareResult:
    matched: int = 0
    problems: list[str] = field(default_factory=list)

    @property
    def ok(self) -> bool:
        return self.matched > 0 and not self.problems


def compare_logs(server_text: str, client_text: str) -> CompareResult:
    """서버 로그와 클라이언트 로그를 맞댄다. 규칙은 파일 머리말을 본다."""
    server = parse_hash_log(server_text, "server")
    client = parse_hash_log(client_text, "client")
    result = CompareResult()
    for key in server.conflicting_keys:
        result.problems.append(f"server log repeats id={key[0]} events={key[1]} with different hashes")
    for key in client.conflicting_keys:
        result.problems.append(f"client log repeats id={key[0]} events={key[1]} with different hashes")
    server_last = last_event_counts(server)
    client_last = last_event_counts(client)
    for key in sorted(client.by_key):
        client_hash, name = client.by_key[key]
        server_entry = server.by_key.get(key)
        if server_entry is None:
            if key[1] > server_last.get(key[0], -1):
                result.problems.append(f"client has id={key[0]} events={key[1]} ({name}) that the server never reached (last {server_last.get(key[0], 'none')})")
        elif server_entry[0] != client_hash:
            result.problems.append(f"hash mismatch id={key[0]} events={key[1]} ({name}): server {server_entry[0]} client {client_hash}")
        else:
            result.matched += 1
    for net_id, events in sorted(client_last.items()):
        if net_id in server_last and events < server_last[net_id]:
            result.problems.append(f"client stopped at events={events} of id={net_id} but the server reached {server_last[net_id]}")
    if result.matched == 0:
        result.problems.append("no (id, events) pair was logged by both sides - nothing was compared")
    return result


def _print_result(result: CompareResult, logs: list[Path]) -> int:
    return report("destruction-net", result.problems, logs, f"{result.matched} (id, events) pairs matched between server and client")


def _run_pair(argument: argparse.Namespace) -> int:
    with ProcessGroup("destruction-net", argument.preset, "DestructionNetPair") as group:
        list_exe = group.require("Server", "App")
        if list_exe is None:
            return int(ExitCode.CHECK_INVALID)
        server_exe, client_exe = list_exe
        scene = app_arguments.variable("gv_firstScene", _SCENE)
        server = group.start("server", [server_exe, app_arguments.option("game", _GAME), scene, app_arguments.variable("gv_netListenPort", _PORT),
                                        app_arguments.variable("gv_serverExitAfterTicks", _SERVER_TICKS)], stdin=False)
        time.sleep(_CLIENT_DELAY_SECONDS)  # 서버가 먼저 서야 한다(클라이언트는 시간 초과면 다시 시도하지만, 첫 연결을 빨리 잡는다)
        # 정상 종료(`quit`)로 닫는다 - 프로세스를 끊으면 아직 파일에 내리지 않은 로그 끝(마지막 사건들)이 사라진다.
        client = group.start("client", [client_exe, app_arguments.option("game", _GAME), scene, app_arguments.variable("gv_standardInputCommands", "1"),
                                        app_arguments.variable("gv_netConnectAddress", f"127.0.0.1:{_PORT}")])
        if not server.wait(_TIMEOUT_SECONDS):  # 서버는 틱 상한에 스스로 끝난다
            return group.fail(f"the server did not finish within {_TIMEOUT_SECONDS} s")
        client.stop()  # 클라이언트는 자동 종료 인자가 없어 서버가 끝난 뒤 닫는다
    if server.return_code != 0:
        return group.fail(f"the server exited with code {server.return_code} (log: {server.log})")
    return _print_result(compare_logs(server.text(), client.text()), [server.log, client.log])


def main(list_argument: list[str]) -> int:
    parser = argparse.ArgumentParser(prog="py -3 -m Scripts destruction-net", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="mode", required=True)
    run_parser = sub.add_parser("run", help="서버와 클라이언트를 띄워 해시를 맞댄다")
    run_parser.add_argument("--preset", default="Ninja-Release", help="빌드 프리셋(build/<프리셋>/Bin 의 Server · App 을 쓴다)")
    compare_parser = sub.add_parser("compare", help="이미 있는 서버 · 클라이언트 로그를 맞댄다")
    compare_parser.add_argument("server_log")
    compare_parser.add_argument("client_log")
    argument = parser.parse_args(list_argument)
    if argument.mode == "compare":
        server_log, client_log = Path(argument.server_log), Path(argument.client_log)
        return _print_result(compare_logs(read_log(server_log), read_log(client_log)), [server_log, client_log])
    return _run_pair(argument)
