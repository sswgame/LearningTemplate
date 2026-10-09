#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckLoopBraces.py

반복문(`for` · 범위 `for` · `while`)은 본문이 한 문장이어도 중괄호를 쓴다(AGENTS.md '분기문 규칙').

`if` 는 한 줄 본문의 중괄호를 벗기지만(`FormatBranchBraces`), 반복문은 늘 중괄호다 — 본문에 한 줄을 더할 때 반복 밖으로 새는 줄이 생기지 않게.
`do { … } while ( … );` 의 꼬리 `while` 은 반복문 머리가 아니다. 본문이 `;` 하나(빈 본문)이면 `{}` 로 쓴다.

판정과 고침은 이 파일 한 자리다 — 고치는 것은 같은 판정을 부르는 픽서 `Scripts/lint/fixer/FormatBranchBraces.py` 의 반복문 패스.
주석 · 전처리기 · 매크로 줄바꿈이 머리와 본문 사이나 본문 안에 끼면 픽서는 건드리지 않고(사람이 고친다) 이 게이트만 알린다.

  python Scripts/lint/gate/CheckLoopBraces.py [--root <repo>] [--files a.cpp b.h]
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common import blankComments, blankCommentsAndLiterals, kLintTargetRelDirs, normalizePath  # noqa: E402
from LintGate import GateResult, LintGate  # noqa: E402

_kSuffixes = (".h", ".hpp", ".inl", ".c", ".cc", ".cpp", ".cxx")
_kLoopHeadRe = re.compile(r"\b(for|while)\s*\(")
_kDoRe = re.compile(r"\bdo\b")
_kIdentifierRe = re.compile(r"[A-Za-z_]\w*")
#: 반복문 머리 바로 앞에 올 수 있는 글자(문장의 시작) — `else` · `do` 는 따로 본다.
_kStatementStartBefore = set(";{}):")


@dataclass
class UnbracedLoop:
    """중괄호 없는 반복문 하나."""

    lineNumber: int        #: 머리(`for` · `while`)의 줄 번호(1 부터)
    headLineStart: int     #: 머리 줄의 첫 글자 위치
    keyword: str
    bodyStart: int         #: 본문 첫 글자 위치(빈 본문이면 `;` 의 위치)
    bodyEnd: int           #: 본문 끝 글자 위치(포함)
    bFixable: bool         #: 픽서가 고칠 수 있다(주석 · 전처리기 · 매크로 줄바꿈이 끼지 않았다)


class _Scanner:
    """주석 · 리터럴을 공백으로 덮은 글(`masked`) 위에서 문장의 끝을 찾는다. 모르는 모양이면 None 을 돌려준다."""

    def __init__(self, masked: str) -> None:
        self.masked = masked
        self.length = len(masked)

    def skipSpace(self, index: int) -> int:
        # 매크로 줄바꿈(`\` + 줄 끝)도 빈칸으로 본다.
        while index < self.length and (self.masked[index].isspace() or (self.masked[index] == "\\" and self.masked[index + 1:index + 2] in ("\n", "\r"))):
            index += 1
        return index

    def wordAt(self, index: int) -> str:
        match = _kIdentifierRe.match(self.masked, index)
        return match.group(0) if match else ""

    def matchClose(self, index: int, openChar: str, closeChar: str) -> int | None:
        depth = 0
        while index < self.length:
            ch = self.masked[index]
            if ch == openChar:
                depth += 1
            elif ch == closeChar:
                depth -= 1
                if depth == 0:
                    return index
            index += 1
        return None

    def parenAfter(self, index: int) -> int | None:
        """@p index 뒤 첫 글자가 `(` 이면 짝 `)` 의 위치."""
        index = self.skipSpace(index)
        if index >= self.length or self.masked[index] != "(":
            return None
        return self.matchClose(index, "(", ")")

    def statementEnd(self, index: int) -> int | None:
        """@p index(문장 첫 글자)에서 시작하는 문장의 마지막 글자 위치(포함)."""
        if index >= self.length:
            return None
        ch = self.masked[index]
        if ch == "{":
            return self.matchClose(index, "{", "}")
        if ch == ";":
            return index
        if ch == "#":
            return None
        word = self.wordAt(index)
        if word == "if":
            cursor = self.skipSpace(index + 2)
            if self.wordAt(cursor) == "constexpr":
                cursor += len("constexpr")
            close = self.parenAfter(cursor)
            if close is None:
                return None
            end = self.statementEnd(self.skipSpace(close + 1))
            if end is None:
                return None
            following = self.skipSpace(end + 1)
            if self.wordAt(following) == "else":
                return self.statementEnd(self.skipSpace(following + 4))
            return end
        if word in ("for", "while", "switch"):
            close = self.parenAfter(index + len(word))
            if close is None:
                return None
            return self.statementEnd(self.skipSpace(close + 1))
        if word == "do":
            end = self.statementEnd(self.skipSpace(index + 2))
            if end is None:
                return None
            tail = self.skipSpace(end + 1)
            if self.wordAt(tail) != "while":
                return None
            close = self.parenAfter(tail + 5)
            if close is None:
                return None
            semicolon = self.skipSpace(close + 1)
            return semicolon if semicolon < self.length and self.masked[semicolon] == ";" else None
        if word in ("try", "case", "default", "else"):
            return None
        # 단순 문장 — 괄호 깊이 0 의 `;` 까지(람다 본문 · 중괄호 초기화는 깊이로 건너뛴다).
        depth = 0
        cursor = index
        while cursor < self.length:
            current = self.masked[cursor]
            if current in "([{":
                depth += 1
            elif current in ")]}":
                depth -= 1
                if depth < 0:
                    return None
            elif current == ";" and depth == 0:
                return cursor
            cursor += 1
        return None


