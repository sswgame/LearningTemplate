"""
식별자 이름을 저장소 전체에서 한 번에 바꾼다. 코드(Source · Test · Scripts · Tools) · 데이터(Content: 리플렉션 칸 · 감시 값 · 개발 명령 이름이 글로 든다) ·
설정(Config) · 문서(docs · 스킬). IDE 의 "Rename Symbol"(VS Code F2 · Visual Studio Ctrl+R,R) 자리다. 언리얼은 CoreRedirects 로 옛 이름을 읽어 주지만
이 저장소는 옛 이름을 남기지 않고 데이터까지 한 번에 옮긴다.

  py -3 -m Scripts rename-symbol isCursorCaptured=shouldShowCursor --why "실제 값은 UI 가 커서를 보이길 바란다" [--dry-run]  # keep-old-name
  py -3 -m Scripts rename-symbol --pairs Config/Rules/Renames/<표>.json [--reapply]

안전 규칙(기본은 안전한 쪽, 풀려면 옵션):
- 낱말 경계로만 바꾼다(부분 일치 없음). 옛 이름을 일부러 적은 줄은 `keep-old-name` 표지로 남고, 금지 이름 표의 키 · 이름 기준선 · 쌍 표는 지킨다(`common/rename_scope.py`).
- C 계열(C++ · HLSL) · 파이썬 소스의 문자열 리터럴 안은 건너뛰고 그 파일을 알린다 — 파일 매직("OggS") · JSON 키 · 내보내기 기호처럼 바깥이 정한 철자다(`--include-strings`).
- 짧거나 흔한 이름(낱말 하나 · 6 글자 미만, 멤버는 4 글자 미만)은 거절한다 — 낱말 치환이 엉뚱한 식별자를 바꾼다(`--allow-common-name`).
- 새 이름이 코드에 이미 있으면 겹친 파일을 알리고 멈춘다(`--force`, 쌍 표는 `"allowExisting": true`). `--reapply` 는 병합 뒤 다시 돌리기라 겹침을 보지 않는다.
- 리플렉션 프로퍼티(`PROPERTY` 가 붙은 `_` 멤버)는 데이터의 직렬화 이름(`_bFooBar` → `fooBar`)도 함께 바꾸고, 그 이름을 다른 멤버도 쓰거나 흔한 낱말이면 멈춘다
  (`--force`, 쌍 표는 데이터를 손으로 옮긴 뒤 `"serializedName": false`).
- 옛 이름을 `_bannedNames`(`rename_scope.BANNED_NAMES_PATH`)에 올려 다른 갈래 · 붙여 넣은 코드가 되살리지 못하게 하고 이름 기준선을 줄인다(`--no-ban`, 쌍 표는 `"ban": false`).
- 형식 이름과 같은 줄기의 소스 파일(`Old.h` · `TestOld.cpp`)은 `git mv` 로 옮긴다 — include 경로는 같은 치환이 따라간다.
쌍 표: `[{"old": …, "new": …, "why": …}, …]`. `"scope": "cmake"` 면 CMake 파일(`*.cmake` · `CMakeLists.txt`)만 바꾼다(CMake 지역 변수 — 낙타 · 밑줄 꼴만 받는다).
"""

from __future__ import annotations

import argparse
import bisect
import datetime
import io
import json
import re
import sys
import tokenize
from pathlib import Path
from typing import Sequence

from common.command import Command, ScriptContext
from common.exit_code import ExitCode
from common.git import run_git_or_raise
from dev.rename_scope import (
    BANNED_NAMES_PATH, C_LIKE_SUFFIX, CPP_SUFFIX, KEEP_OLD_NAME_MARKER, TEXT_SUFFIX, collect_reflected_properties, dump_naming_table, is_rename_table,
    list_tracked_text, plan_serialized_rename, read_text, replace_serialized_name, rewrite_rename_target, write_text,
)

