#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
한 파일에 클래스 · 구조체 정의가 여럿이면 **정의마다 namespace 블록을 따로** 둔다.

  한 블록에 클래스 여럿이 들어 있으면 에디터에서 블록을 접을 때 클래스 단위로 접히지 않고, 클래스 경계가 들여쓰기 하나로만 갈린다.
  `.cpp` 의 내부 도우미를 클래스 구현과 다른 `namespace sw` 블록에 두는 규칙(AGENTS.md "Helpers: Util vs Internal")과 같은 이유다.

  - 대상은 이름 있는 namespace 블록 바로 안의 `class` · `struct` **정의**다(전방 선언 · 한 줄 정의 · enum 은 아니다). 익명 namespace 는
    파일당 하나인 내부 도우미 블록이라 나누지 않는다(CheckCodeConventions `Structure/AnonymousNamespaceCount`).
  - 이름이 같은 정의가 이어지면(템플릿 특수화) 한 묶음으로 둔다.
  - 나누는 자리는 앞 정의의 `};` 바로 뒤다. 그 사이의 함수 · 상수 · 주석은 뒤 정의의 블록으로 간다.
  - 나눌 자리가 namespace 를 연 줄과 다른 전처리기 조건(`#if`) 깊이에 있으면 나누지 않는다 — 한쪽 가지에서만 블록이 맞게 된다.

  python Scripts/lint/gate/CheckNamespaceBlocks.py [--files <path> ...]          # 검사
  python Scripts/lint/fixer/FormatNamespaceBlocks.py [--files <path> ...]       # 고치기(같은 splitNamespaceBlocks)
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common import flatMapConcurrent, kLintTargetRelDirs  # noqa: E402
from LintGate import GateResult, LintGate  # noqa: E402

_kNamespaceRe = re.compile(r"^(\s*)namespace(?:\s+([\w:]+))?\s*(\{)?\s*$")
_kTypeRe = re.compile(r"^\s*(?:template\s*<[^;{]*>\s*)?(?:class|struct)\s+(?:SW_\w+\s+|alignas\s*\([^)]*\)\s+)*(\w+)(?:\s+final)?\s*(?::[^;{]*)?\{?\s*$")
_kTemplateLineRe = re.compile(r"^\s*template\s*<.*>\s*$")
_kIfOpenRe = re.compile(r"^\s*#\s*if")
_kIfCloseRe = re.compile(r"^\s*#\s*endif")


#: 가릴 덩어리 — `//` 주석, `/* */` 주석(안 닫히면 끝까지), 문자열 · 문자 리터럴(백슬래시는 다음 글자와 짝 — 글 끝이면 혼자, 줄바꿈 앞에서 멈춘다).
_kMaskTokenRe = re.compile(r'//[^\n]*|/\*[\s\S]*?(?:\*/|\Z)|"(?:\\[\s\S]?|[^"\\\n])*"?|\'(?:\\[\s\S]?|[^\'\\\n])*\'?')
_kNotNewlineRe = re.compile(r"[^\n]")
#: 블록 깊이와 정의 끝을 바꾸는 글자 — 나머지 글자는 `findSplitPoints` 의 루프에서 아무 일도 하지 않는다.
_kBraceOrSemicolonRe = re.compile(r"[{};]")


def isClosedLiteralInternal(token: str) -> bool:
    """따옴표로 끝나고 그 따옴표가 본문의 이스케이프(`\\"`)가 아닌가 — 끝 따옴표 앞 백슬래시가 짝수 개면 닫힌 것이다."""
    if len(token) < 2 or token[-1] != token[0]:
        return False
    backslashCount = len(token) - 1 - len(token[:-1].rstrip("\\"))
    return backslashCount % 2 == 0


def maskTokenInternal(match: re.Match) -> str:
    token = match.group()
    head = token[0]
    if head == "/":
        return " " * len(token) if token[1] == "/" else _kNotNewlineRe.sub(" ", token)
    if isClosedLiteralInternal(token):
        return head + " " * (len(token) - 2) + head
    return head + " " * (len(token) - 1)


def maskCodeInternal(text: str) -> str:
    """
    주석 · 문자열 · 문자 리터럴을 공백으로 바꾼다(줄바꿈은 남겨 줄 번호가 원문과 맞는다). 따옴표 글자는 남긴다.

    정규식 한 번으로 덩어리를 찾는다 — 글자마다 파이썬 루프를 돌면 트리 전체에서 5 초다. 문자열 안의 백슬래시-줄바꿈은 공백이 되어
    줄 수가 어긋나고, 그 파일은 `splitNamespaceBlocks` 가 건너뛴다.
    """
    return _kMaskTokenRe.sub(maskTokenInternal, text)


