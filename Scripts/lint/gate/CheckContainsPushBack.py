#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckContainsPushBack.py

"이미 있으면 넣지 않기"를 목록 검색으로 하는 모양을 막는다.

  나쁨:  if ( VectorUtil::contains( list, item ) == false ) list.push_back( item );
         if ( std::find( list.begin(), list.end(), item ) == list.end() ) list.push_back( item );
  좋음:  unordered_set<T> set; if ( set.insert( item ).second ) list.push_back( item );   // 순서가 필요하면 집합을 곁에 둔다
         IndexedPtrList<T> list; list.add( pItem );                                        // 포인터 목록이면 이 형식이 O(1)
         VectorUtil::sortUnique( list );                                                   // 모은 뒤 한 번에 거르면 된다

원소가 N 개가 되면 넣을 때마다 목록 전체를 훑어 O(N²) 이다(씬 로드 수천 개, 체크아웃 파일 수천 건에서 실제로 났다).
형식으로 막지 못하는 이유: `contains`/`find` 는 어떤 `vector` 에도 쓸 수 있는 표준 모양이라 "목록에 중복 검사가 붙은 vector" 를 타입으로 구분할 수 없다
(포인터 목록은 `IndexedPtrList` 로 형식이 막는다). 그래서 이 모양이 새로 늘지 않게만 게이트가 맡는다.

판정(보수적으로 — 놓치는 쪽으로 틀린다): 주석 · 문자열을 가린 글에서
  - `VectorUtil::contains|containsIf|containsBy|findIf|findIndex*|findBy( X, …`, `std::find|find_if|any_of|none_of|count*( X.begin(), …`, `std::ranges::find|find_if|any_of|none_of|contains( X, …` 의 첫 인자 글 X 를 읽고,
  - 그 호출 뒤 같은 줄부터 `kWindowLines` 줄 안, 또는 검사가 든 블록이 끝날 때까지(최대 `kMaxBlockLines` 줄) `X.push_back(` · `X.emplace_back(`(`->` 도) 가 있으면 센다.
  떨어진 모양(검사와 넣기 사이에 몇 줄이 낀 것)과 괄호로 감싼 모양도 이 창 안이면 잡는다.
  반복이 작은 목록(필드 몇 개, 종류 몇 개)이면 두어도 된다 — 그 근거는 `rules/CheckContainsPushBack.toml` 의 기준선 표에 적는다.
기준선은 `rules/CheckContainsPushBack.toml` 의 `[baseline] total`. 늘면 실패하고 커밋 훅은 추가한 줄만 본다.

  python Scripts/lint/gate/CheckContainsPushBack.py [--root <repo>] [--files a.cpp b.h] [--whole-files] [--list]
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

#: 검사 호출 뒤 이 줄 수 안에서 같은 목록의 넣기를 찾는다(최소 창).
_kWindowLines = 8
#: 검사가 든 블록이 끝날 때까지 더 보되 이 줄 수를 넘기지 않는다(`for` 몸통에서 검사하고 한참 뒤에 넣는 모양).
_kMaxBlockLines = 40

# 검사 호출 — 첫 인자 글(`arg`)이 검사받는 목록이다. `VectorUtil` 의 찾기 · 포함 도우미, `std::` 알고리즘(`X.begin()` 짝), `std::ranges::` 알고리즘(`X` 통째).
_kCheckRes = (
    re.compile(r"\bVectorUtil::(?:contains|containsIf|containsBy|findIf|findIndex|findIndexBy|findBy)\s*\(\s*(?P<arg>[^,()]+?)\s*,"),
    re.compile(r"\bstd::(?:find|find_if|find_if_not|any_of|none_of|count|count_if)\s*\(\s*(?P<arg>[^,()]+?)\s*\.\s*c?begin\s*\(\s*\)\s*,"),
    re.compile(r"\bstd::ranges::(?:find|find_if|find_if_not|any_of|none_of|contains|count|count_if)\s*\(\s*(?P<arg>[^,()]+?)\s*,"),
)
_kQuickRe = re.compile(r"\bVectorUtil::(?:contains|find)|\bstd::(?:ranges::)?(?:find|any_of|none_of|count|contains)")


