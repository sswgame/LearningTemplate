"""
파괴 복제의 실제 세션 확인 — 전용 서버와 클라이언트를 한 기계에서 두 프로세스로 띄워 구조 해시 로그를 맞댄다.

  py -3 -m Scripts destruction-net run --preset Ninja-Release          # 서버 + 클라이언트를 띄우고 로그를 맞댄다
  py -3 -m Scripts destruction-net compare <서버 로그> <클라이언트 로그>   # 이미 있는 두 로그만 맞댄다

해시 로그 한 줄은 `DestructionNetSession::formatHashLine` 의 모양이다:
  `[DestructionHash] side=<server|client> id=<번호> events=<사건 수> hash=<16 자리> name=<이름>`
같은 그래프에 같은 사건을 같은 순서로 적용하면 같은 해시이므로, 서버와 클라이언트가 같은 (번호, 사건 수) 에서 같은 해시를 남겨야 한다.
클라이언트가 스냅숏으로 건너뛴 사건 수는 그 줄이 없다 — 서버에만 있는 (번호, 사건 수) 는 틀린 것이 아니다.
틀린 것: (1) 같은 (번호, 사건 수) 의 해시가 다름, (2) 클라이언트에만 있는 (번호, 사건 수), (3) 맞댄 쌍이 하나도 없음.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

from common import app_arguments
from common.exit_code import ExitCode
from common.host_platform import executable_name
from common.paths import get_project_root

_HASH_LINE = re.compile(r"\[DestructionHash\]\s+side=(?P<side>server|client)\s+id=(?P<id>\d+)\s+events=(?P<events>\d+)\s+hash=(?P<hash>[0-9a-fA-F]+)\s+name=(?P<name>.*)$")
_SCENE = "game/empty/maps/destructionnet.scene.xml"
_GAME = "Empty"


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
    for key in sorted(client.by_key):
        client_hash, name = client.by_key[key]
        server_entry = server.by_key.get(key)
        if server_entry is None:
            result.problems.append(f"client has id={key[0]} events={key[1]} ({name}) that the server never logged")
        elif server_entry[0] != client_hash:
            result.problems.append(f"hash mismatch id={key[0]} events={key[1]} ({name}): server {server_entry[0]} client {client_hash}")
        else:
            result.matched += 1
    if result.matched == 0:
        result.problems.append("no (id, events) pair was logged by both sides - nothing was compared")
    return result


def _read_log(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def _print_result(result: CompareResult) -> int:
    for problem in result.problems:
        print(f"[destruction-net] FAIL: {problem}")
    if result.ok:
        print(f"[destruction-net] OK: {result.matched} (id, events) pairs matched between server and client")
        return int(ExitCode.PASSED)
    return int(ExitCode.VIOLATION)


def _run_pair(argument: argparse.Namespace) -> int:
    root = get_project_root()
    bin_dir = root / "build" / argument.preset / "Bin"
    server_exe = bin_dir / executable_name("Server")
    client_exe = bin_dir / executable_name("App")
    for exe in (server_exe, client_exe):
        if not exe.exists():
            print(f"[destruction-net] FAIL: {exe} does not exist - build the '{argument.preset}' preset first")
            return int(ExitCode.VIOLATION)
    out_dir = Path(argument.out) if argument.out else root / "Saved" / "DestructionNetPair"
    out_dir.mkdir(parents=True, exist_ok=True)
    server_log = out_dir / "server.log"
    client_log = out_dir / "client.log"
    port = argument.port
    server_args = [str(server_exe), app_arguments.option("game", _GAME), app_arguments.variable("gv_firstScene", _SCENE), app_arguments.variable("gv_netListenPort", port),
                  app_arguments.variable("gv_serverExitAfterTicks", argument.server_ticks)]
    client_args = [str(client_exe), app_arguments.option("game", _GAME), app_arguments.variable("gv_firstScene", _SCENE), app_arguments.variable("gv_netConnectAddress", f"127.0.0.1:{port}")]
    print(f"[destruction-net] server: {' '.join(server_args)}")
    print(f"[destruction-net] client: {' '.join(client_args)}")
    with server_log.open("wb") as server_out, client_log.open("wb") as client_out:
        server = subprocess.Popen(server_args, cwd=str(bin_dir), stdout=server_out, stderr=subprocess.STDOUT)
        time.sleep(argument.client_delay)  # 서버가 먼저 서야 한다(클라이언트는 시간 초과면 다시 시도하지만, 첫 연결을 빨리 잡는다)
        client = subprocess.Popen(client_args, cwd=str(bin_dir), stdout=client_out, stderr=subprocess.STDOUT)
        try:
            server.wait(timeout=argument.timeout)  # 서버는 틱 상한에 스스로 끝난다
        except subprocess.TimeoutExpired:
            print(f"[destruction-net] FAIL: the server did not finish within {argument.timeout} s")
            server.kill()
            client.kill()
            return int(ExitCode.VIOLATION)
        # 클라이언트는 자동 종료 인자가 없어 서버가 끝난 뒤 닫는다.
        client.terminate()
        try:
            client.wait(timeout=10)
        except subprocess.TimeoutExpired:
            client.kill()
    if server.returncode != 0:
        print(f"[destruction-net] FAIL: the server exited with code {server.returncode} (log: {server_log})")
        return int(ExitCode.VIOLATION)
    client_text = _read_log(client_log)
    # 클라이언트가 콘솔이 아니라 파일 로그만 쓰는 구성이면 `--client-log` 로 그 파일을 준다.
    if argument.client_log:
        client_text += "\n" + _read_log(Path(argument.client_log))
    result = compare_logs(_read_log(server_log), client_text)
    print(f"[destruction-net] logs: {server_log} {client_log}")
    return _print_result(result)


def main(list_argument: list[str]) -> int:
    parser = argparse.ArgumentParser(prog="py -3 -m Scripts destruction-net", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="mode", required=True)
    run_parser = sub.add_parser("run", help="서버와 클라이언트를 띄워 해시를 맞댄다")
    run_parser.add_argument("--preset", default="Ninja-Release", help="빌드 프리셋(build/<프리셋>/Bin 의 Server · App 을 쓴다)")
    run_parser.add_argument("--port", type=int, default=7777, help="서버 UDP 포트")
    run_parser.add_argument("--server-ticks", type=int, default=1800, help="서버가 이 틱 수를 돈 뒤 끝난다(기본 30 틱/초 기준 60 초)")
    run_parser.add_argument("--client-delay", type=float, default=2.0, help="서버를 띄운 뒤 클라이언트를 띄울 때까지 기다리는 초")
    run_parser.add_argument("--timeout", type=float, default=180.0, help="서버가 끝나기를 기다리는 최대 초")
    run_parser.add_argument("--out", default="", help="로그를 둘 폴더(기본 Saved/DestructionNetPair)")
    run_parser.add_argument("--client-log", default="", help="클라이언트가 파일로만 남기는 로그 파일(콘솔 출력에 더해 읽는다)")
    compare_parser = sub.add_parser("compare", help="이미 있는 서버 · 클라이언트 로그를 맞댄다")
    compare_parser.add_argument("server_log")
    compare_parser.add_argument("client_log")
    argument = parser.parse_args(list_argument)
    if argument.mode == "compare":
        return _print_result(compare_logs(_read_log(Path(argument.server_log)), _read_log(Path(argument.client_log))))
    return _run_pair(argument)