def findSplitPoints(listMasked: list[str]) -> list[tuple[int, str, str, int]]:
    """
    나눌 자리 목록 (정의가 끝나는 줄 번호, namespace 줄의 들여쓰기, namespace 이름, 다음 정의가 시작하는 줄 번호).
    """
    listIfDepth: list[int] = []
    ifDepth = 0
    for line in listMasked:
        if _kIfOpenRe.match(line):
            ifDepth += 1
        listIfDepth.append(ifDepth)
        if _kIfCloseRe.match(line):
            ifDepth = max(0, ifDepth - 1)

    # 스택 항목: ("ns", 들여쓰기, 이름, 연 줄, 정의 목록) | ("other",)
    stack: list[tuple] = []
    pendingNamespace: tuple[str, str, int] | None = None
    listBlock: list[tuple[str, str, int, list[tuple[str, int, int]]]] = []
    # 정의 추적: 지금 여는 중인 정의(이름, 시작 줄, namespace 스택 깊이)
    pendingType: tuple[str, int] | None = None
    openType: list[tuple[str, int, int]] = []  # (이름, 시작 줄, 그 '{' 의 스택 깊이)

    for lineIndex, line in enumerate(listMasked):
        namespaceMatch = _kNamespaceRe.match(line)
        if namespaceMatch is not None:
            pendingNamespace = (namespaceMatch.group(1), namespaceMatch.group(2) or "", lineIndex)
        elif stack and stack[-1][0] == "ns" and pendingType is None:
            typeMatch = _kTypeRe.match(line)
            if typeMatch is not None:
                startLine = lineIndex
                if lineIndex > 0 and _kTemplateLineRe.match(listMasked[lineIndex - 1]):
                    startLine = lineIndex - 1
                pendingType = (typeMatch.group(1), startLine)

        for ch in _kBraceOrSemicolonRe.findall(line):
            if ch == "{":
                if pendingNamespace is not None:
                    stack.append(("ns", pendingNamespace[0], pendingNamespace[1], pendingNamespace[2], []))
                    pendingNamespace = None
                else:
                    if pendingType is not None and stack and stack[-1][0] == "ns":
                        openType.append((pendingType[0], pendingType[1], len(stack)))
                        pendingType = None
                    stack.append(("other",))
            elif ch == "}":
                if not stack:
                    continue
                top = stack.pop()
                if top[0] == "ns":
                    listBlock.append((top[1], top[2], top[3], top[4]))
                elif openType and openType[-1][2] == len(stack):
                    name, startLine, _ = openType.pop()
                    if stack and stack[-1][0] == "ns":
                        stack[-1][4].append((name, startLine, lineIndex))
            elif ch == ";" and pendingType is not None:
                pendingType = None  # 전방 선언이었다

    listSplit: list[tuple[int, str, str, int]] = []
    for indent, name, openLine, listType in listBlock:
        if name == "":
            continue  # 익명 namespace 는 파일당 하나인 내부 도우미 블록이다(`Structure/AnonymousNamespaceCount`) — 나누지 않는다
        for current, following in zip(listType, listType[1:]):
            if current[0] == following[0]:
                continue  # 같은 이름(특수화)은 한 묶음
            endLine = current[2]
            if listIfDepth[endLine] != listIfDepth[openLine] or listIfDepth[following[1]] != listIfDepth[openLine]:
                continue
            if listMasked[endLine].strip().rstrip() not in ("};", "} ;"):
                continue
            listSplit.append((endLine, indent, name, following[1]))
    return listSplit


def splitNamespaceBlocks(text: str) -> tuple[str, list[tuple[int, str, str, int]]]:
    """정의마다 namespace 블록을 나눈 글과 나눈 자리(`findSplitPoints`). 줄끝(CRLF)은 지킨다. 나눌 것이 없으면 (원래 글, [])."""
    newline = "\r\n" if "\r\n" in text else "\n"
    plainText = text.replace("\r\n", "\n")
    listLine = plainText.split("\n")
    listMasked = maskCodeInternal(plainText).split("\n")
    if len(listMasked) != len(listLine):
        return text, []
    listSplit = findSplitPoints(listMasked)
    if not listSplit:
        return text, []

    mapSplitAfter = {split[0]: split for split in listSplit}
    listOut: list[str] = []
    lineIndex = 0
    while lineIndex < len(listLine):
        listOut.append(listLine[lineIndex])
        split = mapSplitAfter.get(lineIndex)
        if split is not None:
            _, indent, name, _ = split
            # 정의 뒤의 빈 줄은 새 블록을 연 뒤가 아니라 닫은 뒤로 — 블록 머리에 빈 줄이 남지 않게.
            nextIndex = lineIndex + 1
            while nextIndex < len(listLine) and listLine[nextIndex].strip() == "":
                nextIndex += 1
            closeComment = f" // namespace {name}" if name else " // namespace"
            header = f"namespace {name}" if name else "namespace"
            listOut += [f"{indent}}}{closeComment}", "", f"{indent}{header}", f"{indent}{{"]
            lineIndex = nextIndex
            continue
        lineIndex += 1
    return "\n".join(listOut).replace("\n", newline), listSplit


