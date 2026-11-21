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

헤더 하나가 바뀌면 그 헤더를 포함하는 TU 를 모두 돌지 않고 하나만 돈다(같은 폴더 · 같은 이름 .cpp, 없으면 가장 가까운 포함 TU — `select_entries`).
바뀐 줄만 보는 진단 필터(clang-tidy-diff 의 -line-filter)는 두지 않는다 — 분석은 TU 전체를 그대로 해 시간이 줄지 않고, 판정은 진단이 아니라 실패한 TU 수다.

결과 캐시(ccache direct 모드 꼴): 통과한 TU 의 출력을 본 트리 `Saved/TidyCache/` 에 둔다(본 트리 · 워크트리가 나눈다). 키는 clang-tidy 실행 파일 ·
`.clang-tidy` · 검사 인자 · 컴파일 명령 · vcpkg 설치 상태 · TU 와 그 포함 닫힘(-I · /FI 로 찾은 모든 헤더의 경로와 내용 해시 — 조건부 포함도 넣어
넘치게 잡는다)이다. 하나라도 바뀌면 다시 돈다. 풀지 못하는 매크로 포함(`#include MACRO`)이 닫힘에 있는 TU 는 캐시하지 않는다.
clang-tidy 는 낮은 우선순위로 돈다 — 묶음 검증이 changed · cpu 단계와 겹쳐 띄운다(Scripts/dev/preflight.sh).

