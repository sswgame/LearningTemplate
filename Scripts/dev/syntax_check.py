#!/usr/bin/env python3
"""
Scripts/dev/syntax_check.py

TU 마다 구문만 확인한다(clang-cl `-fsyntax-only`) — 빌드하지 않고, 유니티 묶음을 풀어 파일 하나씩 본다. 앞 파일이 끌어온 헤더에 기댄 TU 와, 병합 뒤 어긋난 선언을 빌드 전에 찾는다
(언리얼 UBT 의 `-SingleFile` · `-DisableUnity` 컴파일, 크로미움의 IWYU 검사와 같은 자리).

  py -3 -m Scripts syntax --changed [기준]   # 기준(기본 origin/main)과 다른 .cpp 와, 다른 헤더를 거쳐서라도 포함하는 TU
  py -3 -m Scripts syntax --all              # 컴파일 DB 의 Source TU 전부
  py -3 -m Scripts syntax --files a.cpp b.h  # 지정한 파일(헤더는 포함하는 TU 로)
  py -3 -m Scripts syntax --changed main --rest   # 위 고르기의 나머지(한 번 다 본 뒤 남은 몫)
  py -3 -m Scripts syntax --files A.cpp --changed --direct --define SW_ENABLE_STL_CONTAINER   # 다른 구성의 매크로를 더해(preflight 의 std 컨테이너 갈래)
  … --jobs 4

컴파일 DB 는 **본 트리** `build/<프리셋>/compile_commands.json` 을 읽기만 한다(`Scripts/lint/clang_tidy.py` 와 같은 경로 바꾸기). 낮은 우선순위로 돌아 겹쳐 도는 빌드가 CPU 를 먼저 받는다.

보지 못하는 것 — 생성 코드(`.gen.cpp`)와 생성 헤더는 본 트리 것이다. 워크트리에서는 모듈 의존 문지기(`build/…/gates` — 본 트리의 모듈 그래프로 만든 금지 헤더)를 뺀다:
이 트리가 더한 의존을 옛 문지기가 거절한다. 링크 · 다른 구성(서버 · Shipping · 웹)은 실제 빌드가 본다.

종료 코드: 오류가 난 TU 가 있으면 VIOLATION.
"""

from __future__ import annotations

import argparse
import json
import re
import time
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, replace
from pathlib import Path

from common.command import Command, ScriptContext
from common.compile_database import DEFAULT_BASE, HEADER_EXTENSIONS, SOURCE_EXTENSIONS, IncludeIndex, changed_files, lower_priority, rewrite_command
from common.exit_code import ExitCode
from common.host_platform import is_windows_host
from common.run_process import run_process

#: 유니티 빌드 구성이어도 된다 — 묶음 TU 는 담은 파일로 푼다. 개발 중 늘 짓는 구성이라 생성 헤더가 가장 새것이다.
DEFAULT_PRESET = "Ninja-Release"
DEFAULT_JOBS = 4
PROGRESS_STEP = 100
#: 묶음 TU(`…/Unity/unity_3_cxx.cxx`)가 담은 파일.
UNITY_INCLUDE_PATTERN = re.compile(r'^#include "([^"]+)"', re.MULTILINE)
#: 명령 끝의 원본 파일(` -c -- X.cpp` · ` -c X.cpp`) — 떼고 이 트리의 파일을 붙인다.
SOURCE_ARGUMENT_PATTERN = re.compile(r"\s-c(\s+--)?\s+\S+\s*$")
OUTPUT_ARGUMENT_PATTERN = re.compile(r"\s/F[od]\S+")
#: 모듈 의존 문지기 포함 폴더(`cmake/Modules` 가 만든 금지 헤더).
MODULE_STUB_INCLUDE_PATTERN = re.compile(r"\s-I\S*[/\\]gates[/\\]\S+")
ERROR_PATTERN = re.compile(r"(?::| )(?:fatal )?error: ")
MAX_ERRORS_PER_UNIT = 5


@dataclass(frozen=True)
class SyntaxUnit:
    """TU 하나 — @p arguments 는 원본 파일을 뺀 컴파일 명령이다."""
    file: Path
    directory: str
    arguments: str


def expand_entry(entry: dict) -> list[str]:
    """컴파일 DB 항목이 담은 원본 파일들(본 트리 경로) — 묶음 TU 면 담은 파일, 아니면 자기 자신."""
    file_path = entry["file"].replace("\\", "/")
    if "/unity/" not in file_path.lower():
        return [file_path]
    text = Path(file_path).read_text(encoding="utf-8", errors="replace") if Path(file_path).is_file() else ""
    return [included.replace("\\", "/") for included in UNITY_INCLUDE_PATTERN.findall(text)]


