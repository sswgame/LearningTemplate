#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/OptionalMinusOne.py

`optional<정수>` · `expected<정수>` 자리에 리터럴 `-1` 을 **값** 으로 넣는 곳을 찾는다 — 반환 · 대입 · 초기화 · 기본 인자 · 인자.

**왜 린트인가.** 표지 상수(`constant::invalid_index::k*`)는 따로 된 형식이라 `Expected.h` 가 그 변환을 지워 컴파일에서 막는다. 리터럴 `-1` 은 정수라
`optional<int32>` 의 값으로 합법이다 — 형식이 "없음" 을 뜻한 -1 과 값 -1 을 가르지 못한다(`SkillTreeState::findIndex` 가 -1 을 값으로 돌려줘 `_list[-1]`).
없음은 `nullopt`(optional) · `unexpected(...)`(expected) 다. 값 -1 이 정말 맞는 자리(부호 있는 축의 값)는 이름 단 상수로 적는다.

찾는 범위(글 연산의 한계): 선언은 같은 파일과 짝 파일(같은 이름의 `.h` · `.cpp`)에서 본다 — 파일마다의 선언 · 후보 자리는 내용 해시 색인(`FactCache`)에 둔다. 다른 파일에 선언된 함수의 인자 · 칸은 보지 못한다.
같은 이름이 짝 안에서 다른 형식으로도 선언됐으면 어느 것인지 몰라 넘긴다.

`py -3 -m Scripts lint optional-minus-one [--self-check]`, 커밋 훅(`Rules.py` 표)이 돌린다.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.ExitCode import ExitCode  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402
from lint.FactCache import loadFacts  # noqa: E402
from lint.LintScope import LintScope, listRepoFiles, makeWholeScope  # noqa: E402
from lint.CppText import blankCommentsAndStrings  # noqa: E402

kInt = r"(?:const\s+)?(?:sw::)?(?:u?int(?:8|16|32|64)|size_t|usize|isize|int)"
kOptionalType = r"(?:sw::)?(?:optional|expected)\s*<\s*" + kInt + r"\s*(?:,[^<>;]*(?:<[^<>;]*>)?[^<>;]*)?>"
kMinusOne = r"(?:-\s*1(?![\w.])|static_cast\s*<[^>]*>\s*\(\s*-\s*1\s*\))"
kAnyMinusOne = re.compile(kMinusOne)
kInitOrDefault = re.compile(kOptionalType + r"\s*&?\s*\w+\s*(?:=|\{|\()\s*" + kMinusOne)
kOptionalName = re.compile(kOptionalType + r"\s*&?\s*(\w+)\s*(?=[;={(,)\[]|$)", re.M)
kAssign = re.compile(r"(?<![\w>:])(?:[\w\]\)]+\s*(?:\.|->)\s*)?(\w+)\s*(?:=|\{)\s*" + kMinusOne)
kFunctionHead = re.compile(r"(?:^|[\s;{}])" + kOptionalType + r"\s+(?:[\w:~]+::)?(\w+)\s*\(", re.M)
kReturnMinusOne = re.compile(r"\breturn\s+" + kMinusOne + r"\s*;")
kLambdaHead = re.compile(r"\]\s*(?:\([^()]*(?:\([^()]*\)[^()]*)*\))?\s*(?:mutable\s*)?(?:->\s*[^{;]+)?\{")
kSignature = re.compile(r"\b(\w+)\s*\(([^;{}()]*(?:optional|expected)\s*<[^;{}()]*)\)\s*(?:const\s*)?(?:override\s*)?[;{=]", re.S)
kOtherType = r"(?:" + kInt + r"|float32|float64|bool|auto|dbus_int32_t|long|short)"


def findClose(code: str, openIndex: int, openChar: str, closeChar: str) -> int:
    """`code[openIndex]` 가 여는 괄호일 때 짝 닫는 괄호의 위치(없으면 글 끝)."""
    depth = 0
    for cursor in range(openIndex, len(code)):
        if code[cursor] == openChar:
            depth += 1
        elif code[cursor] == closeChar:
            depth -= 1
            if depth == 0:
                return cursor
    return len(code)


def splitTopLevel(text: str) -> list[str]:
    """최상위 쉼표로 가른다(`()` · `[]` · `{}` · `<>` 안의 쉼표는 두고)."""
    listPart: list[str] = []
    depth = 0
    begin = 0
    for cursor, char in enumerate(text):
        if char in "([{<":
            depth += 1
        elif char in ")]}>":
            depth = max(0, depth - 1)
        elif char == "," and depth == 0:
            listPart.append(text[begin:cursor])
            begin = cursor + 1
    listPart.append(text[begin:])
    return [part.strip() for part in listPart if part.strip()]


def getLineOf(code: str, index: int) -> int:
    return code.count("\n", 0, index) + 1


