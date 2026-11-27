"""
Scripts/common/wsl_path.py

Windows 경로를 WSL 경로로 바꾼다 — 서버 이미지의 설정 검사(`package/server_image.py`)와 Linux 검증(`verify/linux_verify.py`)이 함께 쓴다.
`/mnt/<드라이브>` 를 직접 짓지 않고 배포판의 `wslpath -a` 에 묻는다. 마운트 자리를 바꾼 배포판(`/etc/wsl.conf` 의 `root`)과 UNC 경로도 맞게 바뀐다.
"""

from __future__ import annotations

import subprocess
from pathlib import Path
from typing import Mapping

from .command import CommandError


def to_wsl_path(windows_path: Path, distro: str | None = None, env: Mapping[str, str] | None = None) -> str:
    """@p distro 가 없으면 기본 배포판에 묻는다. 못 바꾸면 멈춘다(고정 경로로 대신하지 않는다)."""
    command = ["wsl.exe", *(["-d", distro] if distro else []), "--", "wslpath", "-a", windows_path.resolve().as_posix()]
    try:
        result = subprocess.run(command, env=None if env is None else dict(env), capture_output=True, text=True, check=False)
    except OSError as exception:
        raise CommandError(f"wslpath 를 띄우지 못했다({windows_path}): {exception}") from exception
    converted = result.stdout.strip()
    if result.returncode != 0 or not converted.startswith("/"):
        raise CommandError(f"wslpath 가 {windows_path} 를 바꾸지 못했다 ({result.stderr.strip()})")
    return converted