def strip_arguments(command: str, keep_module_stubs: bool) -> str:
    """원본 · 산출물 자리와(@p keep_module_stubs 가 거짓이면) 문지기를 뺀 명령."""
    command = OUTPUT_ARGUMENT_PATTERN.sub("", SOURCE_ARGUMENT_PATTERN.sub("", command))
    return command if keep_module_stubs else MODULE_STUB_INCLUDE_PATTERN.sub("", command)


def make_units(database: list[dict], main_root: Path, tree_root: Path) -> dict[str, SyntaxUnit]:
    """컴파일 DB 의 Source TU 를 이 트리의 파일로 푼다(열쇠는 `Source/…` 소문자). 따로 짓는 항목이 묶음에 든 것보다 앞선다. 이 트리에 없는 파일은 뺀다."""
    source_prefix = main_root.as_posix().lower() + "/source/"
    keep_module_stubs = main_root.resolve() == tree_root.resolve()
    units: dict[str, SyntaxUnit] = {}
    for entry in sorted(database, key=lambda item: "/unity/" in item["file"].replace("\\", "/").lower()):
        arguments = strip_arguments(rewrite_command(entry["command"], main_root, tree_root), keep_module_stubs)
        for file_path in expand_entry(entry):
            if not file_path.lower().startswith(source_prefix) or Path(file_path).suffix.lower() not in SOURCE_EXTENSIONS:
                continue
            relative = file_path[len(source_prefix) - len("source/"):]
            tree_file = tree_root / relative
            if relative.lower() not in units and tree_file.is_file():
                units[relative.lower()] = SyntaxUnit(tree_file, entry["directory"], arguments)
    return units


def borrow_unit(relative: str, units: dict[str, SyntaxUnit], tree_root: Path) -> SyntaxUnit | None:
    """DB 에 없는 새 파일은 가장 가까운 폴더의 TU 명령을 빌린다(층 문지기가 폴더마다 다르다) — 같은 폴더 안에서는 테스트 · 서버 · 클라이언트 갈래가 같은 것이 앞선다. 없으면 None."""
    def make_branch(path: str) -> tuple[bool, ...]:
        return tuple(f"/{name}/" in path.lower() for name in ("tests", "server", "client"))

    wanted = make_branch(relative)
    folder = Path(relative).parent
    while len(folder.parts) > 1:
        prefix = folder.as_posix().lower() + "/"
        list_key = [key for key in units if key.startswith(prefix)]
        if list_key:
            unit = units[next((key for key in list_key if make_branch(key) == wanted), list_key[0])]
            return SyntaxUnit(tree_root / relative, unit.directory, unit.arguments)
        folder = folder.parent
    return None


def select_units(list_file: list[Path], units: dict[str, SyntaxUnit], main_root: Path, tree_root: Path) -> tuple[dict[str, SyntaxUnit], list[str]]:
    """@p list_file 의 .cpp 와, 헤더를 (다른 헤더를 거쳐서라도) 포함하는 TU 모두. 둘째는 이 구성이 짓지 않는 파일(본 트리에도 있는데 DB 에 없다)."""
    selected: dict[str, SyntaxUnit] = {}
    list_unbuilt: list[str] = []
    source_root = (tree_root / "Source").resolve()
    list_pending: list[str] = []
    for path in list_file:
        relative = path.resolve().relative_to(tree_root.resolve()).as_posix()
        if path.suffix.lower() in HEADER_EXTENSIONS | {".inl"}:
            list_pending.append(relative)
            continue
        unit = units.get(relative.lower())
        if unit is None and not (main_root / relative).is_file():
            unit = borrow_unit(relative, units, tree_root)
        if unit is None:
            list_unbuilt.append(relative)
        else:
            selected[relative.lower()] = unit
    index = IncludeIndex(source_root)
    visited: set[str] = set()
    while list_pending:
        header = list_pending.pop()
        if header.lower() in visited:
            continue
        visited.add(header.lower())
        for includer in index.get_including_files(header[len("Source/"):]):
            relative = "Source/" + includer.resolve().relative_to(source_root).as_posix()
            if includer.suffix.lower() not in SOURCE_EXTENSIONS:
                list_pending.append(relative)
            elif relative.lower() in units:
                selected[relative.lower()] = units[relative.lower()]
    return selected, list_unbuilt


def check_unit(unit: SyntaxUnit) -> list[str]:
    """TU 하나의 오류 줄(앞 몇 줄). 통과면 빈 목록이다."""
    # 경고는 보지 않되 DB 가 오류로 올린 것(-Werror=…)은 그대로 오류다.
    result = run_process([*split_command(unit.arguments), "-fsyntax-only", "--", str(unit.file)], cwd=Path(unit.directory))
    if result.return_code == 0:
        return []
    list_line = [line for line in (result.stdout + result.stderr).splitlines() if ERROR_PATTERN.search(line)]
    return list_line[:MAX_ERRORS_PER_UNIT] or [(result.stdout + result.stderr).strip()[-400:]]


