#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
C++ 함수마다 본문 줄 수 · 최대 중첩 깊이 · `return` 수 · 본문 안 람다 줄 수를 재서 긴 함수를 보고한다.

[왜 필요한가]
긴 함수를 나눌지 정하려면 목록이 먼저다(docs/plans/LongFunctions.md). 단순 정규식은 여러 줄 시그니처, 람다, 매크로 본문,
주석 · 문자열 속 중괄호에서 틀린다. 그래서 주석 · 리터럴을 지운 글(`blankCommentsAndLiterals`)과 전처리 줄을 지운 글에서 중괄호를 깊이로 센다.

[게이트가 아니다]
줄 수 상한을 게이트로 두면 이유 없는 기계적 분할이 늘어난다(결정 기록). 종료 코드는 늘 0 이다.

[어떻게 재는가]
- 선언 자리(파일 · 네임스페이스 · 클래스 본문)에서 여는 중괄호의 머리(앞의 `;` · 닫힌 본문 뒤부터 그 중괄호까지)를 보고 갈래를 정한다:
  `namespace` · `extern` 은 선언 자리로 내려가고, `class` · `struct` · `union` 도 선언 자리, `enum` 과 `=` 가 든 초기화는 건너뛴다.
  나머지에서 머리가 `이름( … )` 와 한정어(`const` · `noexcept` · `override` …)로 끝나면 함수 본문이다. 생성자의 초기화 목록(`) : _a( a ), _b{ b }`)은
  `)` 뒤의 첫 단독 `:` 에서 잘라 낸다 — 그 안의 `_b{ b }` 는 초기화 중괄호로 건너뛴다.
- 함수 이름은 머리 끝의 `)` 에서 괄호 짝을 거꾸로 맞춰 그 앞의 이름을 읽으므로 여러 줄 시그니처도 맞다. 이름이 대문자 매크로(`SW_TEST_CASE( A, B )`)면 인자를 붙인다.
- 본문 줄 수 = 닫는 중괄호 줄 − 여는 중괄호 줄(clang-tidy `readability-function-size` 의 `LineThreshold` 와 같은 셈 — 빈 줄 · 주석 줄도 든다).
- 중첩 깊이는 본문 안 중괄호 깊이다(본문 자신이 0). 중괄호 없는 `if ( x ) y;` 는 세지 않는다.
- 람다는 여는 중괄호 앞이 `]` 이거나 `]( … )`(뒤의 `mutable` · `-> 타입` 포함)인 것이다. 람다 줄 수는 바깥 람다들의 (닫는 줄 − 여는 줄) 합이고,
  `return` 수는 람다 안의 것을 빼고 센다.
- 전처리 줄(`#…` 과 `\\` 로 이어진 줄)은 지운다 — 매크로 본문의 중괄호를 세지 않는다. `#if` 갈래마다 여는 중괄호가 따로 있으면 짝이 어긋날 수 있어,
  짝이 맞지 않은 파일은 따로 알린다.

사용법:
  py -3 -m Scripts lint report RunFunctionSize                          # Source, 100 줄 넘는 함수
  py -3 Scripts/lint/report/RunFunctionSize.py --min-lines 60 --filter Engine/Automation
  py -3 Scripts/lint/report/RunFunctionSize.py --group-depth 3          # 영역(Source/<층>/<폴더>)별 수