def findViolations(filePath: Path, repositoryRoot: Path) -> list[str]:
    """파일 하나의 위반 — 읽기만 한다(고치는 것은 `fixText` · `FormatNamespaceBlocks`)."""
    try:
        raw = filePath.read_bytes().decode("utf-8")
    except (OSError, UnicodeDecodeError):
        return []
    _, listSplit = splitNamespaceBlocks(raw)
    relativePath = filePath.relative_to(repositoryRoot).as_posix() if filePath.is_relative_to(repositoryRoot) else str(filePath)
    return [f"{relativePath}:{split[3] + 1}: 한 namespace 블록에 클래스 · 구조체 정의가 여럿입니다 — 정의마다 블록을 나누세요 "
            f"(`py -3 Scripts/lint/fixer/FormatNamespaceBlocks.py --files {relativePath}`)" for split in listSplit]


def fixText(text: str) -> tuple[str, bool]:
    """픽서 변환(`FixPass.transform`)."""
    newText, listSplit = splitNamespaceBlocks(text)
    return newText, bool(listSplit)


#: 픽서의 조각 — 게이트의 첫 자가 시험 조각(한 블록에 클래스 둘)과 정의마다 나눈 결과.
kFixBadSample = "#pragma once\n\nnamespace sw\n{\n    class Alpha\n    {\n    };\n\n    class Beta\n    {\n    };\n} // namespace sw\n"
kFixGoodSample = ("#pragma once\n\nnamespace sw\n{\n    class Alpha\n    {\n    };\n} // namespace sw\n\nnamespace sw\n{\n"
                  "    class Beta\n    {\n    };\n} // namespace sw\n")


class CheckNamespaceBlocksGate(LintGate):
    """
    검사만 한다 — 고치는 것은 `fixer/FormatNamespaceBlocks.py`(같은 `splitNamespaceBlocks`)와 그것을 부르는 `FormatModified.py`.
    """

    description = "클래스마다 namespace 블록 검사"
    buildComment = "Checking that each class or struct definition has its own namespace block..."
    timeoutSeconds = 30
    preCommitPattern = ("*.cpp", "*.h", "*.inl")
    preCommitFileArgument = "--files"
    violationHeader = "한 namespace 블록에 정의 여럿"
    selfTestCases = [
        {
            "name": "한 블록에 클래스 둘",
            "files": {
                "Source/Engine/Probe/Probe.h": (
                    "#pragma once\n\nnamespace sw\n{\n    class Alpha\n    {\n    };\n\n    class Beta\n    {\n    };\n} // namespace sw\n"
                ),
            },
        },
        {
            "name": "구조체와 클래스(상속 · 매크로 · final)",
            "files": {
                "Source/Engine/Probe/Probe.h": (
                    "#pragma once\n\nnamespace sw::editor\n{\n    struct Settings\n    {\n        int value{ 0 };\n    };\n\n"
                    "    class SW_API Panel final : public IPanel\n    {\n    };\n} // namespace sw::editor\n"
                ),
            },
        },
        {
            # 두 줄 전방 선언의 `;` 가 여는 중인 정의를 지운다 — 안 지우면 Alpha 의 `{` 가 Beta 로 읽혀 같은 이름 한 묶음이 된다.
            "name": "두 줄 전방 선언 뒤의 정의 둘",
            "files": {
                "Source/Engine/Probe/Probe.h": (
                    "#pragma once\n\nnamespace sw\n{\n    struct Beta\n    ;\n\n    class Alpha\n    {\n    };\n\n    class Beta\n    {\n    };\n"
                    "} // namespace sw\n"
                ),
            },
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser)

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        listFile = self.selectTargetFiles(repositoryRoot, args.files, listScanRoot=kLintTargetRelDirs, suffixes=(".h", ".cpp", ".inl"))
        violations = flatMapConcurrent(lambda path: findViolations(path, repositoryRoot), listFile)
        return GateResult(listViolation=violations, summary=f"{len(listFile)} files scanned")


main = CheckNamespaceBlocksGate.run


if __name__ == "__main__":
    sys.exit(main())
