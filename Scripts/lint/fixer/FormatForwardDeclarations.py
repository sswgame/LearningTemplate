#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/fixer/FormatForwardDeclarations.py

C++ 소스코드 내 전방 선언(Forward Declaration)을
enum (및 enum class) -> struct -> class 순서로 정렬하고,
각 그룹 간 1개의 공백 라인을 삽입하여 구분합니다.
각 그룹 내에서는 타입 이름 기준 알파벳 순으로 정렬합니다.

사용법:
  py -3 Scripts/lint/fixer/FormatForwardDeclarations.py                    # Git 변경 파일 포맷팅
  py -3 Scripts/lint/fixer/FormatForwardDeclarations.py [파일들...]        # 지정한 파일들만 포맷팅
  py -3 Scripts/lint/fixer/FormatForwardDeclarations.py --all              # 전체 파일 포맷팅
  py -3 Scripts/lint/fixer/FormatForwardDeclarations.py --check            # 수정 없이 위반 여부만 검사
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintFixer

from LintFixer import FixPass, LintFixer  # noqa: E402
_kSingleFwdRe = re.compile(
    r"^(\s*)(?:template\s*<[^;{}>]+>\s*)?"
    r"(enum(?:\s+class|\s+struct)?|struct|class)\s+"
    r"(?:(?:SW_\w+_API|SW_API|SW_GF_API|SW_MODULE_API)\s+)?"
    r"([A-Za-z0-9_]+)"
    r"(?:\s*:\s*[A-Za-z0-9_:]+)?"
    r"\s*;(?:\s*//.*)?$"
)

_kTemplateHeaderRe = re.compile(r"^\s*template\s*<[^;{}>]+>\s*$")
_kSecondFwdRe = re.compile(
    r"^(\s*)"
    r"(enum(?:\s+class|\s+struct)?|struct|class)\s+"
    r"(?:(?:SW_\w+_API|SW_API|SW_GF_API|SW_MODULE_API)\s+)?"
    r"([A-Za-z0-9_]+)"
    r"(?:\s*:\s*[A-Za-z0-9_:]+)?"
    r"\s*;(?:\s*//.*)?$"
)

_kNonFwdWords = ("friend", "using", "typedef", "extern", "return", "case", "default")


def _getKindRankInternal(kindStr: str) -> int:
    stripped = kindStr.strip()
    if stripped.startswith("enum"):
        return 0
    if stripped == "struct":
        return 1
    if stripped == "class":
        return 2
    return 3


def _parseNextForwardDeclItemInternal(
    lines: list[str], index: int
) -> tuple[int, str, list[str], int] | None:
    """
    lines[index]부터 전방 선언 항목(1줄 또는 multiline template 2줄)을 파싱합니다.
    성공 시 (kindRank, typeName, linesList, nextIndex) 튜플을 반환하고,
    전방 선언이 아니면 None을 반환합니다.
    """
    if index >= len(lines):
        return None

    line = lines[index]
    stripped = line.strip()
    if not stripped or stripped.startswith(_kNonFwdWords):
        return None

    matchSingle = _kSingleFwdRe.match(line)
    if matchSingle:
        rank = _getKindRankInternal(matchSingle.group(2))
        typeName = matchSingle.group(3)
        return (rank, typeName, [line], index + 1)

    if _kTemplateHeaderRe.match(line) and index + 1 < len(lines):
        nextLine = lines[index + 1]
        matchSecond = _kSecondFwdRe.match(nextLine)
        if matchSecond:
            rank = _getKindRankInternal(matchSecond.group(2))
            typeName = matchSecond.group(3)
            return (rank, typeName, [line, nextLine], index + 2)

    return None


def formatForwardDeclarations(text: str) -> tuple[str, bool]:
    """
    주어진 C++ 코드 문자열에서 전방 선언 블록을 탐색하여
    enum -> struct -> class 순으로 정렬하고 그룹 간 1개의 공백 라인을 둡니다.
    반환값: (포맷팅된 문자열, 변경 여부)
    """
    newline = "\r\n" if "\r\n" in text else "\n"
    rawLines = text.split("\n")
    lines = [line[:-1] if line.endswith("\r") else line for line in rawLines]

    newLines: list[str] = []
    lineIndex = 0
    bModified = False

    while lineIndex < len(lines):
        item = _parseNextForwardDeclItemInternal(lines, lineIndex)
        if item is not None:
            blockItems = [item]
            startIndex = lineIndex
            currIndex = item[3]
            pendingBlankLines = 0

            while currIndex < len(lines):
                candidateLine = lines[currIndex]
                strippedCandidate = candidateLine.strip()
                if not strippedCandidate:
                    pendingBlankLines += 1
                    currIndex += 1
                    continue

                nextItem = _parseNextForwardDeclItemInternal(lines, currIndex)
                if nextItem is not None:
                    pendingBlankLines = 0
                    blockItems.append(nextItem)
                    currIndex = nextItem[3]
                    continue
                break

            # 블록 뒤에 붙어있던 공백 라인은 후속 일반 코드 영역의 빈 줄로 복원
            currIndex -= pendingBlankLines

            enums = [it for it in blockItems if it[0] == 0]
            structs = [it for it in blockItems if it[0] == 1]
            classes = [it for it in blockItems if it[0] == 2]

            enums.sort(key=lambda x: (x[1].lower(), x[1]))
            structs.sort(key=lambda x: (x[1].lower(), x[1]))
            classes.sort(key=lambda x: (x[1].lower(), x[1]))

            formattedBlock: list[str] = []
            groups = [enums, structs, classes]
            nonEmptyGroups = [group for group in groups if group]

            for groupIndex, group in enumerate(nonEmptyGroups):
                if groupIndex > 0:
                    formattedBlock.append("")
                for it in group:
                    formattedBlock.extend(it[2])

            originalBlock = lines[startIndex:currIndex]
            if formattedBlock != originalBlock:
                bModified = True
                newLines.extend(formattedBlock)
            else:
                newLines.extend(originalBlock)

            lineIndex = currIndex
        else:
            newLines.append(lines[lineIndex])
            lineIndex += 1

    return newline.join(newLines), bModified


class FormatForwardDeclarationsFixer(LintFixer):
    """전방 선언을 enum -> struct -> class 로 정렬하고 그룹 사이에 빈 줄을 넣는다."""

    tag = "ForwardDeclaration"
    description = "전방 선언(Forward Declaration) 정렬 (enum -> struct -> class 및 그룹 간 빈 줄 삽입)"
    listPass = (
        FixPass(
            transform=formatForwardDeclarations,
            problem="전방 선언 정렬(enum -> struct -> class 및 빈 줄)이 어긋났습니다.",
            done="전방 선언 정렬 완료",
        ),
    )


_gFixer = FormatForwardDeclarationsFixer()

#: 옛 이름 — `PreCommitLint` · `FormatModified` · `RunClangFormat` 이 이 철자로 부른다.
processFile = _gFixer.processFile
formatForwardDeclarationsBatch = _gFixer.processFiles

main = FormatForwardDeclarationsFixer.run


if __name__ == "__main__":
    sys.exit(main())
