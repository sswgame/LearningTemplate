"""
소스 파일 · 폴더를 옮기거나 C++ 이름을 바꾸고, 저장소의 참조를 한 번에 고친다 — 구조 개편 단계마다 같은 방법으로 옮기려고 둔다.

  py -3 -m Scripts move-source Engine/Object/SceneAnimation=Engine/Animation/Scene Engine/A/B.h=Engine/C/B.h [--dry-run]
  py -3 -m Scripts move-source --base . cmake/Engine=cmake/Modules/Build
  py -3 -m Scripts rename-symbol getOrCreateEntry=getOrCreateEntry [--dry-run]

move-source: 경로는 `Source/` 기준(폴더면 안의 파일 전부, 하위 폴더 포함). `--base .` 이면 저장소 루트 기준(`cmake/` · `Config/` 처럼 Source 밖 파일). 하는 일:
  1. `git mv`
  2. 추적 중인 텍스트 파일(Source · Tools · cmake · Scripts · docs · 스킬 · 최상위 CMake)에서 옮긴 파일의 전체 경로(`Engine/X/Y.h`)와
     폴더 접두어(`Engine/X/` — CMake 정규식 · 주석)를 새 경로로
  3. 같은 폴더 짧은 include(`#include "Y.h"`)가 옮긴 뒤 다른 폴더를 가리키게 되면 전체 경로로
  4. 고친 C++ 파일의 include 순서를 include-order 규칙으로(`format` 의 첫 단계와 같은 픽서)
rename-symbol: 낱말 경계로 바꾼다(부분 일치 없음, 코드 · 문서 .md · 데이터 Content · 설정 Config — 린트 예외 표 포함, 금지 이름 표의 키와 이름 기준선은 지킨다). 옛 이름을 일부러 적은 줄은 `keep-old-name` 표지로 남는다. 새 이름이 이미 쓰이고 있으면 겹친 파일을 알리고 멈춘다(`--force` 로 강행). 리플렉션 프로퍼티(`PROPERTY` 가 붙은 `_` 멤버)는 데이터의 직렬화 이름(`_bFooBar` → `fooBar`)도 함께 바꾸고, 그 이름을 다른 멤버도 쓰면 멈춘다.
  C 계열 소스(C++ · HLSL)의 문자열 리터럴 안은 기본으로 건너뛰고 그 파일을 알린다(`--include-strings` 로 함께 바꾼다).
"""

from __future__ import annotations

import argparse
import io
import re
import sys
import time
import tokenize
from pathlib import Path

from common.exit_code import ExitCode
from common.git import run_git
from common.paths import get_project_root
from common.rename_scope import (
    check_rewrite_unmarked_lines, check_serialized_rename, collect_reflected_properties, plan_serialized_rename, replace_serialized_name, rewrite_rename_target,
    rewrite_unmarked_lines,
)

TEXT_SUFFIX = {".h", ".hpp", ".inl", ".cpp", ".c", ".cmake", ".txt", ".md", ".json", ".in", ".py", ".toml", ".xxx", ".natvis", ".hlsl", ".hlsli", ".xml", ".po", ".tpl",
               ".manifest", ".sh"}
CPP_SUFFIX = {".h", ".hpp", ".inl", ".cpp"}
SCAN_ROOT = ["Source", "Content", "Source/Programs/ReflectionParser", "Source/Programs/OnlineLoadBot", "cmake", "Scripts", "docs", "Config", "Test", "ThirdParty",
             "CMakeLists.txt", "CMakePresets.json", "AGENTS.md", "CLAUDE.md", ".claude/skills", ".claude/hooks"]
SKIP_SCAN_PART = "/vcpkg-port/"   # ThirdParty 는 우리 감싸기 CMake 만 — vcpkg 포트는 남의 코드다
BARE_INCLUDE = re.compile(r'^(\s*#\s*include\s*")([^"/]+)(")', re.M)


# 문서(docs/agents 포함) · 데이터 · 설정도 바꾼다. 옛 이름을 일부러 적은 줄 · 표는 `common/rename_scope.py` 가 지킨다(`rename_identifier.py` 와 같은 함수).
# 스크립트의 시험 글은 폴더가 아니라 형식으로 지킨다 — Python 은 코드 토큰만 바꾼다(`rewrite_python_code`).