TAG = "[rename-symbol]"
#: 데이터 · 설정의 글 형식 가운데 소스 이동(`move-source`)이 보지 않는 것 — 머티리얼 칸 이름 · 설정 키에 식별자가 든다.
RENAME_TEXT_SUFFIX = TEXT_SUFFIX | {".material", ".ini", ".yaml", ".yml"}
#: 새 이름이 이미 있는지 보는 코드 파일(문서는 새 이름을 미리 적을 수 있어 보지 않는다).
CLASH_SUFFIX = CPP_SUFFIX | {".xxx", ".natvis", ".txt", ".cmake"}
CMAKE_SCOPE = "cmake"
MIN_LENGTH = 6
MIN_MEMBER_LENGTH = 4
WORD_SPLIT = re.compile(r"[A-Z]+(?![a-z])|[A-Z]?[a-z0-9]+")
IDENTIFIER = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")

# C 계열 소스의 문자열 · 문자 리터럴과 주석. 문자열 안은 기본으로 건너뛴다 — 약어 일괄 변경(1dafbae0c9)이 그 안까지 바꿔 .ogg 디코드 · 구매 응답 ·
# 머티리얼 깃발을 깨뜨렸다. 주석과 `#include "…"` 경로는 우리 이름이라 바꾼다(형식 이름 파일을 `git mv` 로 옮기면 include 가 따라가야 한다).
C_LITERAL_OR_COMMENT = re.compile(
    r'(?P<include>^[ \t]*#[ \t]*include[ \t]*"[^"\n]*")'
    r'|(?P<comment>//[^\n]*|/\*.*?\*/)'
    r'|(?:u8|[uUL])?R"(?P<delim>[^()\\\s]{0,16})\(.*?\)(?P=delim)"'
    r'|(?:u8|[uUL])?"(?:\\.|[^"\\\n])*"'
    r"|'(?:\\.|[^'\\\n])+'",
    re.S | re.M,
)
#: 파이썬 토큰 가운데 건너뛸 글 — 스크립트의 문자열은 테스트 글 · 바깥 철자라, 금지 명사 이전(1692df1e3a)이
#: NamingWords 테스트 글 `hasPendingWork` 를 바꿔 자체 테스트가 늘 졌다.
PYTHON_LITERAL_TOKEN = {tokenize.STRING, tokenize.COMMENT} | ({tokenize.FSTRING_MIDDLE} if hasattr(tokenize, "FSTRING_MIDDLE") else set())


def count_words(name: str) -> int:
    return len([part for chunk in name.strip("_").split("_") for part in WORD_SPLIT.findall(chunk)])


def check_pair(old: str, new: str, scope: str | None = None, allow_common_name: bool = False) -> str | None:
    """바꿀 수 없는 쌍이면 그 까닭입니다."""
    if IDENTIFIER.fullmatch(old) is None or IDENTIFIER.fullmatch(new) is None:
        return "식별자가 아니다"
    if old == new:
        return "같은 이름이다"
    if scope == CMAKE_SCOPE:
        # CMake 명령 · 키워드는 모두 소문자 한 낱말이나 대문자다 — 낙타 꼴 · 밑줄 이름만 우리 것이라 낱말 단위 치환이 안전하다
        is_ours = re.search(r"[a-z][A-Z]|_", old) is not None
        return None if is_ours else f"'{old}' 는 CMake 낱말과 겹칠 수 있는 꼴이다 — 손으로 옮긴다"
    if scope is not None:
        return f"모르는 scope '{scope}'"
    if allow_common_name:
        return None
    member = old.startswith("_")   # 멤버(`_bNot`)는 머리 `_` 가 있어 짧아도 다른 식별자와 겹치지 않는다
    is_common = len(old) < MIN_MEMBER_LENGTH if member else (len(old) < MIN_LENGTH or count_words(old) < 2)
    if is_common:
        return f"'{old}' 는 흔한 이름이라 낱말 치환이 엉뚱한 식별자를 바꾼다 — 손으로 옮기거나 --allow-common-name"
    return None


def is_cmake_file(path: Path) -> bool:
    return path.suffix.lower() == ".cmake" or path.name == "CMakeLists.txt"


