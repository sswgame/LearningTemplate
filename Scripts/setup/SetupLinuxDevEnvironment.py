r"""
Scripts/setup/SetupLinuxDevEnvironment.py

Linux 및 WSL 개발 환경에 필요한 홈 디렉터리 설정을 자동으로 구성합니다.

적용 내용 (중복 실행 안전):
1. ~/.gdbinit 에 Ubuntu debuginfod 비활성화 설정 추가
   (VS Code/Cursor cppdbg가 "Downloading separate debug info..." 에서 멈추는 문제 방지)
2. ~/.bashrc, ~/.profile 에 DEBUGINFOD_URLS 환경변수 비우기
3. 파일 다이얼로그 도구(zenity/kdialog/yad) 존재 여부 검사 및 안내
4. 클립보드 도구(xclip/xsel/wl-copy) 존재 여부 검사 및 안내
5. Vulkan/XCB 그래픽스 개발 패키지 존재 여부 안내 (apt 설치 안내)

CMake configure 시 SetupEnvironment.py 에서 자동 호출되며, 수동 실행도 가능합니다:
  python3 Scripts/setup/SetupLinuxDevEnvironment.py
"""

from __future__ import annotations

import argparse
import os
import shutil
import sys
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

_kMarker = "Template engine linux-dev"

_kGdbinitBlock = f"""\
# >>> {_kMarker} (자동 생성됨 - 수동 편집 금지)
# debuginfod 서버 지연/접속 불가로 인한 GDB 디버깅 멈춤 현상 방지
set debuginfod urls
set debuginfod enabled off
# <<< {_kMarker}
"""

_kShellBlock = f"""\
# >>> {_kMarker} (자동 생성됨 - 수동 편집 금지)
# Ubuntu debuginfod 비활성화 (서버 다운 시 VS Code/Cursor GDB 중단 방지)
unset DEBUGINFOD_URLS
export DEBUGINFOD_URLS=
# <<< {_kMarker}
"""


def isLinuxInternal() -> bool:
    return sys.platform.startswith("linux")


def isWslInternal() -> bool:
    if os.environ.get("WSL_DISTRO_NAME") or os.environ.get("WSL_INTEROP"):
        return True
    try:
        data = Path("/proc/version").read_text(encoding="utf-8", errors="replace").lower()
        return "microsoft" in data or "wsl" in data
    except OSError:
        return False


def upsertManagedBlockInternal(path: Path, block: str) -> str:
    """
    지정된 파일(path)에 관리용 설정 블록을 삽입하거나 기존 블록을 갱신합니다.

    Returns:
        'created' | 'updated' | 'unchanged'
    """
    begin = f"# >>> {_kMarker}"
    end = f"# <<< {_kMarker}"
    text = path.read_text(encoding="utf-8", errors="replace") if path.exists() else ""

    if begin in text and end in text:
        start = text.index(begin)
        stop = text.index(end, start) + len(end)
        if stop < len(text) and text[stop] == "\n":
            stop += 1
        newText = text[:start] + block
        if stop < len(text):
            newText += text[stop:].lstrip("\n")
            if not newText.endswith("\n") and text[stop:]:
                newText += "\n"
        if newText == text:
            return "unchanged"
        path.write_text(newText, encoding="utf-8", newline="\n")
        return "updated"

    prefix = "\n" if text and not text.endswith("\n") else ""
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a", encoding="utf-8", newline="\n") as fileHandle:
        fileHandle.write(prefix + block)
    return "created" if not text.strip() else "updated"