# C 계열 소스의 주석 · 문자열 · 문자 리터럴. rename-symbol 은 문자열 안을 기본으로 건너뛴다 — 문자열은 파일 매직("OggS") · JSON 키 · 내보내기
# 기호 · 데이터 이름처럼 바깥이 정한 철자가 많아, 약어 일괄 변경(1dafbae0c9)이 그 안까지 바꿔 .ogg 디코드 · 구매 응답 · 머티리얼 깃발을 깨뜨렸다.
C_LIKE_SUFFIX = CPP_SUFFIX | {".c", ".hlsl", ".hlsli", ".xxx"}
C_LITERAL_OR_COMMENT = re.compile(
    r'(?P<comment>//[^\n]*|/\*.*?\*/)'
    r'|(?:u8|[uUL])?R"(?P<delim>[^()\\\s]{0,16})\(.*?\)(?P=delim)"'
    r'|(?:u8|[uUL])?"(?:\\.|[^"\\\n])*"'
    r"|'(?:\\.|[^'\\\n])+'",
    re.S,
)


def rewrite_outside_strings(text: str, rewrite) -> tuple[str, int]:
    """C 계열 글에서 문자열 · 문자 리터럴 밖(코드 · 주석)에만 @p rewrite 를 적용한다. 건너뛴 리터럴 가운데 바뀌었을 것의 수를 함께 돌려준다."""
    list_part: list[str] = []
    skipped_count = 0
    position = 0
    for match in C_LITERAL_OR_COMMENT.finditer(text):
        list_part.append(rewrite(text[position:match.start()]))
        literal = match.group(0)
        if match.group("comment") is not None:
            list_part.append(rewrite(literal))
        else:
            skipped_count += rewrite(literal) != literal
            list_part.append(literal)
        position = match.end()
    list_part.append(rewrite(text[position:]))
    return "".join(list_part), skipped_count


def rewrite_python_code(text: str, rewrite) -> tuple[str, int]:
    """
    Python 글에서 코드의 이름 토큰에만 @p rewrite 를 적용한다(문자열 · f-문자열의 글 부분 · 주석은 그대로). 건너뛴 것 가운데 바뀌었을 것의 수를 함께 돌려준다.
    스크립트의 문자열은 시험 글(`--self-check` 사례) · 바깥 철자라, 금지 명사 이전(1692df1e3a)이 NamingWords 시험 글 `hasPendingWork` 를 바꿔 자체 시험이 늘 졌다.
    """
    list_line_start = [0]
    for line in text.splitlines(keepends=True):
        list_line_start.append(list_line_start[-1] + len(line))
    LITERAL = {tokenize.STRING, tokenize.COMMENT} | ({tokenize.FSTRING_MIDDLE} if hasattr(tokenize, "FSTRING_MIDDLE") else set())
    list_part: list[str] = []
    skipped_count = 0
    position = 0
    for token in tokenize.generate_tokens(io.StringIO(text).readline):
        if token.type != tokenize.NAME and token.type not in LITERAL:
            continue
        start = list_line_start[token.start[0] - 1] + token.start[1]
        end = list_line_start[token.end[0] - 1] + token.end[1]
        original = text[start:end]
        if token.type == tokenize.NAME:
            replaced = rewrite(original)
        else:
            skipped_count += rewrite(original) != original
            replaced = original
        list_part.append(text[position:start])
        list_part.append(replaced)
        position = end
    list_part.append(text[position:])
    return "".join(list_part), skipped_count


def check_rewrite_outside_strings() -> None:
    """문자열 건너뛰기의 자체 확인 — 코드 · 주석은 바뀌고 일반 · 원시 문자열과 주석 속 따옴표는 지킨다."""
    rename = lambda part: re.sub(r"\bOgg\b", "OGG", part)  # noqa: E731
    text = 'Ogg a; // Ogg "x\nf( "Ogg", R"(Ogg)", \'"\' ); /* Ogg */ Ogg b;'
    rewritten, skipped_count = rewrite_outside_strings(text, rename)
    assert rewritten == 'OGG a; // OGG "x\nf( "Ogg", R"(Ogg)", \'"\' ); /* OGG */ OGG b;', rewritten
    assert skipped_count == 2, skipped_count
    python = "def runOgg():  # Ogg\n    return f\"Ogg {Ogg}\" + 'Ogg' + Ogg\n"
    rewritten, skipped_count = rewrite_python_code(python, rename)
    assert rewritten == "def runOgg():  # Ogg\n    return f\"Ogg {OGG}\" + 'Ogg' + OGG\n", rewritten   # 이름 토큰만 — 문자열 · 주석의 시험 글은 지킨다
    assert skipped_count == 3, skipped_count