def is_in_scope(path: Path, pair: dict) -> bool:
    return pair.get("scope") != CMAKE_SCOPE or is_cmake_file(path)


def replace_word(text: str, old: str, new: str) -> tuple[str, int]:
    if old not in text:   # 정규식 전에 거른다 — 쌍 수백 개 × 저장소 전체 파일이면 정규식만으로 몇 분이 걸린다
        return text, 0
    return re.compile(r"(?<![A-Za-z0-9_])" + re.escape(old) + r"(?![A-Za-z0-9_])").subn(new, text)


def find_literal_spans(path: Path, text: str) -> list[tuple[int, int]]:
    """C 계열 · 파이썬 글에서 건너뛸 문자열 리터럴(파이썬은 주석도)의 [시작, 끝) 구간입니다. 그 밖의 형식은 빈 목록입니다."""
    suffix = path.suffix.lower()
    if suffix in C_LIKE_SUFFIX:
        return [match.span() for match in C_LITERAL_OR_COMMENT.finditer(text) if match.group("comment") is None and match.group("include") is None]
    if suffix != ".py":
        return []
    list_line_start = [0]
    for line in text.splitlines(keepends=True):
        list_line_start.append(list_line_start[-1] + len(line))
    list_span = []
    for token in tokenize.generate_tokens(io.StringIO(text).readline):
        if token.type in PYTHON_LITERAL_TOKEN:
            list_span.append((list_line_start[token.start[0] - 1] + token.start[1], list_line_start[token.end[0] - 1] + token.end[1]))
    return list_span


def find_marked_spans(text: str) -> list[tuple[int, int]]:
    """옛 이름을 일부러 적은 줄(`keep-old-name` 표지)의 [시작, 끝) 구간입니다."""
    if KEEP_OLD_NAME_MARKER not in text:
        return []
    list_span = []
    position = 0
    for line in text.splitlines(keepends=True):
        if KEEP_OLD_NAME_MARKER in line:
            list_span.append((position, position + len(line)))
        position += len(line)
    return list_span


def is_inside(list_span: Sequence[tuple[int, int]], position: int) -> bool:
    index = bisect.bisect_right(list_span, (position, sys.maxsize)) - 1
    return index >= 0 and list_span[index][0] <= position < list_span[index][1]


def rename_in_text(path: Path, relative: str, text: str, old: str, new: str, include_strings: bool) -> tuple[str, int]:
    """
    @p text 의 @p old 를 낱말 경계로 @p new 로 바꾼다. 표지 줄은 지키고, C 계열 · 파이썬은 문자열 안을 건너뛴다(@p include_strings 면 바꾼다).
    반환은 (바꾼 글, 문자열 안이라 건너뛴 수). 금지 이름 표 · 이름 기준선 · 쌍 표는 `rewrite_rename_target` 이 따로 다룬다.
    """
    if old not in text:
        return text, 0
    if is_rename_table(relative):
        return rewrite_rename_target(relative, text, lambda part: replace_word(part, old, new)[0]), 0
    list_marked = find_marked_spans(text)
    list_literal = [] if include_strings else sorted(find_literal_spans(path, text))
    skipped = 0

    def replace(match: re.Match[str]) -> str:
        nonlocal skipped
        if is_inside(list_marked, match.start()):
            return match.group(0)
        if is_inside(list_literal, match.start()):
            skipped += 1
            return match.group(0)
        return new

    return re.compile(r"(?<![A-Za-z0-9_])" + re.escape(old) + r"(?![A-Za-z0-9_])").sub(replace, text), skipped


def find_files_named(list_file: Sequence[Path], root: Path, stem: str) -> list[Path]:
    """파일 줄기가 @p stem 인 `Source/` 파일(형식 이름 = 파일 이름) — 테스트 파일 `Test<stem>.cpp` 도."""
    source = root / "Source"
    return [path for path in list_file if path.is_relative_to(source) and path.stem in (stem, f"Test{stem}")]