def _findBlockEnd(masked: str, start: int, limit: int) -> int:
    """`start` 를 감싼 가장 안쪽 블록의 닫는 `}` 위치(`limit` 를 넘으면 `limit`)."""
    depth = 0
    for index in range(start, min(limit, len(masked))):
        char = masked[index]
        if char == "{":
            depth += 1
        elif char == "}":
            if depth == 0:
                return index
            depth -= 1
    return min(limit, len(masked))


def findPushBackAfterCheck(masked: str, listLineStart: list[int]) -> list[tuple[int, int]]:
    """검사 호출마다 (검사 줄, 넣기 줄) — 둘 다 1 부터. 검사 호출 하나에 넣기가 여럿이어도 한 건이다."""
    listFound: list[tuple[int, int]] = []

    def lineStartOf(lineIndex: int) -> int:
        return listLineStart[lineIndex] if lineIndex < len(listLineStart) else len(masked)

    for regex in _kCheckRes:
        for match in regex.finditer(masked):
            argText = re.sub(r"\s+", "", match.group("arg"))
            if not argText:
                continue
            lineIndex = masked.count("\n", 0, match.start())
            windowEnd = lineStartOf(lineIndex + _kWindowLines)
            blockEnd = _findBlockEnd(masked, match.end(), lineStartOf(lineIndex + _kMaxBlockLines))
            window = masked[match.end():max(windowEnd, blockEnd)]
            pushRe = re.compile(re.escape(argText) + r"\s*(?:\.|->)\s*(?:push_back|emplace_back)\s*\(")
            pushMatch = pushRe.search(window)
            if pushMatch is None:
                continue
            pushLine = lineIndex + 1 + window.count("\n", 0, pushMatch.start())
            listFound.append((lineIndex + 1, pushLine))
    return sorted(set(listFound))