def list_tracked_text(root: Path) -> list[Path]:
    result = run_git(["ls-files", "--", *SCAN_ROOT], cwd=root)
    if not result.succeeded:
        raise RuntimeError(f"git ls-files 실패: {result.stderr.strip()}")
    return [root / line for line in result.stdout.splitlines()
            if Path(line).suffix.lower() in TEXT_SUFFIX and SKIP_SCAN_PART not in line and (root / line).is_file()]


def read_text(path: Path) -> str:
    return path.read_bytes().decode("utf-8", "surrogateescape")


def write_text(path: Path, text: str) -> None:
    path.write_bytes(text.encode("utf-8", "surrogateescape"))


def expand_moves(source: Path, list_pair: list[str]) -> dict[str, str]:
    """`old=new` 쌍을 파일 단위 사상(Source 기준 posix 경로)으로 편다. 없는 원본 · 이미 있는 대상은 오류다."""
    map_file: dict[str, str] = {}
    for pair in list_pair:
        old, sep, new = pair.partition("=")
        if not sep or not old or not new:
            raise ValueError(f"'{pair}' 은 old=new 모양이 아니다")
        old, new = old.strip("/"), new.strip("/")
        old_path = source / old
        if old_path.is_file():
            map_file[old] = new
        elif old_path.is_dir():
            for file in sorted(p for p in old_path.rglob("*") if p.is_file()):
                relative = file.relative_to(old_path).as_posix()
                map_file[f"{old}/{relative}"] = f"{new}/{relative}"
        else:
            raise FileNotFoundError(f"{old_path} 이 없다")
    for new in map_file.values():
        if (source / new).exists():
            raise FileExistsError(f"{source / new} 이 이미 있다")
    return map_file


def folder_prefixes(list_pair: list[str], source: Path) -> list[tuple[str, str]]:
    """폴더 이동의 접두어 쌍(긴 것 먼저 — 안쪽 폴더가 바깥 폴더 규칙에 먼저 먹히지 않게)."""
    list_prefix = []
    for pair in list_pair:
        old, _, new = pair.partition("=")
        if (source / old.strip("/")).is_dir():
            list_prefix.append((old.strip("/") + "/", new.strip("/") + "/"))
    return sorted(list_prefix, key=lambda item: -len(item[0]))


def rewrite_bare_includes(text: str, old_folder: str, new_folder: str, source: Path, map_file: dict[str, str]) -> str:
    """파일이 `oldFolder` 에서 `newFolder` 로 갔을 때, 짧은 include 가 다른 폴더를 가리키게 되면 전체 경로로 바꾼다."""

    def replace(match: re.Match[str]) -> str:
        name = match.group(2)
        old_target = f"{old_folder}/{name}" if old_folder else name
        if not (source / old_target).is_file():
            return match.group(0)  # pch.h · 생성 헤더처럼 이 폴더 밖에서 풀리는 이름
        new_target = map_file.get(old_target, old_target)
        if new_target.rpartition("/")[0] == new_folder:
            return match.group(0)
        return f"{match.group(1)}{new_target}{match.group(3)}"

    return BARE_INCLUDE.sub(replace, text)


def make_path_rule(map_file: dict[str, str], list_folder_prefix: list[tuple[str, str]], root_base: bool = False) -> tuple[re.Pattern[str], object]:
    """파일 경로 · 폴더 접두어를 **한 번 훑기** 규칙 하나로 묶는다(긴 것 먼저).
    차례로 적용하면 앞 규칙이 바꾼 글을 뒤 규칙이 다시 바꿨다 — 폴더를 자기 아래로 옮기면(`A=A/Server`) 파일 규칙이 만든 `A/Server/X.h` 를
    폴더 규칙 `A/` 가 다시 `A/Server/Server/X.h` 로 바꿨다. 정규식 하나의 sub 는 바꾼 자리를 다시 보지 않는다.
    전체 경로는 낱말 경계로 — `Engine/A/B.h` 가 `MyEngine/A/B.h` 안에서 바뀌지 않게(`Source/Engine/A/B.h` 처럼 앞에 `/` 가 오는 것은 바꾼다)."""
    map_new: dict[str, str] = {}
    list_alternative: list[tuple[int, str]] = []
    for old, new in map_file.items():
        map_new[old] = new
        list_alternative.append((len(old), re.escape(old) + r"(?![\w.])"))
    for old, new in list_folder_prefix:
        map_new[old] = new
        list_alternative.append((len(old), re.escape(old)))
    list_alternative.sort(key=lambda item: -item[0])
    # 루트 기준 경로(`cmake/Modules/…`)는 바깥 경로 가운데(`emscripten/cmake/Modules/Platform/`)와 겹친다 — `낱말/` 뒤는 받지 않고 `${ROOT}/` 뒤만 받는다.
    # Source 기준은 `Source/Engine/…` 처럼 앞에 폴더가 붙는 것이 정상이라 그대로 둔다.
    root_guard = r"(?<![\w.-]/)" if root_base else ""
    pattern = re.compile(r"(?<![\w.-])" + root_guard + "(?:" + "|".join(part for _, part in list_alternative) + ")")
    return pattern, lambda match: map_new[match.group(0)]


