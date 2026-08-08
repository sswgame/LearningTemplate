r"""
Scripts/SetupLinuxDevEnvironment.py

Linux / WSL 개발 환경에 필요한 홈 디렉터리 설정을 자동 적용합니다.

적용 내용 (idempotent):
1. ~/.gdbinit — Ubuntu debuginfod 비활성화
   (VS Code/Cursor cppdbg가 "Downloading separate debug info..." 에서 멈추는 문제)
2. ~/.bashrc, ~/.profile — DEBUGINFOD_URLS 비우기
   (login/interactive 셸·Remote 서버가 GDB에 URL을 넘기지 않도록)

CMake configure 시 SetupEnvironment.py 에서 호출되며, 수동 실행도 가능합니다:
  python3 Scripts/SetupLinuxDevEnvironment.py
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path
from typing import Optional, Sequence


_MARKER = "Template engine linux-dev"

_GDBINIT_BLOCK = f"""\
# >>> {_MARKER} (managed — do not edit by hand)
# Prevent hang when debuginfod.ubuntu.com is slow/unreachable (VS Code/Cursor cppdbg).
set debuginfod urls
set debuginfod enabled off
# <<< {_MARKER}
"""

_SHELL_BLOCK = f"""\
# >>> {_MARKER} (managed — do not edit by hand)
# Disable Ubuntu debuginfod (breaks VS Code/Cursor GDB when the server is down).
unset DEBUGINFOD_URLS
export DEBUGINFOD_URLS=
# <<< {_MARKER}
"""


def _IsLinux() -> bool:
    return sys.platform.startswith("linux")


def _IsWsl() -> bool:
    if os.environ.get("WSL_DISTRO_NAME") or os.environ.get("WSL_INTEROP"):
        return True
    try:
        data = Path("/proc/version").read_text(encoding="utf-8", errors="replace").lower()
        return "microsoft" in data or "wsl" in data
    except OSError:
        return False


def _UpsertManagedBlock(path: Path, block: str) -> str:
    """
    path 에 managed 블록을 쓰거나 갱신합니다.
    Returns: 'created' | 'updated' | 'unchanged'
    """
    begin = f"# >>> {_MARKER}"
    end = f"# <<< {_MARKER}"
    text = path.read_text(encoding="utf-8", errors="replace") if path.exists() else ""

    if begin in text and end in text:
        start = text.index(begin)
        stop = text.index(end, start) + len(end)
        # Consume a single trailing newline after the end marker if present.
        if stop < len(text) and text[stop] == "\n":
            stop += 1
        new_text = text[:start] + block
        if stop < len(text):
            new_text += text[stop:].lstrip("\n")
            if not new_text.endswith("\n") and text[stop:]:
                new_text += "\n"
        if new_text == text:
            return "unchanged"
        path.write_text(new_text, encoding="utf-8", newline="\n")
        return "updated"

    # Legacy one-shot installs used bare DEBUGINFOD_URLS lines without markers.
    # If those exist and our marker does not, still append a managed block.
    prefix = ""
    if text and not text.endswith("\n"):
        prefix = "\n"
    elif text:
        prefix = "\n"

    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a", encoding="utf-8", newline="\n") as fh:
        fh.write(prefix + block)
    return "created" if not text.strip() else "updated"


def SetupGdbDebuginfod(home: Optional[Path] = None) -> None:
    """~/.gdbinit 및 셸 rc에 debuginfod 비활성화를 적용합니다."""
    home_dir = home or Path.home()
    actions = []

    gdbinit = home_dir / ".gdbinit"
    if gdbinit.exists():
        status = _UpsertManagedBlock(gdbinit, _GDBINIT_BLOCK)
    else:
        gdbinit.write_text(_GDBINIT_BLOCK, encoding="utf-8", newline="\n")
        status = "created"
    actions.append(f".gdbinit:{status}")

    for name in (".bashrc", ".profile"):
        status = _UpsertManagedBlock(home_dir / name, _SHELL_BLOCK)
        actions.append(f"{name}:{status}")

    host = "WSL" if _IsWsl() else "Linux"
    print(f"[SetupLinuxDevEnvironment] {host} debuginfod fix ({', '.join(actions)})")


def SetupLinuxDevEnvironment(home: Optional[Path] = None) -> int:
    """Linux 개발 환경 자동 설정을 수행합니다. non-Linux 에서는 no-op."""
    if not _IsLinux():
        print("[SetupLinuxDevEnvironment] skip (not Linux)")
        return 0

    SetupGdbDebuginfod(home=home)
    return 0


def ParseArgs(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Apply Linux/WSL developer home settings (debuginfod/GDB)."
    )
    parser.add_argument(
        "--home",
        type=Path,
        default=None,
        help="Override home directory (default: Path.home())",
    )
    return parser.parse_args(argv)


def Main(argv: Optional[Sequence[str]] = None) -> int:
    args = ParseArgs(argv)
    return SetupLinuxDevEnvironment(home=args.home)


if __name__ == "__main__":
    sys.exit(Main())