def setupGdbDebuginfod(home: Path | None = None) -> None:
    """~/.gdbinit 및 *rc에 debuginfod 비활성화를 적용합니다."""
    homeDir = home or Path.home()
    actions: list[str] = []

    gdbinit = homeDir / ".gdbinit"
    if gdbinit.exists():
        status = upsertManagedBlockInternal(gdbinit, _kGdbinitBlock)
    else:
        gdbinit.write_text(_kGdbinitBlock, encoding="utf-8", newline="\n")
        status = "created"
    actions.append(f".gdbinit:{status}")

    for name in (".bashrc", ".profile"):
        status = upsertManagedBlockInternal(homeDir / name, _kShellBlock)
        actions.append(f"{name}:{status}")

    host = "WSL" if isWslInternal() else "Linux"
    print(f"[SetupLinuxDevEnvironment] {host} debuginfod fix ({', '.join(actions)})")


def checkFileDialogTools() -> None:
    """
    리눅스 시스템에 파일 다이얼로그(zenity, kdialog 등) 유틸리티가 설치되어 있는지 확인합니다.
    """
    tools = ("zenity", "qarma", "matedialog", "kdialog", "yad")
    if tool := next((name for name in tools if shutil.which(name)), None):
        print(f"[SetupLinuxDevEnvironment] file dialog tool: {tool}")
        return

    host = "WSL" if isWslInternal() else "Linux"
    print(
        f"[SetupLinuxDevEnvironment] {host}: no file dialog tool "
        f"(zenity/kdialog/yad). Editor Import/Save dialogs need one — "
        f"e.g. sudo apt install zenity"
    )


def checkClipboardTools() -> None:
    """FileUtil Linux clipboard 경로(xclip) 존재 여부를 안내합니다."""
    clipboardTools = ("xclip", "xsel", "wl-copy")
    if tool := next((name for name in clipboardTools if shutil.which(name)), None):
        print(f"[SetupLinuxDevEnvironment] clipboard tool: {tool}")
        return

    host = "WSL" if isWslInternal() else "Linux"
    print(
        f"[SetupLinuxDevEnvironment] {host}: no clipboard tool "
        f"(xclip/xsel/wl-clipboard). Editor copy/paste may no-op — "
        f"e.g. sudo apt install xclip"
    )


def checkGraphicsDevPackages() -> None:
    """
    Vulkan/GL/XCB 개발 헤더 힌트 (빌드 시 필요).
    패키지 매니저 상태는 파일 존재로만 대략 확인합니다.
    """
    hints: list[str] = []
    usrInclude = Path("/usr/include")
    if not (usrInclude / "xcb/xcb.h").is_file() and not Path("/usr/local/include/xcb/xcb.h").is_file():
        hints.append("libxcb1-dev libx11-xcb-dev")
    if not (usrInclude / "vulkan/vulkan.h").is_file() and not (usrInclude / "vulkan/vulkan_core.h").is_file():
        if not any((usrInclude / relPath).is_file() for relPath in ("vulkan/vulkan.h", "vulkan/vulkan.hpp")):
            hints.append("libvulkan-dev")
    if hints:
        host = "WSL" if isWslInternal() else "Linux"
        print(
            f"[SetupLinuxDevEnvironment] {host}: missing graphics headers "
            f"({' '.join(hints)}). Vulkan/GL WSI builds need them — "
            f"e.g. sudo apt install {' '.join(hints)}"
        )
    else:
        print("[SetupLinuxDevEnvironment] graphics headers: ok (xcb/vulkan)")


def setupLinuxDevEnvironment(home: Path | None = None) -> int:
    """
    Linux/WSL 환경에 필요한 개발 도구 검사 및 GDB 디버깅 환경(debuginfod 등)을 자동 설정합니다.
    """
    if not isLinuxInternal():
        print("[SetupLinuxDevEnvironment] skip (not Linux)")
        return 0

    setupGdbDebuginfod(home=home)
    checkFileDialogTools()
    checkClipboardTools()
    checkGraphicsDevPackages()
    return 0


def parseArgs(argv: Sequence[str] | None = None) -> argparse.Namespace:
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


def main(argv: Sequence[str] | None = None) -> int:
    args = parseArgs(argv)
    return setupLinuxDevEnvironment(home=args.home)


if __name__ == "__main__":
    sys.exit(main())
