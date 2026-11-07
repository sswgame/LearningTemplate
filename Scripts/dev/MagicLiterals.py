#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/dev/MagicLiterals.py

숫자 리터럴 집계(보고용 — 게이트가 아니다). `Source` 의 C++ 에서 `-1` · 실수 · 두 자리 이상 정수 · 16진수를 모아 **어디에 있는지**(생성물 · 테스트 · 표 줄 ·
리플렉션 메타 · 이름 붙은 정의 · 형식 번호 · 일반 코드)로 나누고, `-1` 은 **무슨 뜻인지**(인덱스 표지 · 찾지 못함 반환 · 무제한 · 기본값 사용 · 방향 ·
이름 붙은 표지)를 이름과 문맥으로 첫 분류한다. 정본은 `docs/DesignBacklog.md` "데이터 기반 값 · 매직 리터럴" 과 `docs/agents/Constants.md`.

**왜 게이트가 아닌가.** 단순 패턴은 감산 · 테스트 기대값 · 행렬 표 · 프로토콜 번호를 가르지 못한다(백로그 판단). 오탐 유형을 사람이 확인하기 전에는
커밋 훅에 넣지 않는다. 분류 규칙이 틀린 줄을 찾으면 여기의 표(`kMeaningRule`)를 고치고 `--self-check` 에 그 줄을 더한다.

    py -3 -m Scripts magic-literals                    # 장소 × 종류 수, `-1` 뜻별 수
    py -3 -m Scripts magic-literals --meaning unclassified --limit 50   # 그 뜻의 줄(파일:줄: 값 | 문맥)
    py -3 -m Scripts magic-literals --out <scratch>/literals.tsv        # 모든 줄(검토 목록)
    py -3 -m Scripts magic-literals --self-check
