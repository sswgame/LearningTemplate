"""
Scripts/dev/disk_path_args.py

`FileUtil` 디스크 I/O(`exists` · `readTextFile` · `writeFile` …)의 문자열 경로 인자를 `FileUtil::toDiskPath( … )`(`AbsolutePath`)로 감싼다.
I/O 가 `string_view` 를 받을 때는 리소스 id · `DataPath` · `PathKey` 에서 꺼낸 논리 경로 글이 그대로 들어갔다(실행 때 OS 경계의 `File.RelativePath` 로만 드러났다).
이제 I/O 는 `const AbsolutePath&` 만 받고, 문자열에서 디스크 경로를 만드는 자리는 `toDiskPath` 하나로 눈에 보인다(`Core/File/FileUtil.h`).

  py -3 -m Scripts disk-path-args [경로…]          # 감싼다(경로 없으면 Source)
  py -3 -m Scripts disk-path-args --check [경로…]  # 문자열이 틀림없는 남은 인자를 찍고, 있으면 실패
  py -3 -m Scripts disk-path-args --self-check     # 변환 자체 테스트

- 대상은 `FileUtil::<I/O 함수>(` 로 부르는 자리의 경로 인자(`IO_PATH_ARGS` — 함수마다 자리)다. 이미 `toDiskPath(` 로 감싼 인자,
  같은 파일에서 `AbsolutePath` 로 선언한 이름 · `*이름`(optional<AbsolutePath>) · `ProcessRoots` 칸(`._saved` …)은 두고 나머지를 감싼다.
- `--check` · 린트(`cpp-shape` 규칙 14)는 **문자열이 틀림없는** 인자만 센다 — 글 상수 · `.str()` · `.c_str()` · `.view()` · 문자열을 돌려주는
  `FileUtil` 글 함수(`joinPath` · `getDirectoryPart` …). 이름 하나는 형식을 글로 알 수 없어(헤더의 멤버 · 매개변수) 컴파일러에 맡긴다.
- `Core/File/FileUtil.*` (정의 · 문자열 판 구현)는 뺀다. 병합 뒤 충돌 파일에 다시 돌린다(이미 감싼 자리는 그대로).
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.exit_code import ExitCode  # noqa: E402
from common.git import run_git_or_raise  # noqa: E402
from common.paths import get_project_root  # noqa: E402
from dev.byte_reader_values import match_paren, split_top  # noqa: E402
from lint.cpp_shape import strip_code  # noqa: E402

TAG = "[disk-path-args]"
EXCLUDED_FILES = ("Source/Core/File/FileUtil.h", "Source/Core/File/FileUtil.cpp")
SKIP_PARTS = ("ThirdParty", "Generated", ".gen.")
CPP_SUFFIX = (".h", ".cpp", ".inl")
WRAPPER = "toDiskPath"

IO_PATH_ARGS: dict[str, tuple[int, ...]] = {
    "ensureParentDirectoryExists": (0,), "ensureDirectoryExists": (0,), "exists": (0,), "isDirectory": (0,), "isRegularFile": (0,),
    "isReadOnlyFile": (0,), "setWritable": (0,), "tryGetFileTimestamp": (0,), "getFileWriteTime": (0,), "setFileWriteTime": (0,),
    "tryGetFileSize": (0,), "getFileStamp": (0,), "copyFile": (0, 1), "removeFile": (0,), "replaceFile": (0, 1), "tryRemoveFile": (0,),
    "removeDirectory": (0,), "renamePath": (0, 1), "writeFile": (0,), "tryWriteFile": (0,), "writeFileDurable": (0,), "readFile": (0,),
    "readTextFile": (0,), "readOptionalTextFile": (0,), "readStreamTextFile": (0,), "writeTextFile": (0,), "writeTextFileDurable": (0,),
    "appendTextFile": (0,), "forEachDirectoryEntry": (0,), "collectFiles": (0,), "collectFolders": (0,),
}
STRING_RETURNING = ("joinPath", "getDirectoryPart", "getFileNamePart", "normalizeSeparators", "toNativeSeparators", "trimTrailingSlashes",
                    "replaceExtension", "removeExtension", "makeWriteTempPath", "reserveUniquePath", "getTempDirectory", "getCurrentPath",
                    "getExecutablePath", "findPathIgnoringCase", "makePathIgnoringCase")
CALL_PATTERN = re.compile(r"((?:::\s*)?(?:sw\s*::\s*)?FileUtil\s*::\s*)(" + "|".join(IO_PATH_ARGS) + r")\s*\(")
ROOT_FIELD = re.compile(r"(?:ProcessRoots\s*::\s*get\s*\(\s*\)|\broots)\s*\.\s*_(?:binary|project|content|packs|saved|derivedData|run)\s*$")   # ProcessRoots 칸(AbsolutePath)
KNOWN_STRING = re.compile(r'^(?:u8)?"|(?:\.|->)\s*(?:str|c_str|view|data)\s*\(\s*\)\s*$|FileUtil\s*::\s*(?:' + "|".join(STRING_RETURNING) + r")\s*\(")


@dataclass(frozen=True)
class Site:
    """감쌀 경로 인자 하나. `start`/`end` 는 인자 글(앞뒤 공백 뺀) 구간, `known_string` 은 문자열이 틀림없는지."""

    start: int
    end: int
    line: int
    function: str
    argument: str
    qualifier: str
    known_string: bool


def collect_typed_names(code: str) -> frozenset[str]:
    """같은 파일에서 `AbsolutePath` · `optional<AbsolutePath>` 로 선언한 이름(이미 형식이라 감싸지 않는다)."""
    return frozenset(re.findall(r"\b(?:optional\s*<\s*)?AbsolutePath\s*>?\s*[&*]?\s+(\w+)\s*[;,)=({\[]", code))


def is_typed(argument: str, typed_names: frozenset[str]) -> bool:
    text = argument.strip()
    if text.startswith(WRAPPER + "(") or re.match(r"(?:::\s*)?(?:sw\s*::\s*)?FileUtil\s*::\s*" + WRAPPER + r"\s*\(", text):
        return True
    if ROOT_FIELD.search(text):
        return True
    bare = text.lstrip("*").strip()
    return re.fullmatch(r"\w+", bare) is not None and bare in typed_names


def find_sites(raw: str, code: str) -> list[Site]:
    """`code` 는 `strip_code(raw)`. 아직 형식이 아닌 경로 인자를 모두 낸다(겹친 자리 포함)."""
    typed_names = collect_typed_names(code)
    list_site: list[Site] = []
    for match in CALL_PATTERN.finditer(code):
        before = code[:match.start()].rstrip()
        if before.endswith((".", "->")) or re.search(r"\b(?:bool|void|string|uint64|FileWriteResult|OptionalFileRead)\s*$", before):
            continue   # 다른 객체의 같은 이름 · 정의 머리
        open_index = match.end() - 1
        close = match_paren(code, open_index)
        if close < 0:
            continue
        pieces = split_top(code, open_index + 1, close, (",",))
        qualifier = re.sub(r"\s+", "", raw[match.start(1):match.end(1)])
        for index in IO_PATH_ARGS[match.group(2)]:
            if index >= len(pieces):
                continue
            start, end = pieces[index]
            while start < end and code[start].isspace():
                start += 1
            while end > start and code[end - 1].isspace():
                end -= 1
            argument = raw[start:end]
            if not argument or is_typed(code[start:end], typed_names):
                continue
            known_string = KNOWN_STRING.search(argument) is not None
            list_site.append(Site(start, end, raw.count("\n", 0, start) + 1, match.group(2), argument, qualifier, known_string))
    return list_site


UNWRAP_STR = re.compile(r"^(.+?)\s*(\.|->)\s*str\s*\(\s*\)$")


def make_replacement(site: Site, typed_names: frozenset[str]) -> str:
    """이미 `AbsolutePath` 인 값의 `.str()` 은 글로 돌렸다 다시 만들지 않고 형식째 넘긴다(복사 · 검사 한 번 덜). 나머지는 `toDiskPath` 로 감싼다."""
    match = UNWRAP_STR.match(site.argument)
    if match is not None:
        owner, access = match.group(1), match.group(2)
        if ROOT_FIELD.search(owner) or (access == "." and owner in typed_names):
            return owner
        if access == "->" and owner in typed_names:
            return f"*{owner}"
    return f"{site.qualifier}{WRAPPER}( {site.argument} )"


def rewrite_text(raw: str) -> tuple[str, list[Site]]:
    """감싼 글과 감싼 자리. 안쪽 자리(인자 안의 또 다른 I/O 부르기)부터 한 바퀴씩 감싸고 다시 찾는다."""
    list_done: list[Site] = []
    while True:
        code = strip_code(raw)
        sites = find_sites(raw, code)
        typed_names = collect_typed_names(code)
        innermost = [site for site in sites if not any(other is not site and site.start <= other.start and other.end <= site.end for other in sites)]
        if not innermost:
            return raw, list_done
        for site in sorted(innermost, key=lambda s: s.start, reverse=True):
            raw = raw[:site.start] + make_replacement(site, typed_names) + raw[site.end:]
        list_done += innermost


def describe_site(site: Site, relative: str) -> str:
    return f"FileUtil::{site.function} 는 디스크 경로 형식만 받는다 — 문자열 `{site.argument}` 는 `FileUtil::toDiskPath( … )` 로 올린다(`py -3 -m Scripts disk-path-args {relative}`)"


def get_module(relative: str) -> str:
    parts = relative.split("/")
    return parts[1] if len(parts) > 2 else "(root)"


def is_target(relative: str) -> bool:
    return (relative.startswith("Source/") and relative.endswith(CPP_SUFFIX) and relative not in EXCLUDED_FILES
            and not any(part in relative for part in SKIP_PARTS))


def list_files(project_root: Path, list_path: Sequence[str]) -> list[Path]:
    output = run_git_or_raise(["ls-files", "--", *(list_path or ["Source"])], cwd=project_root)
    return [project_root / relative for relative in output.splitlines() if is_target(relative)]


def run_self_check() -> int:
    cases = {
        "ok = FileUtil::exists( path );": "ok = FileUtil::exists( FileUtil::toDiskPath( path ) );",
        "ok = sw::FileUtil::copyFile( a, FileUtil::joinPath( root, \"x, y\" ) );":
            "ok = sw::FileUtil::copyFile( sw::FileUtil::toDiskPath( a ), sw::FileUtil::toDiskPath( FileUtil::joinPath( root, \"x, y\" ) ) );",
        "ok = FileUtil::readTextFile( FileUtil::toDiskPath( p ), text );": "ok = FileUtil::readTextFile( FileUtil::toDiskPath( p ), text );",
        "const AbsolutePath& folder = make();\nok = FileUtil::isDirectory( folder ) && FileUtil::exists( roots._saved ) && FileUtil::exists( entry._content );":
            "const AbsolutePath& folder = make();\nok = FileUtil::isDirectory( folder ) && FileUtil::exists( roots._saved ) && FileUtil::exists( FileUtil::toDiskPath( entry._content ) );",
        "ok = FileUtil::forEachDirectoryEntry( dir, true, [&]( const DirectoryEntry& e ) { return FileUtil::removeFile( e._path ); } );":
            "ok = FileUtil::forEachDirectoryEntry( FileUtil::toDiskPath( dir ), true, [&]( const DirectoryEntry& e ) { return FileUtil::removeFile( FileUtil::toDiskPath( e._path ) ); } );",
        "ok = cache.exists( key ); // FileUtil::exists( p )": "ok = cache.exists( key ); // FileUtil::exists( p )",
        "optional<AbsolutePath> found = f();\nok = FileUtil::exists( found->str() ) && FileUtil::exists( ProcessRoots::get()._saved.str() );":
            "optional<AbsolutePath> found = f();\nok = FileUtil::exists( *found ) && FileUtil::exists( ProcessRoots::get()._saved );",
    }
    failures = 0
    for source, want in cases.items():
        got, _ = rewrite_text(source)
        if got != want:
            print(f"{TAG} self-check: {source!r}\n  got  {got!r}\n  want {want!r}")
            failures += 1
    known_string = [site.known_string for site in find_sites('FileUtil::exists( key.str() ); FileUtil::exists( "a" ); FileUtil::exists( path );', strip_code('FileUtil::exists( key.str() ); FileUtil::exists( "a" ); FileUtil::exists( path );'))]
    if known_string != [True, True, False]:
        print(f"{TAG} self-check: 틀림없는 문자열 판정 {known_string}")
        failures += 1
    print(f"{TAG} self-check {'OK' if failures == 0 else 'FAIL'}")
    return ExitCode.PASSED if failures == 0 else ExitCode.VIOLATION


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="py -3 -m Scripts disk-path-args", description="FileUtil 디스크 I/O 의 문자열 경로 인자를 FileUtil::toDiskPath 로 감싼다")
    parser.add_argument("paths", nargs="*", help="볼 경로(저장소 기준, 없으면 Source)")
    parser.add_argument("--check", action="store_true", help="바꾸지 않고 문자열이 틀림없는 남은 인자를 찍는다 — 있으면 실패")
    parser.add_argument("--self-check", action="store_true", help="변환 자체 테스트")
    args = parser.parse_args(argv)
    if args.self_check:
        return run_self_check()
    project_root = get_project_root()
    list_problem: list[str] = []
    counts: dict[str, int] = {}
    changed_files = 0
    for path in list_files(project_root, args.paths):
        with path.open(encoding="utf-8", newline="") as file:
            raw = file.read()
        if "FileUtil" not in raw:
            continue
        relative = path.relative_to(project_root).as_posix()
        if args.check:
            list_problem += [f"{relative}:{site.line}: {describe_site(site, relative)}" for site in find_sites(raw, strip_code(raw)) if site.known_string]
            continue
        new_raw, done = rewrite_text(raw)
        counts[get_module(relative)] = counts.get(get_module(relative), 0) + len(done)
        if new_raw != raw:
            with path.open("w", encoding="utf-8", newline="") as file:
                file.write(new_raw)
            changed_files += 1
    for line in list_problem:
        print(line)
    if not args.check:
        print(f"{TAG} 바꾼 파일 {changed_files} · 감싼 인자 {sum(counts.values())} — " + " · ".join(f"{name} {count}" for name, count in sorted(counts.items()) if count))
    else:
        print(f"{TAG} 남은 자리 {len(list_problem)}")
    return ExitCode.VIOLATION if list_problem else ExitCode.PASSED


if __name__ == "__main__":
    sys.exit(main())
