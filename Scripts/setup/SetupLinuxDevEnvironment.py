r"""
Scripts/setup/SetupLinuxDevEnvironment.py

Linux / WSL 媛쒕컻 ?섍꼍???꾩슂?????붾젆?곕━ ?ㅼ젙???먮룞 ?곸슜?⑸땲??

?곸슜 ?댁슜 (idempotent):
1. ~/.gdbinit ??Ubuntu debuginfod 鍮꾪솢?깊솕
   (VS Code/Cursor cppdbg媛 "Downloading separate debug info..." ?먯꽌 硫덉텛??臾몄젣)
2. ~/.bashrc, ~/.profile ??DEBUGINFOD_URLS 鍮꾩슦湲?
   (login/interactive ?맞톀emote ?쒕쾭媛 GDB??URL???섍린吏 ?딅룄濡?
3. ?뚯씪 ?ㅼ씠?쇰줈洹??꾧뎄(zenity/kdialog/yad) 議댁옱 ?щ? ?덈궡
   (?먮뵒??Import/Save ?ㅼ씠?쇰줈洹????놁쑝硫??⑦궎吏 ?ㅼ튂 ?뚰듃留?異쒕젰)

CMake configure ??SetupEnvironment.py ?먯꽌 ?몄텧?섎ŉ, ?섎룞 ?ㅽ뻾??媛?ν빀?덈떎:
  python3 Scripts/setup/SetupLinuxDevEnvironment.py
"""

from __future__ import annotations

import argparse
import os
import shutil
import sys
from pathlib import Path
from typing import Optional, Sequence

# Allow `python3 Scripts/setup/SetupLinuxDevEnvironment.py`
_SCRIPTS = Path(__file__).resolve().parents[1]
if str(_SCRIPTS) not in sys.path:
    sys.path.insert(0, str(_SCRIPTS))


_MARKER = "Template engine linux-dev"

_GDBINIT_BLOCK = f"""\
# >>> {_MARKER} (managed ??do not edit by hand)
# Prevent hang when debuginfod.ubuntu.com is slow/unreachable (VS Code/Cursor cppdbg).
set debuginfod urls
set debuginfod enabled off
# <<< {_MARKER}
"""

_SHELL_BLOCK = f"""\
# >>> {_MARKER} (managed ??do not edit by hand)
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
    path ??managed 釉붾줉???곌굅??媛깆떊?⑸땲??
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
    """~/.gdbinit 諛???rc??debuginfod 鍮꾪솢?깊솕瑜??곸슜?⑸땲??"""
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


def CheckFileDialogTools() -> None:
    """?먮뵒???뚯씪 ?ㅼ씠?쇰줈洹몄뿉 ?꾩슂???몃? ?꾧뎄 議댁옱 ?щ?瑜??덈궡?⑸땲??"""
    tools = ("zenity", "qarma", "matedialog", "kdialog", "yad")
    found = [name for name in tools if shutil.which(name)]
    if found:
        print(f"[SetupLinuxDevEnvironment] file dialog tool: {found[0]}")
        return

    host = "WSL" if _IsWsl() else "Linux"
    print(
        f"[SetupLinuxDevEnvironment] {host}: no file dialog tool "
        f"(zenity/kdialog/yad). Editor Import/Save dialogs need one ??"
        f"e.g. sudo apt install zenity"
    )


def SetupLinuxDevEnvironment(home: Optional[Path] = None) -> int:
    """Linux 媛쒕컻 ?섍꼍 ?먮룞 ?ㅼ젙???섑뻾?⑸땲?? non-Linux ?먯꽌??no-op."""
    if not _IsLinux():
        print("[SetupLinuxDevEnvironment] skip (not Linux)")
        return 0

    SetupGdbDebuginfod(home=home)
    CheckFileDialogTools()
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
