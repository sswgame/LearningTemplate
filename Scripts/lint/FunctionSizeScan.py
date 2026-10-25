# -*- coding: utf-8 -*-
"""
Scripts/lint/FunctionSizeScan.py

C++ 함수마다 본문 줄 수 · 최대 중첩 깊이 · `return` 수 · 람다 줄 수를 재는 틀 모듈입니다. 보고서 `report/RunFunctionSize.py` 와
계획 문서 표 점검 `lint/LongFunctionTable.py` 가 같은 셈을 쓰도록 둔다(재는 방법 설명은 `RunFunctionSize.py` 머리말).
"""

from __future__ import annotations

import bisect
import re
from dataclasses import dataclass
from pathlib import Path


from common.CodeText import blankCommentsAndLiterals
from common.Constants import kCppAllExtensions, kNotOurDirNames
from common.Search import collectSourceFiles
from lint.CppScope import blankPreprocessor, classifyHead, isLambdaOpen

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




def collectLongFunctions(repositoryRoot: Path, minLines: int, listRootRelative: tuple[str, ...] | list[str], filterText: str = "") -> tuple[list[FunctionSize], list[str], int]:
    """저장소의 C++ 파일을 훑어 본문이 @p minLines 줄을 넘는 함수(긴 순)와 짝이 안 맞는 파일, 훑은 파일 수를 돌려줍니다.
    보고와 `LongFunctionTable`(계획 문서 표 점검)이 같은 셈을 쓰도록 한곳에 둔다."""
    listPath = collectSourceFiles([repositoryRoot / relative for relative in listRootRelative], set(kCppAllExtensions), excludeSubdirs=kNotOurDirNames)
    listLong: list[FunctionSize] = []
    listUnbalanced: list[str] = []
    for filePath in listPath:
        relative = filePath.relative_to(repositoryRoot).as_posix()
        if filterText and filterText not in relative:
            continue
        measure = measureText(filePath.read_text(encoding="utf-8", errors="replace"), relative)
        if not measure.bBalanced:
            listUnbalanced.append(relative)
        listLong += [function for function in measure.listFunction if function.bodyLines > minLines]
    listLong.sort(key=lambda function: (-function.bodyLines, function.path, function.line))
    return listLong, listUnbalanced, len(listPath)