def format_changed(root: Path, list_path: list[Path]) -> int:
    """이름 길이가 바뀌면 clang-format 의 정렬 칸이 어긋난다 — 바꾼 C++ 파일만 다시 포맷한다."""
    from lint.format_files import run_clang_format_batch

    list_cpp = [path for path in list_path if path.suffix.lower() in CPP_SUFFIX]
    return run_clang_format_batch(list_cpp, cwd=root) if list_cpp else ExitCode.PASSED


def check_pairs(root: Path, list_pair: Sequence[dict], map_text: dict[Path, str], options: argparse.Namespace) -> tuple[int, dict[str, tuple[str, str]]]:
    """쌍마다 안전 규칙을 본다. (문제 수, 옛 멤버 이름 → 함께 바꿀 직렬화 이름 쌍)."""
    problems = 0
    map_relative = {path.relative_to(root).as_posix(): text for path, text in map_text.items()}
    properties = collect_reflected_properties({relative: text for relative, text in map_relative.items() if relative.startswith("Source/") and relative.endswith(".h")})
    serialized_pairs: dict[str, tuple[str, str]] = {}
    for pair in list_pair:
        old, new = pair.get("old", ""), pair.get("new", "")
        reason = check_pair(old, new, pair.get("scope"), options.allow_common_name)
        if reason is not None:
            print(f"{TAG} 거절 {old} → {new}: {reason}", file=sys.stderr)
            problems += 1
            continue
        if pair.get("scope") is None and pair.get("serializedName", True):
            serialized, list_reason = plan_serialized_rename(properties, map_relative, old, new)
            for reason in list_reason if not options.force else []:
                print(f"{TAG} 거절 {old} → {new}: {reason}. 데이터를 손으로 옮긴 뒤 --force(쌍 표는 \"serializedName\": false)", file=sys.stderr)
                problems += 1
            if serialized is not None:
                serialized_pairs[old] = serialized
        if options.reapply or options.force or pair.get("allowExisting") is True or pair.get("scope") == CMAKE_SCOPE:
            continue
        list_clash = [path for path, text in map_text.items() if path.suffix.lower() in CLASH_SUFFIX and replace_word(text, new, new)[1] > 0]
        if list_clash:
            print(f"{TAG} 거절 '{new}' 이 이미 쓰인다: " + ", ".join(path.relative_to(root).as_posix() for path in list_clash[:5])
                  + " — 겹치는 이름이면 다른 이름을, 일부러 합치는 것이면 --force(쌍 표는 \"allowExisting\": true)", file=sys.stderr)
            problems += 1
    return problems, serialized_pairs


