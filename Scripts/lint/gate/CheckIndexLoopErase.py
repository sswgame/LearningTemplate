#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckIndexLoopErase.py

반복문 안에서 `list.erase( list.begin() + index )` 로 원소를 하나씩 지우고 반복을 이어 가는 모양을 막는다.

  나쁨:  for ( size_t index = 0; index < list.size(); ) { if ( done ) list.erase( list.begin() + index ); else ++index; }
  좋음:  VectorUtil::removeIf( list, []( const Item& item ) { return item._bDone; } );     // 순서를 지킨다
         VectorUtil::removeAtSwap( list, index );                                          // 순서가 상관없으면

한 프레임에 T 개가 한꺼번에 끝나면 지울 때마다 뒤 원소를 모두 옮겨 O(T²) 이동이 난다(원소가 `string` 을 들면 이동도 싸지 않다).
새 코드가 이 모양을 못 쓰게 하는 구조 정리이고 숫자 근거는 필요 없다 — 이동 횟수가 입력 크기의 제곱으로 느는 모양이다.
같은 문제를 형식으로 막지 못하는 이유: `erase( begin() + index )` 는 `std::vector` 의 표준 멤버 호출이라 래퍼 타입을 두어도 우회할 수 있다.
그래서 `VectorUtil::removeIf` 가 있다는 것을 알리는 이 게이트가 막지 못하는 틈을 맡는다.

판정(보수적으로 — 놓치는 쪽으로 틀린다):
  - 주석 · 문자열은 가린 글에서 본다. `for` 의 본문 안에서 인자가 하나이고 `begin() +` 나 `std::next( ….begin()` 을 든 `erase( … )` 호출이 대상이다.
  - 그 `erase( … );` 뒤, 같은 블록 바로 아래에 `break` · `return` · `goto` 가 있으면 하나만 지우고 반복을 끝내는 것이라 세지 않는다(O(n) 한 번이다).
  - 인자가 둘인 `erase( first, last )` 는 구간 지우기(`removeIf` 가 하는 일)라 세지 않는다.
기준선은 `rules/CheckIndexLoopErase.toml` 의 `[baseline] total` — 지금 있는 건수다(고치며 낮춘다). 늘면 실패하고, 커밋 훅은 추가한 줄만 본다.

  python Scripts/lint/gate/CheckIndexLoopErase.py [--root <repo>] [--files a.cpp b.h] [--whole-files] [--list]
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from lint.LintGate import BaselineSourceGate, CountedFinding  # noqa: E402
from lint.SourceScan import SourceFile  # noqa: E402

_kScanRoot = ("Source",)
_kSuffixes = (".h", ".hpp", ".inl", ".c", ".cc", ".cpp", ".cxx", ".tpl")

_kLoopHeadRe = re.compile(r"(?<![\w$])for\s*\(")
_kEraseRe = re.compile(r"(?<![\w$])erase\s*\(")
_kBeginPlusRe = re.compile(r"\bbegin\s*\(\s*\)\s*\+|\bstd::next\s*\([^;]*?\bbegin\s*\(\s*\)")
_kExitWordRe = re.compile(r"(?:break|return|goto)(?![\w$])")
_kOpenToClose = {"(": ")", "{": "}", "[": "]"}


def findClosing(masked: str, openIndex: int) -> int:
    """`masked[openIndex]` 가 `(` · `{` · `[` 일 때 짝의 위치. 짝이 없으면 -1."""
    openChar = masked[openIndex]
    closeChar = _kOpenToClose[openChar]
    depth = 0
    for index in range(openIndex, len(masked)):
        char = masked[index]
        if char == openChar:
            depth += 1
        elif char == closeChar:
            depth -= 1
            if depth == 0:
                return index
    return -1


def hasTopLevelComma(masked: str, begin: int, end: int) -> bool:
    """`masked[begin:end]` 의 괄호 · 꺾쇠 밖에 쉼표가 있는지(인자가 둘 이상인지). 꺾쇠는 따라가지 않고 `( ) [ ] { }` 만 센다."""
    depth = 0
    for index in range(begin, end):
        char = masked[index]
        if char in "([{":
            depth += 1
        elif char in ")]}":
            depth -= 1
        elif char == "," and depth == 0:
            return True
    return False


