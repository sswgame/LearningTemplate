"""
Scripts/dev/byte_reader_values.py

`ByteReader` 의 출력 인자 읽기(`reader.readPod( x )` · `readString( s )` · `readCount( n, c )` …)를 값 반환 읽기로 바꾼다.
출력 인자 꼴은 반환 `bool` 을 버리면 실패를 잃었고(`(void)` · 무시), 실패 때 출력 칸이 옛 값으로 남았다. 값 읽기는 `[[nodiscard]]` 이고
실패는 끈적한 깃발(`hasOverflowed()` · `finish()`) 하나로 본다(`Core/Serialization/ByteStream.h`).

  py -3 -m Scripts byte-reader-values [경로…]          # 바꾼다(경로 없으면 Source)
  py -3 -m Scripts byte-reader-values --check [경로…]  # 남은 옛 모양을 찍고, 있으면 실패
  py -3 -m Scripts byte-reader-values --self-check     # 변환 자체 테스트

- 받는 쪽이 `ByteReader` 인 자리만 본다 — 이름 앞의 가장 가까운 선언(`ByteReader reader( … )` · `ByteReader& chunk`)으로 정한다.
  같은 이름의 `BitReader` · `Archive` 읽기는 건드리지 않는다.
- 바꾸는 문장 모양: `if ( 읽기 == false [|| …] )` · `if ( 읽기 )` · `[const] bool b = 읽기 && 읽기 [&& …];` · `return 읽기 && …;` ·
  `SW_REQUIRE_TRUE( 읽기 )` · `SW_EXPECT_TRUE/FALSE( 읽기 )` · `(void)읽기;`. 읽기를 문장 앞으로 꺼내 `x = reader.readPod<T>();` 로 적고,
  조건 자리는 `reader.hasOverflowed()` 로 바꾼다. 읽기가 조건의 앞쪽에 모여 있지 않으면(앞 항이 읽기를 건너뛰게 하는 자리) 바꾸지 않고 알린다.
- `readVarUint` · `readVarInt` 는 출력 칸의 선언 형식(32 · 64 비트)으로 이름을 고른다(`readVarUint32` …). 형식을 못 찾으면 알린다.
- 병합 뒤 충돌 파일에 다시 돌린다(이미 바꾼 자리는 그대로). 린트 `cpp-shape` 가 `find_sites` 로 같은 판정을 한다.
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
from lint.cpp_shape import strip_code  # noqa: E402

TAG = "[byte-reader-values]"
SKIP_PARTS = ("/ThirdParty/", "/Generated/", ".gen.")   # 폴더 마디로 본다 — 이름에 Generated 가 든 소스(GeneratedFiles.cpp)를 빼지 않는다
CPP_SUFFIX = (".h", ".cpp", ".inl")
RECEIVER_TYPE = "ByteReader"

OLD_ARITY = {"readVarUint": 1, "readVarInt": 1, "readString": 1, "readStringView": 1, "readByteVector": 1, "readPod": 1, "readView": 2, "readCount": 2}
VAR_INT_NAME = {("readVarUint", "uint64"): "readVarUint", ("readVarUint", "uint32"): "readVarUint32", ("readVarInt", "int64"): "readVarInt", ("readVarInt", "int32"): "readVarInt32"}
CALL_PATTERN = re.compile(r"\b(\w+)\s*(\.|->)\s*(" + "|".join(OLD_ARITY) + r")\s*\(")
IDENTIFIER = re.compile(r"[A-Za-z_]\w*")
MEMBER_ACCESS = re.compile(r"[A-Za-z_]\w*(?:\s*(?:\.|->)\s*[A-Za-z_]\w*)+")


@dataclass(frozen=True)
class Site:
    """옛 모양 읽기 하나. `start`/`end` 는 부르는 글(받는 쪽부터 닫는 괄호까지), `out` 은 출력 인자 글, `call` 은 값 읽기 글(만들 수 없으면 None)."""

    start: int
    end: int
    line: int
    receiver: str
    out: str
    call: str | None
    problem: str | None


# ------------------------------------------------------------------------------
# 글 훑기
# ------------------------------------------------------------------------------
def match_paren(code: str, open_index: int) -> int:
    """`code[open_index]` 의 여는 괄호 · 중괄호 · 대괄호의 짝 위치. 못 찾으면 -1."""
    depth = 0
    for index in range(open_index, len(code)):
        char = code[index]
        if char in "([{":
            depth += 1
        elif char in ")]}":
            depth -= 1
            if depth == 0:
                return index
    return -1


def split_top(code: str, start: int, end: int, separators: tuple[str, ...]) -> list[tuple[int, int]]:
    """`code[start:end]` 를 괄호 밖의 구분자(`,` · `&&` · `||`)로 나눈 구간들."""
    list_piece: list[tuple[int, int]] = []
    depth = 0
    piece_start = start
    index = start
    while index < end:
        char = code[index]
        if char in "([{":
            depth += 1
        elif char in ")]}":
            depth -= 1
        elif depth == 0:
            hit = next((separator for separator in separators if code.startswith(separator, index)), None)
            if hit is not None:
                list_piece.append((piece_start, index))
                index += len(hit)
                piece_start = index
                continue
        index += 1
    list_piece.append((piece_start, end))
    return list_piece


def find_declared_type(code: str, name: str, before: int) -> str | None:
    """`before` 앞에서 가장 가까운 `형식 [&|*] name` 선언의 형식 글(한정 · 꺾쇠 포함). 없으면 None."""
    pattern = re.compile(r"([A-Za-z_][\w:]*(?:<[^<>;(){}]*>)?)\s*[&*]?\s+" + re.escape(name) + r"\s*[;,)=({\[]")
    found = None
    for match in pattern.finditer(code, 0, before):
        if match.group(1) not in ("return", "else", "case", "delete", "new", "throw", "goto"):
            found = match.group(1)
    return found


def last_name(type_text: str) -> str:
    return re.sub(r"<.*>", "", type_text).split("::")[-1]


def get_indent(raw: str, index: int) -> str:
    line_start = raw.rfind("\n", 0, index) + 1
    return re.match(r"[ \t]*", raw[line_start:]).group(0)


# ------------------------------------------------------------------------------
# 자리 찾기
# ------------------------------------------------------------------------------
def make_value_call(code: str, raw: str, receiver: str, access: str, method: str, list_argument: list[str], call_start: int) -> tuple[str | None, str | None]:
    """값 읽기 글과 문제. 출력 인자는 마지막 인자다."""
    out = list_argument[-1]
    rest = ", ".join(list_argument[:-1])
    argument_text = f"( {rest} )" if rest else "()"
    if method in ("readVarUint", "readVarInt"):
        declared = find_declared_type(code, out, call_start) if IDENTIFIER.fullmatch(out) else None
        name = VAR_INT_NAME.get((method, last_name(declared or "")))
        if name is None:
            return None, f"`{out}` 의 선언 형식(32 · 64 비트)을 찾지 못했다 — 손으로 readVarUint · readVarUint32 · readVarInt · readVarInt32 를 고른다"
        return f"{receiver}{access}{name}{argument_text}", None
    if method == "readPod":
        declared = find_declared_type(code, out, call_start) if IDENTIFIER.fullmatch(out) else None
        if declared is not None and declared not in ("auto", "const"):
            type_text = declared
        elif IDENTIFIER.fullmatch(out) or MEMBER_ACCESS.fullmatch(out):
            type_text = f"decltype( {out} )"   # 괄호 없는 이름 · 멤버 접근의 decltype 은 선언 형식이다(참조가 붙지 않는다)
        else:
            return None, f"`{out}` 의 형식을 글로 정하지 못했다 — 손으로 readPod<T>() 를 적는다"
        return f"{receiver}{access}readPod<{type_text}>{argument_text}", None
    return f"{receiver}{access}{method}{argument_text}", None


def find_sites(raw: str, code: str) -> list[Site]:
    """`code` 는 `strip_code(raw)`. 받는 쪽이 ByteReader 인 출력 인자 읽기를 모두 낸다."""
    list_site: list[Site] = []
    for match in CALL_PATTERN.finditer(code):
        receiver, access, method = match.group(1), match.group(2), match.group(3)
        open_index = match.end() - 1
        close = match_paren(code, open_index)
        if close < 0:
            continue
        pieces = split_top(code, open_index + 1, close, (",",))
        list_argument = [raw[a:b].strip() for a, b in pieces]
        if list_argument == [""] or len(list_argument) != OLD_ARITY[method]:
            continue   # 이미 값 읽기(인자가 하나 적다)
        declared = find_declared_type(code, receiver, match.start())
        if declared is None or last_name(declared) != RECEIVER_TYPE:
            continue
        call, problem = make_value_call(code, raw, receiver, access, method, list_argument, match.start())
        list_site.append(Site(match.start(), close + 1, raw.count("\n", 0, match.start()) + 1, receiver, list_argument[-1], call, problem))
    return list_site


# ------------------------------------------------------------------------------
# 바꾸기
# ------------------------------------------------------------------------------
STATEMENT_HEAD = re.compile(r"(?:(?:const\s+)?bool\s+\w+\s*=|return\b|\(\s*void\s*\)|if\s*\(|(SW_(?:REQUIRE|EXPECT)_(?:TRUE|FALSE))\s*\()")


def find_statement_start(code: str, index: int) -> int:
    """`index` 를 담은 문장의 시작(괄호 밖의 앞 `;` · `{` · `}` 바로 뒤의 첫 글자)."""
    depth = 0
    cursor = index - 1
    while cursor >= 0:
        char = code[cursor]
        if char in ")]":
            depth += 1
        elif char in "([":
            if depth == 0:
                pass   # 문장 머리의 `if (` · 매크로 `(` 안이다 — 더 거슬러 간다
            else:
                depth -= 1
        elif char in ";{}" and depth == 0:
            break
        cursor -= 1
    cursor += 1
    while cursor < len(code) and code[cursor].isspace():
        cursor += 1
    return cursor


def classify_term(code: str, start: int, end: int, site_by_start: dict[int, Site]) -> tuple[Site | None, bool]:
    """조건 항 하나가 읽기면 (자리, 부정인지). 읽기가 아니면 (None, False)."""
    text = code[start:end].strip()
    offset = start + (len(code[start:end]) - len(code[start:end].lstrip()))
    negated = False
    if text.startswith("!"):
        negated = True
        offset += 1
        while code[offset].isspace():
            offset += 1
        text = code[offset:end].strip()
    site = site_by_start.get(offset)
    if site is None:
        return None, False
    tail = code[site.end:end].strip()
    if tail == "== false" and not negated:
        return site, True
    if tail == "" and not negated:
        return site, False
    if tail == "" and negated:
        return site, True
    return None, False


def rewrite_statement(raw: str, code: str, sites: list[Site], statement_start: int) -> tuple[int, int, str, int] | None:
    """문장 하나를 바꾼 (시작, 끝, 새 글, 바꾼 읽기 수). 바꿀 수 없으면 None."""
    head = STATEMENT_HEAD.match(code, statement_start)
    if head is None or re.search(r"else\s*$", code[:statement_start]):
        return None   # 모르는 문장이거나 `else if` — 앞에 꺼낼 자리가 없다
    site_by_start = {site.start: site for site in sites}
    text = head.group(0)
    macro = head.group(1)
    is_if = text.startswith("if")
    if is_if or macro:
        open_index = head.end() - 1
        expression_start, expression_end = open_index + 1, match_paren(code, open_index)
        statement_end = expression_end + 1
    else:
        expression_end = code.find(";", head.end())
        if expression_end < 0:
            return None
        expression_start, statement_end = head.end(), expression_end + 1
    expression = code[expression_start:expression_end]
    separator = "||" if "||" in expression else "&&"
    if "||" in expression and "&&" in expression:
        return None
    list_read: list[tuple[Site, bool]] = []
    rest: list[str] = []
    for start, end in split_top(code, expression_start, expression_end, (separator,)):
        site, negated = classify_term(code, start, end, site_by_start)
        if site is None:
            if any(start <= other.start < end for other in sites):
                return None   # 읽기가 다른 식 안에 있다
            rest.append(raw[start:end].strip())
        elif rest or site.call is None:
            return None   # 앞 항이 읽기를 건너뛰게 하거나 값 읽기를 만들 수 없다
        else:
            list_read.append((site, negated))
    if not list_read or len({site.receiver for site, _ in list_read}) != 1:
        return None
    negations = {negated for _, negated in list_read}
    if len(negations) != 1:
        return None
    negated = negations.pop()
    first = list_read[0][0]
    access = "->" if "->" in raw[first.start:first.end].split("(")[0] else "."
    failed = f"{first.receiver}{access}hasOverflowed()"
    indent = get_indent(raw, statement_start)
    hoisted = "".join(f"{site.out} = {site.call};\n{indent}" for site, _ in list_read)
    count = len(list_read)
    if macro:
        if rest or count != 1 or negated:
            return None
        verdict = macro.rsplit("_", 1)[0] + ("_FALSE" if macro.endswith("TRUE") else "_TRUE")
        return statement_start, statement_end, f"{hoisted}{verdict}( {failed} )", count
    if is_if:
        if separator == "||" or (count == 1 and not rest and negated):
            if not negated:
                return None
            condition = " || ".join([failed] + rest)
        elif negated:
            return None
        else:
            condition = " && ".join([f"{failed} == false"] + rest)
        return statement_start, statement_end, f"{hoisted}if ( {condition} )", count
    if text.startswith("("):
        if negated:
            return None
        if rest or count != 1:
            return None
        return statement_start, statement_end, hoisted.rstrip(), count
    if separator == "||":
        if not negated:
            return None
        condition = " || ".join([failed] + rest)
    elif negated:
        return None
    else:
        condition = " && ".join([f"{failed} == false"] + rest)
    return statement_start, statement_end, f"{hoisted}{text} {condition};", count


def rewrite_text(raw: str) -> tuple[str, int, list[Site]]:
    """바꾼 글, 바꾼 읽기 수, 남은 자리. 뒤 문장부터 하나씩 바꾸고 다시 찾는다."""
    done = 0
    tried: set[int] = set()
    while True:
        code = strip_code(raw)
        sites = find_sites(raw, code)
        candidates = sorted({find_statement_start(code, site.start) for site in sites} - tried, reverse=True)
        for statement_start in candidates:
            tried.add(statement_start)
            result = rewrite_statement(raw, code, sites, statement_start)
            if result is not None:
                start, end, text, count = result
                raw = raw[:start] + text + raw[end:]
                done += count
                tried = {index for index in tried if index < start}   # 앞쪽 자리는 위치가 그대로다
                break
        else:
            return raw, done, find_sites(raw, strip_code(raw))


def describe_site(site: Site, relative: str, check: bool) -> str:
    """남은 자리 한 줄 설명(린트 `cpp-shape` 도 쓴다)."""
    if site.problem:
        return site.problem
    if check:
        return f"ByteReader 는 값 읽기로(`{site.out} = {site.call}` · 실패는 hasOverflowed) — `py -3 -m Scripts byte-reader-values {relative}`"
    return "ByteReader 는 값 읽기로 — 문장 모양이 자동 변환 밖이다(손으로 `x = reader.readX(); if ( reader.hasOverflowed() )`)"


def is_target(relative: str) -> bool:
    return relative.startswith("Source/") and relative.endswith(CPP_SUFFIX) and not any(part in relative for part in SKIP_PARTS)


def list_files(project_root: Path, list_path: Sequence[str]) -> list[Path]:
    output = run_git_or_raise(["ls-files", "--", *(list_path or ["Source"])], cwd=project_root)
    return [project_root / relative for relative in output.splitlines() if is_target(relative)]


def run_self_check() -> int:
    head = "void f( ByteReader& reader, Archive& arch )\n{\n    uint32 count = 0;\n    uint64 big = 0;\n    Header header;\n    string text;\n"
    cases = {
        "    if ( reader.readCount( 4, count ) == false )\n        return;\n":
            "    count = reader.readCount( 4 );\n    if ( reader.hasOverflowed() )\n        return;\n",
        "    const bool bHead = reader.readPod( header ) && reader.readString( text ) && count > 0;\n":
            "    header = reader.readPod<Header>();\n    text = reader.readString();\n    const bool bHead = reader.hasOverflowed() == false && count > 0;\n",
        "    SW_REQUIRE_TRUE( reader.readVarUint( big ) );\n": "    big = reader.readVarUint();\n    SW_REQUIRE_FALSE( reader.hasOverflowed() );\n",
        "    SW_EXPECT_FALSE( reader.readVarUint( count ) );\n": "    count = reader.readVarUint32();\n    SW_EXPECT_TRUE( reader.hasOverflowed() );\n",
        "    return reader.readPod( entry._a ) && reader.readPod( entry._b );\n":
            "    entry._a = reader.readPod<decltype( entry._a )>();\n    entry._b = reader.readPod<decltype( entry._b )>();\n    return reader.hasOverflowed() == false;\n",
        "    if ( bKnown == false || reader.readString( text ) == false )\n        return;\n": None,
        "    const bool bCut = reader.readPod( header ) == false || reader.readCount( 4, count ) == false;\n":
            "    header = reader.readPod<Header>();\n    count = reader.readCount( 4 );\n    const bool bCut = reader.hasOverflowed();\n",
        "    if ( arch.readCount( 4, count ) == false )\n        return;\n": None,
        "    if ( reader.readString( text ) == false || text.empty() )\n        return;\n":
            "    text = reader.readString();\n    if ( reader.hasOverflowed() || text.empty() )\n        return;\n",
    }
    failures = 0
    for body, want in cases.items():
        source = head + body + "}\n"
        got, _, _ = rewrite_text(source)
        expected = source if want is None else head + want + "}\n"
        if got != expected:
            print(f"{TAG} self-check:\n{source}\n  got\n{got}\n  want\n{expected}")
            failures += 1
    left = find_sites(head + cases_left_body() + "}\n", strip_code(head + cases_left_body() + "}\n"))
    if len(left) != 1:
        print(f"{TAG} self-check: --check 는 남은 옛 모양 하나를 찾아야 한다 — {len(left)}")
        failures += 1
    print(f"{TAG} self-check {'OK' if failures == 0 else 'FAIL'}")
    return ExitCode.PASSED if failures == 0 else ExitCode.VIOLATION


def cases_left_body() -> str:
    return "    (void)reader.readString( text );\n    text = reader.readString();\n    (void)arch.readString( text );\n"


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="py -3 -m Scripts byte-reader-values", description="ByteReader 의 출력 인자 읽기를 값 반환 읽기로 바꾼다")
    parser.add_argument("paths", nargs="*", help="볼 경로(저장소 기준, 없으면 Source)")
    parser.add_argument("--check", action="store_true", help="바꾸지 않고 남은 옛 모양을 찍는다 — 있으면 실패")
    parser.add_argument("--self-check", action="store_true", help="변환 자체 테스트")
    args = parser.parse_args(argv)
    if args.self_check:
        return run_self_check()
    project_root = get_project_root()
    list_problem: list[str] = []
    changed_files = 0
    total = 0
    for path in list_files(project_root, args.paths):
        with path.open(encoding="utf-8", newline="") as file:
            raw = file.read()
        if CALL_PATTERN.search(raw) is None:
            continue
        relative = path.relative_to(project_root).as_posix()
        if args.check:
            new_raw, sites = raw, find_sites(raw, strip_code(raw))
        else:
            new_raw, done, sites = rewrite_text(raw)
            total += done
        for site in sites:
            list_problem.append(f"{relative}:{site.line}: {describe_site(site, relative, args.check)}")
        if new_raw != raw:
            with path.open("w", encoding="utf-8", newline="") as file:
                file.write(new_raw)
            changed_files += 1
    for line in list_problem:
        print(line)
    if not args.check:
        print(f"{TAG} 바꾼 파일 {changed_files} · 읽기 {total}")
    print(f"{TAG} 남은 자리 {len(list_problem)}")
    return ExitCode.VIOLATION if list_problem else ExitCode.PASSED


if __name__ == "__main__":
    sys.exit(main())