"""

from __future__ import annotations

import argparse
import bisect
import re
import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintReport

from common import blankCommentsAndLiterals, blankMatch, collectSourceFiles, kCppAllExtensions, kNotOurDirNames  # noqa: E402
from LintReport import LintReport, ReportContext  # noqa: E402

kDefaultMinLines = 100
kDefaultRoots: tuple[str, ...] = ("Source",)

#: 전처리 줄 — `\` 로 이어진 줄까지 한 덩어리.
_kPreprocessorRe = re.compile(r"^[ \t]*#(?:[^\n]*\\\n)*[^\n]*", re.MULTILINE)
#: 머리 끝에서 떼어 낼 한정어(함수 본문 앞에 올 수 있는 것).
_kFunctionQualifiers: tuple[str, ...] = ("const", "volatile", "noexcept", "override", "final", "mutable", "&&", "&")
_kLambdaQualifiers: tuple[str, ...] = ("mutable", "constexpr", "noexcept")
#: 접근 지정 레이블(`public:`) — 머리 앞에 남아 있을 수 있다.
_kAccessLabelRe = re.compile(r"^\s*(?:public|protected|private)\s*:(?!:)")
_kIdentifierEndRe = re.compile(r"([A-Za-z_~][A-Za-z_0-9]*)\s*$")
_kOperatorEndRe = re.compile(r"\boperator\s*(\(\)|\[\]|[^\sA-Za-z_0-9(]+|\s+[A-Za-z_][A-Za-z_0-9:<>*&\s]*?)\s*$")
_kReturnRe = re.compile(r"\breturn\b")
_kTemplateHeadRe = re.compile(r"^\s*template\s*<")
#: 걷는 자리 — 선언 자리를 끊는 `;` 와 중괄호.
_kTokenRe = re.compile(r"[{};]")
#: 람다인지 볼 때 여는 중괄호 앞에서 읽는 글자 수(캡처 목록 · 인자 목록 · 뒤 반환 형식이 들어갈 만큼).
_kLambdaLookBehind = 4000


@dataclass
class FunctionSize:
    """함수 하나의 잰 값."""

    path: str
    name: str
    line: int            # 여는 중괄호 줄(1 부터)
    bodyLines: int
    maxDepth: int
    returnCount: int
    lambdaLines: int


@dataclass
class FileMeasure:
    """파일 하나를 잰 결과. `bBalanced` 가 거짓이면 중괄호 짝이 맞지 않았다(`#if` 갈래 등) — 그 파일의 값은 믿지 않는다."""

    listFunction: list[FunctionSize]
    bBalanced: bool


def blankPreprocessor(text: str) -> str:
    """전처리 줄을 같은 길이의 공백으로 바꿉니다(줄바꿈은 남긴다)."""
    return _kPreprocessorRe.sub(blankMatch, text)


def stripTrailingWordInternal(text: str, listWord: tuple[str, ...]) -> str:
    """끝의 낱말 하나(@p listWord 가운데)를 떼어 냅니다. 끝에 `$` 를 건 정규식은 긴 글에서 느려 글 끝만 본다."""
    for word in listWord:
        if not text.endswith(word):
            continue
        rest = text[:-len(word)]
        if word[0].isalpha() and rest and (rest[-1].isalnum() or rest[-1] == "_"):
            continue   # `myconst` 처럼 낱말의 일부다
        return rest.rstrip()
    return text


def findMatchingOpenInternal(text: str, closeIndex: int) -> int:
    """`text[closeIndex]` 의 `)` 와 짝인 `(` 의 위치입니다. 없으면 -1."""
    depth = 0
    for index in range(closeIndex, -1, -1):
        character = text[index]
        if character == ")":
            depth += 1
        elif character == "(":
            depth -= 1
            if depth == 0:
                return index
    return -1


def topLevelOutsideParensInternal(head: str) -> str:
    """괄호 · 꺾쇠 바깥 글만 남깁니다(안은 공백). `=` · `:` 를 최상위에서 찾을 때 쓴다."""
    listCharacter: list[str] = []
    parenDepth = 0
    for character in head:
        if character == "(":
            parenDepth += 1
        elif character == ")":
            parenDepth -= 1
            listCharacter.append(" ")
            continue
        listCharacter.append(character if parenDepth == 0 else " ")
    return "".join(listCharacter)


def stripTemplateHeadInternal(head: str) -> str:
    """머리 앞의 `template < … >` 를 떼어 냅니다(여러 겹이어도)."""
    while _kTemplateHeadRe.match(head):
        start = head.index("<")
        depth = 0
        for index in range(start, len(head)):
            if head[index] == "<":
                depth += 1
            elif head[index] == ">":
                depth -= 1
                if depth == 0:
                    head = head[index + 1:]
                    break
        else:
            return head
    return head


def cutInitializerListInternal(head: str) -> str:
    """최상위 `)` 뒤의 첫 단독 `:`(생성자 초기화 목록)이나 `->`(뒤 반환 형식)에서 머리를 자릅니다."""
    outside = topLevelOutsideParensInternal(head)
    firstClose = head.find(")")
    if firstClose < 0:
        return head
    for index in range(firstClose, len(outside)):
        if outside.startswith("->", index):
            return head[:index]
        if outside[index] == ":" and outside[index - 1:index] != ":" and outside[index + 1:index + 2] != ":":
            return head[:index]
    return head


def readFunctionNameInternal(head: str) -> str | None:
    """함수 본문 머리면 함수 이름을, 아니면 None 을 돌려줍니다."""
    head = _kAccessLabelRe.sub("", head)
    cut = cutInitializerListInternal(head)
    initializerList = head[len(cut):].strip()
    if initializerList.startswith(":") and re.search(r"[A-Za-z_0-9>]$", initializerList):
        return None   # 초기화 목록 안의 중괄호 초기화(`: _a( a ), _b{`) — 본문은 그 뒤다
    head = cut.rstrip()
    while True:
        stripped = stripTrailingWordInternal(head.rstrip(), _kFunctionQualifiers)
        if stripped.endswith(")"):
            # `noexcept( … )` 는 한정어다 — 그 앞이 또 `)` 면 떼어 낸다.
            openIndex = findMatchingOpenInternal(stripped, len(stripped) - 1)
            before = stripped[:openIndex].rstrip() if openIndex >= 0 else ""
            if before.endswith("noexcept") and before[:-len("noexcept")].rstrip().endswith(")"):
                head = before[:-len("noexcept")].rstrip()
                continue
        if stripped == head:
            break
        head = stripped
    if not head.endswith(")"):
        return None
    openIndex = findMatchingOpenInternal(head, len(head) - 1)
    if openIndex <= 0:
        return None
    before = head[:openIndex].rstrip()
    if before.endswith("]"):
        return None   # 선언 자리의 람다 — 초기화의 일부다
    operatorMatch = _kOperatorEndRe.search(before)
    if operatorMatch:
        scopeMatch = re.search(r"((?:[A-Za-z_][A-Za-z_0-9]*\s*::\s*)*)operator", before[:operatorMatch.end()])
        prefix = re.sub(r"\s+", "", scopeMatch.group(1)) if scopeMatch else ""
        return prefix + "operator" + re.sub(r"\s+", " ", operatorMatch.group(1)).strip()
    nameMatch = _kIdentifierEndRe.search(before)
    if not nameMatch:
        return None
    name = nameMatch.group(1)
    if name in {"if", "for", "while", "switch", "catch", "return", "sizeof", "alignas", "decltype"}:
        return None
    # 이름 앞의 한정 이름(`Foo::bar`)을 붙인다.
    scopeMatch = re.search(r"((?:[A-Za-z_~][A-Za-z_0-9]*\s*(?:<[^<>;{}]*>)?\s*::\s*)+)" + re.escape(name) + r"\s*$", before)
    if scopeMatch:
        name = re.sub(r"\s+", "", scopeMatch.group(1)) + name
    if name.isupper() or re.fullmatch(r"[A-Z][A-Z0-9_]+", name):
        arguments = re.sub(r"\s+", " ", head[openIndex + 1:-1]).strip()
        name = f"{name}({arguments})"
    return name


def classifyHeadInternal(head: str) -> str:
    """선언 자리의 여는 중괄호 머리 → `scope`(네임스페이스 · 클래스), `skip`(enum · 초기화), 아니면 함수 이름."""
    stripped = stripTemplateHeadInternal(head).strip()
    outside = topLevelOutsideParensInternal(stripped)
    if re.search(r"\bnamespace\b", outside) or re.fullmatch(r"extern\s*", outside.strip() + " "):
        return "scope"
    if re.search(r"\benum\b", outside):
        return "skip"
    if re.search(r"(?<![=!<>])=(?!=)", re.sub(r"\boperator\s*\S*=", "", outside)) or re.search(r"\breturn\b", outside):
        return "skip"
    name = readFunctionNameInternal(stripped)
    if re.search(r"\b(?:class|struct|union)\b", outside) and name is None:
        return "scope"
    if name is None:
        return "skip"
    return "function:" + name


def isLambdaOpenInternal(text: str, braceIndex: int) -> bool:
    """`text[braceIndex]` 의 `{` 가 람다 본문을 여는가. 앞의 `_kLambdaLookBehind` 글자만 본다(파일 앞부분을 통째로 자르면 느리다)."""
    before = text[max(0, braceIndex - _kLambdaLookBehind):braceIndex].rstrip()
    # `-> 타입` 은 떼어 낸다(타입 안에 `{` 는 없다).
    arrowIndex = before.rfind("->")
    if arrowIndex >= 0 and re.fullmatch(r"->\s*[A-Za-z_0-9:<>,\s*&]*", before[arrowIndex:]):
        before = before[:arrowIndex].rstrip()
    while True:
        stripped = stripTrailingWordInternal(before, _kLambdaQualifiers)
        if stripped == before:
            break
        before = stripped
    if before.endswith("]"):
        return True
    if before.endswith(")"):
        openIndex = findMatchingOpenInternal(before, len(before) - 1)
        return openIndex > 0 and before[:openIndex].rstrip().endswith("]")
    return False


def lineAt(listLineStart: list[int], offset: int) -> int:
    """오프셋 → 줄 번호(1 부터). @p listLineStart 는 줄마다 시작 오프셋이다."""
    return bisect.bisect_right(listLineStart, offset)


def measureText(text: str, path: str = "") -> FileMeasure:
    """C++ 글 하나에서 함수마다 값을 잽니다. 글자마다 돌지 않고 `{` · `}` · `;` 자리만 걷는다."""
    code = blankPreprocessor(blankCommentsAndLiterals(text))
    listLineStart = [0] + [match.end() for match in re.finditer("\n", code)]
    listToken = [(match.start(), match.group(0)) for match in _kTokenRe.finditer(code)]
    listFunction: list[FunctionSize] = []

    # 선언 자리의 틀 — 종류는 `scope`(네임스페이스 · 클래스) · `skip`(enum · 초기화).
    listFrame: list[str] = []
    headStart = 0
    bBalanced = True
    tokenIndex = 0
    while tokenIndex < len(listToken):
        index, character = listToken[tokenIndex]
        if character == ";" and (not listFrame or listFrame[-1] == "scope"):
            headStart = index + 1
        elif character == "{":
            if listFrame and listFrame[-1] != "scope":
                listFrame.append("skip")
            else:
                kind = classifyHeadInternal(code[headStart:index])
                if kind.startswith("function:"):
                    closeTokenIndex, measure = measureBodyInternal(code, listToken, tokenIndex, listLineStart)
                    if closeTokenIndex < 0:
                        bBalanced = False
                        break
                    listFunction.append(FunctionSize(path=path, name=kind[len("function:"):], line=lineAt(listLineStart, index), **measure))
                    tokenIndex = closeTokenIndex + 1
                    headStart = listToken[closeTokenIndex][0] + 1
                    continue
                listFrame.append(kind)
                if kind == "scope":
                    headStart = index + 1
        elif character == "}":
            if not listFrame:
                bBalanced = False
                headStart = index + 1
            elif listFrame.pop() == "scope":
                # 초기화 중괄호(`_b{ b }` · `= { … }`)는 머리를 끊지 않는다 — 생성자 초기화 목록이 뒤에 이어진다.
                headStart = index + 1
        tokenIndex += 1
    if listFrame:
        bBalanced = False
    return FileMeasure(listFunction=listFunction, bBalanced=bBalanced)


def measureBodyInternal(code: str, listToken: list[tuple[int, str]], openTokenIndex: int, listLineStart: list[int]) -> tuple[int, dict[str, int]]:
    """`listToken[openTokenIndex]` 의 `{` 에서 여는 함수 본문을 끝까지 잽니다. 닫는 중괄호의 토큰 순번(못 찾으면 -1)과 값을 돌려줍니다."""
    openIndex = listToken[openTokenIndex][0]
    depth = 0
    maxDepth = 0
    returnCount = 0
    lambdaLines = 0
    lambdaDepth = -1            # 바깥 람다가 열린 깊이(-1 이면 람다 밖)
    lambdaOpenLine = 0
    segmentStart = openIndex + 1
    for tokenIndex in range(openTokenIndex, len(listToken)):
        index, character = listToken[tokenIndex]
        if character == ";":
            continue
        if character == "{":
            if index != openIndex:
                if lambdaDepth < 0:
                    returnCount += len(_kReturnRe.findall(code, segmentStart, index))
                    if isLambdaOpenInternal(code, index):
                        lambdaDepth = depth + 1
                        lambdaOpenLine = lineAt(listLineStart, index)
                    segmentStart = index + 1
            depth += 1
            maxDepth = max(maxDepth, depth - 1)
        elif character == "}":
            depth -= 1
            if lambdaDepth >= 0 and depth + 1 == lambdaDepth:
                lambdaLines += lineAt(listLineStart, index) - lambdaOpenLine
                lambdaDepth = -1
                segmentStart = index + 1
            elif lambdaDepth < 0:
                returnCount += len(_kReturnRe.findall(code, segmentStart, index))
                segmentStart = index + 1
            if depth == 0:
                bodyLines = lineAt(listLineStart, index) - lineAt(listLineStart, openIndex)
                return tokenIndex, {"bodyLines": bodyLines, "maxDepth": maxDepth, "returnCount": returnCount, "lambdaLines": lambdaLines}
    return -1, {}


def areaOf(path: str, groupDepth: int) -> str:
    """저장소 상대 경로의 앞 @p groupDepth 마디(폴더)입니다."""
    listPart = path.split("/")[:-1]
    return "/".join(listPart[:groupDepth])


class RunFunctionSizeReport(LintReport):
    description = "C++ 함수 길이 보고(본문 줄 · 중첩 · return · 람다 줄)"
    bUsesFilter = True
    bUsesOut = True

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        parser.add_argument("--min-lines", type=int, default=kDefaultMinLines, help=f"본문이 이 줄 수보다 긴 함수만(기본 {kDefaultMinLines})")
        parser.add_argument("--path", action="append", default=[], help=f"훑을 저장소 상대 폴더(여러 번, 기본 {', '.join(kDefaultRoots)})")
        parser.add_argument("--group-depth", type=int, default=0, help="0 이 아니면 영역(경로 앞 마디 수)별 수와 상위 함수를 낸다")
        parser.add_argument("--top", type=int, default=5, help="--group-depth 일 때 영역마다 보일 상위 함수 수")

    def produce(self, context: ReportContext, args: argparse.Namespace) -> int:
        repositoryRoot = context.repositoryRoot
        listRoot = [repositoryRoot / relative for relative in (args.path or kDefaultRoots)]
        listPath = collectSourceFiles(listRoot, set(kCppAllExtensions), excludeSubdirs=kNotOurDirNames)
        listLong: list[FunctionSize] = []
        listUnbalanced: list[str] = []
        for filePath in listPath:
            relative = filePath.relative_to(repositoryRoot).as_posix()
            if args.filter and args.filter not in relative:
                continue
            measure = measureText(filePath.read_text(encoding="utf-8", errors="replace"), relative)
            if not measure.bBalanced:
                listUnbalanced.append(relative)
            listLong += [function for function in measure.listFunction if function.bodyLines > args.min_lines]
        listLong.sort(key=lambda function: (-function.bodyLines, function.path, function.line))

        listOutput = [f"[RunFunctionSize] 본문이 {args.min_lines} 줄을 넘는 함수 {len(listLong)}개 (파일 {len(listPath)}개를 훑음)"]
        if args.group_depth > 0:
            mapCount = Counter(areaOf(function.path, args.group_depth) for function in listLong)
            for area, count in sorted(mapCount.items(), key=lambda item: (-item[1], item[0])):
                listOutput.append(f"  {count:4d}  {area}")
                listTop = [function for function in listLong if areaOf(function.path, args.group_depth) == area][:args.top]
                listOutput += [f"          {function.bodyLines:5d}  {function.path}:{function.line} {function.name}" for function in listTop]
        else:
            listOutput.append("   lines  depth  return  lambda  file:line function")
            listOutput += [f"  {function.bodyLines:6d}  {function.maxDepth:5d}  {function.returnCount:6d}  {function.lambdaLines:6d}  "
                           f"{function.path}:{function.line} {function.name}" for function in listLong]
        if listUnbalanced:
            listOutput.append(f"[RunFunctionSize] 중괄호 짝이 맞지 않아 값을 믿을 수 없는 파일 {len(listUnbalanced)}개(`#if` 갈래마다 여는 중괄호 등)")
            listOutput += [f"        {relative}" for relative in listUnbalanced]
        print("\n".join(listOutput))
        if args.out:
            args.out.write_text("\n".join(listOutput) + "\n", encoding="utf-8")
        return 0


main = RunFunctionSizeReport.run


if __name__ == "__main__":
    sys.exit(main())