def check_path_rule_single_pass() -> None:
    """자기 아래 폴더로 옮겨도 한 번만 바뀌는지의 자체 확인."""
    pattern, replacement = make_path_rule({"A/X.h": "A/Server/X.h"}, [("A/", "A/Server/")])
    text = '#include "A/X.h"\n#include "A/Y.h"\nMyA/X.h'
    expected = '#include "A/Server/X.h"\n#include "A/Server/Y.h"\nMyA/X.h'
    assert pattern.sub(replacement, text) == expected, pattern.sub(replacement, text)
    pattern, replacement = make_path_rule({}, [("cmake/Modules/", "cmake/Toolchain/")], root_base=True)
    text = "${CMAKE_SOURCE_DIR}/cmake/Modules/X.cmake emsdk/cmake/Modules/Platform/E.cmake cmake/Modules/Y.cmake"
    expected = "${CMAKE_SOURCE_DIR}/cmake/Toolchain/X.cmake emsdk/cmake/Modules/Platform/E.cmake cmake/Toolchain/Y.cmake"
    assert pattern.sub(replacement, text) == expected, pattern.sub(replacement, text)


def main_move(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(prog="py -3 -m Scripts move-source", description="소스를 옮기고 참조를 고친다")
    parser.add_argument("pair", nargs="+", help="--base 기준 old=new (파일 또는 폴더)")
    parser.add_argument("--base", default="Source", help="경로의 기준 폴더(저장소 루트 기준, 기본 Source — 루트는 `.`)")
    parser.add_argument("--dry-run", action="store_true", help="옮길 파일 목록만 찍는다")
    args = parser.parse_args(argv)
    check_path_rule_single_pass()

    root = get_project_root()
    source = root / args.base   # pathlib 가 `.` 을 접는다
    base_prefix = "" if source == root else source.relative_to(root).as_posix() + "/"
    map_file = expand_moves(source, args.pair)
    # 폴더 접두어는 옮기기 전에 잰다 — 옮긴 뒤에는 옛 폴더가 없어 규칙이 비고, 폴더 이동의 설정 · 문서 경로(`Source/X/Old/`)가 그대로 남았다.
    list_folder_prefix = folder_prefixes(args.pair, source)
    if args.dry_run:
        for old, new in map_file.items():
            print(f"{old} -> {new}")
        return ExitCode.PASSED

    # 짧은 include 는 옮기기 전 자리에서 풀어야 하므로 내용 재작성을 먼저 계산한다.
    map_moved_text: dict[str, str] = {}
    for old, new in map_file.items():
        path = source / old
        if path.suffix.lower() in CPP_SUFFIX:
            map_moved_text[new] = rewrite_bare_includes(read_text(path), old.rpartition("/")[0], new.rpartition("/")[0], source, map_file)
    list_neighbour_text: dict[Path, str] = {}
    moved_set = set(map_file)
    for path in list_tracked_text(root):
        relative = path.relative_to(source).as_posix() if path.is_relative_to(source) else None
        if relative is None or relative in moved_set or path.suffix.lower() not in CPP_SUFFIX:
            continue
        folder = relative.rpartition("/")[0]
        text = read_text(path)
        rewritten = rewrite_bare_includes(text, folder, folder, source, map_file)
        if rewritten != text:
            list_neighbour_text[path] = rewritten

    for old, new in map_file.items():
        (source / new).parent.mkdir(parents=True, exist_ok=True)
        for _ in range(20):  # IDE 의 git 이 잠깐 잡는 index.lock 은 기다리면 풀린다 — 반쯤 옮긴 채 멈추지 않게
            result = run_git(["mv", f"{base_prefix}{old}", f"{base_prefix}{new}"], cwd=root)
            if result.succeeded or "index.lock" not in result.stderr:
                break
            time.sleep(0.5)
        if not result.succeeded:
            raise RuntimeError(f"git mv {old} 실패: {result.stderr.strip()}")
    for new, text in map_moved_text.items():
        write_text(source / new, text)
    for path, text in list_neighbour_text.items():
        write_text(path, text)
    for old in map_file:
        folder = (source / old).parent
        while folder != source and folder.is_dir() and not any(folder.iterdir()):
            folder.rmdir()
            folder = folder.parent

    list_rule = [make_path_rule(map_file, list_folder_prefix, root_base=base_prefix == "")]
    # `@file` 은 최상위 폴더 없이 적은 곳도 있다(`@file Utility/X.h`).
    list_rule += [(re.compile(r"(@file\s+)" + re.escape(old.partition("/")[2]) + r"\b"), lambda match, new=new: match.group(1) + new) for old, new in map_file.items() if "/" in old]
    changed_cpp_set = {source / new for new in map_moved_text} | set(list_neighbour_text)
    for path in list_tracked_text(root):
        text = read_text(path)
        def rewrite_paths(part: str) -> str:
            for pattern, replacement in list_rule:
                part = pattern.sub(replacement, part)
            return part

        rewritten = rewrite_unmarked_lines(text, rewrite_paths)
        if rewritten != text:
            write_text(path, rewritten)
            if path.suffix.lower() in CPP_SUFFIX:
                changed_cpp_set.add(path)
    list_changed_cpp = sorted(changed_cpp_set)

    from lint.format import FormatIncludeOrderFixer, run_clang_format_batch

    FormatIncludeOrderFixer().process_files(list_changed_cpp, check_only=False)
    run_clang_format_batch(list_changed_cpp, cwd=root)  # 경로 길이가 바뀌면 정렬 칸이 어긋난다
    print(f"[move-source] {len(map_file)} 파일을 옮기고 C++ {len(list_changed_cpp)} 파일의 참조 · include 순서를 고쳤다")
    return ExitCode.PASSED


def main_rename(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(prog="py -3 -m Scripts rename-symbol", description="C++ 이름을 낱말 경계로 바꾼다")
    parser.add_argument("pair", nargs="+", help="Old=New")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--force", action="store_true", help="새 이름이 이미 쓰여도 바꾼다")
    parser.add_argument("--regex", action="store_true", help="Old 를 정규식(낱말 경계 없이)으로, New 를 치환식으로 — 이름 일부를 바꾼다. 충돌 검사는 하지 않으니 빌드로 확인한다")
    parser.add_argument("--include-strings", action="store_true",
                        help="C 계열 소스의 문자열 리터럴 안도 바꾼다(기본은 건너뛰고 그 자리를 알린다 — 매직 · JSON 키 · 내보내기 기호는 바깥 철자다)")
    args = parser.parse_args(argv)
    check_rewrite_outside_strings()
    check_path_rule_single_pass()
    check_rewrite_unmarked_lines()
    check_serialized_rename()

    root = get_project_root()
    list_pair = [pair.partition("=")[::2] for pair in args.pair]
    list_skipped: list[str] = []

    def rewrite_file(path: Path, text: str, rewrite) -> str:
        suffix = path.suffix.lower()
        if args.include_strings or (suffix not in C_LIKE_SUFFIX and suffix != ".py"):
            return rewrite_rename_target(path.relative_to(root).as_posix(), text, rewrite)
        rewritten, skipped_count = (rewrite_python_code if suffix == ".py" else rewrite_outside_strings)(text, rewrite)
        if skipped_count:
            list_skipped.append(f"{path.relative_to(root).as_posix()}({skipped_count})")
        return rewritten

    def report_skipped() -> None:
        if list_skipped:
            print(f"[rename-symbol] 문자열 안은 건너뛰었다 — 데이터 · 바깥 이름이면 그대로 두고, 우리 이름이면 손으로 고치거나 --include-strings: "
                  + ", ".join(list_skipped[:20]) + (" …" if len(list_skipped) > 20 else ""))

    if args.regex:
        list_rule = [(re.compile(old), new) for old, new in list_pair]

        def rewrite_regex(part: str) -> str:
            for pattern, replacement in list_rule:
                part = pattern.sub(replacement, part)
            return part

        changed_count = 0
        list_changed: list[Path] = []
        for path in list_tracked_text(root):
            text = read_text(path)
            rewritten = rewrite_file(path, text, rewrite_regex)
            if rewritten != text:
                changed_count += 1
                list_changed.append(path)
                if not args.dry_run:
                    write_text(path, rewritten)
        print(f"[rename-symbol] {changed_count} 파일" + (" (dry-run)" if args.dry_run else ""))
        report_skipped()
        return ExitCode.PASSED if args.dry_run else format_changed(root, list_changed)
    # 문서 · 데이터 · 설정도 함께 바꾼다 — 빼면 개명마다 규칙 문서의 예시 이름 · 데이터의 칸 이름 · 린트 예외 표가 낡는다.
    # 겹침 검사는 코드만 본다(문서는 새 이름을 미리 적을 수 있다).
    code_suffix = CPP_SUFFIX | {".xxx", ".natvis", ".txt", ".cmake"}
    list_file = list_tracked_text(root)
    map_text = {path: read_text(path) for path in list_file}
    clash = False
    for old, new in list_pair:
        if not old or not new:
            raise ValueError("Old=New 모양이어야 한다")
        list_clash = [path for path, text in map_text.items() if path.suffix.lower() in code_suffix and re.search(rf"\b{re.escape(new)}\b", text)]
        if list_clash and not args.force:
            clash = True
            print(f"[rename-symbol] '{new}' 이 이미 쓰인다: " + ", ".join(str(path.relative_to(root)) for path in list_clash[:5]), file=sys.stderr)
    # 리플렉션 프로퍼티는 데이터에 직렬화 이름으로 적힌다 — 그 쌍도 함께 바꾸고, 어느 멤버 것인지 가릴 수 없으면 멈춘다(`rename_scope.plan_serialized_rename`).
    map_relative = {path.relative_to(root).as_posix(): text for path, text in map_text.items()}
    properties = collect_reflected_properties({relative: text for relative, text in map_relative.items() if relative.startswith("Source/") and relative.endswith(".h")})
    list_serialized: list[tuple[str, str]] = []
    for old, new in list_pair:
        serialized, list_reason = plan_serialized_rename(properties, map_relative, old, new)
        for reason in list_reason if not args.force else []:
            clash = True
            print(f"[rename-symbol] {old}: {reason}. 데이터를 손으로 옮긴 뒤 --force", file=sys.stderr)
        if serialized is not None:
            list_serialized.append(serialized)
    if clash:
        return ExitCode.VIOLATION

    def rewrite_serialized(part: str) -> str:
        for serialized_old, serialized_new in list_serialized:
            part = replace_serialized_name(part, serialized_old, serialized_new)[0]
        return part

    list_word_rule = [(re.compile(rf"\b{re.escape(old)}\b"), new) for old, new in list_pair]

    def rewrite_words(part: str) -> str:
        for pattern, new in list_word_rule:
            part = pattern.sub(lambda _, new=new: new, part)
        return part

    changed_count = 0
    list_changed = []
    for path, text in map_text.items():
        rewritten = rewrite_file(path, text, rewrite_words)
        if list_serialized:   # 직렬화 이름은 문자열 · 데이터 안에 산다 — 문자열 건너뛰기와 따로 건다
            rewritten = rewrite_rename_target(path.relative_to(root).as_posix(), rewritten, rewrite_serialized)
        if rewritten != text:
            changed_count += 1
            list_changed.append(path)
            if not args.dry_run:
                write_text(path, rewritten)
    print(f"[rename-symbol] {changed_count} 파일" + (" (dry-run)" if args.dry_run else "")
          + "".join(f" · 직렬화 이름 {serialized_old} → {serialized_new}" for serialized_old, serialized_new in list_serialized))
    report_skipped()
    return ExitCode.PASSED if args.dry_run else format_changed(root, list_changed)


def format_changed(root: Path, list_path: list[Path]) -> int:
    """이름 길이가 바뀌면 clang-format 의 정렬 칸이 어긋난다 — 바꾼 C++ 파일만 다시 포맷한다."""
    from lint.format import run_clang_format_batch

    list_cpp = [path for path in list_path if path.suffix.lower() in CPP_SUFFIX]
    return run_clang_format_batch(list_cpp, cwd=root) if list_cpp else ExitCode.PASSED