def blankLambdaBodies(body: str) -> str:
    """함수 본문 안 람다 본문을 빈칸으로 — 람다의 `return -1` 은 람다의 반환 형식이다."""
    chars = list(body)
    for match in kLambdaHead.finditer(body):
        start = match.end() - 1
        end = findClose(body, start, "{", "}")
        for cursor in range(start + 1, min(end, len(chars))):
            if chars[cursor] != "\n":
                chars[cursor] = " "
    return "".join(chars)


def collectDeclarations(code: str) -> tuple[set[str], dict[str, set[int]]]:
    """(optional 로 선언한 이름, 함수 이름 → optional 매개변수 위치)."""
    setOptional = {match.group(1) for match in kOptionalName.finditer(code)}
    mapParam: dict[str, set[int]] = {}
    for match in kSignature.finditer(code):
        for index, param in enumerate(splitTopLevel(match.group(2))):
            if re.match(r"(?:const\s+)?" + kOptionalType + r"\s*&?\s*\w+", param):
                mapParam.setdefault(match.group(1), set()).add(index)
    return setOptional, mapParam


kOtherDeclaration = re.compile(r"(?=\b" + kOtherType + r"\s*&?\s*(\w+)\s*[;={(,)\[])")
kCallHead = re.compile(r"\b(\w+)\s*\(")


def extractFacts(text: str) -> dict:
    """
    파일 하나의 사실 — 짝(같은 이름의 .h · .cpp · .inl)의 판정은 이 사실의 합으로 낸다(`judgeStem`). 스테이지 파일 하나에 짝 파일을 다시 읽지 않게
    내용 해시 색인(`FactCache`)에 둔다. 선언(optional 이름 · 매개변수 자리 · 다른 형식으로 선언된 이름)과, 짝의 선언에 따라 위반이 되는 자리 후보를 담는다.
    """
    code = blankCommentsAndStrings(text)
    setOptional, mapParam = collectDeclarations(code)
    listInit: list[int] = []
    listAssign: list[list] = []
    for lineIndex, line in enumerate(code.split("\n")):
        if kAnyMinusOne.search(line) is None:
            continue
        if kInitOrDefault.search(line):
            listInit.append(lineIndex + 1)
        listAssign.extend([lineIndex + 1, match.group(1)] for match in kAssign.finditer(line))
    listReturn: list[list] = []
    if kReturnMinusOne.search(code):
        for match in kFunctionHead.finditer(code):
            closeParen = findClose(code, match.end() - 1, "(", ")")
            after = re.match(r"\s*(?:const\s*)?(?:noexcept\s*)?(?:override\s*)?(?:final\s*)?\{", code[closeParen + 1:closeParen + 200])
            if after is None:
                continue
            bodyStart = closeParen + 1 + after.end()
            bodyEnd = findClose(code, bodyStart - 1, "{", "}")
            body = blankLambdaBodies(code[bodyStart:bodyEnd])
            listReturn.extend([getLineOf(code, bodyStart + found.start()), match.group(1)] for found in kReturnMinusOne.finditer(body))
    listCall: list[list] = []
    if kAnyMinusOne.search(code):
        for match in kCallHead.finditer(code):
            openIndex = match.end() - 1
            listArgument = splitTopLevel(code[openIndex + 1:findClose(code, openIndex, "(", ")")])
            listIndex = [index for index, argument in enumerate(listArgument) if re.fullmatch(kMinusOne, argument)]
            if listIndex:
                listCall.append([getLineOf(code, match.start()), match.group(1), listIndex])
    return {"minus": kAnyMinusOne.search(text) is not None, "optional": "optional" in text or "expected" in text,
            "declared": sorted(setOptional), "params": {name: sorted(setIndex) for name, setIndex in mapParam.items()},
            "other": sorted({match.group(1) for match in kOtherDeclaration.finditer(code)}),
            "init": listInit, "assign": listAssign, "return": listReturn, "call": listCall}


def judgeStem(mapFact: dict[str, dict]) -> list[str]:
    """짝 하나(경로 → 사실)의 위반 — 선언은 짝 전체에서 모은다."""
    if not any(fact["minus"] for fact in mapFact.values()) or not any(fact["optional"] for fact in mapFact.values()):
        return []   # -1 이 없거나 optional · expected 가 없는 짝은 볼 것이 없다
    setOptional = {name for fact in mapFact.values() for name in fact["declared"]}
    setOther = {name for fact in mapFact.values() for name in fact["other"]}
    mapParam: dict[str, set[int]] = {}
    for fact in mapFact.values():
        for name, listIndex in fact["params"].items():
            mapParam.setdefault(name, set()).update(listIndex)
    listProblem: list[str] = []
    for path, fact in mapFact.items():
        mapLine: dict[int, list[str]] = {}
        for line in fact["init"]:
            mapLine.setdefault(line, []).append(f"{path}:{line}: optional · expected 의 초기값 · 기본 인자가 -1 이다 — 없음은 nullopt · unexpected")
        for line, name in fact["assign"]:
            if name in setOptional and name not in setOther:
                mapLine.setdefault(line, []).append(f"{path}:{line}: optional · expected 칸 '{name}' 에 -1 을 값으로 넣는다 — 없음은 nullopt(reset) · unexpected")
        listProblem += [message for line in sorted(mapLine) for message in mapLine[line]]
        listProblem += [f"{path}:{line}: optional · expected 를 돌려주는 {name} 이 -1 을 값으로 돌려준다 — 없음은 nullopt · unexpected" for line, name in fact["return"]]
        for line, name, listIndex in fact["call"]:
            listProblem += [f"{path}:{line}: {name} 의 optional · expected 인자({index + 1} 번째)에 -1 을 넘긴다 — 없음은 nullopt"
                            for index in sorted(mapParam.get(name, ())) if index in listIndex]
    return listProblem


