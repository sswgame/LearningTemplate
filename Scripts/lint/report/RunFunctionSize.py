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

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.CodeText import blankCommentsAndLiterals  # noqa: E402
from common.Constants import kCppAllExtensions, kNotOurDirNames  # noqa: E402
from common.Search import collectSourceFiles  # noqa: E402
from common.ExitCode import ExitCode  # noqa: E402
from lint.CppScope import blankPreprocessor, classifyHead, isLambdaOpen  # noqa: E402
from lint.LintReport import LintReport, ReportContext  # noqa: E402

kDefaultMinLines = 100
kDefaultRoots: tuple[str, ...] = ("Source",)

_kReturnRe = re.compile(r"\breturn\b")
#: 걷는 자리 — 선언 자리를 끊는 `;` 와 중괄호.
_kTokenRe = re.compile(r"[{};]")


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
                kind = classifyHead(code[headStart:index])
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
                    if isLambdaOpen(code, index):
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
        return ExitCode.Passed


main = RunFunctionSizeReport.run


if __name__ == "__main__":
    sys.exit(main())
