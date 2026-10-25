"""
Scripts/lint/ForwardDeclarationRules.py

전방 선언(Forward Declaration) 정렬 규칙 — 이어진 전방 선언 덩어리를 enum(enum class) -> struct -> class 순서로 놓고, 그룹 사이에 빈 줄 하나를 둔다.
각 그룹 안에서는 타입 이름의 알파벳 순이다. 픽서 `FormatForwardDeclarations` 가 읽는다(짝 게이트는 아직 없다 — 생기면 `inspectSource` 를 부른다).

판정과 고침은 한 함수다: 파일 하나(`SourceFile`)를 받아 고친 완성 글을 `FileFindings.fixedText` 로 돌려준다. 줄 끝(CRLF) · BOM 은 `SourceFile.restore` 가 지킨다 —
규칙은 줄 끝을 `\\n` 으로 맞춘 글(`SourceFile.text`)만 본다.
"""

from __future__ import annotations

import re

from lint.SourceScan import FileFindings, SourceFile

_kSingleFwdRe = re.compile(
    r"^(\s*)(?:template\s*<[^;{}>]+>\s*)?"
    r"(enum(?:\s+class|\s+struct)?|struct|class)\s+"
    r"(?:(?:SW_\w+_API|SW_API|SW_GF_API|SW_KIT_API|SW_MODULE_API)\s+)?"
    r"([A-Za-z0-9_]+)"
    r"(?:\s*:\s*[A-Za-z0-9_:]+)?"
    r"\s*;(?:\s*//.*)?$"
)

_kTemplateHeaderRe = re.compile(r"^\s*template\s*<[^;{}>]+>\s*$")
_kSecondFwdRe = re.compile(
    r"^(\s*)"
    r"(enum(?:\s+class|\s+struct)?|struct|class)\s+"
    r"(?:(?:SW_\w+_API|SW_API|SW_GF_API|SW_KIT_API|SW_MODULE_API)\s+)?"
    r"([A-Za-z0-9_]+)"
    r"(?:\s*:\s*[A-Za-z0-9_:]+)?"
    r"\s*;(?:\s*//.*)?$"
)

_kNonFwdWords = ("friend", "using", "typedef", "extern", "return", "case", "default")


def getKindRankInternal(kindStr: str) -> int:
    stripped = kindStr.strip()
    if stripped.startswith("enum"):
        return 0
    if stripped == "struct":
        return 1
    if stripped == "class":
        return 2
    return 3


def parseNextForwardDeclItemInternal(lines: list[str], index: int) -> tuple[int, str, list[str], int] | None:
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
        rank = getKindRankInternal(matchSingle.group(2))
        typeName = matchSingle.group(3)
        return (rank, typeName, [line], index + 1)

    if _kTemplateHeaderRe.match(line) and index + 1 < len(lines):
        nextLine = lines[index + 1]
        matchSecond = _kSecondFwdRe.match(nextLine)
        if matchSecond:
            rank = getKindRankInternal(matchSecond.group(2))
            typeName = matchSecond.group(3)
            return (rank, typeName, [line, nextLine], index + 2)

    return None


def sortForwardDeclarationsInternal(text: str) -> str | None:
    """
    주어진 C++ 글(줄 끝 `\\n`)에서 전방 선언 블록을 찾아 enum -> struct -> class 순으로 정렬하고 그룹 간 1개의 공백 라인을 둔 글.
    바꿀 것이 없으면 None.
    """
    lines = text.split("\n")

    newLines: list[str] = []
    lineIndex = 0
    bModified = False

    while lineIndex < len(lines):
        item = parseNextForwardDeclItemInternal(lines, lineIndex)
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

                nextItem = parseNextForwardDeclItemInternal(lines, currIndex)
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

    return "\n".join(newLines) if bModified else None


def inspectSource(source: SourceFile) -> FileFindings:
    """파일 하나의 전방 선언 정렬 — 고친 글(`fixedText`)을 원본의 BOM · 줄 끝으로 되돌려 돌려준다. 정렬이 맞으면 `fixedText` 는 None 이다."""
    fixedBody = sortForwardDeclarationsInternal(source.text)
    return FileFindings(fixedText=None if fixedBody is None else source.restore(fixedBody))
