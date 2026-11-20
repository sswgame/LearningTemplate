"""
여러 프로세스(전용 서버 · 게임 클라이언트 · 호스트)를 한 번에 띄워 시나리오를 돌리는 묶음 실행기 — 온라인 · 락스텝 · 파괴 복제 · 텔레메트리 실행기가 이것 위에 선다.

언리얼 Gauntlet(서버 · 클라이언트 역할을 띄우고 로그를 모아 판정)과 유니티 Multiplayer Play Mode 의 자리다. 실행기마다 손으로 짜던 다섯 가지를 여기 하나로 둔다:
- 빌드 산출물 찾기: `build/<프리셋>/Bin` 의 실행 파일, 없으면 "그 프리셋을 먼저 지어라" 로 멈춘다(`require`).
- 띄우기: 표준 출력 · 오류를 프로세스마다 로그 파일 하나로, 표준 입력은 개발 명령 줄용 파이프로(`start`).
- 기다리기: 로그에 줄이 나오거나 프로세스가 끝날 때까지, 시간 제한과 함께(`ChildProcess.wait_for`).
- 멈추기: 표준 입력 `quit` 로 정상 종료를 청하고, 시간을 넘기면 죽인다. 묶음을 나갈 때 남은 프로세스는 모두 멈춘다(`with` 블록).
- 판정 출력: 허용 밖 `[Error]` · `[Warning]` 줄 모으기(`problem_lines`)와 `[태그] FAIL: …` · 로그 경로 · OK 한 줄 · 종료 코드(`report`).
"""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Mapping, Sequence

from common.exit_code import ExitCode
from common.host_platform import executable_name
from common.paths import get_bin_dir, get_project_root

PROBLEM_LINE = re.compile(r"\[(Error|Warning)\]")
USER_DATA_ROOT_ENVIRONMENT = "SW_USER_DATA_ROOT"  # Core/File/UserDataPath.h 의 kUserDataRootVariable — 띄운 App · Server 의 사용자 데이터 폴더를 옮긴다
_POLL_SECONDS = 0.25  # 로그 줄을 다시 읽는 간격


class ChildProcess:
    """묶음이 띄운 프로세스 하나 — 이름(로그 · 메시지에 쓴다), 로그 파일, 표준 입력 파이프."""

    def __init__(self, name: str, process: subprocess.Popen, log: Path) -> None:
        self.name = name
        self.process = process
        self.log = log

    @property
    def return_code(self) -> int | None:
        return self.process.returncode

    def text(self) -> str:
        return self.log.read_text(encoding="utf-8", errors="replace")

    def send(self, line: str) -> None:
        """표준 입력으로 개발 명령 한 줄을 보낸다. 프로세스가 이미 끝났으면 OSError 다."""
        if self.process.stdin is None:
            raise OSError(f"{self.name}: 표준 입력 파이프 없이 띄웠다")
        self.process.stdin.write((line + "\n").encode("utf-8"))
        self.process.stdin.flush()

    def wait_for(self, pattern: re.Pattern[str], timeout_seconds: float) -> bool:
        """로그에 @p pattern 이 나오면 참, 프로세스가 먼저 끝나거나 시간을 넘기면 거짓."""
        deadline = time.monotonic() + timeout_seconds
        while time.monotonic() < deadline:
            if pattern.search(self.text()):
                return True
            if self.process.poll() is not None:
                return False
            time.sleep(_POLL_SECONDS)
        return False

    def wait(self, timeout_seconds: float) -> bool:
        """스스로 끝나기를 기다린다. 시간을 넘기면 죽이고 거짓."""
        try:
            self.process.wait(timeout=timeout_seconds)
            return True
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
            return False

    def stop(self, quit_line: bool = True, timeout_seconds: float = 20.0) -> None:
        """정상 종료를 청한다(@p quit_line 이면 표준 입력 `quit`, 아니면 terminate). 시간을 넘기면 죽인다."""
        if self.process.poll() is not None:
            return
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

    def start(self, name: str, command: Sequence[str | Path], *, environment: Mapping[str, str] | None = None, stdin: bool = True) -> ChildProcess:
        """@p command 를 `Bin` 에서 띄운다. 출력은 `<out_dir>/<name>.log`, @p environment 는 이 프로세스의 환경 위에 더한다."""
        list_argument = [str(part) for part in command]
        print(f"[{self.tag}] {name}: {' '.join(list_argument)}")
        log = self.out_dir / f"{name}.log"
        log_file = log.open("wb")
        self._list_log_file.append(log_file)
        process = subprocess.Popen(list_argument, cwd=str(self.bin_dir), env={**os.environ, **environment} if environment else None,
                                   stdin=subprocess.PIPE if stdin else None, stdout=log_file, stderr=subprocess.STDOUT)
        child = ChildProcess(name, process, log)
        self._list_process.append(child)
        return child

    def fail(self, message: str) -> int:
        print(f"[{self.tag}] FAIL: {message}")
        return int(ExitCode.VIOLATION)


@dataclass
class CheckResult:
    """로그 판정 — 문제 줄이 없으면 통과."""

    problems: list[str] = field(default_factory=list)

    @property
    def ok(self) -> bool:
        return not self.problems


def problem_lines(name: str, text: str, allowed: re.Pattern[str] | None = None) -> list[str]:
    """@p text 의 `[Error]` · `[Warning]` 줄(@p allowed 에 맞는 줄은 뺀다)을 `<name> log: <줄>` 로."""
    return [f"{name} log: {line.strip()}" for line in text.splitlines() if PROBLEM_LINE.search(line) and (allowed is None or allowed.search(line) is None)]


def read_log(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


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
