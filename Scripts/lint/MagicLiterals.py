#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/MagicLiterals.py

숫자 리터럴 래칫(게이트)과 집계(보고). `Source` 의 C++ 에서 `-1` · 실수 · 두 자리 이상 정수 · 16진수를 모아 **어디에 있는지**(생성물 · 테스트 ·
표 줄 · 리플렉션 메타 · 이름 붙은 정의 · 형식 번호 · 일반 코드)로 나누고, `-1` 은 **무슨 뜻인지**(인덱스 표지 · 찾지 못함 반환 · 무제한 · 기본값 사용 ·
방향 · 이름 붙은 표지)를 이름과 문맥으로 첫 분류한다. 정본 규칙은 `docs/agents/Constants.md`, 판단은 `docs/DesignBacklog.md` "데이터 기반 값 · 매직 리터럴".

**왜 래칫인가.** 리터럴 하나가 매직인지는 형식이 가르지 못한다(`0.5f` 는 절반이고 `0.35f` 는 튜닝이다). 그래서 모든 줄을 막지 않고, **일반 코드**
(`code` 장소)에서 흔한 수학 값이 아닌 리터럴의 **파일마다 종류별 수**를 기준선(`Config/Rules/Lint/MagicLiteralBaseline.json`)에 두고:
- 수가 기준선보다 **늘면** 커밋이 멈춘다 — 새 값에 이름을 붙이거나(`XxxInternal::k…`) 데이터(PROPERTY · 카탈로그)로 옮긴다.
- 수가 **줄면** 기준선도 같이 낮춰야 커밋이 선다(`--ratchet` 이 낮추기만 한다) — 옮긴 자리에 다시 리터럴이 들어오지 못한다.
- 기준선을 **올리는** 길은 `--accept <파일…>` 하나다(통합에서 합친 브랜치가 규칙 전 코드를 가져올 때). diff 에 그대로 보인다.

세지 않는 것(오탐 유형은 `--self-check` 가 고정한다): 테스트 · 생성물 · 리플렉션 메타 · 표 줄 · 이름 붙은 정의 · 형식 번호 장소, 흔한 수학 값
(`kCommonFloat` — 0 · 1 · 2 · 0.5), 모든 비트 마스크(`0xFF…`), 비트 이동 폭(`<< 16`), 뜻이 산술 · 운영체제 계약인 `-1`(방향 · 파일 기술자).

    py -3 -m Scripts lint magic-literals                    # 장소 × 종류 수, `-1` 뜻별 수, 기준선 합
    py -3 -m Scripts lint magic-literals --check [--all]    # 래칫 검사(커밋 훅은 staged 파일)
    py -3 -m Scripts lint magic-literals --ratchet          # 줄어든 파일의 기준선을 낮춘다(올리지 않는다)
    py -3 -m Scripts lint magic-literals --accept <파일…>   # 그 파일의 기준선을 지금 수로(올리는 유일한 길)
    py -3 -m Scripts lint magic-literals --meaning unclassified --limit 50   # 그 뜻의 줄(파일:줄: 값 | 문맥)
    py -3 -m Scripts lint magic-literals --out <scratch>/literals.tsv        # 모든 줄(검토 목록)
    py -3 -m Scripts lint magic-literals --self-check
