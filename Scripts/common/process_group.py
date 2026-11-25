"""
여러 프로세스(전용 서버 · 게임 클라이언트 · 호스트)를 한 번에 띄워 시나리오를 돌리는 묶음 실행기 — 온라인 · 락스텝 · 파괴 복제 · 텔레메트리 실행기가 이것 위에 선다.

언리얼 Gauntlet(서버 · 클라이언트 역할을 띄우고 로그를 모아 판정)과 유니티 Multiplayer Play Mode 의 자리다. 실행기마다 손으로 짜던 다섯 가지를 여기 하나로 둔다:
- 빌드 산출물 찾기: `build/<프리셋>/Bin` 의 실행 파일, 없으면 "그 프리셋을 먼저 지어라" 로 멈춘다(`require`).
- 띄우기: 표준 출력 · 오류를 프로세스마다 로그 파일 하나로, 표준 입력은 개발 명령 줄용 파이프로(`start`).
- 시나리오: 클라이언트를 시나리오로 띄우고(`start_scenario`) 값 판정은 시나리오가 한다 — 실행기는 종료 코드 · 보고 JSON(`scenario_problems` · `records`)만 본다.
- 기다리기: 로그에 그 id 의 줄이 나오거나 프로세스가 끝날 때까지, 시간 제한과 함께(`ChildProcess.wait_for_log` — 글 정규식으로 기다리는 길은 없다).
- 포트: 빈 포트를 받는다(`free_port`) — 고정 포트는 나란히 도는 실행 · 사람의 서버와 겹친다.
- 멈추기: 표준 입력 `quit` 로 정상 종료를 청하고, 시간을 넘기면 죽인다. 묶음을 나갈 때 남은 프로세스는 모두 멈춘다(`with` 블록).
- 판정 출력: `[태그] FAIL: …` · 로그 경로 · OK 한 줄 · 종료 코드(`report`). 로그 줄 읽기(id · 수준 · JSON 본문)는 `common/log_lines.py`.
"""

from __future__ import annotations

import json
import os
import shutil
import socket
import subprocess
import time
from pathlib import Path
from typing import Mapping, Sequence

from common import app_arguments
from common.exit_code import ExitCode
from common.host_platform import executable_name
from common.log_lines import count_id, read_log
from common.paths import get_bin_dir, get_project_root

NET_LISTENING_ID = "GameNetSession.NetSessionListeningUDP"  # 서버(리슨 호스트)가 게임 UDP 포트를 열었다
DEFAULT_PRESET = "Ninja-Release"  # 손으로 부를 때의 build/<프리셋>/Bin (ctest 는 제 빌드 폴더를 준다 — Test/Net)
USER_DATA_ROOT_ENVIRONMENT = "SW_USER_DATA_ROOT"  # Core/File/UserDataPath.h 의 kUserDataRootVariable — 띄운 App · Server 의 사용자 데이터 폴더를 옮긴다
_POLL_SECONDS = 0.25  # 로그 줄을 다시 읽는 간격


def free_port(kind: int) -> int:
    """지금 비어 있는 루프백 포트(@p kind 는 `socket.SOCK_STREAM` · `SOCK_DGRAM`)."""
    with socket.socket(socket.AF_INET, kind) as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