종료 코드: clang-tidy 오류(WarningsAsErrors 로 오른 검사 · 컴파일 오류)가 하나라도 있으면 VIOLATION.
"""

from __future__ import annotations

import argparse
import ctypes
import hashlib
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
from common.host_platform import is_windows_host  # noqa: E402
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
#: 캐시 키의 포함 닫힘을 훑는 꼴 — 따옴표 · 꺾쇠, 그리고 둘 다 아닌 매크로 포함(풀지 못해 그 TU 는 캐시하지 않는다).
ANY_INCLUDE_PATTERN = re.compile(r'^[ \t]*#[ \t]*include[ \t]*(?:"([^"]+)"|<([^>]+)>|(\S.*))', re.MULTILINE)
#: 명령의 포함 폴더(-I · /I)와 강제 포함(/FI) — 붙여 쓴 꼴(`-ID:\…`)이다. /imsvc 시스템 폴더는 vcpkg 설치 상태 해시로 대신한다.
INCLUDE_DIR_PATTERN = re.compile(r'(?:^|\s)[-/](?:external:)?I("[^"]+"|\S+)')
FORCE_INCLUDE_PATTERN = re.compile(r'(?:^|\s)[-/]FI("[^"]+"|\S+)')
#: 결과 캐시 — 본 트리 기준.
CACHE_DIR = Path("Saved") / "TidyCache"
#: 캐시한 출력 · 키 안의 이 트리 경로 자리 — 본 트리 · 워크트리가 같은 캐시를 나눈다.
TREE_PLACEHOLDER = "<SW_TREE>"


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


def normalize(path: str) -> str:
    return os.path.normcase(os.path.normpath(path))


def unquote(text: str) -> str:
    return text[1:-1] if len(text) >= 2 and text[0] == text[-1] == '"' else text


class IncludeClosure:
    """TU 의 포함 닫힘을 (경로, 내용 해시) 목록으로 — 파일 읽기 · 포함 풀기 · 강제 포함(PCH 헤더)의 닫힘은 한 실행 안에서 한 번씩만 한다."""

    def __init__(self) -> None:
        self._map_digest: dict[str, str] = {}
        self._map_include: dict[str, list[tuple[str, bool, bool]]] = {}   # 경로 → (글, 따옴표인가, 매크로인가)
        self._map_dirs_id: dict[tuple[str, ...], int] = {}
        self._map_children: dict[tuple[str, int], list[str] | None] = {}
        self._map_root_closure: dict[tuple[str, int], frozenset[str] | None] = {}

    def _children(self, path: str, include_dirs: tuple[str, ...], dirs_id: int) -> list[str] | None:
        """@p path 가 포함하는 파일 중 포함 폴더에서 찾은 것. 매크로 포함이 있으면 None. 찾지 못한 것은 시스템 헤더(/imsvc · MSVC)로 본다."""
        key = (path, dirs_id)
        if key not in self._map_children:
            if path not in self._map_digest:
                data = Path(path).read_bytes()
                self._map_digest[path] = hashlib.sha256(data).hexdigest()
                self._map_include[path] = [(quoted or angled or other.strip(), bool(quoted), bool(other))
                                           for quoted, angled, other in ANY_INCLUDE_PATTERN.findall(data.decode("utf-8", errors="replace"))]
            list_child: list[str] | None = []
            includer_dir = os.path.dirname(path)
            for spec, quoted, is_macro in self._map_include[path]:
                if is_macro:
                    list_child = None
                    break
                candidates = ([includer_dir] if quoted else []) + list(include_dirs)
                resolved = next((normalize(os.path.join(d, spec)) for d in candidates if os.path.isfile(os.path.join(d, spec))), None)
                if resolved is not None:
                    list_child.append(resolved)
            self._map_children[key] = list_child
        return self._map_children[key]

    def _traverse(self, path: str, include_dirs: tuple[str, ...], dirs_id: int, seen: set[str]) -> bool:
        """@p seen 에 @p path 의 닫힘을 더한다(이미 든 것 아래는 닫혀 있어 내려가지 않는다). 매크로 포함을 만나면 거짓."""
        list_pending = [path]
        while list_pending:
            current = list_pending.pop()
            if current in seen:
                continue
            seen.add(current)
            list_child = self._children(current, include_dirs, dirs_id)
            if list_child is None:
                return False
            list_pending.extend(child for child in list_child if child not in seen)
        return True

    def collect(self, file_path: str, list_forced: list[str], include_dirs: tuple[str, ...]) -> list[tuple[str, str]] | None:
        """
        TU(@p file_path)와 강제 포함(@p list_forced)의 닫힘을 (경로, 해시) 경로 순으로. 풀린 경로가 키에 들어가므로 앞 폴더에 같은 이름 헤더가
        새로 생겨도 다음 실행의 키가 바뀐다. 매크로 포함을 만나면 None(풀 수 없어 키가 닫히지 않는다).
        """
        dirs_id = self._map_dirs_id.setdefault(include_dirs, len(self._map_dirs_id))
        seen: set[str] = set()
        for forced in map(normalize, list_forced):
            key = (forced, dirs_id)
            if key not in self._map_root_closure:
                closure: set[str] = set()
                self._map_root_closure[key] = frozenset(closure) if self._traverse(forced, include_dirs, dirs_id, closure) else None
            if self._map_root_closure[key] is None:
                return None
            seen |= self._map_root_closure[key]
        if not self._traverse(normalize(file_path), include_dirs, dirs_id, seen):
            return None
        return sorted((path, self._map_digest[path]) for path in seen)


def make_base_digest(clang_tidy: Path, tree_root: Path, main_root: Path, list_extra: list[str]) -> str:
    """모든 TU 가 나누는 키 재료: clang-tidy 실행 파일 · `.clang-tidy` · 검사 인자 · vcpkg 설치 상태(꺾쇠 시스템 헤더의 판)."""
    # shortcut: MSVC STL · Windows SDK 판은 키에 없다(그 헤더의 진단은 세지 않고, 컴파일 오류는 빌드가 먼저 낸다), 판 올림 뒤 Saved/TidyCache 를 비운다.
    digest = hashlib.sha256()
    for path in (clang_tidy, tree_root / ".clang-tidy", main_root / "build" / "vcpkg_installed" / "vcpkg" / "status"):
        digest.update(hashlib.sha256(path.read_bytes()).hexdigest().encode() if path.is_file() else b"-")
    digest.update(json.dumps(list_extra).encode())
    return digest.hexdigest()


def make_cache_key(base_digest: str, entry: dict, closure: IncludeClosure, tree_root: Path) -> str | None:
    """TU 하나의 캐시 키. 포함 닫힘을 풀 수 없으면 None(캐시하지 않는다)."""
    command, directory = entry["command"], entry["directory"]
    include_dirs = tuple(normalize(os.path.join(directory, unquote(d))) for d in INCLUDE_DIR_PATTERN.findall(command))
    list_forced = [os.path.join(directory, unquote(f)) for f in FORCE_INCLUDE_PATTERN.findall(command)]
    list_file = closure.collect(entry["file"], list_forced, include_dirs)
    if list_file is None:
        return None
    tree_text = normalize(str(tree_root))
    digest = hashlib.sha256(base_digest.encode())
    digest.update(command.replace(str(tree_root), TREE_PLACEHOLDER).replace(tree_root.as_posix(), TREE_PLACEHOLDER).encode())
    for path, file_digest in list_file:
        digest.update(f"\n{path.replace(tree_text, TREE_PLACEHOLDER)}={file_digest}".encode())
    return digest.hexdigest()


def cache_path(cache_dir: Path, key: str) -> Path:
    return cache_dir / key[:2] / f"{key}.txt"


def read_cache(cache_dir: Path, key: str, tree_root: Path) -> str | None:
    path = cache_path(cache_dir, key)
    return path.read_text(encoding="utf-8").replace(TREE_PLACEHOLDER, str(tree_root)) if path.is_file() else None


def write_cache(cache_dir: Path, key: str, output: str, tree_root: Path) -> None:
    # shortcut: 항목(TU 출력 몇 KB)을 지우지 않고 쌓는다, Saved/TidyCache 가 커지면 오래된 것부터 지우는 정리를 둔다.
    path = cache_path(cache_dir, key)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(f".{os.getpid()}.tmp")
    temporary.write_text(output.replace(str(tree_root), TREE_PLACEHOLDER).replace(tree_root.as_posix(), TREE_PLACEHOLDER), encoding="utf-8")
    os.replace(temporary, path)   # 두 실행이 같은 키를 써도 반쯤 쓴 파일을 읽지 않는다


def lower_priority() -> None:
    """이 프로세스(와 우선순위를 물려받는 clang-tidy 자식)를 낮춘다 — 겹쳐 도는 테스트가 CPU 를 먼저 받는다."""
    if is_windows_host():
        below_normal_priority_class = 0x4000
        ctypes.windll.kernel32.SetPriorityClass(ctypes.windll.kernel32.GetCurrentProcess(), below_normal_priority_class)
    else:
        os.nice(10)


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
    parser.add_argument("--checks", help="`.clang-tidy` 의 Checks 대신 쓸 목록(검사 확대 실측용)")
    parser.add_argument("--out", type=Path, help="중복을 뺀 진단 전체를 JSON 으로 쓴다")
    args = parser.parse_args(list_argument)

    tree_root = get_project_root()
    main_root = main_tree_root(tree_root)
    clang_tidy = clang_tidy_path(main_root)
    list_entry = load_database(main_root, tree_root, DEFAULT_PRESET)

    list_skipped: list[Path] = []
    if args.changed is not None:
        list_entry, list_skipped = select_entries(list_entry, changed_files(tree_root, args.changed), tree_root / "Source")
    elif args.files:
        list_entry, list_skipped = select_entries(list_entry, [path if path.is_absolute() else tree_root / path for path in args.files], tree_root / "Source")
    for path in list_skipped:
        print(f"[tidy] 건너뜀(컴파일 DB 에 TU 가 없다): {path}")
    # `--` 뒤 인자(--fix 등)는 부작용이 있어 캐시를 쓰지 않는다. --checks 는 키에 들어간다.
    use_cache = not list_extra
    if args.checks:
        list_extra = [f"--checks={args.checks}", *list_extra]

    lower_priority()
    started = time.monotonic()
    cache_dir = main_root / CACHE_DIR
    base_digest = make_base_digest(clang_tidy, tree_root, main_root, list_extra) if use_cache else ""
    closure = IncludeClosure()
    list_result: list[tuple[str, int, str]] = []
    list_run: list[tuple[dict, str | None]] = []
    for entry in list_entry:
        key = make_cache_key(base_digest, entry, closure, tree_root) if use_cache else None
        cached = read_cache(cache_dir, key, tree_root) if key else None
        if cached is None:
            list_run.append((entry, key))
        else:
            list_result.append((entry["file"], 0, cached))
    print(f"[tidy] 캐시 적중 {len(list_result)}/{len(list_entry)} TU ({time.monotonic() - started:.0f} 초)", flush=True)
    with tempfile.TemporaryDirectory(prefix="sw-tidy-") as database_dir:
        (Path(database_dir) / "compile_commands.json").write_text(json.dumps([entry for entry, _ in list_run]), encoding="utf-8")
        with ThreadPoolExecutor(max_workers=max(1, (os.cpu_count() or 2) - 2)) as pool:
            futures = [(pool.submit(run_one, clang_tidy, Path(database_dir), entry["file"], list_extra), key) for entry, key in list_run]
            for index, (future, key) in enumerate(futures, 1):
                result = future.result()
                if key and result[1] == 0:   # 진 TU 는 적지 않는다 — 고친 뒤 다시 돌아야 한다
                    write_cache(cache_dir, key, result[2], tree_root)
                list_result.append(result)
                if index % 100 == 0:
                    print(f"[tidy] {index}/{len(futures)} TU", flush=True)

    diagnostics: dict[tuple[str, int, int, str], dict] = {}
    error_count = 0
    for file_path, return_code, output in list_result:
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

    list_diagnostic = sorted(diagnostics.values(), key=lambda d: (d["check"], d["path"], d["line"]))
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