def findLoopBodies(masked: str) -> list[tuple[int, int]]:
    """`for` 마다 본문의 [시작, 끝) 위치. 중괄호가 없으면 다음 `;` 까지의 한 문장이다."""
    listBody: list[tuple[int, int]] = []
    for match in _kLoopHeadRe.finditer(masked):
        closeParen = findClosing(masked, match.end() - 1)
        if closeParen < 0:
            continue
        bodyBegin = closeParen + 1
        while bodyBegin < len(masked) and masked[bodyBegin].isspace():
            bodyBegin += 1
        if bodyBegin >= len(masked):
            continue
        if masked[bodyBegin] == "{":
            closeBrace = findClosing(masked, bodyBegin)
            if closeBrace > 0:
                listBody.append((bodyBegin, closeBrace))
        elif masked[bodyBegin] == ";":
            continue       # `while ( … );` 빈 본문
        else:
            statementEnd = masked.find(";", bodyBegin)
            if statementEnd > 0:
                listBody.append((bodyBegin, statementEnd))
    return listBody


def findIndexLoopErases(masked: str) -> list[tuple[int, int]]:
    """반복문 본문 안에서 `begin() + …` 로 하나를 지우고 반복을 이어 가는 `erase( … )` 의 [호출 시작, 호출 끝) 위치(중복 없이 앞에서부터)."""
    setFound: set[tuple[int, int]] = set()
    for bodyBegin, bodyEnd in findLoopBodies(masked):
        for match in _kEraseRe.finditer(masked, bodyBegin, bodyEnd):
            openParen = match.end() - 1
            closeParen = findClosing(masked, openParen)
            if closeParen < 0 or closeParen >= bodyEnd:
                continue
            if _kBeginPlusRe.search(masked, openParen, closeParen) is None or hasTopLevelComma(masked, openParen + 1, closeParen):
                continue
            statementEnd = masked.find(";", closeParen)
            if statementEnd > 0 and leavesBlockAfter(masked, statementEnd + 1):
                continue       # 하나만 지우고 반복을 끝낸다
            setFound.add((match.start(), closeParen + 1))
    return sorted(setFound)


def leavesBlockAfter(masked: str, begin: int) -> bool:
    """`begin` 부터 같은 블록이 끝날 때까지, 블록 바로 아래(중괄호 안쪽 제외)에 `break` · `return` · `goto` 가 있으면 true — 지운 뒤 반복이 끝난다."""
    depth = 0
    index = begin
    while index < len(masked):
        char = masked[index]
        if char == "{":
            depth += 1
        elif char == "}":
            if depth == 0:
                return False
            depth -= 1
        elif depth == 0 and (char == "b" or char == "r" or char == "g") and (index == 0 or not (masked[index - 1].isalnum() or masked[index - 1] in "_$")):
            if _kExitWordRe.match(masked, index) is not None:
                return True
        index += 1
    return False


