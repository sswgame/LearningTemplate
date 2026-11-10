#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/DiskPathCase.py

경로 포함 · 같음 판정 함수(`startsWithPathComponent` · `suffixAfterPathComponent` · `isPathUnderRoot` · `isPathAtOrUnderRoot` · `tryMakePathUnderRoot` ·
`isSameDiskPath`)에 소문자로 접은 글(`FileUtil::normalizePath(…)` · `StringUtil::toLower(…)`, 또는 그 결과를 담은 같은 파일의 지역 변수)을 넘기는 곳을 찾는다.

**왜 린트인가.** `normalizePath` 는 리소스 id · 맵 키용이라 늘 소문자로 접는다. 그 결과로 디스크 경로의 "뿌리 아래인가" 를 보면 리눅스에서 대소문자만 다른 바깥 폴더가
안으로 보인다(자동화 쓰기 문이 `Content/SCRATCH` 를 사본으로 보고 열어 줬다). 결과는 `string` 이고 받는 함수는 `string_view` 라 형식이 출처를 가르지 못한다 —
`normalizePath` 의 반환 형식을 바꾸면 153 곳이 맵 키 · id 로 쓰는 정상 코드를 함께 깬다. 디스크 경로는 `FileUtil::isPathAtOrUnderRoot` · `isSameDiskPath` ·
`makeComparableDiskPath`(OS 규칙)를 쓴다. 리소스 id 끼리의 판정(늘 소문자)은 예외 `Config/Rules/Lint/DiskPathCase.json` 에 `경로::식별자` 와 까닭을 적는다.
매개변수로 들어온 소문자 글(다른 함수가 접은 것)은 보지 못한다(머리말의 한계).

`py -3 -m Scripts lint disk-path-case [--self-check]` 로 돌리고, 커밋 훅(`Rules.py` 표)이 돌린다.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Iterable, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.ExitCode import ExitCode  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402
from lint.NamePathArguments import blankCommentsAndStrings, splitArguments  # noqa: E402

kConfigPath = "Config/Rules/Lint/DiskPathCase.json"
kFold = r"(?:\bFileUtil::normalizePath|\bnormalizePath|\bStringUtil::toLower|\btoLower)\s*\("
kFoldCall = re.compile(kFold)
kFoldedVariable = re.compile(r"\b(\w+)\s*(?:=|\{)\s*(?:sw::)?" + kFold)
kPathCheck = re.compile(r"\b(startsWithPathComponent|suffixAfterPathComponent|isPathUnderRoot|isPathAtOrUnderRoot|tryMakePathUnderRoot|isSameDiskPath)\s*\(")


def findFoldedArguments(text: str) -> list[tuple[str, str, int]]:
    """글 하나에서 (판정 함수, 접은 글의 식별자 — 직접 부르면 `normalizePath` 같은 함수 이름, 줄 번호) 를 돌려준다."""
    code = blankCommentsAndStrings(text)
    setFolded = {match.group(1) for match in kFoldedVariable.finditer(code)}
    listFound: list[tuple[str, str, int]] = []
    for match in kPathCheck.finditer(code):
        line = code.count("\n", 0, match.start()) + 1
        for argument in splitArguments(code, match.end()):
            direct = kFoldCall.search(argument)
            folded = direct.group(0).split("::")[-1].rstrip("( ") if direct else next((word for word in re.findall(r"\b\w+\b", argument) if word in setFolded), "")
            if folded:
                listFound.append((match.group(1), folded, line))
                break  # 판정 하나에 한 줄
    return listFound


def findProblems(listSource: Iterable[tuple[str, str]], exceptions: dict) -> list[str]:
    listProblem: list[str] = []
    for path, text in listSource:
        for function, folded, line in findFoldedArguments(text):
            if f"{path}::{folded}" in exceptions:
                continue
            listProblem.append(f"{path}:{line}: {function} 에 소문자로 접은 글('{folded}')을 넘긴다 — 디스크 경로면 isPathAtOrUnderRoot · isSameDiskPath · makeComparableDiskPath(OS 규칙)를, "
                               f"리소스 id 끼리면 예외({kConfigPath})에 까닭을 적는다")
    return listProblem


def readSources(projectRoot: Path) -> list[tuple[str, str]]:
    listSource: list[tuple[str, str]] = []
    for pattern in ("*.cpp", "*.h"):
        for path in (projectRoot / "Source").rglob(pattern):
            try:
                listSource.append((path.relative_to(projectRoot).as_posix(), path.read_text(encoding="utf-8")))
            except (UnicodeDecodeError, OSError):
                continue
    return listSource


def checkDiskPathCase(projectRoot: Path) -> list[str]:
    exceptions = json.loads((projectRoot / kConfigPath).read_text(encoding="utf-8"))["_exceptions"]
    return findProblems(readSources(projectRoot), exceptions)


def runSelfCheck() -> None:
    direct = 'bool f( string_view a, string_view b ) { return FileUtil::startsWithPathComponent( FileUtil::normalizePath( a ), FileUtil::normalizePath( b ) ); }\n'
    assert len(findProblems([("a.cpp", direct)], {})) == 1, "직접 넘긴 normalizePath 는 잡아야 한다"
    variable = 'void f( string_view a, string_view r )\n{\n    const string folded = FileUtil::normalizePath( a );\n    if ( FileUtil::startsWithPathComponent( folded, r ) ) {}\n}\n'
    assert len(findProblems([("a.cpp", variable)], {})) == 1, "접은 글을 담은 변수도 잡아야 한다"
    assert findProblems([("a.cpp", variable)], {"a.cpp::folded": "리소스 id"}) == [], "예외는 통과해야 한다"
    lower = 'bool f( const string& a, const string& b ) { return FileUtil::isPathUnderRoot( b, StringUtil::toLower( a.c_str() ) ); }\n'
    assert len(findProblems([("a.cpp", lower)], {})) == 1, "toLower 도 잡아야 한다"
    clean = ('bool f( string_view a, string_view b )\n{\n    const string key = FileUtil::normalizePath( a ); // 맵 키\n'
             '    use( key );\n    return FileUtil::isPathAtOrUnderRoot( b, a ) && FileUtil::startsWithPathComponent( FileUtil::makeComparableDiskPath( a ), b );\n}\n')
    assert findProblems([("a.cpp", clean)], {}) == [], "OS 규칙 글과 판정 밖의 접은 글은 통과해야 한다"
    comment = 'void f() { /* startsWithPathComponent( normalizePath( a ), b ) */ }\n'
    assert findProblems([("a.cpp", comment)], {}) == [], "주석은 통과해야 한다"


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="경로 포함 · 같음 판정에 소문자로 접은 글을 넘기는 곳을 찾는다")
    parser.add_argument("--self-check", action="store_true")
    arguments = parser.parse_args(argv)
    if arguments.self_check:
        runSelfCheck()
        print("[DiskPathCase] self-check OK")
        return ExitCode.Passed
    listProblem = checkDiskPathCase(getProjectRoot())
    for problem in listProblem:
        print(f"  - {problem}")
    print(f"[DiskPathCase] {'FAIL' if listProblem else 'OK'} ({len(listProblem)}건)")
    return ExitCode.Violation if listProblem else ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
