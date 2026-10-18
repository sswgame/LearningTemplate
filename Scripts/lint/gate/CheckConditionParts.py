#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckConditionParts.py

`if` · `else if` · `while` 의 조건이 `&&` · `||` 로 세 부분 이상이면 먼저 이름 붙인 지역 변수에 담는다(AGENTS.md '분기문 규칙').

  나쁨:  if ( bArmed && bHasAmmo && bNotReloading )
  좋음:  const bool bCanFire = bArmed && bHasAmmo && bNotReloading;
         if ( bCanFire )

기준선은 0 이다(기존 위반을 모두 고쳤다, `rules/CheckConditionParts.toml` 의 `[baseline] total`). 커밋 훅은 추가한 줄이 속한 조건만,
전체 훑기는 기준선을 넘을 때만 실패한다 — 두 모드 · 병합 커밋 · `--whole-files` · `--list` 는 기반(`LintGate.BaselineSourceGate`)이 정한다.

판정(보수적으로 — 놓치는 쪽으로 틀린다):
  - 주석 · 문자열 · 문자 리터럴은 가린 글에서 본다. 조건은 `if` / `while` 뒤 `(` 부터 짝 `)` 까지(여러 줄 가능), 전처리기 줄(`#if`)은 뺀다.
  - 괄호 `( ) [ ] { }` 안의 `&&` · `||` 는 세지 않는다 — 괄호 안 부분식은 한 부분이다.
  - 꺾쇠(템플릿 인자)는 따라가지 않는다. 대신 `&&` 는 **앞이 피연산자의 끝(식별자 · 숫자 · `)` · `]` · 리터럴)이고 뒤가 `> , ) ;` 가 아닐 때만** 연산자로 본다 —
    `static_cast<T&&>( x )` · `Foo<T&&, U>` 의 오른값 참조를 건너뛰기 위해서다. `||` 는 늘 연산자다.
  - 부분 수 = 최상위 연산자 수 + 1. 3 개 이상이 위반.

  python Scripts/lint/gate/CheckConditionParts.py [--root <repo>] [--files a.cpp b.h] [--whole-files] [--list]