def judgeFacts(mapFact: dict[str, dict]) -> list[str]:
    """경로 → 사실. 짝(확장자를 뺀 같은 경로)마다 판정하고 한 줄에 한 번만 낸다(초기화가 대입으로도 보인다)."""
    mapStem: dict[str, dict[str, dict]] = {}
    for path, fact in mapFact.items():
        mapStem.setdefault(path.rsplit(".", 1)[0], {})[path] = fact
    mapByLine: dict[str, str] = {}
    for mapPair in mapStem.values():
        for problem in judgeStem(mapPair):
            mapByLine.setdefault(problem.split(": ", 1)[0], problem)
    return sorted(mapByLine.values())


def findProblems(mapSource: dict[str, str]) -> list[str]:
    """경로 → 글 — 자체 검사의 입구."""
    return judgeFacts({path: extractFacts(text) for path, text in mapSource.items()})


def checkOptionalMinusOne(projectRoot: Path, scope: LintScope) -> list[str]:
    """scope 의 파일과 짝(같은 이름의 .h · .cpp · .inl) — 짝 파일의 사실은 색인에서 꺼낸다."""
    kSuffixes = (".cpp", ".h", ".inl")
    setStem = {path.rsplit(".", 1)[0] for path in scope.select(("Source",), kSuffixes)}
    if not setStem:
        return []
    listPath = [path for path in listRepoFiles(projectRoot) if path.startswith("Source/") and path.endswith(kSuffixes)]
    mapFact = loadFacts(projectRoot, "optional-minus-one", [__file__], listPath, lambda _, text: extractFacts(text),
                        want=[path for path in listPath if path.rsplit(".", 1)[0] in setStem], shardOf=lambda path: path.rsplit(".", 1)[0])   # 한 짝은 한 조각
    return judgeFacts(mapFact)


def runSelfCheck() -> None:
    def problems(text: str, header: str = "") -> list[str]:
        mapSource = {"a.cpp": text}
        if header:
            mapSource["a.h"] = header
        return findProblems(mapSource)

    assert len(problems("optional<int32> f() { if ( x ) return 3; return -1; }\n")) == 1, "반환"
    assert len(problems("expected<int32> A::f( int32 a )\n{\n    return -1;\n}\n")) == 1, "expected 반환 · 한정 이름"
    assert problems("optional<int32> f() { auto g = []() { return -1; }; return nullopt; }\n") == [], "람다의 반환은 람다 것이다"
    assert problems("int32 f() { return -1; }\n") == [], "정수 반환은 통과한다"
    assert len(problems("void f( optional<int32> slot = -1 );\n")) == 1, "기본 인자"
    assert len(problems("void g() { h( 1, -1 ); }\n", "void h( int32 a, optional<int32> b );\n")) == 1, "짝 헤더의 매개변수 위치"
    assert problems("void g() { h( -1, nullopt ); }\n", "void h( int32 a, optional<int32> b );\n") == [], "정수 자리의 -1 · nullopt 는 통과한다"
    assert len(problems("void g() { _slot = -1; }\n", "struct S { optional<uint32> _slot{}; };\n")) == 1, "칸 대입"
    assert problems("void g() { _slot = -1; }\n", "struct S { optional<uint32> _slot{}; };\nstruct T { int32 _slot; };\n") == [], "같은 이름이 정수로도 있으면 넘긴다"
    assert problems("optional<int32> f() { return -1.0f; }\n") == [], "실수는 다른 자리다"
    assert problems("// optional<int32> f() { return -1; }\n") == [], "주석은 통과한다"


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="optional · expected 자리에 리터럴 -1 을 값으로 넣는 곳을 찾는다")
    parser.add_argument("--self-check", action="store_true")
    arguments = parser.parse_args(argv)
    if arguments.self_check:
        runSelfCheck()
        print("[OptionalMinusOne] self-check OK")
        return ExitCode.Passed
    listProblem = checkOptionalMinusOne(getProjectRoot(), makeWholeScope(getProjectRoot()))
    for problem in listProblem:
        print(f"  - {problem}")
    print(f"[OptionalMinusOne] {'FAIL' if listProblem else 'OK'} ({len(listProblem)}건)")
    return ExitCode.Violation if listProblem else ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