def _collectDoTailsInternal(scanner: _Scanner) -> set[int]:
    """`do … while ( … );` 의 꼬리 `while` 위치 — 반복문 머리로 보지 않는다."""
    uniqueTail: set[int] = set()
    for match in _kDoRe.finditer(scanner.masked):
        # 매크로의 `do { … } while ( 0 )` 는 끝에 `;` 가 없다 — 본문 블록만 보고 꼬리를 찾는다.
        bodyStart = scanner.skipSpace(match.end())
        end = scanner.matchClose(bodyStart, "{", "}") if bodyStart < scanner.length and scanner.masked[bodyStart] == "{" else scanner.statementEnd(bodyStart)
        if end is None:
            continue
        tail = scanner.skipSpace(end + 1)
        if scanner.wordAt(tail) == "while":
            uniqueTail.add(tail)
    return uniqueTail


def _isStatementStartInternal(masked: str, index: int) -> bool:
    """@p index 의 낱말이 문장의 시작인가 — 앞의 첫 글자가 `; { } ) :` 이거나 `else` · `do` 다(파일 처음도)."""
    cursor = index - 1
    while cursor >= 0 and masked[cursor].isspace():
        cursor -= 1
    if cursor < 0:
        return True
    if masked[cursor] in _kStatementStartBefore:
        return not (masked[cursor] == ":" and cursor > 0 and masked[cursor - 1] == ":")
    wordEnd = cursor + 1
    while cursor >= 0 and (masked[cursor].isalnum() or masked[cursor] == "_"):
        cursor -= 1
    return masked[cursor + 1:wordEnd] in ("else", "do")


def findUnbracedLoops(text: str) -> list[UnbracedLoop]:
    """파일 하나에서 본문에 중괄호가 없는 `for` · `while` 반복문을 찾습니다(픽서와 게이트가 같이 쓴다)."""
    masked = blankCommentsAndLiterals(text)
    noComment = blankComments(text)
    if len(masked) != len(text) or len(noComment) != len(text):
        return []
    scanner = _Scanner(masked)
    uniqueDoTail = _collectDoTailsInternal(scanner)
    listLoop: list[UnbracedLoop] = []
    for match in _kLoopHeadRe.finditer(masked):
        start = match.start()
        if start in uniqueDoTail or _isStatementStartInternal(masked, start) is False:
            continue
        close = scanner.matchClose(match.end() - 1, "(", ")")
        if close is None:
            continue
        bodyStart = scanner.skipSpace(close + 1)
        if bodyStart >= len(masked) or masked[bodyStart] == "{":
            continue
        bodyEnd = scanner.statementEnd(bodyStart)
        lineNumber = text.count("\n", 0, start) + 1
        if bodyEnd is None:
            listLoop.append(UnbracedLoop(lineNumber, text.rfind("\n", 0, start) + 1, match.group(1), bodyStart, bodyStart, False))
            continue
        # 머리 줄부터 본문 끝 줄까지 주석 · 전처리기 · 매크로 줄바꿈이 끼었으면 고치지 않는다.
        lineBegin = text.rfind("\n", 0, start) + 1
        lineEnd = text.find("\n", bodyEnd)
        lineEnd = len(text) if lineEnd == -1 else lineEnd
        region = text[lineBegin:lineEnd]
        # 머리 줄 끝의 주석(`for ( … ) // 이유`)은 그대로 두고 고친다 — 머리 다음 줄부터 본문 끝까지 주석이 끼면 고치지 않는다.
        headLineEnd = text.find("\n", close)
        commentFrom = bodyStart if headLineEnd == -1 or bodyStart < headLineEnd else headLineEnd + 1
        bFixable = noComment[commentFrom:bodyEnd + 1] == text[commentFrom:bodyEnd + 1]
        bFixable = bFixable and all(line.strip().startswith("#") is False and line.rstrip().endswith("\\") is False for line in region.split("\n"))
        # 본문 끝 뒤 같은 줄에 다른 코드가 있으면(한 줄에 문장 둘) 고치지 않는다.
        bFixable = bFixable and masked[bodyEnd + 1:lineEnd].strip() == ""
        listLoop.append(UnbracedLoop(lineNumber, lineBegin, match.group(1), bodyStart, bodyEnd, bFixable))
    return listLoop