def split_command(command: str) -> list[str]:
    """컴파일 DB 의 Windows 명령 글을 인자로 가른다 — 따옴표로 묶은 낱말은 하나이고, `\\"` 는 글자 그대로의 따옴표다."""
    list_argument: list[str] = []
    for match in re.finditer(r'(?:[^\s"\\]|\\+"|\\|"(?:[^"\\]|\\.)*")+', command):
        word = re.sub(r'(?<!\\)"', "", match.group(0))
        list_argument.append(word.replace('\\"', '"'))
    return list_argument


class SyntaxCheckCommand(Command):
    name = "syntax"
    help = "유니티를 푼 구문 확인(clang-cl -fsyntax-only, 본 트리 컴파일 DB 를 읽기만 한다) — --changed [기준]: 바뀐 파일이 닿는 TU, --all: 전부, --files: 지정 파일, --direct: 헤더를 넓히지 않음, --define: 더할 매크로, --jobs: 동시 수"

    def add_arguments(self, parser: argparse.ArgumentParser) -> None:
        mode = parser.add_argument_group("고르기(하나 이상)")
        mode.add_argument("--changed", nargs="?", const=DEFAULT_BASE, metavar="기준", help=f"기준(기본 {DEFAULT_BASE})과 다른 파일이 닿는 TU")
        mode.add_argument("--all", action="store_true", help="컴파일 DB 의 Source TU 전부")
        mode.add_argument("--files", nargs="+", type=Path, help="지정한 파일(헤더는 포함하는 TU 로)")
        parser.add_argument("--direct", action="store_true", help="고른 .cpp 만 — 고른 헤더를 포함하는 TU 로 넓히지 않는다")
        parser.add_argument("--define", action="append", default=[], metavar="이름", help="모든 TU 에 더할 매크로(되풀이해 준다)")
        parser.add_argument("--rest", action="store_true", help="고른 것의 나머지 TU")
        parser.add_argument("--jobs", type=int, default=DEFAULT_JOBS, help=f"동시 수(기본 {DEFAULT_JOBS})")
        parser.add_argument("--preset", default=DEFAULT_PRESET, help=f"읽을 컴파일 DB 의 프리셋(기본 {DEFAULT_PRESET})")

    def run(self, context: ScriptContext, args: argparse.Namespace) -> int:
        if args.all == (args.changed is not None or args.files is not None):
            self.parser.error("--all 하나 또는 --changed · --files 를 준다")
        if args.rest and args.all:
            self.parser.error("--rest 는 --changed · --files 와 쓴다")
        if not is_windows_host():
            print("[syntax] Windows 의 clang-cl 컴파일 DB 만 읽는다")
            return ExitCode.CHECK_INVALID

        tree_root = context.root
        main_root = context.main_root
        database_path = main_root / "build" / args.preset / "compile_commands.json"
        if not database_path.is_file():
            print(f"[syntax] {database_path} 가 없다 — 본 트리를 구성한 뒤 돌린다(이 명령은 빌드하지 않는다)")
            return ExitCode.CHECK_INVALID
        units = make_units(json.loads(database_path.read_text(encoding="utf-8")), main_root, tree_root)
        selected, list_unbuilt = units, []
        if not args.all:
            list_file = [path if path.is_absolute() else tree_root / path for path in args.files or []]
            if args.changed is not None:
                list_file += changed_files(tree_root, args.changed)
            if args.direct:
                list_file = [path for path in list_file if path.suffix.lower() in SOURCE_EXTENSIONS]
            selected, list_unbuilt = select_units(list_file, units, main_root, tree_root)
        for relative in list_unbuilt:
            print(f"[syntax] 건너뜀({args.preset} 이 짓지 않는 파일): {relative}")
        if args.rest:
            selected = {key: unit for key, unit in units.items() if key not in selected}

        defines = "".join(f" /D{name}" for name in args.define)
        selected = {key: replace(unit, arguments=unit.arguments + defines) for key, unit in selected.items()}

        lower_priority()
        started = time.monotonic()
        print(f"[syntax] TU {len(selected)}개 · 동시 {args.jobs}", flush=True)
        failed_count = 0
        with ThreadPoolExecutor(args.jobs) as pool:
            for done_count, (key, list_error) in enumerate(zip(selected, pool.map(check_unit, selected.values())), start=1):
                if list_error:
                    failed_count += 1
                    print(f"  {key}", *(f"    {line}" for line in list_error), sep="\n", flush=True)
                if done_count % PROGRESS_STEP == 0:
                    print(f"[syntax] {done_count}/{len(selected)} ({time.monotonic() - started:.0f} s)", flush=True)
        print(f"[syntax] {'FAIL' if failed_count else 'OK'} — TU {len(selected)}개 가운데 오류 {failed_count}개 ({time.monotonic() - started:.0f} s)")
        return ExitCode.VIOLATION if failed_count else ExitCode.PASSED
