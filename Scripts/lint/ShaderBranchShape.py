#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/ShaderBranchShape.py

`if ( a && b ) { … } else if ( a ) { … }` 처럼 앞 갈래의 `&&` 조건과 같은 항을 뒤 `else if` 가 다시 묻는 사슬을 막는다.
NVIDIA GL(ARB_gl_spirv)은 DXC 가 낸 이 모양의 SPIR-V 를 잘못 컴파일해 a 가 거짓인 점도 a 쪽 갈래를 탔다 — 캐스케이드 상자 밖 바닥이
어두웠고 같은 SPIR-V 의 Vulkan 은 맞았다(커밋 46c0d6ad17). 고치는 모양은 겹친 분기다: `if ( a ) { if ( b ) { … } else { … } }`.
**왜 형식이 아니라 린트인가.** HLSL 에는 분기 모양을 형식으로 막을 자리가 없고, 문제는 드라이버가 SPIR-V 구조를 받는 방식이다.

- 사슬: `if ( … )` 뒤에 `else if ( … )` 가 이어진 갈래들. 조건은 맨 바깥 `&&` 로 나눈 항(공백 · 바깥 괄호를 뺀 글)으로 본다.
- 위반: 뒤 갈래의 항 하나가 앞 갈래 중 항이 둘 이상인 조건의 항과 같다. 전처리 줄은 지운다(보수적).

`py -3 -m Scripts lint shader-branch-shape [--self-check]` 로 돌리고, 커밋 훅(`Rules.py` 표)이 돌린다.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import Iterable, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.ExitCode import ExitCode  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402
from lint.LintScope import LintScope, makeWholeScope, readTexts  # noqa: E402
from lint.ShaderKillOrder import kCommentPattern, kExtensions, kPreprocessorPattern  # noqa: E402

kIfPattern = re.compile(r"\bif\s*\(")
kElsePattern = re.compile(r"\s*else\b")
kElseIfPattern = re.compile(r"\s*else\s+(?:SW_\w+\s+)?if\s*\(")


def findClosing(code: str, openIndex: int, opener: str, closer: str) -> int:
    depth = 0
    for index in range(openIndex, len(code)):
        if code[index] == opener:
            depth += 1
        elif code[index] == closer:
            depth -= 1
            if depth == 0:
                return index
    return len(code)


def skipStatement(code: str, index: int) -> int:
    """@p index 에서 시작하는 문장(블록 · 한 줄 · 겹친 if 사슬)의 끝 다음 자리를 돌려준다."""
    while index < len(code) and code[index].isspace():
        index += 1
    if index < len(code) and code[index] == "{":
        return findClosing(code, index, "{", "}") + 1
    match = re.compile(r"(?:SW_\w+\s+)?if\s*\(").match(code, index)
    if match:
        index = skipStatement(code, findClosing(code, match.end() - 1, "(", ")") + 1)
        if kElsePattern.match(code, index):
            return skipStatement(code, kElsePattern.match(code, index).end())
        return index
    depth = 0
    while index < len(code):
        if code[index] in "([":
            depth += 1
        elif code[index] in ")]":
            depth -= 1
        elif code[index] == ";" and depth == 0:
            return index + 1
        index += 1
    return index


def stripOuter(term: str) -> str:
    term = re.sub(r"\s+", "", term)
    while term.startswith("(") and findClosing(term, 0, "(", ")") == len(term) - 1:
        term = term[1:-1]
    return term


def splitConjuncts(condition: str) -> list[str]:
    condition = stripOuter(condition)
    listTerm: list[str] = []
    depth = 0
    start = 0
    for index, char in enumerate(condition):
        if char in "([":
            depth += 1
        elif char in ")]":
            depth -= 1
        elif depth == 0 and condition.startswith("&&", index):
            listTerm.append(condition[start:index])
            start = index + 2
    listTerm.append(condition[start:])
    if any("||" in term and stripOuter(term) == term for term in listTerm):
        return [condition]  # 맨 바깥에 || 가 있으면 && 항으로 나누지 않는다
    return [stripOuter(term) for term in listTerm]