def rename_all(root: Path, list_pair: Sequence[dict], options: argparse.Namespace) -> int:
    list_file = list_tracked_text(root, RENAME_TEXT_SUFFIX)
    map_text = {path: read_text(path) for path in list_file}
    problems, serialized_pairs = check_pairs(root, list_pair, map_text, options)
    if problems:
        return ExitCode.VIOLATION

    stamp = datetime.date.today().isoformat()
    set_changed: set[Path] = set()
    list_skipped: list[str] = []
    for pair in list_pair:
        old, new = pair["old"], pair["new"]
        touched: list[Path] = []
        for path in list_file:
            if not is_in_scope(path, pair):
                continue
            relative = path.relative_to(root).as_posix()
            changed, skipped = rename_in_text(path, relative, map_text[path], old, new, options.include_strings)
            if old in serialized_pairs:   # 직렬화 이름은 문자열 · 데이터 안에 산다 — 문자열 건너뛰기와 따로 건다
                changed = rewrite_rename_target(relative, changed, lambda part, relative=relative: replace_serialized_name(part, *serialized_pairs[old], relative)[0])
            if skipped:
                list_skipped.append(f"{relative}({skipped})")
            if changed != map_text[path]:
                map_text[path] = changed
                touched.append(path)
        data = [path for path in touched if path.relative_to(root).as_posix().startswith("Content/")]
        print(f"{TAG} {old} → {new}: 파일 {len(touched)}" + (f" · 데이터 {len(data)}" if data else "")
              + (" · 직렬화 이름 {} → {}".format(*serialized_pairs[old]) if old in serialized_pairs else "") + (" (dry-run)" if options.dry_run else ""))
        if options.dry_run:
            continue
        for path in touched:
            write_text(path, map_text[path])
            set_changed.add(path)
        for path in find_files_named(list_file, root, old) if pair.get("scope") is None else []:
            target = path.with_name(path.name.replace(old, new, 1))
            run_git_or_raise(["mv", str(path), str(target)], cwd=root)
            list_file = [target if item == path else item for item in list_file]
            map_text[target] = map_text.pop(path)
            set_changed.discard(path)
            set_changed.add(target)
            print(f"    파일 {path.relative_to(root).as_posix()} → {target.name}")
        if pair.get("ban", True) is False or options.no_ban:
            continue
        naming_path = root / BANNED_NAMES_PATH
        table = json.loads(naming_path.read_text(encoding="utf-8"))
        table["_bannedNames"][old] = f"{new} — {pair.get('why', '')}({stamp})"
        naming_path.write_text(dump_naming_table(table), encoding="utf-8", newline="\n")
        map_text[naming_path] = naming_path.read_text(encoding="utf-8")
    if list_skipped:
        print(f"{TAG} 문자열 안은 건너뛰었다 — 데이터 · 바깥 이름이면 그대로 두고, 우리 이름이면 손으로 고치거나 --include-strings: "
              + ", ".join(list_skipped[:20]) + (" …" if len(list_skipped) > 20 else ""))
    if options.dry_run:
        return ExitCode.PASSED
    return format_changed(root, sorted(set_changed))


class RenameSymbolCommand(Command):
    name = "rename-symbol"
    help = "식별자 이름을 코드 · 데이터 · 설정 · 문서에서 한 번에 바꾼다(Old=New 또는 --pairs 표, --reapply) — 문자열 안 · 흔한 이름 · 이미 있는 새 이름은 멈추고, 옛 이름은 금지 이름 표에 올린다"

    def add_arguments(self, parser: argparse.ArgumentParser) -> None:
        parser.add_argument("pair", nargs="*", help="Old=New")
        parser.add_argument("--pairs", help="쌍 표(JSON 목록: old · new · why[, scope · ban · serializedName · allowExisting])")
        parser.add_argument("--why", default="", help="금지 이름 표에 적을 까닭(Old=New 쌍)")
        parser.add_argument("--dry-run", action="store_true")
        parser.add_argument("--reapply", action="store_true", help="병합 뒤 같은 쌍을 다시 돌린다(새 이름이 이미 있어도 옛 이름을 마저 바꾼다)")
        parser.add_argument("--force", action="store_true", help="새 이름이 이미 쓰이거나 직렬화 이름을 가를 수 없어도 바꾼다")
        parser.add_argument("--include-strings", action="store_true", help="C 계열 · 파이썬 소스의 문자열 리터럴 안도 바꾼다")
        parser.add_argument("--allow-common-name", action="store_true", help="짧거나 흔한 이름도 바꾼다")
        parser.add_argument("--no-ban", action="store_true", help="옛 이름을 금지 이름 표에 올리지 않는다(지역 변수 — 옛 철자가 다른 뜻으로 다시 쓰일 수 있다)")

    def run(self, context: ScriptContext, args: argparse.Namespace) -> int:
        list_pair: list[dict] = json.loads(Path(args.pairs).read_text(encoding="utf-8")) if args.pairs else []
        for text in args.pair:
            old, separator, new = text.partition("=")
            if not separator:
                self.parser.error(f"'{text}' 은 Old=New 모양이 아니다")
            list_pair.append({"old": old, "new": new, "why": args.why})
        if not list_pair:
            self.parser.error("Old=New 쌍이나 --pairs 표를 준다")
        return rename_all(context.root, list_pair, args)
