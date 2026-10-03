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

  python Scripts/lint/gate/CheckNamespaceBlocks.py [--fix] [--files <path> ...]
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common import flatMapConcurrent  # noqa: E402
from LintGate import GateResult, LintGate  # noqa: E402

_kNamespaceRe = re.compile(r"^(\s*)namespace(?:\s+([\w:]+))?\s*(\{)?\s*$")
_kTypeRe = re.compile(r"^\s*(?:template\s*<[^;{]*>\s*)?(?:class|struct)\s+(?:SW_\w+\s+|alignas\s*\([^)]*\)\s+)*(\w+)(?:\s+final)?\s*(?::[^;{]*)?\{?\s*$")
_kTemplateLineRe = re.compile(r"^\s*template\s*<.*>\s*$")
_kIfOpenRe = re.compile(r"^\s*#\s*if")
_kIfCloseRe = re.compile(r"^\s*#\s*endif")


def maskCodeInternal(text: str) -> str:
    """주석 · 문자열 · 문자 리터럴을 공백으로 바꾼다(줄바꿈은 남겨 줄 번호가 원문과 맞는다)."""
    out: list[str] = []
    index = 0
    length = len(text)
    while index < length:
        if text.startswith("//", index):
            end = text.find("\n", index)
            end = length if end < 0 else end
            out.append(" " * (end - index))
            index = end
        elif text.startswith("/*", index):
            end = text.find("*/", index + 2)
            end = length if end < 0 else end + 2
            out.append("".join(ch if ch == "\n" else " " for ch in text[index:end]))
            index = end
        elif text[index] in "\"'":
            quote = text[index]
            end = index + 1
            while end < length and text[end] != quote and text[end] != "\n":
                end += 2 if text[end] == "\\" else 1
            out.append(quote + " " * max(0, min(end, length) - index - 1) + (quote if end < length and text[end] == quote else ""))
            index = end + 1 if end < length and text[end] == quote else end
        else:
            out.append(text[index])
            index += 1
    return "".join(out)


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

        for ch in line:
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


def processFile(filePath: Path, repositoryRoot: Path, checkOnly: bool = True) -> list[str]:
    try:
        raw = filePath.read_bytes().decode("utf-8")
    except (OSError, UnicodeDecodeError):
        return []
    newline = "\r\n" if "\r\n" in raw else "\n"
    text = raw.replace("\r\n", "\n")
    listLine = text.split("\n")
    listMasked = maskCodeInternal(text).split("\n")
    if len(listMasked) != len(listLine):
        return []

    listSplit = findSplitPoints(listMasked)
    if not listSplit:
        return []

    relativePath = filePath.relative_to(repositoryRoot).as_posix() if filePath.is_relative_to(repositoryRoot) else str(filePath)
    if checkOnly:
        return [f"{relativePath}:{split[3] + 1}: 한 namespace 블록에 클래스 · 구조체 정의가 여럿입니다 — 정의마다 블록을 나누세요 "
                f"(`py -3 Scripts/lint/gate/CheckNamespaceBlocks.py --fix --files {relativePath}`)" for split in listSplit]

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

    newText = "\n".join(listOut).replace("\n", newline)
    if newText != raw:
        filePath.write_bytes(newText.encode("utf-8"))
    return []


class CheckNamespaceBlocksGate(LintGate):
    """
    기본은 **검사만** 한다. 고치려면 `--fix` 를 준다(`FormatModified.py` 는 `processFile(..., checkOnly=False)` 를 직접 부른다).
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
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        parser.add_argument("--fix", action="store_true", help="보고만 하지 않고 파일을 고칩니다")
        self.addFilesArgument(parser)

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        listFile = self.selectTargetFiles(repositoryRoot, args.files, listScanRoot=("Source",), suffixes=(".h", ".cpp", ".inl"))
        violations = flatMapConcurrent(lambda path: processFile(path, repositoryRoot, checkOnly=not args.fix), listFile)
        return GateResult(listViolation=violations, summary=f"{len(listFile)} files scanned")


main = CheckNamespaceBlocksGate.run


if __name__ == "__main__":
    sys.exit(main())