def findProblemsInText(path: str, text: str) -> list[str]:
    code = kPreprocessorPattern.sub("", kCommentPattern.sub("", text))
    listProblem: list[str] = []
    setElseIfStart: set[int] = set()
    for match in kIfPattern.finditer(code):
        if match.start() in setElseIfStart:
            continue
        listCondition: list[list[str]] = []
        openIndex = match.end() - 1
        while True:
            closeIndex = findClosing(code, openIndex, "(", ")")
            listTerm = splitConjuncts(code[openIndex + 1:closeIndex])
            shared = next((term for earlier in listCondition if len(earlier) > 1 for term in listTerm if term in earlier), None)
            if shared is not None:
                line = code.count("\n", 0, openIndex) + 1
                listProblem.append(f"{path}:{line}: else if 가 앞 갈래의 && 항 `{shared}` 를 다시 묻는다 — NVIDIA GL(ARB_gl_spirv)이 이 모양을 잘못 컴파일한다(46c0d6ad17). "
                                   f"`if ( {shared} ) {{ if ( 나머지 ) … else … }}` 로 겹친다")
            listCondition.append(listTerm)
            nextMatch = kElseIfPattern.match(code, skipStatement(code, closeIndex + 1))
            if not nextMatch:
                break
            ifStart = kIfPattern.search(code, nextMatch.start())
            setElseIfStart.add(ifStart.start())
            openIndex = nextMatch.end() - 1
    return listProblem


def findProblems(listSource: Iterable[tuple[str, str]]) -> list[str]:
    listProblem: list[str] = []
    for path, text in listSource:
        listProblem.extend(findProblemsInText(path, text))
    return listProblem


def checkShaderBranchShape(projectRoot: Path, scope: LintScope) -> list[str]:
    return findProblems(readTexts(projectRoot, scope.select(("Content", "Source"), kExtensions, ("ThirdParty",))))


def runSelfCheck() -> None:
    bad = "float f( bool a, bool b )\n{\n    float x = 0;\n    if ( a && b )\n    {\n        x = 1;\n    }\n    else if ( a )\n        x = 2;\n    return x;\n}\n"
    badBranch = "void f() { SW_BRANCH if ( ( inside ) && usesSoft() ) { x = 1; } else if ( c ) { } else SW_BRANCH if ( inside ) { x = 2; } }"
    badNested = "void f() { if ( a && b ) { if ( c ) { x = 1; } else { x = 3; } } else if ( b && d ) { x = 2; } }"
    good = "void f() { SW_BRANCH if ( a ) { SW_BRANCH if ( b ) { x = 1; } else { x = 2; } } }"
    goodOr = "void f() { if ( a || b && c ) { x = 1; } else if ( a ) { x = 2; } }"
    goodSeparate = "void f() { if ( a && b ) { x = 1; } if ( a ) { x = 2; } }"
    goodSingle = "void f() { if ( a ) { x = 1; } else if ( a && b ) { x = 2; } }"
    assert len(findProblems([("a.hlsl", bad)])) == 1, findProblems([("a.hlsl", bad)])
    assert ":8:" in findProblems([("a.hlsl", bad)])[0]
    assert len(findProblems([("a.hlsl", badBranch)])) == 1, findProblems([("a.hlsl", badBranch)])
    assert len(findProblems([("a.hlsl", badNested)])) == 1, findProblems([("a.hlsl", badNested)])
    for text in (good, goodOr, goodSeparate, goodSingle, "// if ( a && b ) {} else if ( a ) {}\n"):
        assert findProblems([("a.hlsl", text)]) == [], (text, findProblems([("a.hlsl", text)]))


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="`if ( a && b ) … else if ( a )` 분기 사슬을 본다")
    parser.add_argument("--self-check", action="store_true")
    arguments = parser.parse_args(argv)
    if arguments.self_check:
        runSelfCheck()
        print("[ShaderBranchShape] self-check OK")
        return ExitCode.Passed
    listProblem = checkShaderBranchShape(getProjectRoot(), makeWholeScope(getProjectRoot()))
    for problem in listProblem:
        print(f"  - {problem}")
    print(f"[ShaderBranchShape] {'FAIL' if listProblem else 'OK'} ({len(listProblem)}건)")
    return ExitCode.Violation if listProblem else ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