class CheckIndexLoopEraseGate(BaselineSourceGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다."""

    description = "반복문 안에서 erase( begin() + index ) 로 하나씩 지우지 않고 VectorUtil::removeIf 를 쓰는지 검사(훅은 추가한 줄만, 전체는 기준선 초과만)"
    buildComment = "Checking that loops erase through VectorUtil::removeIf, not erase( begin() + index )..."
    timeoutSeconds = 60
    preCommitPattern = tuple(f"{root}/*" for root in _kScanRoot)
    sourceScanRoots = _kScanRoot
    sourceSuffixes = _kSuffixes
    violationHeader = "반복문 안 erase( begin() + index )"
    countedNoun = "index loop erases"
    baselineExceededHint = "VectorUtil::removeIf( list, predicate ) 나 순서가 상관없으면 VectorUtil::removeAtSwap( list, index ) 로"
    hint = (
        "  지우기는 Source/Core/Container/VectorUtil.h 의 도우미입니다(한 프레임에 T 개가 끝나면 하나씩 지우는 모양은 O(T²) 이동입니다):\n"
        "      #include \"Core/Container/VectorUtil.h\"      VectorUtil::removeIf( _listEntry, []( const Entry& e ) { return e._bDead; } );   // 순서를 지킨다\n"
        "      VectorUtil::removeAtSwap( _listEntry, index );                                                                     // 순서가 상관없다\n"
        "  지우기 전에 해야 하는 일(완료 콜백 등)이 있으면 먼저 범위 for 로 한 번 돌아 처리하고, 지우는 것은 그다음 removeIf 한 번으로 합니다.\n"
        "  하나만 지우고 끝내는 `erase( begin() + index ); break;` 는 세지 않습니다. 전체를 보려면 --whole-files 또는 --list."
    )
    selfTestCases = [
        {
            "name": "인덱스 for 안에서 하나씩 지우고 이어 간다",
            "expectedViolation": "1  Source/Probe/A.cpp",
            "files": {
                "Source/Probe/A.cpp": (
                    "void f( std::vector<int>& list )\n"
                    "{\n"
                    "    for ( size_t index = 0; index < list.size(); )\n"
                    "    {\n"
                    "        if ( list[index] == 0 )\n"
                    "            list.erase( list.begin() + static_cast<ptrdiff_t>( index ) );\n"
                    "        else\n"
                    "            ++index;\n"
                    "    }\n"
                    "}\n"
                ),
            },
            "args": ["--baseline", "0"],
        },
        {
            "name": "거꾸로 도는 for 와 continue",
            "expectedViolation": "1  Source/Probe/B.cpp",
            "files": {
                "Source/Probe/B.cpp": (
                    "void f( std::vector<int>& list )\n"
                    "{\n"
                    "    for ( size_t index = list.size(); index > 0; --index )\n"
                    "    {\n"
                    "        if ( list[index - 1] != 0 )\n"
                    "            continue;\n"
                    "        list.erase( list.begin() + static_cast<ptrdiff_t>( index - 1 ) );\n"
                    "    }\n"
                    "}\n"
                ),
            },
            "args": ["--baseline", "0"],
        },
        {
            "name": "std::next 로 지운다",
            "expectedViolation": "1  Source/Probe/C.cpp",
            "files": {
                "Source/Probe/C.cpp": (
                    "void f( std::vector<int>& list )\n"
                    "{\n"
                    "    for ( size_t index = 0; index < list.size(); )\n"
                    "        list.erase( std::next( list.begin(), index ) );\n"
                    "}\n"
                ),
            },
            "args": ["--baseline", "0"],
        },
        {
            "name": "하나만 지우고 반복을 끝내면 세지 않는다(break · return)",
            "expectedViolation": "Source/Probe/Bad.cpp:4:",
            "unexpectedViolation": "Source/Probe/Ok",
            "files": {
                "Source/Probe/Bad.cpp": (
                    "void f( std::vector<int>& list )\n"
                    "{\n"
                    "    for ( size_t index = 0; index < list.size(); ++index )\n"
                    "        list.erase( list.begin() + static_cast<ptrdiff_t>( index ) );\n"
                    "}\n"
                ),
                "Source/Probe/OkReturn.cpp": (
                    "bool f( std::vector<int>& list, int value )\n"
                    "{\n"
                    "    for ( size_t index = 0; index < list.size(); ++index )\n"
                    "    {\n"
                    "        if ( list[index] != value )\n"
                    "            continue;\n"
                    "        list.erase( list.begin() + static_cast<ptrdiff_t>( index ) );\n"
                    "        ++g_version;\n"
                    "        return true;\n"
                    "    }\n"
                    "    return false;\n"
                    "}\n"
                ),
                "Source/Probe/OkBreak.cpp": (
                    "void f( std::vector<int>& list, int value )\n"
                    "{\n"
                    "    for ( size_t index = 0; index < list.size(); ++index )\n"
                    "    {\n"
                    "        if ( list[index] == value )\n"
                    "        {\n"
                    "            list.erase( list.begin() + static_cast<ptrdiff_t>( index ) );\n"
                    "            break;\n"
                    "        }\n"
                    "    }\n"
                    "}\n"
                ),
                "Source/Probe/OkRange.cpp": (
                    "void f( std::vector<int>& list )\n"
                    "{\n"
                    "    for ( int pass = 0; pass < 2; ++pass )\n"
                    "        list.erase( list.begin() + 1, list.end() );\n"
                    "}\n"
                ),
            },
            "args": ["--files", "Source/Probe/Bad.cpp", "Source/Probe/OkReturn.cpp", "Source/Probe/OkBreak.cpp", "Source/Probe/OkRange.cpp", "--whole-files"],
        },
        {
            "name": "--files + --whole-files",
            "expectedViolation": "Source/Probe/D.cpp:5: 반복문 안에서 erase( begin() + index )",
            "files": {
                "Source/Probe/D.cpp": (
                    "void f( std::vector<int>& list )\n"
                    "{\n"
                    "    for ( size_t index = 0; index < list.size(); ++index )\n"
                    "    {\n"
                    "        list.erase( list.begin() + index );\n"
                    "    }\n"
                    "}\n"
                ),
            },
            "args": ["--files", "Source/Probe/D.cpp", "--whole-files"],
        },
    ]

    def findCounted(self, source: SourceFile) -> list[CountedFinding] | None:
        masked = source.masked
        listFound = findIndexLoopErases(masked) if "erase" in masked else []
        listLine = source.listLine
        listResult: list[CountedFinding] = []
        for begin, end in listFound:
            firstLine = masked.count("\n", 0, begin) + 1
            lastLine = masked.count("\n", 0, end) + 1
            listResult.append(CountedFinding("total", source.relative, firstLine, lastLine, "반복문 안에서 erase( begin() + index ) — removeIf · removeAtSwap 로",
                                             listLine[firstLine - 1].strip()))
        return listResult


main = CheckIndexLoopEraseGate.run

if __name__ == "__main__":
    sys.exit(main())