"""

from __future__ import annotations

import argparse
import collections
import functools
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.ExitCode import ExitCode  # noqa: E402
from lint.CppShape import stripCode  # noqa: E402
from lint.Parallel import flatMapInProcesses  # noqa: E402


# 단항 `-1`(뺄셈 `x - 1` 은 아니다): 앞이 여는 괄호 · 쉼표 · 대입 · 비교 · return · 삼항 · 중괄호다.
kMinusOne = re.compile(r"(?:(?<=[(,=<>?:{\[])|(?<=return)|(?<=\breturn ))\s*-\s*1\b(?![.\deEfFxX'])")
kFloat = re.compile(r"(?<![\w.])\d+\.\d*(?:[eE][+-]?\d+)?[fF]?(?![\w.])|(?<![\w.])\d+[eE][+-]?\d+[fF]?(?![\w.])")
kHex = re.compile(r"(?<![\w.])0[xX][0-9A-Fa-f']+[uUlL]*(?![\w.])")
kInteger = re.compile(r"(?<![\w.'])\d[\d']+[uUlL]*(?![\w.'])")
kNamedDefinition = re.compile(r"\b(?:constexpr|const)\b[^=;()]*\bk[A-Z]\w*\s*(?:=|\{)")
kFormatName = re.compile(r"\bk\w*(?:Version|Magic|Tag|Revision)\b")
kReflectionMeta = re.compile(r"\b(?:PROPERTY|REFLECT|FUNCTION|ENUM|EVENT)\s*\(")
kTableRow = re.compile(r"^\s*\{.*\}\s*,?\s*$")
# 뜻을 사람이 볼 장소(테스트 기대값 · 생성물 · 리플렉션 메타는 옮기지 않는다 — Constants.md "Literals that stay literal")
kReviewPlaces = ("code", "named-definition", "table-row")


@dataclass(frozen=True)
class MeaningRule:
    """`-1` 의 뜻 하나와 그 뜻을 고르는 줄 정규식입니다. 위에서부터 처음 맞는 것이 이긴다."""

    meaning: str
    pattern: re.Pattern[str]
    advice: str


kMeaningRule: tuple[MeaningRule, ...] = (
    MeaningRule("os-contract", re.compile(r"\b\w*(?:Fd|fd|FD|Descriptor)\w*\s*(?:\[\d\])?\s*(?:=|==|!=|\{)\s*-\s*1\b|MultiByteToWideChar|WideCharToMultiByte"),
                "운영체제 · C API 계약(파일 기술자, 널 끝 길이) — 그대로 둔다"),
    MeaningRule("named-sentinel", re.compile(r"\bk[A-Z]\w*\s*(?:=|\{)\s*-\s*1\b"),
                "도메인 이름은 두고 값은 constant::invalid_index 에서 가져온다(Constants.md)"),
    MeaningRule("unlimited", re.compile(r"\w*(?:Max|Limit|Cap|Stock|Quota|Budget|Remaining|Uses|Charges)\w*\s*(?:=|==|!=|\{|\()\s*-\s*1\b|-\s*1\b.*//.*(?:무제한|무한|제한 없|끝낼 때까지|unlimited|infinite)", re.I),
                "무제한은 표지 값이 아니라 형식(optional · 제한 없음 열거자 · 명시적 제한 타입)으로"),
    MeaningRule("use-default", re.compile(r"\w*(?:Price|Default|Override|Fallback|Inherit)\w*\s*(?:=|==|!=|\{|\()\s*-\s*1\b", re.I),
                "'기본값 사용' 은 optional 이나 열거자로 — invalid_index 로 가장하지 않는다"),
    MeaningRule("index-sentinel", re.compile(r"-\s*1\b.*//.*(?:없음|없으면|없다|아니|none|not found)", re.I),
                "주석이 '없음' 이라 말하는 칸 — 이름 붙은 표지(invalid_index 값)나 optional 로"),
    MeaningRule("index-sentinel", re.compile(r"\w*(?:Index|ID|Id|Slot|Handle|Cursor|Selected|Found|Hovered|Active|Current|Target|Node|Row|Column|Entry|Pos)\w*\s*(?:=|==|!=|\{)\s*-\s*1\b|\(\s*-\s*1\s*\)\s*;"),
                "인덱스 표지 — 이름 붙은 표지(invalid_index 값)나 optional 로"),
    MeaningRule("not-yet-known", re.compile(r"\w*(?:Last|First|Prev|Previous|Next|Nanoseconds|Seconds|Milliseconds|Time|Hour|Day|Frame|Tick|Round|Code|Number)\w*\s*(?:=|==|!=|\{)\s*-\s*1\b"),
                "아직 모름 · 아직 없음 — optional 로(첫 값과 표지가 겹치지 않게)"),
    MeaningRule("not-found-return", re.compile(r"\breturn\s+-\s*1\s*;|\?[^;]*:\s*-\s*1\b"),
                "찾지 못함 반환 — 함수가 optional · bool + 출력 인자 · 이름 붙은 표지를 돌려주게"),
    MeaningRule("direction", re.compile(r"\b(?:Vec\d|Vector\d|Point\d?|[Dd]irection|[Aa]xis|[Ss]ign|[Ss]cale|[Ss]tep|[Dd]elta|[Oo]ffset|dir)\w*(?:\s+\w+)?\s*[({=][^;]*-\s*1\b|\*\s*-\s*1\b"),
                "방향 · 부호 — 산술이다(옮기지 않는다)"),
    MeaningRule("event-no-subject", re.compile(r"\b(?:pushEvent|push|emplace_back|push_back)\s*\(.*-\s*1\b|\w+Event\s*\{.*-\s*1\b"),
                "이벤트 · 기록의 '대상 없음' 칸 — 칸 형식에 이름 붙은 표지나 optional 을 둔다(호출마다 -1 을 적지 않게)"),
    MeaningRule("default-argument", re.compile(r"\b\w+\s*=\s*-\s*1\s*[,)]"),
                "기본 인자 — 그 인자의 뜻(위 분류)을 함수 선언에서 본다"),
    MeaningRule("unnamed-none", re.compile(r"\b_?\w+\s*(?:\{\s*-\s*1\s*\}|=\s*-\s*1\s*;)"),
                "이름에 뜻이 없는 '없음' 칸 · 지역 — 칸 이름이나 형식으로 뜻을 드러낸다(사람이 본다)"),
)


@dataclass(frozen=True)
class LiteralRow:
    """리터럴 하나입니다. TSV 한 줄이 된다."""

    path: str
    line: int
    kind: str      # minus-one · float · hex · integer
    value: str
    place: str     # generated · test · reflection-meta · table-row · named-definition · format · code
    meaning: str   # `-1` 의 뜻(그 밖은 빈 글)
    context: str

    def toTsv(self) -> str:
        return "\t".join((self.path, str(self.line), self.kind, self.value, self.place, self.meaning, self.context.replace("\t", " ")))


def classifyPlace(relative: str, line: str) -> str:
    """리터럴이 놓인 장소입니다. 생성물 · 테스트가 가장 먼저(줄 모양과 무관하게 옮기지 않는다)."""
    if ".gen." in relative or "/Generated/" in relative or ".generated." in relative:
        return "generated"
    if "/Tests/" in relative or "/Test/" in relative:
        return "test"
    if kReflectionMeta.search(line) is not None:
        return "reflection-meta"
    if kFormatName.search(line) is not None:
        return "format"
    if kNamedDefinition.search(line) is not None:
        return "named-definition"
    if kTableRow.match(line) is not None:
        return "table-row"
    return "code"


def classifyMinusOne(line: str) -> str:
    """`-1` 한 줄의 뜻입니다. 규칙에 맞지 않으면 `unclassified`."""
    for rule in kMeaningRule:
        if rule.pattern.search(line) is not None:
            return rule.meaning
    return "unclassified"


def inspectText(text: str, relative: str) -> list[LiteralRow]:
    """글 하나의 리터럴을 모읍니다(자체 시험이 파일 없이 부를 수 있게 나눴다). 주석 · 문자열 · 전처리 줄은 보지 않는다."""
    listRow: list[LiteralRow] = []
    listRaw = text.split("\n")
    for index, line in enumerate(stripCode(text).split("\n")):
        if not any(char.isdigit() for char in line):
            continue
        place = classifyPlace(relative, line)
        context = listRaw[index].strip()[:160]
        for _ in kMinusOne.finditer(line):
            listRow.append(LiteralRow(relative, index + 1, "minus-one", "-1", place, classifyMinusOne(listRaw[index]), context))  # 뜻은 주석까지 본다(`// 무제한`)
        masked = kMinusOne.sub(lambda match: " " * len(match.group(0)), line)
        for kind, pattern in (("float", kFloat), ("hex", kHex)):
            for match in pattern.finditer(masked):
                listRow.append(LiteralRow(relative, index + 1, kind, match.group(0), place, "", context))
            masked = pattern.sub(lambda match: " " * len(match.group(0)), masked)
        for match in kInteger.finditer(masked):
            listRow.append(LiteralRow(relative, index + 1, "integer", match.group(0), place, "", context))
    return listRow


def inspectFile(projectRoot: Path, file: Path) -> list[LiteralRow]:
    """파일 하나를 봅니다(프로세스로 넘기므로 모듈 최상위에 둔다)."""
    return inspectText(file.read_text(encoding="utf-8", errors="ignore"), file.relative_to(projectRoot).as_posix())


def collectRows(projectRoot: Path) -> list[LiteralRow]:
    listFile = sorted(path for path in (projectRoot / "Source").rglob("*") if path.suffix in (".h", ".cpp", ".inl") and "ThirdParty" not in path.parts)
    listRow = flatMapInProcesses(functools.partial(inspectFile, projectRoot), listFile)
    return sorted(listRow, key=lambda row: (row.path, row.line))


def printSummary(listRow: list[LiteralRow]) -> None:
    countByPlace: collections.Counter[tuple[str, str]] = collections.Counter((row.place, row.kind) for row in listRow)
    listKind = ("minus-one", "float", "integer", "hex")
    listPlace = ("code", "named-definition", "format", "table-row", "reflection-meta", "test", "generated")
    print("## 장소 × 종류(리터럴 수)")
    print(f"{'place':<18}" + "".join(f"{kind:>11}" for kind in listKind))
    for place in listPlace:
        print(f"{place:<18}" + "".join(f"{countByPlace[(place, kind)]:>11}" for kind in listKind))
    print("\n## `-1` 의 뜻(일반 코드 · 이름 붙은 정의 — 테스트 · 생성물 제외)")
    listMinusOne = [row for row in listRow if row.kind == "minus-one" and row.place in kReviewPlaces]
    countByMeaning = collections.Counter(row.meaning for row in listMinusOne)
    adviceByMeaning = {rule.meaning: rule.advice for rule in kMeaningRule}
    for meaning, count in countByMeaning.most_common():
        print(f"- {meaning:<18} {count:>6}  {adviceByMeaning.get(meaning, '사람이 본다 — 맞는 규칙을 kMeaningRule 에 더한다')}")
    countByTop = collections.Counter("/".join(row.path.split("/")[:3]) for row in listMinusOne)
    print("\n## `-1` 이 많은 폴더(Source 아래 둘째 단계)")
    for folder, count in countByTop.most_common(12):
        print(f"- {folder:<48} {count:>6}")


def runSelfCheck() -> int:
    kArrCase: tuple[tuple[str, str], ...] = (
        ("static constexpr int32 kNoTeam = -1;", "named-sentinel"),
        ("int32 _stock{ -1 }; // 무제한", "unlimited"),
        ("if ( _maxAlive == -1 )", "unlimited"),
        ("int64 _priceOverride = -1;", "use-default"),
        ("int32 selectedIndex = -1;", "index-sentinel"),
        ("    return -1;", "not-found-return"),
        ("const Vec3 down( 0.0f, -1, 0.0f );", "direction"),
        ("int32 fileDescriptor{ -1 };", "os-contract"),
        ("int32 _lastLogHour{ -1 };", "not-yet-known"),
        ("return found ? index : -1;", "not-found-return"),
        ("static uint32 formatNumber( T value, int32 precisionOrBase = -1 )", "default-argument"),
        ("pushEvent( Kind::Sidestep, player, -1 );", "event-no-subject"),
        ("int32 _expireMinute{ -1 }; ///< -1 이면 게임이 끝낼 때까지", "unlimited"),
        ("int32 _segment{ -1 }; ///< 지금 구간이다(-1 = 없음).", "index-sentinel"),
        ("int32 _break{ -1 };", "unnamed-none"),
        ("foo( a, -1 );", "unclassified"),
    )
    failures = 0
    for line, expected in kArrCase:
        actual = classifyMinusOne(line)
        if actual != expected:
            print(f"[MagicLiterals] self-check: '{line}' -> {actual}, expected {expected}")
            failures += 1
    listRow = inspectText("int32 last = size - 1;\nint32 none = -1; // -1 in a comment\nfloat32 half = 0.5f;\nuint32 mask = 0xFF;\n", "Source/Sample.cpp")
    listKind = [row.kind for row in listRow]
    if listKind != ["minus-one", "float", "hex"]:
        print(f"[MagicLiterals] self-check: kinds {listKind}, expected ['minus-one', 'float', 'hex'] (subtraction and comments are not literals)")
        failures += 1
    if classifyPlace("Source/Engine/Foo/Tests/TestFoo.cpp", "x = 42;") != "test" or classifyPlace("Source/Engine/Foo.cpp", "PROPERTY( Min = 10 )") != "reflection-meta":
        print("[MagicLiterals] self-check: place classification")
        failures += 1
    print(f"[MagicLiterals] self-check {'OK' if failures == 0 else 'FAIL'}")
    return ExitCode.Passed if failures == 0 else ExitCode.Violation


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="숫자 리터럴 집계(보고용 — 게이트 아님): 장소 × 종류, `-1` 의 뜻 첫 분류")
    parser.add_argument("--meaning", default=None, help="이 뜻의 `-1` 줄을 찍는다(unclassified · index-sentinel · unlimited …)")
    parser.add_argument("--place", default=None, help="--meaning 과 함께 장소를 고른다(없으면 code · named-definition · table-row, test 도 고를 수 있다)")
    parser.add_argument("--limit", type=int, default=100, help="찍는 줄 수 상한")
    parser.add_argument("--out", default=None, help="모든 줄을 TSV 로 쓴다(검토 목록 — 경로 · 줄 · 종류 · 값 · 장소 · 뜻 · 문맥)")
    parser.add_argument("--self-check", action="store_true", help="분류 규칙의 자체 시험")
    args = parser.parse_args(argv)
    sys.stdout.reconfigure(encoding="utf-8")
    if args.self_check:
        return runSelfCheck()

    projectRoot = Path(__file__).resolve().parents[2]
    listRow = collectRows(projectRoot)
    if args.out is not None:
        Path(args.out).write_text("path\tline\tkind\tvalue\tplace\tmeaning\tcontext\n" + "\n".join(row.toTsv() for row in listRow) + "\n", encoding="utf-8")
        print(f"[MagicLiterals] {len(listRow)} rows -> {args.out}")
    if args.meaning is None:
        printSummary(listRow)
        return ExitCode.Passed
    listMatch = [row for row in listRow if row.kind == "minus-one" and row.meaning == args.meaning and row.place in ((args.place,) if args.place else kReviewPlaces)]
    for row in listMatch[: args.limit]:
        print(f"{row.path}:{row.line}: [{row.place}] {row.context}")
    print(f"[MagicLiterals] {len(listMatch)} lines ({min(len(listMatch), args.limit)} shown)")
    return ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