def insertLoopBraces(text: str) -> tuple[str, bool]:
    """중괄호 없는 반복문 본문을 중괄호로 감싼 글과 수정 여부(픽서 `FormatBranchBraces` 의 반복문 패스). 들여쓰기는 뒤의 clang-format 이 맞춘다."""
    listLoop = [loop for loop in findUnbracedLoops(text) if loop.bFixable]
    if not listLoop:
        return text, False
    eol = "\r\n" if text.count("\r\n") * 2 > text.count("\n") else "\n"
    listEdit: list[tuple[int, int, str]] = []  # (위치, 지울 글자 수, 넣을 글) — 모은 차례가 바깥 반복문 → 안쪽 반복문
    for loop in listLoop:
        headLine = text[loop.headLineStart:].split("\n", 1)[0]
        indent = headLine[: len(headLine) - len(headLine.lstrip())]
        if text[loop.bodyStart] == ";" and loop.bodyStart == loop.bodyEnd:
            listEdit.append((loop.bodyStart, 1, "{}"))
            continue
        bodyLineStart = text.rfind("\n", 0, loop.bodyStart) + 1
        if text[bodyLineStart:loop.bodyStart].strip():
            # 본문이 머리(닫는 괄호)와 같은 줄이다 — 본문 앞에서 줄을 끊는다.
            listEdit.append((loop.bodyStart, 0, eol + indent + "{" + eol + indent + "    "))
        else:
            listEdit.append((bodyLineStart, 0, indent + "{" + eol))
        lineEnd = text.find("\n", loop.bodyEnd)
        if lineEnd == -1:
            listEdit.append((len(text), 0, eol + indent + "}"))
        else:
            listEdit.append((lineEnd + 1, 0, indent + "}" + eol))
    # 뒤에서부터 적용한다. 같은 자리에 둘이 들어가면(본문 끝이 같은 바깥 · 안쪽 반복문) 나중에 모은 안쪽 것을 먼저 넣어
    # 앞에 놓이게 한다 — 같은 자리 삽입은 나중에 넣은 것이 앞에 선다.
    result = text
    for _, (position, removeCount, insert) in sorted(enumerate(listEdit), key=lambda item: (item[1][0], -item[0]), reverse=True):
        result = result[:position] + insert + result[position + removeCount:]
    return result, True


class CheckLoopBracesGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다."""

    description = "본문에 중괄호가 없는 for · while 반복문 검사(한 문장 본문도 중괄호)"
    buildComment = "Checking that every for / while loop body has braces..."
    timeoutSeconds = 60
    preCommitPattern = tuple(f"{root}/*" for root in kLintTargetRelDirs)
    preCommitFileArgument = "--files"
    violationHeader = "중괄호 없는 반복문"
    hint = ("  for ( int32 index = 0; index < count; ++index )\n  {\n      doThing( index );\n  }\n"
            "  고치기: py -3 Scripts/lint/fixer/FormatBranchBraces.py --files <파일>(주석 · 전처리기가 낀 본문은 손으로)")
    selfTestCases = [
        {"name": "한 문장 본문의 for", "files": {"Source/Probe/A.cpp": "void f()\n{\n    for ( int i = 0; i < 3; ++i )\n        g( i );\n}\n"}},
        {"name": "같은 줄 본문의 범위 for", "files": {"Source/Probe/B.cpp": "void f()\n{\n    for ( int value : list ) g( value );\n}\n"}},
        {"name": "빈 본문의 while", "files": {"Test/Probe/C.cpp": "void f()\n{\n    while ( poll() );\n}\n"}},
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser)

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        listPath = self.selectTargetFiles(repositoryRoot, args.files, listScanRoot=kLintTargetRelDirs, suffixes=_kSuffixes)
        listViolation: list[str] = []
        fileCount = 0
        for path, text in self.readFiles(listPath):
            fileCount += 1
            relative = normalizePath(str(path.relative_to(repositoryRoot)))
            listLine = text.split("\n")
            for loop in findUnbracedLoops(text):
                manual = "" if loop.bFixable else " (픽서가 고치지 않는다 — 손으로)"
                listViolation.append(f"{relative}:{loop.lineNumber}: {loop.keyword} 본문에 중괄호가 없습니다{manual} | {listLine[loop.lineNumber - 1].strip()}")
        return GateResult(listViolation=listViolation, summary=f"{fileCount} files")


main = CheckLoopBracesGate.run

if __name__ == "__main__":
    sys.exit(main())
