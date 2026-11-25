#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/magic_literals.py

숫자 리터럴 래칫. `Source` 의 C++ 에서 `-1` · 실수 · 두 자리 이상 정수 · 16진수를 모아 **어디에 있는지**(생성물 · 테스트 · 표 줄 · 리플렉션 메타 ·
이름 붙은 정의 · 형식 번호 · 일반 코드)로 나눈다. 정본 규칙은 `docs/agents/CppStyle.md Constants`.

**왜 래칫인가.** 리터럴 하나가 매직인지는 형식이 가르지 못한다(`0.5f` 는 절반이고 `0.35f` 는 튜닝이다). 그래서 모든 줄을 막지 않고, **일반 코드**
(`code` 장소)에서 흔한 수학 값이 아닌 리터럴의 **파일마다 종류별 수**를 기준선(`Config/Rules/Lint/MagicLiteralsBaseline.json`)에 두고:
- 수가 기준선보다 **늘면** 커밋이 멈춘다 — 새 값에 이름을 붙이거나(`XxxInternal::k…`) 데이터(PROPERTY · 카탈로그)로 옮긴다.
- 수가 **줄면** 기준선도 같이 낮춰야 커밋이 선다(`--ratchet` 이 낮추기만 한다) — 옮긴 자리에 다시 리터럴이 들어오지 못한다.
- 기준선을 **올리는** 길은 `--accept <파일…>` 하나다(통합에서 합친 브랜치가 규칙 전 코드를 가져올 때). diff 에 그대로 보인다.

세지 않는 것(오탐 유형은 `--self-check` 가 고정한다): 테스트 · 생성물 · 리플렉션 메타 · 표 줄 · 이름 붙은 정의 · 형식 번호 장소, 흔한 수학 값
(`COMMON_FLOAT` — 0 · 1 · 2 · 0.5), 모든 비트 마스크(`0xFF…`), 비트 이동 폭(`<< 16`), 뜻이 산술 · 운영체제 계약인 `-1`(방향 · 파일 기술자).

    py -3 -m Scripts lint magic-literals                    # 래칫 검사(저장소 전체)
    py -3 -m Scripts lint magic-literals --ratchet          # 줄어든 파일의 기준선을 낮춘다(올리지 않는다)
    py -3 -m Scripts lint magic-literals --accept <파일…>   # 그 파일의 기준선을 지금 수로(올리는 유일한 길)
    py -3 -m Scripts lint magic-literals --self-check
