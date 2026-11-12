#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/OptionalMinusOne.py

`optional<정수>` · `expected<정수>` 자리에 리터럴 `-1` 을 **값** 으로 넣는 곳을 찾는다 — 반환 · 대입 · 초기화 · 기본 인자 · 인자.

**왜 린트인가.** 표지 상수(`constant::invalid_index::k*`)는 따로 된 형식이라 `Expected.h` 가 그 변환을 지워 컴파일에서 막는다. 리터럴 `-1` 은 정수라
`optional<int32>` 의 값으로 합법이다 — 형식이 "없음" 을 뜻한 -1 과 값 -1 을 가르지 못한다(`SkillTreeState::findIndex` 가 -1 을 값으로 돌려줘 `_list[-1]`).
없음은 `nullopt`(optional) · `unexpected(...)`(expected) 다. 값 -1 이 정말 맞는 자리(부호 있는 축의 값)는 이름 단 상수로 적는다.

찾는 범위(글 연산의 한계): 선언은 같은 파일과 짝 파일(같은 이름의 `.h` · `.cpp`)에서 본다. 다른 파일에 선언된 함수의 인자 · 칸은 보지 못한다.
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
from lint.LintScope import LintScope, listRepoFiles, makeWholeScope, readTexts  # noqa: E402
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


def isDeclaredOtherwise(name: str, code: str) -> bool:
    """같은 이름이 정수 · 실수 등 다른 형식으로도 선언됐는지."""
    return re.search(r"\b" + kOtherType + r"\s*&?\s*" + re.escape(name) + r"\s*[;={(,)\[]", code) is not None


def findInFile(path: str, code: str, joined: str, setOptional: set[str], mapParam: dict[str, set[int]]) -> list[str]:
    listProblem: list[str] = []
    for lineIndex, line in enumerate(code.split("\n")):
        if kAnyMinusOne.search(line) is None:
            continue
        if kInitOrDefault.search(line):
            listProblem.append(f"{path}:{lineIndex + 1}: optional · expected 의 초기값 · 기본 인자가 -1 이다 — 없음은 nullopt · unexpected")
        for match in kAssign.finditer(line):
            name = match.group(1)
            if name in setOptional and isDeclaredOtherwise(name, joined) is False:
                listProblem.append(f"{path}:{lineIndex + 1}: optional · expected 칸 '{name}' 에 -1 을 값으로 넣는다 — 없음은 nullopt(reset) · unexpected")
    if kReturnMinusOne.search(code):
        for match in kFunctionHead.finditer(code):
            closeParen = findClose(code, match.end() - 1, "(", ")")
            after = re.match(r"\s*(?:const\s*)?(?:noexcept\s*)?(?:override\s*)?(?:final\s*)?\{", code[closeParen + 1:closeParen + 200])
            if after is None:
                continue
            bodyStart = closeParen + 1 + after.end()
            bodyEnd = findClose(code, bodyStart - 1, "{", "}")
            body = blankLambdaBodies(code[bodyStart:bodyEnd])
            for found in kReturnMinusOne.finditer(body):
                listProblem.append(f"{path}:{getLineOf(code, bodyStart + found.start())}: optional · expected 를 돌려주는 {match.group(1)} 이 -1 을 값으로 돌려준다 — 없음은 nullopt · unexpected")
    if mapParam:
        callPattern = re.compile(r"\b(" + "|".join(map(re.escape, mapParam)) + r")\s*\(")
        for match in callPattern.finditer(code):
            openIndex = match.end() - 1
            listArgument = splitTopLevel(code[openIndex + 1:findClose(code, openIndex, "(", ")")])
            for index in mapParam[match.group(1)]:
                if index < len(listArgument) and re.fullmatch(kMinusOne, listArgument[index]):
                    listProblem.append(f"{path}:{getLineOf(code, match.start())}: {match.group(1)} 의 optional · expected 인자({index + 1} 번째)에 -1 을 넘긴다 — 없음은 nullopt")
    return listProblem


def findProblems(mapSource: dict[str, str]) -> list[str]:
    mapStem: dict[str, list[str]] = {}
    for path in mapSource:
        mapStem.setdefault(path.rsplit(".", 1)[0], []).append(path)
    listProblem: list[str] = []
    for listPath in mapStem.values():
        if not any(kAnyMinusOne.search(mapSource[path]) for path in listPath):
            continue  # -1 이 없는 짝은 볼 것이 없다
        if not any("optional" in mapSource[path] or "expected" in mapSource[path] for path in listPath):
            continue
        mapCode = {path: blankCommentsAndStrings(mapSource[path]) for path in listPath}
        joined = "\n".join(mapCode.values())
        setOptional, mapParam = collectDeclarations(joined)
        for path in listPath:
            listProblem += findInFile(path, mapCode[path], joined, setOptional, mapParam)
    mapByLine: dict[str, str] = {}
    for problem in listProblem:
        mapByLine.setdefault(problem.split(": ", 1)[0], problem)  # 한 줄에 한 번(초기화가 대입으로도 보인다)
    return sorted(mapByLine.values())


def readSources(projectRoot: Path, scope: LintScope) -> dict[str, str]:
    """scope 의 파일과 짝(같은 이름의 .h · .cpp · .inl) — 판정은 짝 안에서만 한다(`findProblems`)."""
    kSuffixes = (".cpp", ".h", ".inl")
    setStem = {path.rsplit(".", 1)[0] for path in scope.select(("Source",), kSuffixes)}
    listPath = [path for path in listRepoFiles(projectRoot) if path.startswith("Source/") and path.endswith(kSuffixes) and path.rsplit(".", 1)[0] in setStem]
    return dict(readTexts(projectRoot, listPath))


def checkOptionalMinusOne(projectRoot: Path, scope: LintScope) -> list[str]:
    return findProblems(readSources(projectRoot, scope))


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
