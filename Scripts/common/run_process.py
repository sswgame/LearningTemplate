"""
자식 프로세스 하나를 돌리는 자리 — git · clang-format · 빌드 도구를 띄우는 스크립트가 이것을 쓴다.

모양을 하나로 두는 이유 둘:
- **디코딩** — `text=True` 만 주면 출력을 시스템 코드 페이지(cp949 · cp1252)로 읽는다. UTF-8 진단 · 한글 경로가 섞이면 도구가
  `UnicodeDecodeError` 로 죽거나(진단 대신 트레이스백) 글자가 깨진다. 여기서는 늘 UTF-8 · `errors="replace"` 다.
- **실패의 모양** — 실행 파일이 없으면(`FileNotFoundError`) · 시간이 넘으면(`TimeoutExpired`) 곳마다 다르게 죽었다. 여기서는 예외 대신
  `ProcessResult` 의 칸으로 돌려준다(부르는 쪽이 "못 띄웠다" 와 "띄웠는데 졌다" 를 가른다).

"""

from __future__ import annotations

import os
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Mapping, Sequence


@dataclass(frozen=True)
class ProcessResult:
    """
    자식 프로세스 한 번의 결과.

    - `return_code` : 종료 코드(띄우지 못했거나 시간을 넘겼으면 -1).
    - `stdout` · `stderr` : 잡은 출력(`bCapture=False` 면 빈 문자열).
    - `bLaunched` : 실행 파일을 찾아 띄웠는가.
    - `bTimedOut` : 제한 시간을 넘겨 죽였는가.
    """

    return_code: int
    stdout: str = ""
    stderr: str = ""
    launched: bool = True
    timed_out: bool = False

    @property
    def succeeded(self) -> bool:
        """띄웠고, 제때 끝났고, 0 으로 끝났는가."""
        return self.launched and not self.timed_out and self.return_code == 0


def run_process(command: Sequence[str | Path],
               *,
               cwd: Path | None = None,
               timeout_seconds: float | None = None,
               capture: bool = True,
               environment: Mapping[str, str] | None = None) -> ProcessResult:
    """
    `command` 를 돌리고 끝날 때까지 기다립니다.

    - `bCapture` : 출력을 잡는다(UTF-8, 깨진 바이트는 대체 글자). 거짓이면 부모 콘솔로 흘린다.
    - `environment` : 자식의 환경(없으면 이 프로세스의 것) — git 은 `git.run_git` 이 저장소 환경 변수를 뺀 것을 넘긴다.
    """
    list_argument = [str(part) for part in command]
    try:
        completed = subprocess.run(list_argument, cwd=str(cwd) if cwd else None, capture_output=capture, timeout=timeout_seconds,
                                   encoding="utf-8", errors="replace", check=False, env=dict(environment) if environment is not None else None)
        return ProcessResult(return_code=completed.returncode, stdout=completed.stdout or "", stderr=completed.stderr or "")
    except (FileNotFoundError, PermissionError) as error:
        return ProcessResult(return_code=-1, stderr=f"실행 파일을 띄우지 못했다: {list_argument[0]} ({error})", launched=False)
    except subprocess.TimeoutExpired as error:
        partial = error.stdout.decode("utf-8", "replace") if isinstance(error.stdout, bytes) else (error.stdout or "")
        return ProcessResult(return_code=-1, stdout=partial, stderr=f"{timeout_seconds} 초를 넘겼다", timed_out=True)


def run_cmake(list_argument: Sequence[str | Path], *, cwd: Path, capture: bool = True) -> ProcessResult:
    """
    cmake 구성 · 빌드의 입구(`cmake --preset` · `cmake --build --preset`). 연결된 워크트리(`.git` 이 파일)에서는 멈춘다 — 빌드는 메인 작업 폴더
    하나에서만 한다(공용 vcpkg · 디스크가 겹친다, 사용자 지시 2026-11-12). 작업자는 코드 → 린트 → 커밋만 하고, 컴파일 · 실행은 통합이 본 트리에서 한다.
    """
    if (cwd / ".git").is_file():
        raise SystemExit(f"[Scripts] 워크트리({cwd})에서 cmake {' '.join(str(part) for part in list_argument)} 를 시작하지 않는다 — 빌드 · 구성은 메인 작업 폴더에서만")
    return run_process(["cmake", *list_argument], cwd=cwd, capture=capture)


def build_preset_or_exit(preset: str, *, cwd: Path) -> None:
    """
    지어 둔 App 으로 무엇을 돌리기 전에 `preset` 을 증분 빌드한다(바뀐 것이 없으면 몇 초). 짓지 않으면 고친 코드 대신 옛 바이너리가 돌아 고친 단계가 다시 지거나,
    모듈 리로드처럼 소스와 바이너리를 견주는 시나리오가 거짓으로 진다(preflight.sh --from 과 같은 틈). 지면 멈춘다 — 반쯤 지은 바이너리로 돌리지 않는다.
    """
    print(f"{preset} 증분 빌드", flush=True)
    result = run_cmake(["--build", "--preset", preset], cwd=cwd)
    if not result.succeeded:
        sys.exit("\n".join([line for line in result.stdout.splitlines() if "error" in line or "FAILED" in line][:30] + [f"{preset} 빌드 실패 — 고친 뒤 다시 돌린다"]))
