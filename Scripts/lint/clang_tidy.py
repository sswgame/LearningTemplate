#!/usr/bin/env python3
"""
Scripts/lint/clang_tidy.py

clang-tidy 를 병렬로 돌리고 진단을 검사 이름별로 모아 센다. 검사 목록은 루트 `.clang-tidy` 하나다.

  py -3 -m Scripts tidy                      # Source 의 모든 TU
  py -3 -m Scripts tidy --changed [기준]     # 기준(기본 origin/main)과 작업 트리가 다른 파일만 — 묶음 검증이 부른다
  py -3 -m Scripts tidy --files a.cpp b.h    # 지정한 파일만
  py -3 -m Scripts tidy --checks '-*,modernize-*' --out 진단.json   # 검사 바꿔 보기 · 진단 전체를 JSON 으로
  py -3 -m Scripts tidy --files a.cpp -- --fix                       # `--` 뒤는 clang-tidy 에 그대로

컴파일 DB 는 **본 트리** `build/<프리셋>/compile_commands.json` 을 읽기만 한다 — 빌드하지 않고 빌드 폴더에 쓰지도 않는다.
워크트리에서 돌리면 DB 의 본 트리 경로(build · Tools 밖)를 이 트리로 바꾼 사본을 임시 폴더에 두고 그것을 쓴다(헤더도 이 트리 것을 본다).
PCH 사용 플래그(/Yu · /Fp)는 뺀다 — clang-tidy 가 남이 짓는 중인 .pch 를 읽지 않게. 강제 포함(/FI)은 둔다(TU 가 그 헤더에 기댄다).

종료 코드: clang-tidy 오류(WarningsAsErrors 로 오른 검사 · 컴파일 오류)가 하나라도 있으면 VIOLATION.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
import tempfile
import time
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다
from common.exit_code import ExitCode  # noqa: E402
from common.git import run_git  # noqa: E402
from common.paths import get_project_root  # noqa: E402
from common.project_constants import KEY_LLVM_TOOLS_SUBDIR  # noqa: E402
from common.run_process import run_process  # noqa: E402
from common.toolchain_config import load_search_paths  # noqa: E402
from setup.llvm import clang_tidy_file_name  # noqa: E402

#: 유니티 빌드를 끈 묶음 검증 구성 — 유니티 구성(Ninja-Release)의 DB 에는 묶음 TU 만 있어 바꾼 파일을 고르지 못한다.
DEFAULT_PRESET = "Ninja-Release-ASan"
DEFAULT_BASE = "origin/main"
SOURCE_EXTENSIONS = {".cpp", ".cc", ".cxx"}
HEADER_EXTENSIONS = {".h", ".hpp"}
#: `경로:줄:열: warning|error: 메시지 [검사,별칭]`
DIAGNOSTIC_PATTERN = re.compile(r"^(?P<path>.+?):(?P<line>\d+):(?P<column>\d+): (?P<level>warning|error): (?P<message>.*?) \[(?P<check>[^\]]+)\]$")
#: PCH 를 쓰라는 clang-cl 플래그(/Yu<헤더> · /Fp<파일>) — 붙여 쓴 꼴이라 한 낱말이다.
PCH_USE_PATTERN = re.compile(r'\s/(Yu|Fp)("[^"]*"|\S*)')
#: 저장소 include 꼴(`#include "Core/…/X.h"` — `Source/` 기준 경로).
INCLUDE_PATTERN = re.compile(r'^[ \t]*#[ \t]*include[ \t]*"([^"]+)"', re.MULTILINE)


def main_tree_root(tree_root: Path) -> Path:
    """워크트리면 본 트리, 아니면 자기 자신. 공통 git 폴더(`.git`)의 부모다."""
    result = run_git(["rev-parse", "--path-format=absolute", "--git-common-dir"], cwd=tree_root)
    if result.return_code != 0:
        raise RuntimeError(f"git rev-parse --git-common-dir 실패: {result.stderr.strip()}")
    return Path(result.stdout.strip()).parent


def clang_tidy_path(main_root: Path) -> Path:
    """본 트리 LLVM 키트의 clang-tidy. 없으면 오류 — 다른 판으로 대신하지 않는다(판마다 검사 목록 · 결과가 다르다)."""
    path = main_root / load_search_paths()[KEY_LLVM_TOOLS_SUBDIR] / "bin" / clang_tidy_file_name()
    if not path.is_file():
        raise RuntimeError(f"{path} 가 없다 — `py -3 -m Scripts llvm` 이 이미 깐 키트에도 clang-tidy 를 더한다")
    return path


def rewrite_command(command: str, main_root: Path, tree_root: Path) -> str:
    """PCH 사용 플래그를 빼고, 워크트리면 본 트리 소스 경로(build · Tools 밖)를 이 트리로 바꾼다."""
    command = PCH_USE_PATTERN.sub("", command)
    if main_root.resolve() == tree_root.resolve():
        return command
    for separator in ("/", "\\"):
        main_text = str(main_root).replace("\\", "/").replace("/", separator) + separator
        tree_text = str(tree_root).replace("\\", "/").replace("/", separator) + separator
        command = re.sub(re.escape(main_text) + r"(?!build[/\\]|Tools[/\\])", lambda _: tree_text, command, flags=re.IGNORECASE)
    return command


def load_database(main_root: Path, tree_root: Path, preset: str) -> list[dict]:
    """본 트리 컴파일 DB 의 Source TU 를 이 트리 경로로 바꿔 돌려준다(빌드가 만든 생성 소스는 뺀다)."""
    database_path = main_root / "build" / preset / "compile_commands.json"
    if not database_path.is_file():
        raise RuntimeError(f"{database_path} 가 없다 — 본 트리를 `cmake --preset {preset}` 로 구성한 뒤 돌린다(이 명령은 빌드하지 않는다)")
    source_root = (main_root / "Source").as_posix().lower() + "/"
    list_entry = []
    for entry in json.loads(database_path.read_text(encoding="utf-8")):
        file_path = entry["file"].replace("\\", "/")
        if not file_path.lower().startswith(source_root):
            continue
        relative = file_path[len(str(main_root)) + 1:]
        list_entry.append({
            "directory": entry["directory"],
            "file": (tree_root / relative).as_posix(),
            "command": rewrite_command(entry["command"], main_root, tree_root),
        })
    return list_entry


def changed_files(tree_root: Path, base: str) -> list[Path]:
    """기준과 작업 트리가 다른 C++ 파일(지운 것은 뺀다)."""
    result = run_git(["diff", "--name-only", "--diff-filter=ACMR", base, "--", "Source"], cwd=tree_root)
    if result.return_code != 0:
        raise RuntimeError(f"git diff {base} 실패: {result.stderr.strip()}")
    return [tree_root / line.strip() for line in result.stdout.splitlines()
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


def find_including_unit(header: Path, entry_by_file: dict[str, dict], index: IncludeIndex, source_root: Path) -> dict | None:
    """@p header 를 (헤더를 거쳐서라도) 포함하는 첫 TU 의 컴파일 DB 항목 — 가까운 것부터 넓게 찾는다. 없으면 None."""
    visited: set[str] = set()
    list_pending = [header]
    while list_pending:
        current = list_pending.pop(0)
        try:
            key = current.resolve().relative_to(source_root.resolve()).as_posix()
        except ValueError:
            continue
        if key.lower() in visited:
            continue
        visited.add(key.lower())
        for includer in index.get_including_files(key):
            hit = entry_by_file.get(includer.resolve().as_posix().lower())
            if hit is not None:
                return hit
            if includer.suffix.lower() in HEADER_EXTENSIONS:
                list_pending.append(includer)
    return None


def select_entries(list_entry: list[dict], list_file: list[Path], source_root: Path) -> tuple[list[dict], list[Path]]:
    """
    파일 목록을 TU 로 바꾼다. 헤더는 같은 폴더 · 같은 이름의 .cpp 로 보고, 짝이 없으면 그 헤더를 포함하는 가장 가까운 TU 로 본다(`find_including_unit`).
    """
    entry_by_file = {entry["file"].lower(): entry for entry in list_entry}
    index = IncludeIndex(source_root)
    selected: dict[str, dict] = {}
    list_skipped: list[Path] = []
    for path in list_file:
        candidates = [path] if path.suffix.lower() in SOURCE_EXTENSIONS else [path.with_suffix(ext) for ext in sorted(SOURCE_EXTENSIONS)]
        hit = next((entry_by_file[c.resolve().as_posix().lower()] for c in candidates if c.resolve().as_posix().lower() in entry_by_file), None)
        if hit is None and path.suffix.lower() in HEADER_EXTENSIONS:
            hit = find_including_unit(path, entry_by_file, index, source_root)
        if hit is None:
            list_skipped.append(path)
        else:
            selected[hit["file"].lower()] = hit
    return list(selected.values()), list_skipped


def run_one(clang_tidy: Path, database_dir: Path, file_path: str, list_extra: list[str]) -> tuple[str, int, str]:
    result = run_process([clang_tidy, "-p", database_dir, "--quiet", *list_extra, file_path])
    return file_path, result.return_code, result.stdout + result.stderr


def main(argv: list[str] | None = None) -> int:
    list_argument = list(sys.argv[1:] if argv is None else argv)
    list_extra: list[str] = []
    if "--" in list_argument:
        cut = list_argument.index("--")
        list_argument, list_extra = list_argument[:cut], list_argument[cut + 1:]
    parser = argparse.ArgumentParser(prog="py -3 -m Scripts tidy", description="clang-tidy 병렬 실행 · 검사별 집계")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--changed", nargs="?", const=DEFAULT_BASE, metavar="기준", help=f"기준(기본 {DEFAULT_BASE})과 다른 파일만")
    mode.add_argument("--files", nargs="+", type=Path, help="지정한 파일만")
    parser.add_argument("--preset", default=DEFAULT_PRESET, help="컴파일 DB 를 읽을 본 트리 프리셋")
    parser.add_argument("--checks", help="`.clang-tidy` 의 Checks 대신 쓸 목록(검사 확대 실측용)")
    parser.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) - 2))
    parser.add_argument("--out", type=Path, help="중복을 뺀 진단 전체를 JSON 으로 쓴다")
    parser.add_argument("--show", action="store_true", help="진단 줄을 모두 찍는다(기본은 집계만)")
    args = parser.parse_args(list_argument)

    tree_root = get_project_root()
    main_root = main_tree_root(tree_root)
    clang_tidy = clang_tidy_path(main_root)
    list_entry = load_database(main_root, tree_root, args.preset)

    list_skipped: list[Path] = []
    if args.changed is not None:
        list_entry, list_skipped = select_entries(list_entry, changed_files(tree_root, args.changed), tree_root / "Source")
    elif args.files:
        list_entry, list_skipped = select_entries(list_entry, [path if path.is_absolute() else tree_root / path for path in args.files], tree_root / "Source")
    for path in list_skipped:
        print(f"[tidy] 건너뜀(컴파일 DB 에 TU 가 없다): {path}")
    if args.checks:
        list_extra = [f"--checks={args.checks}", *list_extra]

    started = time.monotonic()
    diagnostics: dict[tuple[str, int, int, str], dict] = {}
    error_count = 0
    with tempfile.TemporaryDirectory(prefix="sw-tidy-") as database_dir:
        (Path(database_dir) / "compile_commands.json").write_text(json.dumps(list_entry), encoding="utf-8")
        with ThreadPoolExecutor(max_workers=args.jobs) as pool:
            futures = [pool.submit(run_one, clang_tidy, Path(database_dir), entry["file"], list_extra) for entry in list_entry]
            for index, future in enumerate(futures, 1):
                file_path, return_code, output = future.result()
                for line in output.splitlines():
                    match = DIAGNOSTIC_PATTERN.match(line.strip())
                    # 시스템 헤더(MSVC STL 등)에 찍힌 것은 우리 코드의 매크로 · 템플릿이 끌고 들어간 것이라 그 자리를 고칠 수 없다 — 이 트리 밖은 세지 않는다.
                    if match and Path(match["path"]).resolve().is_relative_to(tree_root.resolve()):
                        key = (str(Path(match["path"]).resolve()).lower(), int(match["line"]), int(match["column"]), match["check"])
                        if key not in diagnostics:
                            diagnostics[key] = {"path": str(Path(match["path"]).resolve()), "line": int(match["line"]),
                                                "column": int(match["column"]), "level": match["level"],
                                                "check": match["check"].split(",")[0], "message": match["message"]}
                if return_code != 0:
                    error_count += 1
                    if not any(DIAGNOSTIC_PATTERN.match(line.strip()) for line in output.splitlines()):
                        print(f"[tidy] {file_path}: clang-tidy {return_code}\n{output.strip()[:2000]}")
                if index % 100 == 0:
                    print(f"[tidy] {index}/{len(futures)} TU", flush=True)

    list_diagnostic = sorted(diagnostics.values(), key=lambda d: (d["check"], d["path"], d["line"]))
    if args.show:
        for d in list_diagnostic:
            print(f"{d['path']}:{d['line']}:{d['column']}: {d['level']}: {d['message']} [{d['check']}]")
    if args.out:
        args.out.write_text(json.dumps(list_diagnostic, ensure_ascii=False, indent=1), encoding="utf-8")
    counts = Counter(d["check"] for d in list_diagnostic)
    errors = Counter(d["check"] for d in list_diagnostic if d["level"] == "error")
    for check, count in counts.most_common():
        print(f"{count:6d}  {check}{'  (오류 ' + str(errors[check]) + ')' if errors[check] else ''}")
    print(f"[tidy] TU {len(list_entry)}, 진단 {len(list_diagnostic)}(오류 {sum(errors.values())}), 실패한 TU {error_count}, "
          f"{time.monotonic() - started:.0f} 초")
    return ExitCode.VIOLATION if error_count else ExitCode.PASSED


if __name__ == "__main__":
    sys.exit(main())