class CheckContainsPushBackGate(BaselineSourceGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다."""

    description = "contains/find 로 중복을 검사하고 push_back 하는 모양(O(N²))을 막는다 — 집합 · IndexedPtrList · sortUnique 로(훅은 추가한 줄만, 전체는 기준선 초과만)"
    buildComment = "Checking that vectors are not deduplicated by contains before push_back..."
    timeoutSeconds = 60
    preCommitPattern = tuple(f"{root}/*" for root in _kScanRoot)
    sourceScanRoots = _kScanRoot
    sourceSuffixes = _kSuffixes
    violationHeader = "contains/find 뒤 push_back"
    countedNoun = "contains-then-push_back sites"
    baselineExceededHint = "unordered_set 을 곁에 두거나(순서가 필요할 때) IndexedPtrList(포인터) · VectorUtil::sortUnique(모은 뒤 한 번에)로"
    hint = (
        "  목록에 중복 없이 모을 때 넣을 때마다 목록을 훑으면 원소가 N 개일 때 O(N²) 입니다(씬 로드 · 체크아웃에서 수천 개):\n"
        "      unordered_set<T> setSeen;  if ( setSeen.insert( item ).second ) list.push_back( item );   // 넣은 순서를 지킨다\n"
        "      IndexedPtrList<T> list;    list.add( pItem );  list.remove( pItem );                       // 포인터 목록(Core/Container/IndexedPtrList.h)\n"
        "      모두 모은 뒤 VectorUtil::sortUnique( list );                                               // 순서가 상관없을 때\n"
        "  원소가 늘 작은 목록(필드 몇 개, 종류 몇 개)이면 기준선 표(rules/CheckContainsPushBack.toml)에 근거를 적고 그대로 둡니다. 전체를 보려면 --whole-files 또는 --list."
    )
    selfTestCases = [
        {
            "name": "contains 뒤 push_back",
            "expectedViolation": "1  Source/Probe/A.cpp",
            "files": {
                "Source/Probe/A.cpp": (
                    "void f( std::vector<int>& list, int item )\n"
                    "{\n"
                    "    if ( VectorUtil::contains( list, item ) == false )\n"
                    "        list.push_back( item );\n"
                    "}\n"
                ),
            },
            "args": ["--baseline", "0"],
        },
        {
            "name": "std::find 와 떨어진 push_back, 괄호로 감싼 모양",
            "expectedViolation": "1  Source/Probe/B.cpp",
            "files": {
                "Source/Probe/B.cpp": (
                    "void f( Owner& owner, int item )\n"
                    "{\n"
                    "    if ( std::find( owner._listItem.begin(), owner._listItem.end(), item ) == owner._listItem.end() )\n"
                    "    {\n"
                    "        log( item );\n"
                    "        owner._listItem.push_back( item );\n"
                    "    }\n"
                    "}\n"
                ),
            },
            "args": ["--baseline", "0"],
        },
        {
            "name": "다른 목록에 넣거나 집합을 쓰면 세지 않는다",
            "expectedViolation": "Source/Probe/Bad.cpp:3:",
            "unexpectedViolation": "Source/Probe/Ok",
            "files": {
                "Source/Probe/Bad.cpp": (
                    "void f( std::vector<int>& list, int item )\n"
                    "{\n"
                    "    if ( VectorUtil::contains( list, item ) == false )\n"
                    "        list.emplace_back( item );\n"
                    "}\n"
                ),
                "Source/Probe/OkOther.cpp": (
                    "void f( std::vector<int>& listSeen, std::vector<int>& listOut, int item )\n"
                    "{\n"
                    "    if ( VectorUtil::contains( listSeen, item ) )\n"
                    "        return;\n"
                    "    listOut.push_back( item );\n"
                    "}\n"
                ),
                "Source/Probe/OkSet.cpp": (
                    "void f( std::unordered_set<int>& setSeen, std::vector<int>& list, int item )\n"
                    "{\n"
                    "    if ( setSeen.insert( item ).second )\n"
                    "        list.push_back( item );\n"
                    "}\n"
                ),
            },
            "args": ["--files", "Source/Probe/Bad.cpp", "Source/Probe/OkOther.cpp", "Source/Probe/OkSet.cpp", "--whole-files"],
        },
        {
            "name": "any_of · ranges · find_if 와 8 줄보다 떨어진 넣기",
            "expectedViolation": "3  Source/Probe/C.cpp",
            "files": {
                "Source/Probe/C.cpp": (
                    "void f( std::vector<int>& listA, std::vector<int>& listB, std::vector<int>& listC, int item )\n"
                    "{\n"
                    "    if ( std::any_of( listA.begin(), listA.end(), [item]( int v ) { return v == item; } ) == false )\n"
                    "        listA.push_back( item );\n"
                    "    if ( std::ranges::find( listB, item ) == listB.end() )\n"
                    "        listB.push_back( item );\n"
                    "    for ( int i = 0; i < 3; ++i )\n"
                    "    {\n"
                    "        const auto it = std::find_if( listC.begin(), listC.end(), [item]( int v ) { return v == item; } );\n"
                    "        step();\n"
                    "        step();\n"
                    "        step();\n"
                    "        step();\n"
                    "        step();\n"
                    "        step();\n"
                    "        step();\n"
                    "        step();\n"
                    "        step();\n"
                    "        if ( it == listC.end() )\n"
                    "            listC.push_back( item );\n"
                    "    }\n"
                    "}\n"
                ),
            },
            "args": ["--baseline", "0"],
        },
    ]

    def findCounted(self, source: SourceFile) -> list[CountedFinding] | None:
        masked = source.masked
        if _kQuickRe.search(masked) is None:
            return []
        listLineStart = [0]
        for index, char in enumerate(masked):
            if char == "\n":
                listLineStart.append(index + 1)
        listLine = source.listLine
        listResult: list[CountedFinding] = []
        for checkLine, pushLine in findPushBackAfterCheck(masked, listLineStart):
            listResult.append(CountedFinding("total", source.relative, checkLine, max(checkLine, pushLine),
                                             "contains/find 로 중복을 검사하고 push_back — 집합 · IndexedPtrList · sortUnique 로",
                                             listLine[checkLine - 1].strip()))
        return listResult


main = CheckContainsPushBackGate.run

if __name__ == "__main__":
    sys.exit(main())