"""

from __future__ import annotations

import argparse
import collections
import functools
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.ExitCode import ExitCode  # noqa: E402
from common.Git import runGit  # noqa: E402
from lint.CppShape import stripCode  # noqa: E402
from lint.Format import getModifiedCppFiles  # noqa: E402
from lint.LintScope import LintScope  # noqa: E402
from lint.Parallel import flatMapInProcesses  # noqa: E402


kBaselinePath = "Config/Rules/Lint/MagicLiteralBaseline.json"
kSourceExtensions = (".h", ".cpp", ".inl")
kKinds = ("minus-one", "float", "integer", "hex")

# 단항 `-1`(뺄셈 `x - 1` 은 아니다): 앞이 여는 괄호 · 쉼표 · 대입 · 비교 · return · 삼항 · 중괄호 · 논리 연산(`&& -1 <= x`)이다.
kMinusOne = re.compile(r"(?:(?<=[(,=<>?:{\[])|(?<=&&)|(?<=\|\|)|(?<=return)|(?<=\breturn ))\s*-\s*1\b(?![.\deEfFxX'])")
kFloat = re.compile(r"(?<![\w.])\d+\.\d*(?:[eE][+-]?\d+)?[fF]?(?![\w.])|(?<![\w.])\d+[eE][+-]?\d+[fF]?(?![\w.])")
kHex = re.compile(r"(?<![\w.])0[xX][0-9A-Fa-f']+[uUlL]*(?![\w.])")
# 앞이 `\` 면 문자 상수의 탈출(`L'\0'` — stripCode 는 접두 붙은 문자 상수를 지우지 못한다)이라 수가 아니다.
kInteger = re.compile(r"(?<![\w.'\\])\d[\d']+[uUlL]*(?![\w.'])")
kShiftWidth = re.compile(r"(?:<<|>>)=?\s*$")
kNamedDefinition = re.compile(r"\b(?:constexpr|const)\b[^=;()]*\bk[A-Z]\w*\s*(?:=|\{)")
kFormatName = re.compile(r"\bk\w*(?:Version|Magic|Tag|Revision)\b")
kReflectionMeta = re.compile(r"\b(?:PROPERTY|REFLECT|FUNCTION|ENUM|EVENT)\s*\(")
kTableRow = re.compile(r"^\s*\{.*\}\s*,?\s*$")
# 칸의 기본값: 선언(`float32 _speed{ 3.5f };`)과 생성자 초기화 목록(`, _speed{ 3.5f }`). 튜닝 값의 정본 자리다(PROPERTY · 설정 · 카탈로그 칸 —
# Constants.md "Tuning values are data") — 칸 이름이 뜻을 말한다. 단 `-1` 은 '없음' 표지라 여기서도 센다.
kFieldDefault = re.compile(r"^\s*(?!return\b)(?:[\w:<>,\*&]+\s+)+_\w+\s*(?:\{[^;{}]*\}|=\s*[^;=]+)\s*;|^\s*[:,]\s*_\w+\s*[{(][^;]*[})]\s*,?\s*$")
# 뜻을 사람이 볼 장소(테스트 기대값 · 생성물 · 리플렉션 메타는 옮기지 않는다 — Constants.md "Literals that stay literal")
kReviewPlaces = ("code", "named-definition", "table-row", "field-default")
# 래칫이 세는 장소. 이름 붙은 정의 · 표 줄 · 형식 번호 · 칸 기본값은 이미 이름이나 표가 뜻을 말한다.
kCountedPlace = "code"
# 흔한 수학 값 — 0 · 1 · 2 · 절반은 이름이 뜻을 더하지 않는다(`kHalf = 0.5f` 는 별칭이다). 0.25 · 10 · 100 은 넣지 않는다: 튜닝과 섞인다.
kCommonFloat = frozenset({0.0, 1.0, 2.0, 0.5})
kAllBitsHex = re.compile(r"0[xX](?:F|f)+[uUlL]*$")
# 뜻이 산술 · 외부 계약인 `-1` — 표지가 아니다.
kUncountedMeaning = frozenset({"os-contract", "direction"})


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
    MeaningRule("direction", re.compile(r"\?\s*1\s*:\s*-\s*1\b|\?\s*-\s*1\s*:\s*[01]\b|-\s*1\s*:\s*[01]\b|\bclamp\s*\([^;]*-\s*1\s*,\s*1\b|nextInt\s*\(\s*-\s*1\b"),
                "부호 · 방향 삼항(`? 1 : -1`), -1 ~ 1 범위 — 산술이다(옮기지 않는다)"),
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
    place: str     # generated · test · reflection-meta · format · named-definition · table-row · field-default · code
    meaning: str   # `-1` 의 뜻(그 밖은 빈 글)
    context: str
    common: bool   # 흔한 수학 값 · 모든 비트 마스크 · 비트 이동 폭 · 산술 `-1` — 래칫이 세지 않는다

    @property
    def counted(self) -> bool:
        if self.common:
            return False
        return self.place == kCountedPlace or (self.place == "field-default" and self.kind == "minus-one")

    def toTsv(self) -> str:
        return "\t".join((self.path, str(self.line), self.kind, self.value, self.place, self.meaning, "common" if self.common else "", self.context.replace("\t", " ")))


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
    if kFieldDefault.match(line) is not None:
        return "field-default"
    return "code"


def classifyMinusOne(line: str) -> str:
    """`-1` 한 줄의 뜻입니다. 규칙에 맞지 않으면 `unclassified`."""
    for rule in kMeaningRule:
        if rule.pattern.search(line) is not None:
            return rule.meaning
    return "unclassified"


def isCommonFloat(value: str) -> bool:
    try:
        return float(value.rstrip("fF")) in kCommonFloat
    except ValueError:
        return False


def inspectText(text: str, relative: str) -> list[LiteralRow]:
    """글 하나의 리터럴을 모읍니다(자체 시험이 파일 없이 부를 수 있게 나눴다). 주석 · 문자열 · 전처리 줄은 보지 않는다."""
    listRow: list[LiteralRow] = []
    listRaw = text.split("\n")
    for index, line in enumerate(stripCode(text).split("\n")):
        if not any(char.isdigit() for char in line):
            continue
        place = classifyPlace(relative, line)
        context = listRaw[index].strip()[:160]

        def addRow(kind: str, value: str, meaning: str, common: bool) -> None:
            listRow.append(LiteralRow(relative, index + 1, kind, value, place, meaning, context, common))

        for _ in kMinusOne.finditer(line):
            meaning = classifyMinusOne(listRaw[index])   # 뜻은 주석까지 본다(`// 무제한`)
            addRow("minus-one", "-1", meaning, meaning in kUncountedMeaning)
        masked = kMinusOne.sub(lambda match: " " * len(match.group(0)), line)
        for match in kFloat.finditer(masked):
            addRow("float", match.group(0), "", isCommonFloat(match.group(0)))
        masked = kFloat.sub(lambda match: " " * len(match.group(0)), masked)
        for match in kHex.finditer(masked):
            addRow("hex", match.group(0), "", kAllBitsHex.match(match.group(0)) is not None)
        masked = kHex.sub(lambda match: " " * len(match.group(0)), masked)
        for match in kInteger.finditer(masked):
            addRow("integer", match.group(0), "", kShiftWidth.search(masked[: match.start()]) is not None)
    return listRow


def inspectFile(projectRoot: Path, file: Path) -> list[LiteralRow]:
    """파일 하나를 봅니다(프로세스로 넘기므로 모듈 최상위에 둔다)."""
    return inspectText(file.read_text(encoding="utf-8", errors="ignore"), file.relative_to(projectRoot).as_posix())


def isSourceFile(relative: str) -> bool:
    return relative.startswith("Source/") and relative.endswith(kSourceExtensions) and "/ThirdParty/" not in relative


def listSourceFiles(projectRoot: Path) -> list[Path]:
    return sorted(path for path in (projectRoot / "Source").rglob("*") if path.is_file() and isSourceFile(path.relative_to(projectRoot).as_posix()))


def collectRows(projectRoot: Path, listFile: Sequence[Path] | None = None) -> list[LiteralRow]:
    listRow = flatMapInProcesses(functools.partial(inspectFile, projectRoot), list(listFile) if listFile is not None else listSourceFiles(projectRoot))
    return sorted(listRow, key=lambda row: (row.path, row.line))


# --- 래칫 ------------------------------------------------------------------------------------------

def countByFile(listRow: Iterable[LiteralRow]) -> dict[str, dict[str, int]]:
    """파일마다 래칫이 세는 종류별 수입니다(0 인 종류 · 파일은 적지 않는다 — 기준선 JSON 이 짧고 diff 가 읽힌다)."""
    counts: dict[str, collections.Counter[str]] = collections.defaultdict(collections.Counter)
    for row in listRow:
        if row.counted:
            counts[row.path][row.kind] += 1
    return {path: {kind: counter[kind] for kind in kKinds if counter[kind] > 0} for path, counter in counts.items()}


def readBaseline(projectRoot: Path) -> dict[str, dict[str, int]]:
    """기준선입니다. 파일이 없으면 오류로 멈춘다(빈 기준선으로 돌면 모든 수가 '늘었다' 가 아니라 검사 없이 지나갈 수 있다)."""
    return json.loads((projectRoot / kBaselinePath).read_text(encoding="utf-8"))["_files"]


def writeBaseline(projectRoot: Path, baseline: dict[str, dict[str, int]]) -> None:
    comment = ("매직 리터럴 래칫 기준선(Scripts/lint/MagicLiterals.py). 파일마다 일반 코드의 이름 없는 리터럴 수 — 늘면 커밋이 멈추고, 줄면 "
               "`py -3 -m Scripts lint magic-literals --ratchet` 로 낮춘다. 손으로 올리지 않는다(올리는 길은 --accept 하나, docs/agents/Constants.md).")
    # 파일 하나가 한 줄이고 합계 줄은 두지 않는다 — 병렬 브랜치가 다른 파일을 낮출 때 병합이 부딪히지 않는다(합계는 보고가 찍는다).
    listLine = [f"    {json.dumps(path)}: {json.dumps(baseline[path])}" for path in sorted(baseline) if baseline[path]]
    text = (f"{{\n  \"_comment\": {json.dumps(comment, ensure_ascii=False)},\n"
            "  \"_files\": {\n" + ",\n".join(listLine) + "\n  }\n}\n")
    (projectRoot / kBaselinePath).write_text(text, encoding="utf-8", newline="\n")


def compareCounts(path: str, actual: dict[str, int], allowed: dict[str, int]) -> tuple[list[str], list[str]]:
    """(늘어난 종류, 줄어든 종류) 설명입니다."""
    listGrown: list[str] = []
    listShrunk: list[str] = []
    for kind in kKinds:
        have, limit = actual.get(kind, 0), allowed.get(kind, 0)
        if have > limit:
            listGrown.append(f"{path}: {kind} {limit} -> {have}")
        elif have < limit:
            listShrunk.append(f"{path}: {kind} {limit} -> {have}")
    return listGrown, listShrunk


def readStagedRenames(projectRoot: Path) -> dict[str, str]:
    """staged 이름 바꾸기의 새 경로 → 옛 경로입니다(옮긴 파일은 옛 경로의 기준선을 이어받는다)."""
    result = runGit(["diff", "--cached", "--name-status", "-M"], cwd=projectRoot)
    if result.returnCode != 0:
        raise RuntimeError(f"git diff --cached --name-status 실패: {result.stderr.strip()}")
    mapOld: dict[str, str] = {}
    for line in result.stdout.splitlines():
        listPart = line.split("\t")
        if len(listPart) == 3 and listPart[0].startswith("R"):
            mapOld[listPart[2]] = listPart[1]
    return mapOld


def findRatchetProblems(actual: dict[str, dict[str, int]], baseline: dict[str, dict[str, int]], listPath: Iterable[str], mapOld: dict[str, str]) -> list[str]:
    """`listPath` 파일마다 수를 기준선과 대조합니다. 늘었으면 이름 붙이기를, 줄었으면 기준선 낮추기를 요구한다."""
    listProblem: list[str] = []
    for path in sorted(set(listPath)):
        allowed = baseline.get(path, baseline.get(mapOld.get(path, ""), {}))
        listGrown, listShrunk = compareCounts(path, actual.get(path, {}), allowed)
        listProblem += [f"{line} — 늘었다: 이름 붙은 상수(XxxInternal · 소유 타입)나 데이터(PROPERTY · 카탈로그)로 옮긴다" for line in listGrown]
        listProblem += [f"{line} — 줄었다: `py -3 -m Scripts lint magic-literals --ratchet` 로 기준선을 낮추고 함께 커밋한다" for line in listShrunk]
    return listProblem


def checkMagicLiterals(projectRoot: Path, scope: LintScope) -> list[str]:
    """scope 의 C++ 파일의 래칫 검사입니다. 없는 파일의 기준선은 저장소 전체일 때만 센다."""
    listRelative = [relative for relative in scope.select() if isSourceFile(relative)]
    listProblem: list[str] = []
    if listRelative:
        actual = countByFile(collectRows(projectRoot, [projectRoot / relative for relative in listRelative]))
        listProblem = findRatchetProblems(actual, readBaseline(projectRoot), listRelative, readStagedRenames(projectRoot))
    if scope.bWholeRepo:
        setExisting = set(scope.listFile)
        listProblem += [f"{path}: 없는 파일의 기준선 — `--ratchet` 으로 지운다" for path in sorted(readBaseline(projectRoot)) if path not in setExisting]
    return listProblem


def lowerBaseline(baseline: dict[str, dict[str, int]], actual: dict[str, dict[str, int]], listExisting: set[str]) -> dict[str, dict[str, int]]:
    """줄어든 수만 낮춘다(늘어난 수는 기준선을 지킨다). 없어진 파일은 지운다."""
    lowered: dict[str, dict[str, int]] = {}
    for path, allowed in baseline.items():
        if path not in listExisting:
            continue
        have = actual.get(path, {})
        lowered[path] = {kind: min(allowed[kind], have.get(kind, 0)) for kind in allowed if min(allowed[kind], have.get(kind, 0)) > 0}
    return lowered


# --- 보고 ------------------------------------------------------------------------------------------

def printSummary(listRow: list[LiteralRow]) -> None:
    countByPlace: collections.Counter[tuple[str, str]] = collections.Counter((row.place, row.kind) for row in listRow)
    listPlace = ("code", "field-default", "named-definition", "format", "table-row", "reflection-meta", "test", "generated")
    print("## 장소 × 종류(리터럴 수)")
    print(f"{'place':<18}" + "".join(f"{kind:>11}" for kind in kKinds))
    for place in listPlace:
        print(f"{place:<18}" + "".join(f"{countByPlace[(place, kind)]:>11}" for kind in kKinds))
    countCounted = collections.Counter(row.kind for row in listRow if row.counted)
    print(f"{'ratchet':<18}" + "".join(f"{countCounted[kind]:>11}" for kind in kKinds) + "   (일반 코드에서 흔한 값을 뺀 수 — 기준선이 세는 것)")
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

    def expect(condition: bool, message: str) -> None:
        nonlocal failures
        if not condition:
            print(f"[MagicLiterals] self-check: {message}")
            failures += 1

    for line, expected in kArrCase:
        actual = classifyMinusOne(line)
        expect(actual == expected, f"'{line}' -> {actual}, expected {expected}")
    listRow = inspectText("int32 last = size - 1;\nint32 none = -1; // -1 in a comment\nfloat32 half = 0.5f;\nuint32 mask = 0xFF;\n", "Source/Sample.cpp")
    expect([row.kind for row in listRow] == ["minus-one", "float", "hex"], "subtraction and comments are not literals")
    expect(classifyPlace("Source/Engine/Foo/Tests/TestFoo.cpp", "x = 42;") == "test", "tests are their own place")
    expect(classifyPlace("Source/Engine/Foo.cpp", "PROPERTY( Min = 10 )") == "reflection-meta", "reflection metadata is its own place")

    # 래칫이 세지 않는 오탐 유형 — 하나씩 고정한다.
    def countedValues(code: str) -> list[str]:
        return [row.value for row in inspectText(code, "Source/Sample.cpp") if row.counted]

    expect(countedValues("x = 0.0f + 1.0 * 2.0f - 0.5f;") == [], "common math floats (0 · 1 · 2 · 0.5) are not counted")
    expect(countedValues("x = speed * 0.35f;") == ["0.35f"], "a tuning float is counted")
    expect(countedValues("m = v & 0xFFFFFFFFu; n = w & 0xFF;") == [], "all-bits masks are not counted")
    expect(countedValues("m = v & 0x7F;") == ["0x7F"], "a partial mask is counted")
    expect(countedValues("x = ( a << 16 ) | ( b >> 24 ); y <<= 8;") == [], "shift widths are not counted")
    expect(countedValues("x = a * 16;") == ["16"], "a multiplier is counted")
    expect(countedValues("if ( *p == L'\\0' ) {}") == [], "an escaped char literal is not a number")
    expect(countedValues("const Vec3 down( 0.0f, -1, 0.0f );") == [], "direction -1 is arithmetic")
    expect(countedValues("static constexpr float32 kSpeed = 3.5f;") == [], "a named definition is not counted")
    expect(countedValues("    { 3.5f, 12, 0x10 },") == [], "an initializer table row is not counted")
    expect(countedValues("int32 _target = -1;") == ["-1"], "an index sentinel -1 is counted")
    expect(countedValues("io.check( 0 <= a && -1 <= b );") == ["-1"], "-1 after a logical operator is unary")
    expect(countedValues("        float32 _walkSpeed{ 3.5f }; ///< 걷기 속도") == [], "a field default is the tuning value's home")
    expect(countedValues("        , _walkSpeed{ 3.5f }") == [], "a constructor initializer is the PROPERTY default's home")
    expect(countedValues("        int32 _target{ -1 };") == ["-1"], "a -1 field default is still a sentinel")
    expect(countedValues("        pilot._heat = 3.5f;") == ["3.5f"], "assigning a member in a body is code")
    expect(countedValues("        return _speed = 3.5f;") == ["3.5f"], "return is not a type")
    expect(countedValues("return a < b ? -1 : 1;") == [], "a sign ternary is arithmetic")

    # 래칫 판정 — 늘면 · 줄면 모두 멈추고, 옮긴 파일은 옛 기준선을 잇는다.
    baseline = {"Source/A.cpp": {"float": 2}, "Source/Old.cpp": {"hex": 1}}
    expect(findRatchetProblems({"Source/A.cpp": {"float": 2}}, baseline, ["Source/A.cpp"], {}) == [], "equal counts pass")
    expect(len(findRatchetProblems({"Source/A.cpp": {"float": 3}}, baseline, ["Source/A.cpp"], {})) == 1, "a grown count fails")
    expect(len(findRatchetProblems({"Source/A.cpp": {"float": 1}}, baseline, ["Source/A.cpp"], {})) == 1, "a shrunk count asks to lower the baseline")
    expect(len(findRatchetProblems({"Source/New.cpp": {"integer": 1}}, baseline, ["Source/New.cpp"], {})) == 1, "a new file starts at zero")
    expect(findRatchetProblems({"Source/Moved.cpp": {"hex": 1}}, baseline, ["Source/Moved.cpp"], {"Source/Moved.cpp": "Source/Old.cpp"}) == [], "a moved file keeps its baseline")
    lowered = lowerBaseline({"Source/A.cpp": {"float": 2, "hex": 1}, "Source/Gone.cpp": {"float": 1}}, {"Source/A.cpp": {"float": 3}}, {"Source/A.cpp"})
    expect(lowered == {"Source/A.cpp": {"float": 2}}, f"--ratchet lowers only and drops missing files: {lowered}")
    print(f"[MagicLiterals] self-check {'OK' if failures == 0 else 'FAIL'}")
    return ExitCode.Passed if failures == 0 else ExitCode.Violation


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="숫자 리터럴 래칫(일반 코드의 이름 없는 리터럴이 파일마다 늘지 않게)과 집계: 장소 × 종류, `-1` 의 뜻 첫 분류")
    parser.add_argument("--check", action="store_true", help="수정된 C++ 파일(작업 트리 대 HEAD)을 기준선과 대조한다(--all 은 모든 파일)")
    parser.add_argument("--all", action="store_true", help="--check 와 함께: 모든 Source 파일")
    parser.add_argument("--ratchet", action="store_true", help="줄어든 파일의 기준선을 낮추고 없어진 파일을 지운다(올리지 않는다)")
    parser.add_argument("--accept", nargs="+", default=None, metavar="FILE", help="그 파일들의 기준선을 지금 수로 바꾼다(올리는 유일한 길 — 통합에서만)")
    parser.add_argument("--meaning", default=None, help="이 뜻의 `-1` 줄을 찍는다(unclassified · index-sentinel · unlimited …)")
    parser.add_argument("--place", default=None, help="--meaning 과 함께 장소를 고른다(없으면 code · named-definition · table-row, test 도 고를 수 있다)")
    parser.add_argument("--limit", type=int, default=100, help="찍는 줄 수 상한")
    parser.add_argument("--out", default=None, help="모든 줄을 TSV 로 쓴다(검토 목록 — 경로 · 줄 · 종류 · 값 · 장소 · 뜻 · 흔한 값 · 문맥)")
    parser.add_argument("--self-check", action="store_true", help="분류 · 래칫 규칙의 자체 시험")
    args = parser.parse_args(argv)
    sys.stdout.reconfigure(encoding="utf-8")
    if args.self_check:
        return runSelfCheck()

    projectRoot = Path(__file__).resolve().parents[2]
    if args.check:
        listFile = listSourceFiles(projectRoot) if args.all else getModifiedCppFiles(projectRoot)
        listRelative = [path.relative_to(projectRoot).as_posix() for path in listFile]
        actual = countByFile(collectRows(projectRoot, [path for path, relative in zip(listFile, listRelative) if isSourceFile(relative)]))
        baseline = readBaseline(projectRoot)
        listProblem = findRatchetProblems(actual, baseline, [relative for relative in listRelative if isSourceFile(relative)], {})
        if args.all:
            setExisting = set(listRelative)
            listProblem += [f"{path}: 없는 파일의 기준선 — `--ratchet` 으로 지운다" for path in sorted(baseline) if path not in setExisting]
        for problem in listProblem:
            print(f"  - {problem}")
        print(f"[MagicLiterals] {'FAIL' if listProblem else 'OK'} ({len(listProblem)}건)")
        return ExitCode.Violation if listProblem else ExitCode.Passed
    if args.ratchet or args.accept is not None:
        listFile = listSourceFiles(projectRoot)
        listExisting = {path.relative_to(projectRoot).as_posix() for path in listFile}
        baselinePath = projectRoot / kBaselinePath
        baseline = readBaseline(projectRoot) if baselinePath.is_file() else {}
        if args.accept is not None:
            listAccept = [Path(path).resolve() for path in args.accept]
            actual = countByFile(collectRows(projectRoot, [path for path in listAccept if path.is_file()]))
            for path in listAccept:
                relative = path.relative_to(projectRoot).as_posix()
                print(f"[MagicLiterals] accept {relative}: {baseline.get(relative, {})} -> {actual.get(relative, {})}")
                baseline[relative] = actual.get(relative, {})
        else:
            baseline = lowerBaseline(baseline, countByFile(collectRows(projectRoot, listFile)), listExisting)
        writeBaseline(projectRoot, baseline)
        print(f"[MagicLiterals] baseline -> {kBaselinePath}")
        return ExitCode.Passed

    listRow = collectRows(projectRoot)
    if args.out is not None:
        Path(args.out).write_text("path\tline\tkind\tvalue\tplace\tmeaning\tcommon\tcontext\n" + "\n".join(row.toTsv() for row in listRow) + "\n", encoding="utf-8")
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
