"""
본 트리 컴파일 DB(`build/<프리셋>/compile_commands.json`)를 읽기만 하는 명령(`tidy` · `syntax`)이 함께 쓰는 것 — 명령 고쳐 쓰기, 바뀐 C++ 파일, 역 포함 표, 낮은 우선순위.
"""

from __future__ import annotations

import ctypes
import os
import re
from pathlib import Path

from common.git import run_git_or_raise
from common.host_platform import is_windows_host

DEFAULT_BASE = "origin/main"
SOURCE_EXTENSIONS = {".cpp", ".cc", ".cxx"}
HEADER_EXTENSIONS = {".h", ".hpp"}
#: PCH 를 쓰라는 clang-cl 플래그(/Yu<헤더> · /Fp<파일>) — 붙여 쓴 꼴이라 한 낱말이다.
PCH_USE_PATTERN = re.compile(r'\s/(Yu|Fp)("[^"]*"|\S*)')
#: CMake 가 강제 포함하는 미리 컴파일 머리 — 담은 헤더를 본 트리의 절대 경로로 적는다.
PCH_HEADER_PATTERN = re.compile(r'(\s/FI)"?([^"\s]*cmake_pch\.hxx)"?')
#: 저장소 include 꼴(`#include "Core/…/X.h"` — `Source/` 기준 경로).
INCLUDE_PATTERN = re.compile(r'^[ \t]*#[ \t]*include[ \t]*"([^"]+)"', re.MULTILINE)


def expand_pch_header(match: re.Match[str]) -> str:
    """강제 포함한 `cmake_pch.hxx` 를 그 안의 헤더 강제 포함으로 바꾼다. 아직 만들지 않았으면 그대로 둔다."""
    path = Path(match.group(2))
    if not path.is_file():
        return match.group(0)
    return "".join(f"{match.group(1)}{header}" for header in INCLUDE_PATTERN.findall(path.read_text(encoding="utf-8", errors="replace")))


def rewrite_command(command: str, main_root: Path, tree_root: Path) -> str:
    """PCH 사용 플래그를 빼고, 워크트리면 본 트리 소스 경로(build · Tools 밖)를 이 트리로 바꾼다."""
    command = PCH_USE_PATTERN.sub("", command)
    if main_root.resolve() == tree_root.resolve():
        return command
    command = PCH_HEADER_PATTERN.sub(expand_pch_header, command)   # 담은 헤더를 직접 강제 포함해 아래에서 이 트리 것이 된다 — 그대로 두면 같은 헤더가 두 번 정의된다
    for separator in ("/", "\\"):
        main_text = str(main_root).replace("\\", "/").replace("/", separator) + separator
        tree_text = str(tree_root).replace("\\", "/").replace("/", separator) + separator
        command = re.sub(re.escape(main_text) + r"(?!build[/\\]|Tools[/\\])", lambda _: tree_text, command, flags=re.IGNORECASE)
    return command


def changed_files(tree_root: Path, base: str) -> list[Path]:
    """기준과 작업 트리가 다른 C++ 파일(지운 것은 뺀다)."""
    output = run_git_or_raise(["diff", "--name-only", "--diff-filter=ACMR", base, "--", "Source"], cwd=tree_root)
    return [tree_root / line.strip() for line in output.splitlines()
            if line.strip() and Path(line.strip()).suffix.lower() in SOURCE_EXTENSIONS | HEADER_EXTENSIONS]


class IncludeIndex:
    """`Source/` 아래 파일의 역 포함 표(include 글 → 그 글을 적은 파일) — 짝 .cpp 가 없는 헤더를 포함하는 TU 를 찾는다. 처음 물을 때 한 번 훑는다."""

    def __init__(self, source_root: Path) -> None:
        self._source_root = source_root
        self._map_includer: dict[str, list[Path]] | None = None

    def get_including_files(self, include_key: str) -> list[Path]:
        if self._map_includer is None:
            self._map_includer = {}
            for path in self._source_root.rglob("*"):
                if path.suffix.lower() not in SOURCE_EXTENSIONS | HEADER_EXTENSIONS or not path.is_file():
                    continue
                for included in INCLUDE_PATTERN.findall(path.read_text(encoding="utf-8", errors="replace")):
                    self._map_includer.setdefault(included.replace("\\", "/").lower(), []).append(path)
        return self._map_includer.get(include_key.lower(), [])


def lower_priority() -> None:
    """이 프로세스(와 우선순위를 물려받는 clang-tidy 자식)를 낮춘다 — 겹쳐 도는 테스트가 CPU 를 먼저 받는다."""
    if is_windows_host():
        below_normal_priority_class = 0x4000
        ctypes.windll.kernel32.SetPriorityClass(ctypes.windll.kernel32.GetCurrentProcess(), below_normal_priority_class)
    else:
        os.nice(10)