"""

from __future__ import annotations

import argparse
import collections
import functools
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.exit_code import ExitCode  # noqa: E402
from common.git import run_git_in_hook_repository  # noqa: E402
from lint.cpp_shape import strip_code  # noqa: E402
from lint.lint_scope import LintScope, make_whole_scope, read_text  # noqa: E402
from lint.parallel import flat_map_in_processes  # noqa: E402
from lint.rule_tables import Baseline  # noqa: E402


RULE_ID = "magic-literals"
GROWN_ADVICE = "이름 붙은 상수(XxxInternal · 소유 타입)나 데이터(PROPERTY · 카탈로그)로 옮긴다"
RATCHET_COMMAND = "py -3 -m Scripts lint magic-literals --ratchet"
SOURCE_EXTENSIONS = (".h", ".cpp", ".inl")
KINDS = ("minus-one", "float", "integer", "hex")

# 단항 `-1`(뺄셈 `x - 1` 은 아니다): 앞이 여는 괄호 · 쉼표 · 대입 · 비교 · return · 삼항 · 중괄호 · 논리 연산(`&& -1 <= x`)이다.
MINUS_ONE = re.compile(r"(?:(?<=[(,=<>?:{\[])|(?<=&&)|(?<=\|\|)|(?<=return)|(?<=\breturn ))\s*-\s*1\b(?![.\deEfFxX'])")
FLOAT = re.compile(r"(?<![\w.])\d+\.\d*(?:[eE][+-]?\d+)?[fF]?(?![\w.])|(?<![\w.])\d+[eE][+-]?\d+[fF]?(?![\w.])")
HEX = re.compile(r"(?<![\w.])0[xX][0-9A-Fa-f']+[uUlL]*(?![\w.])")
# 앞이 `\` 면 문자 상수의 탈출(`L'\0'` — strip_code 는 접두 붙은 문자 상수를 지우지 못한다)이라 수가 아니다.
INTEGER = re.compile(r"(?<![\w.'\\])\d[\d']+[uUlL]*(?![\w.'])")
SHIFT_WIDTH = re.compile(r"(?:<<|>>)=?\s*$")
NAMED_DEFINITION = re.compile(r"\b(?:constexpr|const)\b[^=;()]*\bk[A-Z]\w*\s*(?:=|\{)")
FORMAT_NAME = re.compile(r"\bk\w*(?:Version|Magic|Tag|Revision)\b")
REFLECTION_META = re.compile(r"\b(?:PROPERTY|REFLECT|FUNCTION|ENUM|EVENT)\s*\(")
TABLE_ROW = re.compile(r"^\s*\{.*\}\s*,?\s*$")
# 칸의 기본값: 선언(`float32 _speed{ 3.5f };`)과 생성자 초기화 목록(`, _speed{ 3.5f }`). 튜닝 값의 정본 자리다(PROPERTY · 설정 · 카탈로그 칸 —
# CppStyle.md Constants "Tuning values are data") — 칸 이름이 뜻을 말한다. 단 `-1` 은 '없음' 표지라 여기서도 센다.
FIELD_DEFAULT = re.compile(r"^\s*(?!return\b)(?:[\w:<>,\*&]+\s+)+_\w+\s*(?:\{[^;{}]*\}|=\s*[^;=]+)\s*;|^\s*[:,]\s*_\w+\s*[{(][^;]*[})]\s*,?\s*$")
# 래칫이 세는 장소. 이름 붙은 정의 · 표 줄 · 형식 번호 · 칸 기본값은 이미 이름이나 표가 뜻을 말한다.
COUNTED_PLACE = "code"
# 흔한 수학 값 — 0 · 1 · 2 · 절반은 이름이 뜻을 더하지 않는다(`kHalf = 0.5f` 는 별칭이다). 0.25 · 10 · 100 은 넣지 않는다: 튜닝과 섞인다.
COMMON_FLOAT = frozenset({0.0, 1.0, 2.0, 0.5})
ALL_BITS_HEX = re.compile(r"0[xX](?:F|f)+[uUlL]*$")
# `-1` 줄의 뜻 — 위에서부터 처음 맞는 줄이 이긴다. 참이면 산술 · 외부 계약(운영체제 · C API, 부호 · 방향)이라 래칫이 세지 않고, 거짓이면 표지라 센다.
MINUS_ONE_RULES: tuple[tuple[bool, re.Pattern[str]], ...] = (
    (True, re.compile(r"\b\w*(?:Fd|fd|FD|Descriptor)\w*\s*(?:\[\d\])?\s*(?:=|==|!=|\{)\s*-\s*1\b|MultiByteToWideChar|WideCharToMultiByte")),   # os-contract
    (False, re.compile(r"\bk[A-Z]\w*\s*(?:=|\{)\s*-\s*1\b")),   # named-sentinel
    (False, re.compile(r"\w*(?:Max|Limit|Cap|Stock|Quota|Budget|Remaining|Uses|Charges)\w*\s*(?:=|==|!=|\{|\()\s*-\s*1\b|-\s*1\b.*//.*(?:무제한|무한|제한 없|끝낼 때까지|unlimited|infinite)", re.I)),   # unlimited
    (False, re.compile(r"\w*(?:Price|Default|Override|Fallback|Inherit)\w*\s*(?:=|==|!=|\{|\()\s*-\s*1\b", re.I)),   # use-default
    (False, re.compile(r"-\s*1\b.*//.*(?:없음|없으면|없다|아니|none|not found)", re.I)),   # index-sentinel
    (False, re.compile(r"\w*(?:Index|ID|Id|Slot|Handle|Cursor|Selected|Found|Hovered|Active|Current|Target|Node|Row|Column|Entry|Pos)\w*\s*(?:=|==|!=|\{)\s*-\s*1\b|\(\s*-\s*1\s*\)\s*;")),   # index-sentinel
    (False, re.compile(r"\w*(?:Last|First|Prev|Previous|Next|Nanoseconds|Seconds|Milliseconds|Time|Hour|Day|Frame|Tick|Round|Code|Number)\w*\s*(?:=|==|!=|\{)\s*-\s*1\b")),   # not-yet-known
    (True, re.compile(r"\?\s*1\s*:\s*-\s*1\b|\?\s*-\s*1\s*:\s*[01]\b|-\s*1\s*:\s*[01]\b|\bclamp\s*\([^;]*-\s*1\s*,\s*1\b|nextInt\s*\(\s*-\s*1\b")),   # direction
    (False, re.compile(r"\breturn\s+-\s*1\s*;|\?[^;]*:\s*-\s*1\b")),   # not-found-return
    (True, re.compile(r"\b(?:Vec\d|Vector\d|Point\d?|[Dd]irection|[Aa]xis|[Ss]ign|[Ss]cale|[Ss]tep|[Dd]elta|[Oo]ffset|dir)\w*(?:\s+\w+)?\s*[({=][^;]*-\s*1\b|\*\s*-\s*1\b")),   # direction
)


@dataclass(frozen=True)
class LiteralRow:
    """리터럴 하나입니다."""

    path: str
    line: int
    kind: str      # minus-one · float · hex · integer
    value: str
    place: str     # generated · test · reflection-meta · format · named-definition · table-row · field-default · code
    common: bool   # 흔한 수학 값 · 모든 비트 마스크 · 비트 이동 폭 · 산술 `-1` — 래칫이 세지 않는다

    @property
    def counted(self) -> bool:
        if self.common:
            return False
        return self.place == COUNTED_PLACE or (self.place == "field-default" and self.kind == "minus-one")


def classify_place(relative: str, line: str) -> str:
    """리터럴이 놓인 장소입니다. 생성물 · 테스트가 가장 먼저(줄 모양과 무관하게 옮기지 않는다)."""
    if ".gen." in relative or "/Generated/" in relative or ".generated." in relative:
        return "generated"
    if "/Tests/" in relative or "/Test/" in relative:
        return "test"
    if REFLECTION_META.search(line) is not None:
        return "reflection-meta"
    if FORMAT_NAME.search(line) is not None:
        return "format"
    if NAMED_DEFINITION.search(line) is not None:
        return "named-definition"
    if TABLE_ROW.match(line) is not None:
        return "table-row"
    if FIELD_DEFAULT.match(line) is not None:
        return "field-default"
    return "code"


def is_arithmetic_minus_one(line: str) -> bool:
    """`-1` 한 줄이 산술 · 외부 계약인가(맞는 줄이 없으면 표지로 센다)."""
    return next((is_arithmetic for is_arithmetic, pattern in MINUS_ONE_RULES if pattern.search(line) is not None), False)


def is_common_float(value: str) -> bool:
    try:
        return float(value.rstrip("fF")) in COMMON_FLOAT
    except ValueError:
        return False


def inspect_text(text: str, relative: str) -> list[LiteralRow]:
    """글 하나의 리터럴을 모읍니다(자체 시험이 파일 없이 부를 수 있게 나눴다). 주석 · 문자열 · 전처리 줄은 보지 않는다."""
    list_row: list[LiteralRow] = []
    list_raw = text.split("\n")
    for index, line in enumerate(strip_code(text).split("\n")):
        if not any(char.isdigit() for char in line):
            continue
        place = classify_place(relative, line)

        def add_row(kind: str, value: str, common: bool) -> None:
            list_row.append(LiteralRow(relative, index + 1, kind, value, place, common))

        for _ in MINUS_ONE.finditer(line):
            add_row("minus-one", "-1", is_arithmetic_minus_one(list_raw[index]))   # 뜻은 주석까지 본다(`// 무제한`)
        masked = MINUS_ONE.sub(lambda match: " " * len(match.group(0)), line)
        for match in FLOAT.finditer(masked):
            add_row("float", match.group(0), is_common_float(match.group(0)))
        masked = FLOAT.sub(lambda match: " " * len(match.group(0)), masked)
        for match in HEX.finditer(masked):
            add_row("hex", match.group(0), ALL_BITS_HEX.match(match.group(0)) is not None)
        masked = HEX.sub(lambda match: " " * len(match.group(0)), masked)
        for match in INTEGER.finditer(masked):
            add_row("integer", match.group(0), SHIFT_WIDTH.search(masked[: match.start()]) is not None)
    return list_row


def inspect_file(project_root: Path, file: Path) -> list[LiteralRow]:
    """파일 하나를 봅니다(프로세스로 넘기므로 모듈 최상위에 둔다)."""
    text = read_text(file, errors="ignore")
    if text is None:
        raise FileNotFoundError(file)   # scope 의 파일은 있다 — 예전 read_text 처럼 멈춘다
    return inspect_text(text, file.relative_to(project_root).as_posix())


def is_source_file(relative: str) -> bool:
    return relative.startswith("Source/") and relative.endswith(SOURCE_EXTENSIONS) and "/ThirdParty/" not in relative


def list_source_files(project_root: Path) -> list[Path]:
    return sorted(path for path in (project_root / "Source").rglob("*") if path.is_file() and is_source_file(path.relative_to(project_root).as_posix()))


def collect_rows(project_root: Path, list_file: Sequence[Path] | None = None) -> list[LiteralRow]:
    list_row = flat_map_in_processes(functools.partial(inspect_file, project_root), list(list_file) if list_file is not None else list_source_files(project_root))
    return sorted(list_row, key=lambda row: (row.path, row.line))


# --- 래칫 ------------------------------------------------------------------------------------------

def count_by_file(list_row: Iterable[LiteralRow]) -> dict[str, dict[str, int]]:
    """파일마다 래칫이 세는 종류별 수입니다(0 인 종류 · 파일은 적지 않는다 — 기준선 JSON 이 짧고 diff 가 읽힌다)."""
    counts: dict[str, collections.Counter[str]] = collections.defaultdict(collections.Counter)
    for row in list_row:
        if row.counted:
            counts[row.path][row.kind] += 1
    return {path: {kind: counter[kind] for kind in KINDS if counter[kind] > 0} for path, counter in counts.items()}


def read_staged_renames(project_root: Path) -> dict[str, str]:
    """staged 이름 바꾸기의 새 경로 → 옛 경로입니다(옮긴 파일은 옛 경로의 기준선을 이어받는다)."""
    result = run_git_in_hook_repository(["diff", "--cached", "--name-status", "-M"], cwd=project_root)
    if result.return_code != 0:
        raise RuntimeError(f"git diff --cached --name-status 실패: {result.stderr.strip()}")
    map_old: dict[str, str] = {}
    for line in result.stdout.splitlines():
        list_part = line.split("\t")
        if len(list_part) == 3 and list_part[0].startswith("R"):
            map_old[list_part[2]] = list_part[1]
    return map_old


def check_magic_literals(project_root: Path, scope: LintScope) -> list[str]:
    """scope 의 C++ 파일의 래칫 검사입니다. 없는 파일의 기준선은 저장소 전체일 때만 센다."""
    list_relative = [relative for relative in scope.select() if is_source_file(relative)]
    list_problem: list[str] = []
    if list_relative:
        actual = count_by_file(collect_rows(project_root, [project_root / relative for relative in list_relative]))
        list_problem = compare_baseline(Baseline.load(project_root, RULE_ID), actual, list_relative, read_staged_renames(project_root))
    if scope.whole_repo:
        list_problem += Baseline.load(project_root, RULE_ID).list_missing(set(scope.list_file))
    return list_problem


def compare_baseline(baseline: Baseline, actual: dict[str, dict[str, int]], list_path: Iterable[str], map_old: dict[str, str]) -> list[str]:
    """늘었으면 이름 붙이기를, 줄었으면 기준선 낮추기를 요구한다(옮긴 파일은 옛 경로의 기준선을 잇는다)."""
    return baseline.compare(actual, list_path, map_old, KINDS, lambda _: GROWN_ADVICE, RATCHET_COMMAND)


def run_self_check() -> int:
    arr_case: tuple[tuple[str, bool], ...] = (
        ("static constexpr int32 kNoTeam = -1;", False),
        ("int32 _stock{ -1 }; // 무제한", False),
        ("int64 _priceOverride = -1;", False),
        ("int32 selectedIndex = -1;", False),
        ("    return -1;", False),
        ("const Vec3 down( 0.0f, -1, 0.0f );", True),
        ("int32 fileDescriptor{ -1 };", True),
        ("int32 _lastLogHour{ -1 };", False),
        ("return found ? index : -1;", False),
        ("pushEvent( Kind::Sidestep, player, -1 );", False),
        ("foo( a, -1 );", False),
    )
    failures = 0

    def expect(condition: bool, message: str) -> None:
        nonlocal failures
        if not condition:
            print(f"[MagicLiterals] self-check: {message}")
            failures += 1

    for line, expected in arr_case:
        expect(is_arithmetic_minus_one(line) == expected, f"'{line}' -> arithmetic should be {expected}")
    list_row = inspect_text("int32 last = size - 1;\nint32 none = -1; // -1 in a comment\nfloat32 half = 0.5f;\nuint32 mask = 0xFF;\n", "Source/Sample.cpp")
    expect([row.kind for row in list_row] == ["minus-one", "float", "hex"], "subtraction and comments are not literals")
    expect(classify_place("Source/Engine/Foo/Tests/TestFoo.cpp", "x = 42;") == "test", "tests are their own place")
    expect(classify_place("Source/Engine/Foo.cpp", "PROPERTY( Min = 10 )") == "reflection-meta", "reflection metadata is its own place")

    # 래칫이 세지 않는 오탐 유형 — 하나씩 고정한다.
    def counted_values(code: str) -> list[str]:
        return [row.value for row in inspect_text(code, "Source/Sample.cpp") if row.counted]

    expect(counted_values("x = 0.0f + 1.0 * 2.0f - 0.5f;") == [], "common math floats (0 · 1 · 2 · 0.5) are not counted")
    expect(counted_values("x = speed * 0.35f;") == ["0.35f"], "a tuning float is counted")
    expect(counted_values("m = v & 0xFFFFFFFFu; n = w & 0xFF;") == [], "all-bits masks are not counted")
    expect(counted_values("m = v & 0x7F;") == ["0x7F"], "a partial mask is counted")
    expect(counted_values("x = ( a << 16 ) | ( b >> 24 ); y <<= 8;") == [], "shift widths are not counted")
    expect(counted_values("x = a * 16;") == ["16"], "a multiplier is counted")
    expect(counted_values("if ( *p == L'\\0' ) {}") == [], "an escaped char literal is not a number")
    expect(counted_values("const Vec3 down( 0.0f, -1, 0.0f );") == [], "direction -1 is arithmetic")
    expect(counted_values("static constexpr float32 kSpeed = 3.5f;") == [], "a named definition is not counted")
    expect(counted_values("    { 3.5f, 12, 0x10 },") == [], "an initializer table row is not counted")
    expect(counted_values("int32 _target = -1;") == ["-1"], "an index sentinel -1 is counted")
    expect(counted_values("io.check( 0 <= a && -1 <= b );") == ["-1"], "-1 after a logical operator is unary")
    expect(counted_values("        float32 _walkSpeed{ 3.5f }; ///< 걷기 속도") == [], "a field default is the tuning value's home")
    expect(counted_values("        , _walkSpeed{ 3.5f }") == [], "a constructor initializer is the PROPERTY default's home")
    expect(counted_values("        int32 _target{ -1 };") == ["-1"], "a -1 field default is still a sentinel")
    expect(counted_values("        pilot._heat = 3.5f;") == ["3.5f"], "assigning a member in a body is code")
    expect(counted_values("        return _speed = 3.5f;") == ["3.5f"], "return is not a type")
    expect(counted_values("return a < b ? -1 : 1;") == [], "a sign ternary is arithmetic")
    # 래칫 판정(늘면 · 줄면 · 옮긴 파일 · --ratchet)은 표 형식 테스트(Scripts/lint/tests/test_rule_tables.py)가 본다.
    print(f"[MagicLiterals] self-check {'OK' if failures == 0 else 'FAIL'}")
    return ExitCode.PASSED if failures == 0 else ExitCode.VIOLATION


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="숫자 리터럴 래칫(일반 코드의 이름 없는 리터럴이 파일마다 늘지 않게)")
    parser.add_argument("--ratchet", action="store_true", help="줄어든 파일의 기준선을 낮추고 없어진 파일을 지운다(올리지 않는다)")
    parser.add_argument("--accept", nargs="+", default=None, metavar="FILE", help="그 파일들의 기준선을 지금 수로 바꾼다(올리는 유일한 길 — 통합에서만)")
    parser.add_argument("--self-check", action="store_true", help="분류 · 래칫 규칙의 자체 시험")
    args = parser.parse_args(argv)
    sys.stdout.reconfigure(encoding="utf-8")
    if args.self_check:
        return run_self_check()

    project_root = Path(__file__).resolve().parents[2]
    if args.ratchet or args.accept is not None:
        Baseline.load(project_root, RULE_ID).update(project_root, "MagicLiterals", list_source_files(project_root),
                                                    lambda list_file: count_by_file(collect_rows(project_root, list_file)), args.accept)
        return ExitCode.PASSED

    list_problem = check_magic_literals(project_root, make_whole_scope(project_root))
    for problem in list_problem:
        print(f"  - {problem}")
    print(f"[MagicLiterals] {'FAIL' if list_problem else 'OK'} ({len(list_problem)}건)")
    return ExitCode.VIOLATION if list_problem else ExitCode.PASSED


if __name__ == "__main__":
    sys.exit(main())