class ChildProcess:
    """
    묶음이 띄운 프로세스 하나 — 이름(로그 · 메시지에 쓴다), 로그 파일, 표준 입력 파이프, 시나리오 보고(시나리오로 띄웠을 때).

    같은 묶음의 다른 프로세스가 뜻밖에 끝나면(`find_lost_peer`) 기다리기는 곧바로 그친다 — 상대가 죽은 실행은 어차피 졌는데 남은 쪽이 제 시한(몇 분)을 다 쓰지 않게 한다.
    """

    def __init__(self, name: str, process: subprocess.Popen, log: Path, report_path: Path | None = None, list_peer: Sequence[ChildProcess] = ()) -> None:
        self.name = name
        self.process = process
        self.log = log
        self.report_path = report_path
        self.stop_requested = False              # 실행기가 멈추게 했다 — 그 뒤의 끝남은 뜻밖이 아니다
        self.lost_peer: ChildProcess | None = None   # 이 프로세스를 멈추게 한, 먼저 끝난 상대
        self._list_peer = list_peer

    @property
    def return_code(self) -> int | None:
        return self.process.returncode

    def ended_early(self) -> bool:
        """실행기가 멈추지 않았는데 끝났다 — 시나리오는 지거나 크래시한 것(종료 코드 0 이 아님), 시나리오 없는 프로세스(서버)는 끝난 것 자체다."""
        code = self.process.poll()
        return code is not None and not self.stop_requested and (code != 0 or self.report_path is None)

    def find_lost_peer(self) -> ChildProcess | None:
        """같은 묶음에서 뜻밖에 끝난 다른 프로세스. 없으면 None."""
        return next((peer for peer in self._list_peer if peer is not self and peer.ended_early()), None)

    def text(self) -> str:
        return read_log(self.log)

    def count_log(self, log_id: str) -> int:
        return count_id(self.text(), log_id)

    def send(self, line: str) -> None:
        """표준 입력으로 개발 명령 한 줄을 보낸다. 프로세스가 이미 끝났으면 OSError 다."""
        if self.process.stdin is None:
            raise OSError(f"{self.name}: 표준 입력 파이프 없이 띄웠다")
        self.process.stdin.write((line + "\n").encode("utf-8"))
        self.process.stdin.flush()

    def send_if_running(self, line: str) -> None:
        """다른 프로세스의 사건을 잇는 명령 — 받는 쪽이 이미 끝났으면 아무것도 하지 않는다(그 프로세스의 종료 코드가 판정한다)."""
        try:
            self.send(line)
        except OSError:
            pass

    def wait_for_log(self, log_id: str, timeout_seconds: float, count: int = 1, *, while_running: ChildProcess | None = None) -> bool:
        """로그에 id 가 @p log_id 인 줄이 @p count 개 나오면 참, 이 프로세스(또는 @p while_running)가 먼저 끝나거나 묶음의 다른 프로세스가 뜻밖에 끝나거나 시간을 넘기면 거짓."""
        deadline = time.monotonic() + timeout_seconds
        while time.monotonic() < deadline:
            exited = any(process.process.poll() is not None for process in (self, while_running) if process is not None) or self.find_lost_peer() is not None  # 끝난 뒤 마지막 줄까지 읽고 정한다
            if self.count_log(log_id) >= count:
                return True
            if exited:
                return False
            time.sleep(_POLL_SECONDS)
        return False

    def scenario_report(self) -> dict:
        """시나리오 보고 JSON(`-scenario-report`). 못 읽으면 빈 보고."""
        try:
            return json.loads(self.report_path.read_text(encoding="utf-8")) if self.report_path is not None else {}
        except (OSError, json.JSONDecodeError):
            return {}

    def records(self) -> dict[str, float]:
        """시나리오의 `<Record watch>` 값들(보고의 `records[]` — `frame N <감시 값> = <수>`). 감시 값 이름 → 마지막 값."""
        values = {}
        for line in self.scenario_report().get("records", []):
            head, _, value = line.rpartition(" = ")
            values[head.split(" ", 2)[2]] = float(value)
        return values

    def scenario_problems(self) -> list[str]:
        """시나리오로 띄운 프로세스가 통과하지 않은 까닭(통과면 빈 목록)."""
        if self.return_code == 0:
            return []
        if self.lost_peer is not None:
            peer = self.lost_peer
            return [f"{self.name} was stopped because {peer.name} ended first with code {peer.return_code} (log: {peer.log})"]
        failure = self.scenario_report().get("failure") or {}
        return [f"{self.name} exited with code {self.return_code}: {failure.get('kind', 'no report')} {failure.get('reason', '')} at {failure.get('step', '?')} (log: {self.log})"]

    def wait(self, timeout_seconds: float) -> bool:
        """스스로 끝나기를 기다린다. 묶음의 다른 프로세스가 뜻밖에 끝나면 이쪽도 멈추고(`lost_peer`) 거짓, 시간을 넘기면 죽이고 거짓."""
        deadline = time.monotonic() + timeout_seconds
        while time.monotonic() < deadline:
            try:
                self.process.wait(timeout=_POLL_SECONDS)
                return True
            except subprocess.TimeoutExpired:
                self.lost_peer = self.find_lost_peer()
            if self.lost_peer is not None:
                self.stop(quit_line=False, timeout_seconds=10)
                return False
        self.stop_requested = True
        self.process.kill()
        self.process.wait()
        return False

    def stop(self, quit_line: bool = True, timeout_seconds: float = 20.0) -> None:
        """정상 종료를 청한다(@p quit_line 이면 표준 입력 `quit`, 아니면 terminate). 시간을 넘기면 죽인다."""
        if self.process.poll() is not None:
            return
        self.stop_requested = True
        try:
            if quit_line and self.process.stdin is not None:
                self.send("quit")
            else:
                self.process.terminate()
            self.process.wait(timeout=timeout_seconds)
        except (subprocess.TimeoutExpired, OSError):
            self.process.kill()
            self.process.wait()


