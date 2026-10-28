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
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence


@dataclass(frozen=True)
class ProcessResult:
    """
    자식 프로세스 한 번의 결과.

    - `returnCode` : 종료 코드(띄우지 못했거나 시간을 넘겼으면 -1).
    - `stdout` · `stderr` : 잡은 출력(`bCapture=False` 면 빈 문자열).
    - `bLaunched` : 실행 파일을 찾아 띄웠는가.
    - `bTimedOut` : 제한 시간을 넘겨 죽였는가.
    """

    returnCode: int
    stdout: str = ""
    stderr: str = ""
    bLaunched: bool = True
    bTimedOut: bool = False

    @property
    def bSucceeded(self) -> bool:
        """띄웠고, 제때 끝났고, 0 으로 끝났는가."""
        return self.bLaunched and not self.bTimedOut and self.returnCode == 0


def runProcess(command: Sequence[str | Path],
               *,
               cwd: Path | None = None,
               timeoutSeconds: float | None = None,
               bCapture: bool = True) -> ProcessResult:
    """
    `command` 를 돌리고 끝날 때까지 기다립니다.

    - `bCapture` : 출력을 잡는다(UTF-8, 깨진 바이트는 대체 글자). 거짓이면 부모 콘솔로 흘린다.
    """
    listArgument = [str(part) for part in command]
    try:
        completed = subprocess.run(listArgument, cwd=str(cwd) if cwd else None, capture_output=bCapture, timeout=timeoutSeconds,
                                   encoding="utf-8", errors="replace", check=False)
        return ProcessResult(returnCode=completed.returncode, stdout=completed.stdout or "", stderr=completed.stderr or "")
    except (FileNotFoundError, PermissionError) as error:
        return ProcessResult(returnCode=-1, stderr=f"실행 파일을 띄우지 못했다: {listArgument[0]} ({error})", bLaunched=False)
    except subprocess.TimeoutExpired as error:
        partial = error.stdout.decode("utf-8", "replace") if isinstance(error.stdout, bytes) else (error.stdout or "")
        return ProcessResult(returnCode=-1, stdout=partial, stderr=f"{timeoutSeconds} 초를 넘겼다", bTimedOut=True)