"""

from __future__ import annotations

import re
import sys
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common import blankComments, blankCommentsAndLiterals, kLintTargetRelDirs  # noqa: E402
from LintGate import BaselineSourceGate, CountedFinding  # noqa: E402
from SourceScan import SourceFile  # noqa: E402

_kSuffixes = (".h", ".hpp", ".inl", ".c", ".cc", ".cpp", ".cxx")
_kConditionHeadRe = re.compile(r"\b(if|while)\b\s*(?:constexpr\b\s*)?\(")
_kMinPartCount = 3
#: 앞쪽 피연산자가 끝나는 글자 — 이 뒤의 `&&` 만 논리 연산자다(오른값 참조 `T&&` 는 앞이 타입 이름이라 식별자 끝으로 보이지만 뒤가 `> , )` 로 막힌다).
_kOperandEnd = set("_)]\"'")


@dataclass
class LongCondition:
    """부분이 많은 조건 하나."""

    firstLine: int      #: `if` / `while` 의 줄(1 부터)
    lastLine: int       #: 닫는 `)` 의 줄
    keyword: str
    partCount: int


def _isOperandEndInternal(char: str) -> bool:
    return char.isalnum() or char in _kOperandEnd


def countConditionParts(masked: str, noComment: str, open_: int, close: int) -> int:
    """`masked[open_]` 가 `(`, `masked[close]` 가 짝 `)` 일 때 조건의 부분 수(최상위 `&&` · `||` 수 + 1)."""
    depth = 0
    operatorCount = 0
    index = open_ + 1
    while index < close:
        char = masked[index]
        if char in "([{":
            depth += 1
        elif char in ")]}":
            depth -= 1
        elif depth == 0 and (masked.startswith("&&", index) or masked.startswith("||", index)):
            bOperator = True
            if char == "&":
                before = index - 1
                while before > open_ and noComment[before].isspace():
                    before -= 1
                after = index + 2
                while after < close and noComment[after].isspace():
                    after += 1
                bOperator = _isOperandEndInternal(noComment[before]) and not (after < close and noComment[after] in ">,);")
                bOperator = bOperator and after < close
            if bOperator:
                operatorCount += 1
            index += 2
            continue
        index += 1
    return operatorCount + 1


def findLongConditions(text: str, masked: str | None = None, noComment: str | None = None) -> list[LongCondition]:
    """파일 하나에서 부분이 세 개 이상인 `if` · `while` 조건을 찾습니다. `masked` · `noComment` 는 이미 가린 글(함께 도는 게이트와 나눠 쓴다)."""
    masked = blankCommentsAndLiterals(text) if masked is None else masked
    noComment = blankComments(text) if noComment is None else noComment
    if len(masked) != len(text) or len(noComment) != len(text):
        return []
    listFound: list[LongCondition] = []
    for match in _kConditionHeadRe.finditer(masked):
        lineBegin = masked.rfind("\n", 0, match.start()) + 1
        if masked[lineBegin:match.start()].lstrip().startswith("#"):
            continue       # `#if ( … )` 는 전처리기 식이다
        open_ = match.end() - 1
        depth = 0
        close = -1
        for index in range(open_, len(masked)):
            char = masked[index]
            if char == "(":
                depth += 1
            elif char == ")":
                depth -= 1
                if depth == 0:
                    close = index
                    break
        if close < 0:
            continue
        partCount = countConditionParts(masked, noComment, open_, close)
        if partCount >= _kMinPartCount:
            listFound.append(LongCondition(text.count("\n", 0, match.start()) + 1, text.count("\n", 0, close) + 1, match.group(1), partCount))
    return listFound


class CheckConditionPartsGate(BaselineSourceGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다."""

    description = "if · while 조건이 && · || 로 세 부분 이상이면 이름 붙인 지역 변수로(훅은 추가한 줄만, 전체는 기준선 초과만)"
    buildComment = "Checking that if / while conditions with three or more parts are named first..."
    timeoutSeconds = 120
    preCommitPattern = tuple(f"{root}/*" for root in kLintTargetRelDirs)
    sourceScanRoots = kLintTargetRelDirs
    sourceSuffixes = _kSuffixes
    violationHeader = "조건 부분이 3 개 이상인 if / while"
    countedNoun = "long conditions"
    baselineExceededHint = "새 조건을 이름 붙인다"
    hint = ("  const bool bCanFire = bArmed && bHasAmmo && bNotReloading;\n  if ( bCanFire )\n  {\n      fire();\n  }\n"
            "  괄호 안 부분식은 한 부분이다 — 묶어서 `bool` 하나로 이름 붙여도 된다. 전체를 보려면 --whole-files 또는 --list.")
    selfTestCases = [
        {"name": "한 줄 세 부분", "expectedViolation": "1  Source/Probe/A.cpp", "files": {"Source/Probe/A.cpp": "void f()\n{\n    if ( bArmed && bHasAmmo && bNotReloading )\n    {\n        fire();\n    }\n}\n"}, "args": ["--baseline", "0"]},
        {"name": "여러 줄 + else if || 섞임", "expectedViolation": "1  Source/Probe/B.cpp", "files": {"Source/Probe/B.cpp": "void f()\n{\n    if ( a )\n    {\n    }\n    else if ( bX\n              || bY\n              && bZ )\n    {\n    }\n}\n"}, "args": ["--baseline", "0"]},
        {"name": "while 세 부분", "expectedViolation": "1  Test/Probe/C.cpp", "files": {"Test/Probe/C.cpp": "void f()\n{\n    while ( a < 1 && b || !c )\n    {\n    }\n}\n"}, "args": ["--baseline", "0"]},
        {"name": "--files + --whole-files", "expectedViolation": "Source/Probe/D.cpp:3: if 조건이 3 부분", "files": {"Source/Probe/D.cpp": "void f()\n{\n    if ( a && b && c )\n    {\n    }\n}\n"},
         "args": ["--files", "Source/Probe/D.cpp", "--whole-files"]},
    ]

    def findCounted(self, source: SourceFile) -> list[CountedFinding] | None:
        listLine = source.text.split("\n")
        return [CountedFinding("total", source.relative, condition.firstLine, condition.lastLine,
                               f"{condition.keyword} 조건이 {condition.partCount} 부분 — 먼저 이름 붙인 `const bool` 로", listLine[condition.firstLine - 1].strip())
                for condition in findLongConditions(source.text, source.masked, source.noComment)]


main = CheckConditionPartsGate.run

if __name__ == "__main__":
    sys.exit(main())