class ProcessGroup:
    """
    한 실행의 프로세스 묶음 — `with ProcessGroup(...) as group:` 안에서 띄우고, 블록을 나가면 남은 프로세스를 멈추고 로그 파일을 닫는다.

    - `tag`      : 메시지 머리(`[net-pair]`).
    - `preset`   : 실행 파일을 찾는 빌드 프리셋(`build/<프리셋>/Bin`).
    - `out_name` : 로그 · 설정을 쓰는 폴더(`Saved/<out_name>`). `clean` 이면 비우고 시작한다.
    """

    def __init__(self, tag: str, preset: str, out_name: str, *, clean: bool = False) -> None:
        self.tag = tag
        self.preset = preset
        self.bin_dir = get_bin_dir(preset)
        self.out_dir = get_project_root() / "Saved" / out_name
        if clean and self.out_dir.exists():
            shutil.rmtree(self.out_dir)
        self.out_dir.mkdir(parents=True, exist_ok=True)
        self._list_process: list[ChildProcess] = []
        self._list_log_file: list = []

    def __enter__(self) -> ProcessGroup:
        return self

    def __exit__(self, *_exc: object) -> None:
        for child in reversed(self._list_process):
            child.stop(quit_line=False, timeout_seconds=10)
        for log_file in self._list_log_file:
            log_file.close()

    def require(self, *stems: str) -> list[Path] | None:
        """실행 파일들(`Server` · `App`)의 경로. 하나라도 없으면 까닭을 찍고 None."""
        list_path = [self.bin_dir / executable_name(stem) for stem in stems]
        for path in list_path:
            if not path.exists():
                print(f"[{self.tag}] FAIL: {path} does not exist - build the '{self.preset}' preset first")
                return None
        return list_path

    def start_scenario(self, name: str, app: Path, game: str, scenario: str, variables: Mapping[str, object] | None = None, *, stdin: bool = False) -> ChildProcess:
        """
        @p app 을 시나리오 @p scenario(리소스 경로)로 띄운다. 보고는 `<out_dir>/<name>.report.json`. @p stdin 이면 표준 입력 줄을 개발 명령으로 받는다(실행기가 다른 프로세스의 사건을 잇는다).
        무인 실행의 사용자 데이터는 프로세스마다 Saved/Automation 아래 제 폴더다 — 밖을 가리키면 App 이 시작을 거절한다(`AutomationUserDataRoot`).
        """
        report_path = self.out_dir / f"{name}.report.json"
        report_path.unlink(missing_ok=True)
        command = [app, app_arguments.option("game", game), app_arguments.option("scenario", scenario), app_arguments.option("scenario-report", report_path), app_arguments.flag("unattended")]
        command += app_arguments.variables({**(variables or {}), **({"gv_standardInputCommands": 1} if stdin else {})})
        return self.start(name, command, stdin=stdin, report_path=report_path)

    def start(self, name: str, command: Sequence[str | Path], *, environment: Mapping[str, str] | None = None, stdin: bool = True, report_path: Path | None = None) -> ChildProcess:
        """@p command 를 `Bin` 에서 띄운다. 출력은 `<out_dir>/<name>.log`, @p environment 는 이 프로세스의 환경 위에 더한다."""
        list_argument = [str(part) for part in command]
        print(f"[{self.tag}] {name}: {' '.join(list_argument)}")
        log = self.out_dir / f"{name}.log"
        log_file = log.open("wb")
        self._list_log_file.append(log_file)
        process = subprocess.Popen(list_argument, cwd=str(self.bin_dir), env={**os.environ, **environment} if environment else None,
                                   stdin=subprocess.PIPE if stdin else None, stdout=log_file, stderr=subprocess.STDOUT)
        child = ChildProcess(name, process, log, report_path, self._list_process)
        self._list_process.append(child)
        return child

    def fail(self, message: str) -> int:
        print(f"[{self.tag}] FAIL: {message}")
        return int(ExitCode.VIOLATION)


def report(tag: str, problems: Sequence[str], logs: Sequence[Path], ok_message: str) -> int:
    """판정을 찍고 종료 코드를 돌려준다 — 문제마다 FAIL 한 줄, 로그 경로, 문제가 없으면 OK 한 줄."""
    for problem in problems:
        print(f"[{tag}] FAIL: {problem}")
    if logs:
        print(f"[{tag}] logs: {' '.join(str(log) for log in logs)}")
    if problems:
        return int(ExitCode.VIOLATION)
    print(f"[{tag}] OK: {ok_message}")
    return int(ExitCode.PASSED)
