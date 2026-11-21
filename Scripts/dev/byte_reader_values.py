"""
Scripts/dev/byte_reader_values.py

바이트 · 비트 읽기 형식(`ByteReader` · `Archive` · `BitReader`)의 출력 인자 읽기(`reader.readPod( x )` · `readString( s )` · `readCount( n, c )` …)를
값 반환 읽기로 바꾼다. 출력 인자 꼴은 반환 `bool` 을 버리면 실패를 잃었고(`(void)` · 무시), 실패 때 출력 칸이 옛 값으로 남았다. 값 읽기는 `[[nodiscard]]` 이고
실패는 끈적한 깃발 하나로 본다(`ByteReader` · `BitReader` 는 `hasOverflowed()`, `Archive` 는 `isError()` — 언리얼 `FArchive::IsError` 자리).

  py -3 -m Scripts byte-reader-values [경로…]          # 바꾼다(경로 없으면 Source)
  py -3 -m Scripts byte-reader-values --check [경로…]  # 남은 옛 모양 · 실패해도 덮는 꼴을 찍고, 있으면 실패
  py -3 -m Scripts byte-reader-values --self-check     # 변환 자체 테스트

- 받는 쪽 형식은 이름 앞의 가장 가까운 선언(`ByteReader reader( … )` · `Archive& arch` · `BitReader& body`)으로 정한다(`SPECS`). 형식마다 옛 꼴의 인자 수가
  달라(`readCount( n, c )` → `readCount( n )`) 이미 값 읽기인 자리는 건드리지 않는다.
- 바꾸는 문장 모양: `if ( 읽기 == false [|| …] )` · `if ( 읽기 )` · `[const] bool b = 읽기 && 읽기 [&& …];` · `return 읽기 && …;` ·
  `SW_REQUIRE_TRUE( 읽기 )` · `SW_EXPECT_TRUE/FALSE( 읽기 )` · `(void)읽기;`. 읽기를 문장 앞으로 꺼내고 조건 자리는 실패 깃발로 바꾼다.
  읽기가 조건의 앞쪽에 모여 있지 않으면(앞 항이 읽기를 건너뛰게 하는 자리) 바꾸지 않고 알린다.
- **대상이 같은 함수의 값 지역 변수(또는 그 칸)가 아니면**(바깥 칸 · 출력 인자 · 참조 · 역참조 `*p` · `p->x`) 실패해도 덮지 않게 지역 값으로 읽고
  실패 검사를 지난 뒤에만 옮긴다: `const auto loadedX = r.readX(); if ( r.hasOverflowed() ) return …; 대상 = loadedX;`. 그 모양은 실패에 빠져나가는
  `if` 와 `return 읽기 && …;` 에서만 만든다 — 다른 문장이면 알린다. 이미 값 읽기로 바뀐 `대상 = reader.readX(); if ( reader.hasOverflowed() ) 빠져나감` 도
  같은 모양으로 고친다(`ByteReader` — 2026-11-21 의 첫 변환이 남긴 꼴).
- `readVarUint` · `readVarInt` · `readPod` · `readEnum` 은 출력 칸의 선언 형식으로 이름 · 템플릿 인자를 고른다. 형식을 못 찾으면 알린다.
- 병합 뒤 충돌 파일에 다시 돌린다(이미 바꾼 자리는 그대로). 린트 `cpp-shape` 규칙 13 이 `find_sites` 로 같은 판정을 한다.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass, field
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
VAR_INT_NAME = {("readVarUint", "uint64"): "readVarUint", ("readVarUint", "uint32"): "readVarUint32", ("readVarInt", "int64"): "readVarInt", ("readVarInt", "int32"): "readVarInt32"}


@dataclass(frozen=True)
class ReaderSpec:
    """읽기 형식 하나의 옛 꼴. `arity` 는 옛 꼴의 인자 수(출력 인자는 마지막), `typed` 는 출력 칸 형식을 템플릿 인자로 받는 읽기, `heavy` 는 옮길 때 `std::move` 할 결과."""

    arity: dict[str, int]
    failed: str
    typed: frozenset[str] = field(default_factory=frozenset)
    heavy: frozenset[str] = field(default_factory=frozenset)


SPECS: dict[str, ReaderSpec] = {
    "ByteReader": ReaderSpec({"readVarUint": 1, "readVarInt": 1, "readString": 1, "readStringView": 1, "readByteVector": 1, "readPod": 1, "readView": 2, "readCount": 2},
                             "hasOverflowed()", frozenset({"readPod"}), frozenset({"readString", "readByteVector"})),
    "Archive": ReaderSpec({"readVarUint": 1, "readVarInt": 1, "readString": 1, "readStringView": 1, "readCount": 2, "readPooledString": 1},
                          "isError()", frozenset(), frozenset({"readString", "readPooledString"})),
    "BitReader": ReaderSpec({"readInt": 3, "readFiniteFloat": 1, "readQuantizedFloat": 4, "readQuantizedFloat3": 4, "readEnum": 2, "readCount": 2},
                            "hasOverflowed()", frozenset({"readEnum"})),
}
RECEIVER_TYPE = "ByteReader"   # 이미 값 읽기로 바뀐 꼴의 덮어쓰기를 고치는 형식(`find_overwrite_sites`)
ALL_METHODS = sorted({method for spec in SPECS.values() for method in spec.arity})
CALL_PATTERN = re.compile(r"\b(\w+)\s*(\.|->)\s*(" + "|".join(ALL_METHODS) + r")\s*\(")
IDENTIFIER = re.compile(r"[A-Za-z_]\w*")
MEMBER_ACCESS = re.compile(r"[A-Za-z_]\w*(?:\s*(?:\.|->)\s*[A-Za-z_]\w*)+")
LOCAL_TARGET = re.compile(r"([A-Za-z_]\w*)(?:\s*\.\s*[A-Za-z_]\w*)*")   # 이름 · 이름.칸 — `->` · `*` · `[]` 는 바깥을 가리킬 수 있다
CONTROL_WORDS = ("if", "for", "while", "switch", "catch")
EXIT_HEAD = re.compile(r"(?:return|continue|break|goto|throw)\b")
DECLARATION_EXCLUDED = ("return", "else", "case", "delete", "new", "throw", "goto")
TRAILING_QUALIFIER = re.compile(r"(?:\s*(?:const|noexcept|override|final|mutable)\b|\s*->\s*[\w:<>,\s*&]+)$")


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
    reader_type: str = RECEIVER_TYPE
    method: str = ""


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


def match_paren_back(code: str, close_index: int) -> int:
    """`code[close_index]` 의 닫는 괄호의 짝(여는 괄호) 위치. 못 찾으면 -1."""
    depth = 0
    for index in range(close_index, -1, -1):
        char = code[index]
        if char in ")]}":
            depth += 1
        elif char in "([{":
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


def find_declaration(code: str, name: str, before: int) -> tuple[str, bool, int] | None:
    """`before` 앞에서 가장 가까운 `형식 [&|*] name` 선언의 (형식 글, 참조인지, 위치). 없으면 None. 포인터 변수 자신은 값이다(`*p` · `p->x` 대상은 `LOCAL_TARGET` 이 거른다)."""
    pattern = re.compile(r"([A-Za-z_][\w:]*(?:<[^<>;(){}]*>)?)(?:\s*([&*]+)\s*|\s+)" + re.escape(name) + r"\s*[;,)=({\[]")
    found = None
    for match in pattern.finditer(code, 0, before):
        if match.group(1) not in DECLARATION_EXCLUDED:
            found = (match.group(1), "&" in (match.group(2) or ""), match.start())
    return found


def find_declared_type(code: str, name: str, before: int) -> str | None:
    """`before` 앞에서 가장 가까운 `형식 [&|*] name` 선언의 형식 글(한정 · 꺾쇠 포함). 없으면 None."""
    found = find_declaration(code, name, before)
    return found[0] if found else None


def last_name(type_text: str) -> str:
    return re.sub(r"<.*>", "", type_text).split("::")[-1]


def get_indent(raw: str, index: int) -> str:
    line_start = raw.rfind("\n", 0, index) + 1
    return re.match(r"[ \t]*", raw[line_start:]).group(0)


def find_function_body(code: str, position: int) -> tuple[int, int]:
    """`position` 을 담은 가장 안쪽 함수 · 람다 본문의 (여는 `{` 위치, 매개변수 목록 `(` 위치). 못 찾으면 (-1, -1)."""
    depth = 0
    for index in range(position - 1, -1, -1):
        char = code[index]
        if char == "}":
            depth += 1
        elif char == "{":
            if depth > 0:
                depth -= 1
                continue
            head = code[:index].rstrip()
            while True:
                trimmed = TRAILING_QUALIFIER.sub("", head).rstrip()
                if trimmed == head:
                    break
                head = trimmed
            if head.endswith("]"):
                return index, index   # 매개변수 없는 람다
            if head.endswith(")"):
                open_index = match_paren_back(code, len(head) - 1)
                word = re.search(r"(\w+)\s*$", code[:open_index]) if open_index > 0 else None
                if word is None or word.group(1) not in CONTROL_WORDS:
                    return index, open_index
    return -1, -1


def is_local_target(code: str, target: str, position: int) -> bool:
    """`target` 이 같은 함수의 값 지역 변수(값 매개변수 포함) 이름이거나 그 `.칸` 이면 true — 실패한 읽기가 덮어도 바깥에 새지 않는다."""
    match = LOCAL_TARGET.fullmatch(target.strip())
    if match is None:
        return False
    body_start, parameter_start = find_function_body(code, position)
    if body_start < 0:
        return False
    declaration = find_declaration(code, match.group(1), position)
    if declaration is None:
        return False
    _, is_reference, declared_at = declaration
    return is_reference is False and declared_at > parameter_start


def make_temp_name(code: str, target: str, position: int, taken: set[str]) -> str:
    """덮지 않게 읽을 지역 값 이름(`loaded` + 대상의 마지막 이름). 같은 함수에 이미 있으면 번호를 붙인다."""
    base = IDENTIFIER.findall(target)[-1].lstrip("_")
    base = re.sub(r"^(?:inout|out|p)(?=[A-Z])", "", base) or "value"
    name = "loaded" + base[0].upper() + base[1:]
    body_start, _ = find_function_body(code, position)
    body_end = match_paren(code, body_start) if body_start >= 0 else -1
    region = code[max(body_start, 0):body_end if body_end > 0 else len(code)]
    candidate, number = name, 2
    while candidate in taken or re.search(r"\b" + candidate + r"\b", region):
        candidate, number = f"{name}{number}", number + 1
    taken.add(candidate)
    return candidate


def replace_target(text: str, target: str, temp: str) -> str:
    """조건 나머지 글에서 대상 글을 지역 값 이름으로 바꾼다(앞뒤가 이름 글자가 아닌 자리만)."""
    return re.sub(r"(?<![\w.>])" + re.escape(target) + r"(?![\w])", temp, text)


def lift_reads(code: str, list_read: list[tuple[int, str, str, str]], heavy: frozenset[str]) -> tuple[list[str], list[str], list[tuple[str, str]]]:
    """(위치, 대상, 값 읽기 글, 메서드) 들을 꺼낸 문장 · 검사 뒤에 옮길 문장 · (대상, 지역 값 이름). 값 지역 대상은 바로 담는다."""
    taken: set[str] = set()
    lines: list[str] = []
    assigns: list[str] = []
    renames: list[tuple[str, str]] = []
    for position, target, call, method in list_read:
        if is_local_target(code, target, position):
            lines.append(f"{target} = {call};")
            continue
        temp = make_temp_name(code, target, position, taken)
        is_heavy = method in heavy
        lines.append(f"{'auto' if is_heavy else 'const auto'} {temp} = {call};")
        assigns.append(f"{target} = {'std::move( ' + temp + ' )' if is_heavy else temp};")
        renames.append((target, temp))
    return lines, assigns, renames


# ------------------------------------------------------------------------------
# 자리 찾기
# ------------------------------------------------------------------------------
def make_value_call(code: str, spec: ReaderSpec, receiver: str, access: str, method: str, list_argument: list[str], call_start: int) -> tuple[str | None, str | None]:
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
    if method in spec.typed:
        declared = find_declared_type(code, out, call_start) if IDENTIFIER.fullmatch(out) else None
        if declared is not None and declared not in ("auto", "const"):
            type_text = declared
        elif IDENTIFIER.fullmatch(out) or MEMBER_ACCESS.fullmatch(out):
            type_text = f"decltype( {out} )"   # 괄호 없는 이름 · 멤버 접근의 decltype 은 선언 형식이다(참조가 붙지 않는다)
        else:
            return None, f"`{out}` 의 형식을 글로 정하지 못했다 — 손으로 {method}<T>() 를 적는다"
        return f"{receiver}{access}{method}<{type_text}>{argument_text}", None
    return f"{receiver}{access}{method}{argument_text}", None


def find_sites(raw: str, code: str) -> list[Site]:
    """`code` 는 `strip_code(raw)`. 받는 쪽이 `SPECS` 형식인 출력 인자 읽기를 모두 낸다."""
    list_site: list[Site] = []
    for match in CALL_PATTERN.finditer(code):
        receiver, access, method = match.group(1), match.group(2), match.group(3)
        open_index = match.end() - 1
        close = match_paren(code, open_index)
        if close < 0:
            continue
        declared = find_declared_type(code, receiver, match.start())
        reader_type = last_name(declared) if declared is not None else ""
        spec = SPECS.get(reader_type)
        if spec is None or method not in spec.arity:
            continue
        pieces = split_top(code, open_index + 1, close, (",",))
        list_argument = [raw[a:b].strip() for a, b in pieces]
        if list_argument == [""] or len(list_argument) != spec.arity[method]:
            continue   # 이미 값 읽기(인자가 하나 적다)
        call, problem = make_value_call(code, spec, receiver, access, method, list_argument, match.start())
        list_site.append(Site(match.start(), close + 1, raw.count("\n", 0, match.start()) + 1, receiver, list_argument[-1], call, problem, reader_type, method))
    return list_site


# ------------------------------------------------------------------------------
# 바꾸기
# ------------------------------------------------------------------------------
STATEMENT_HEAD = re.compile(r"(?:(?:const\s+)?bool\s+\w+\s*=|return\b|\(\s*void\s*\)|if\s*\(|(SW_(?:REQUIRE|EXPECT)_(?:TRUE|FALSE))\s*\()")


@dataclass(frozen=True)
class Edit:
    """문장 하나의 바꿈. `[start, end)` 를 `text` 로, 옮길 문장(`after`)은 `after_at`(실패에 빠져나가는 `if` 본문 끝) 뒤에 넣는다."""

    start: int
    end: int
    text: str
    count: int
    after_at: int = -1
    after: str = ""


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


def find_exit_body_end(code: str, body_start: int) -> int:
    """`if ( … )` 뒤 `body_start` 의 본문이 늘 빠져나가면(return · continue · break · goto · throw) 본문 끝 다음 위치, 아니면 -1. `else` 가 붙어도 -1."""
    cursor = body_start
    while cursor < len(code) and code[cursor].isspace():
        cursor += 1
    if cursor >= len(code):
        return -1
    if code[cursor] == "{":
        close = match_paren(code, cursor)
        if close < 0:
            return -1
        inner_end = close - 1
        while inner_end > cursor and code[inner_end].isspace():
            inner_end -= 1
        if code[inner_end] != ";":
            return -1
        last_start = find_statement_start(code, inner_end)
        if last_start <= cursor or EXIT_HEAD.match(code, last_start) is None:
            return -1
        end = close + 1
    else:
        if EXIT_HEAD.match(code, cursor) is None:
            return -1
        semicolon = code.find(";", cursor)
        if semicolon < 0:
            return -1
        end = semicolon + 1
    if re.match(r"\s*else\b", code[end:]):
        return -1
    return end


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
    site = site_by_start.get(offset)
    if site is None:
        return None, False
    tail = code[site.end:end].strip()
    if tail == "== false" and not negated:
        return site, True
    if tail == "":
        return site, negated
    return None, False


def rewrite_statement(raw: str, code: str, sites: list[Site], statement_start: int) -> Edit | None:
    """문장 하나를 바꾼다. 바꿀 수 없으면 None."""
    head = STATEMENT_HEAD.match(code, statement_start)
    if head is None or re.search(r"else\s*$", code[:statement_start]):
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
    spec = SPECS[first.reader_type]
    access = "->" if "->" in raw[first.start:first.end].split("(")[0] else "."
    failed = f"{first.receiver}{access}{spec.failed}"
    indent = get_indent(raw, statement_start)
    lines, assigns, renames = lift_reads(code, [(site.start, site.out, site.call, site.method) for site, _ in list_read], spec.heavy)
    for target, temp in renames:
        rest = [replace_target(term, target, temp) for term in rest]
    hoisted = "".join(f"{line}\n{indent}" for line in lines)
    count = len(list_read)
    exits_on_failure = separator == "||" or (count == 1 and not rest and negated)
    if assigns:
        return rewrite_deferred(code, statement_start, statement_end, text, is_if, exits_on_failure, negated, failed, rest, hoisted, assigns, indent, count)
    if macro:
        if rest or count != 1 or negated:
            return None
        verdict = macro.rsplit("_", 1)[0] + ("_FALSE" if macro.endswith("TRUE") else "_TRUE")
        return Edit(statement_start, statement_end, f"{hoisted}{verdict}( {failed} )", count)
    if is_if:
        if exits_on_failure:
            if not negated:
                return None
            condition = " || ".join([failed] + rest)
        elif negated:
            return None
        else:
            condition = " && ".join([f"{failed} == false"] + rest)
        return Edit(statement_start, statement_end, f"{hoisted}if ( {condition} )", count)
    if text.startswith("("):
        if negated or rest or count != 1:
            return None
        return Edit(statement_start, statement_end, hoisted.rstrip(), count)
    if separator == "||":
        if not negated:
            return None
        condition = " || ".join([failed] + rest)
    elif negated:
        return None
    else:
        condition = " && ".join([f"{failed} == false"] + rest)
    return Edit(statement_start, statement_end, f"{hoisted}{text} {condition};", count)


def rewrite_deferred(code: str, statement_start: int, statement_end: int, text: str, is_if: bool, exits_on_failure: bool, negated: bool, failed: str,
                     rest: list[str], hoisted: str, assigns: list[str], indent: str, count: int) -> Edit | None:
    """대상이 바깥을 가리키는 읽기 — 실패 검사를 지난 뒤에만 옮긴다. 실패에 빠져나가는 `if` 와 `return 읽기 && …;` 만 만든다."""
    assign_text = "".join(f"\n{indent}{line}" for line in assigns)
    if is_if:
        if not (exits_on_failure and negated):
            return None
        body_end = find_exit_body_end(code, statement_end)
        if body_end < 0:
            return None
        condition = " || ".join([failed] + rest)
        return Edit(statement_start, statement_end, f"{hoisted}if ( {condition} )", count, body_end, assign_text)
    if text.startswith("return") and not negated and not exits_on_failure:
        tail = " && ".join(rest) if rest else "true"
        return Edit(statement_start, statement_end, f"{hoisted}if ( {failed} )\n{indent}    return false;{assign_text}\n{indent}return {tail};", count)
    return None


def apply_edit(raw: str, edit: Edit) -> str:
    if edit.after_at < 0:
        return raw[:edit.start] + edit.text + raw[edit.end:]
    return raw[:edit.start] + edit.text + raw[edit.end:edit.after_at] + edit.after + raw[edit.after_at:]


# ------------------------------------------------------------------------------
# 이미 값 읽기로 바뀐 덮어쓰기 꼴
# ------------------------------------------------------------------------------
@dataclass(frozen=True)
class Overwrite:
    """`대상 = reader.readX();` 들 뒤에 `if ( reader.hasOverflowed() … ) 빠져나감` 이 오는데 대상 하나라도 바깥을 가리키는 묶음."""

    start: int
    line: int
    reads: tuple[tuple[int, str, str, str], ...]   # (문장 시작, 대상, 읽기 글, 메서드)
    if_start: int
    condition_start: int
    condition_end: int
    body_end: int


VALUE_READ = re.compile(r"([^;{}=]+?)\s*=\s*((\w+)\s*(?:\.|->)\s*(read\w*)\s*(?:<[^;]*?>)?\s*\(.*\))\s*;", re.S)
NEW_DECLARATION = re.compile(r"(?:const\s+)?[\w:<>,\s]+?[\s&*]\w+")


def find_overwrite_sites(raw: str, code: str) -> list[Overwrite]:
    """받는 쪽이 `RECEIVER_TYPE` 인 값 읽기를 바깥 칸에 바로 담고 바로 뒤의 실패 검사로 빠져나가는 묶음."""
    flag = SPECS[RECEIVER_TYPE].failed.replace("()", "")
    pattern = re.compile(r"\bif\s*\(\s*(\w+)\s*(?:\.|->)\s*" + flag + r"\s*\(\s*\)")
    list_found: list[Overwrite] = []
    for match in pattern.finditer(code):
        receiver = match.group(1)
        declared = find_declared_type(code, receiver, match.start())
        if declared is None or last_name(declared) != RECEIVER_TYPE:
            continue
        open_index = code.index("(", match.start())
        close = match_paren(code, open_index)
        if "&&" in code[open_index:close]:
            continue
        body_end = find_exit_body_end(code, close + 1)
        if body_end < 0:
            continue
        reads: list[tuple[int, str, str, str]] = []
        cursor = match.start()
        while True:
            statement_end = code.rfind(";", 0, cursor)
            if statement_end < 0 or code[statement_end + 1:cursor].strip():
                break
            statement_start = find_statement_start(code, statement_end)
            read = VALUE_READ.fullmatch(code, statement_start, statement_end + 1)
            if read is None or read.group(3) != receiver:
                break
            target = raw[read.start(1):read.end(1)].strip()
            if NEW_DECLARATION.fullmatch(code[read.start(1):read.end(1)].strip()):
                break   # 새 지역 선언이다
            reads.append((statement_start, target, raw[read.start(2):read.end(2)].strip(), read.group(4)))
            cursor = statement_start
        reads.reverse()
        if reads and any(is_local_target(code, target, start) is False for start, target, _, _ in reads):
            list_found.append(Overwrite(reads[0][0], raw.count("\n", 0, reads[0][0]) + 1, tuple(reads), match.start(), open_index, close, body_end))
    return list_found


def rewrite_overwrite(raw: str, code: str, found: Overwrite) -> str:
    """묶음 하나를 지역 값으로 읽고 검사 뒤에 옮기는 꼴로 바꾼다."""
    indent = get_indent(raw, found.if_start)
    lines, assigns, renames = lift_reads(code, list(found.reads), SPECS[RECEIVER_TYPE].heavy)
    condition = raw[found.condition_start:found.condition_end]
    for target, temp in renames:
        condition = replace_target(condition, target, temp)
    head = "".join(f"{line}\n{indent}" for line in lines)
    tail = "".join(f"\n{indent}{line}" for line in assigns)
    return raw[:found.start] + head + raw[found.if_start:found.condition_start] + condition + raw[found.condition_end:found.body_end] + tail + raw[found.body_end:]


def rewrite_text(raw: str) -> tuple[str, int, list[Site]]:
    """바꾼 글, 바꾼 읽기 수, 남은 자리. 뒤 문장부터 하나씩 바꾸고 다시 찾는다. 끝에 덮어쓰기 꼴을 고친다."""
    done = 0
    tried: set[int] = set()
    while True:
        code = strip_code(raw)
        sites = find_sites(raw, code)
        candidates = sorted({find_statement_start(code, site.start) for site in sites} - tried, reverse=True)
        for statement_start in candidates:
            tried.add(statement_start)
            edit = rewrite_statement(raw, code, sites, statement_start)
            if edit is not None:
                raw = apply_edit(raw, edit)
                done += edit.count
                tried = {index for index in tried if index < edit.start}   # 앞쪽 자리는 위치가 그대로다
                break
        else:
            break
    while True:
        code = strip_code(raw)
        list_overwrite = find_overwrite_sites(raw, code)
        if not list_overwrite:
            break
        raw = rewrite_overwrite(raw, code, list_overwrite[-1])
        done += 1
    return raw, done, find_sites(raw, strip_code(raw))


def describe_site(site: Site, relative: str, check: bool) -> str:
    """남은 자리 한 줄 설명(린트 `cpp-shape` 도 쓴다)."""
    if site.problem:
        return site.problem
    failed = SPECS[site.reader_type].failed
    if check:
        return f"{site.reader_type} 는 값 읽기로(`{site.out} = {site.call}` · 실패는 {failed}) — `py -3 -m Scripts byte-reader-values {relative}`"
    return f"{site.reader_type} 는 값 읽기로 — 문장 모양이 자동 변환 밖이다(손으로 `x = r.readX(); if ( r.{failed} )`, 대상이 바깥 칸이면 지역 값으로 읽고 검사 뒤에 옮긴다)"


def is_target(relative: str) -> bool:
    return relative.startswith("Source/") and relative.endswith(CPP_SUFFIX) and not any(part in relative for part in SKIP_PARTS)


def list_files(project_root: Path, list_path: Sequence[str]) -> list[Path]:
    output = run_git_or_raise(["ls-files", "--", *(list_path or ["Source"])], cwd=project_root)
    return [project_root / relative for relative in output.splitlines() if is_target(relative)]


SELF_CHECK_HEAD = ("void f( ByteReader& reader, Archive& arch, BitReader& bits, Header& outHeader, uint32* pCount )\n{\n    uint32 count = 0;\n    uint64 big = 0;\n"
                   "    Header header;\n    string text;\n    int32 step = 0;\n    Kind kind = Kind::A;\n")
SELF_CHECK_CASES: dict[str, str | None] = {
    "    if ( reader.readCount( 4, count ) == false )\n        return;\n":
        "    count = reader.readCount( 4 );\n    if ( reader.hasOverflowed() )\n        return;\n",
    "    const bool bHead = reader.readPod( header ) && reader.readString( text ) && count > 0;\n":
        "    header = reader.readPod<Header>();\n    text = reader.readString();\n    const bool bHead = reader.hasOverflowed() == false && count > 0;\n",
    "    SW_REQUIRE_TRUE( reader.readVarUint( big ) );\n": "    big = reader.readVarUint();\n    SW_REQUIRE_FALSE( reader.hasOverflowed() );\n",
    "    SW_EXPECT_FALSE( reader.readVarUint( count ) );\n": "    count = reader.readVarUint32();\n    SW_EXPECT_TRUE( reader.hasOverflowed() );\n",
    # 대상이 바깥 칸(선언을 못 찾는 이름의 칸) — 지역 값으로 읽고 검사 뒤에 옮긴다
    "    return reader.readPod( entry._a ) && reader.readPod( entry._b );\n":
        "    const auto loadedA = reader.readPod<decltype( entry._a )>();\n    const auto loadedB = reader.readPod<decltype( entry._b )>();\n"
        "    if ( reader.hasOverflowed() )\n        return false;\n    entry._a = loadedA;\n    entry._b = loadedB;\n    return true;\n",
    "    if ( bKnown == false || reader.readString( text ) == false )\n        return;\n": None,
    "    const bool bCut = reader.readPod( header ) == false || reader.readCount( 4, count ) == false;\n":
        "    header = reader.readPod<Header>();\n    count = reader.readCount( 4 );\n    const bool bCut = reader.hasOverflowed();\n",
    "    if ( reader.readString( text ) == false || text.empty() )\n        return;\n":
        "    text = reader.readString();\n    if ( reader.hasOverflowed() || text.empty() )\n        return;\n",
    # Archive · BitReader 도 같은 변환이다(실패 깃발은 형식마다)
    "    if ( arch.readCount( 4, count ) == false )\n        return;\n":
        "    count = arch.readCount( 4 );\n    if ( arch.isError() )\n        return;\n",
    "    if ( bits.readInt( -3, 3, step ) == false || bits.readEnum( 2, kind ) == false )\n        return false;\n":
        "    step = bits.readInt( -3, 3 );\n    kind = bits.readEnum<Kind>( 2 );\n    if ( bits.hasOverflowed() )\n        return false;\n",
    # 출력 인자 · 역참조 대상 — 실패해도 덮지 않는다(조건 나머지도 지역 값을 본다)
    "    if ( arch.readString( outHeader._name ) == false || outHeader._name.empty() )\n    {\n        log();\n        return false;\n    }\n    next();\n":
        "    auto loadedName = arch.readString();\n    if ( arch.isError() || loadedName.empty() )\n    {\n        log();\n        return false;\n    }\n"
        "    outHeader._name = std::move( loadedName );\n    next();\n",
    "    if ( bits.readCount( 8, *pCount ) == false )\n        return false;\n":
        "    const auto loadedCount = bits.readCount( 8 );\n    if ( bits.hasOverflowed() )\n        return false;\n    *pCount = loadedCount;\n",
    # 바깥 대상인데 빠져나가지 않는 문장은 바꾸지 않는다(손으로)
    "    if ( arch.readCount( 4, *pCount ) == false )\n        log();\n": None,
    "    (void)bits.readFiniteFloat( outHeader._speed );\n": None,
    # 이미 값 읽기로 바뀐 덮어쓰기 꼴 — 바깥 칸에 바로 담은 것만 고친다
    "    outHeader._size = reader.readUint32();\n    count = reader.readUint32();\n    if ( reader.hasOverflowed() )\n        return false;\n":
        "    const auto loadedSize = reader.readUint32();\n    count = reader.readUint32();\n    if ( reader.hasOverflowed() )\n        return false;\n    outHeader._size = loadedSize;\n",
    "    count = reader.readUint32();\n    if ( reader.hasOverflowed() )\n        return false;\n": None,
    "    const uint32 size = reader.readUint32();\n    if ( reader.hasOverflowed() )\n        return false;\n": None,
    # 포인터 지역 변수 자신은 값이다(가리키는 곳이 아니라 변수를 바꾼다)
    "    const uint8* pName = nullptr;\n    pName = reader.readView( 4 );\n    if ( reader.hasOverflowed() )\n        return false;\n": None,
}


def run_self_check() -> int:
    failures = 0
    for body, want in SELF_CHECK_CASES.items():
        source = SELF_CHECK_HEAD + body + "}\n"
        got, _, _ = rewrite_text(source)
        expected = source if want is None else SELF_CHECK_HEAD + want + "}\n"
        if got != expected:
            print(f"{TAG} self-check:\n{source}\n  got\n{got}\n  want\n{expected}")
            failures += 1
    left_source = SELF_CHECK_HEAD + "    (void)reader.readString( text );\n    text = reader.readString();\n    (void)other.readString( text );\n    count = bits.readCount( 8 );\n}\n"
    left = find_sites(left_source, strip_code(left_source))
    if len(left) != 1:
        print(f"{TAG} self-check: --check 는 남은 옛 모양 하나를 찾아야 한다 — {len(left)}")
        failures += 1
    print(f"{TAG} self-check {'OK' if failures == 0 else 'FAIL'}")
    return ExitCode.PASSED if failures == 0 else ExitCode.VIOLATION


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="py -3 -m Scripts byte-reader-values", description="ByteReader · Archive · BitReader 의 출력 인자 읽기를 값 반환 읽기로 바꾼다")
    parser.add_argument("paths", nargs="*", help="볼 경로(저장소 기준, 없으면 Source)")
    parser.add_argument("--check", action="store_true", help="바꾸지 않고 남은 옛 모양 · 덮어쓰기 꼴을 찍는다 — 있으면 실패")
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
        if CALL_PATTERN.search(raw) is None and RECEIVER_TYPE not in raw:
            continue
        relative = path.relative_to(project_root).as_posix()
        if args.check:
            code = strip_code(raw)
            new_raw, sites = raw, find_sites(raw, code)
            for found in find_overwrite_sites(raw, code):
                list_problem.append(f"{relative}:{found.line}: 실패해도 바깥 칸을 덮는다 — 지역 값으로 읽고 검사 뒤에 옮긴다(`py -3 -m Scripts byte-reader-values {relative}`)")
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
