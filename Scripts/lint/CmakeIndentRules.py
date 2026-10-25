"""
Scripts/lint/CmakeIndentRules.py

CMake 들여쓰기 규칙 — 줄머리의 공백 들여쓰기를 탭으로(4 칸 = 탭 하나, 남는 칸은 공백 그대로). 따옴표 문자열 안의 줄(`file(CONFIGURE CONTENT "…")` 의 C++ 코드 등)과
대괄호 인자(`[[ … ]]`)가 든 파일은 건드리지 않는다. 픽서 `FormatCmakeIndent` 가 읽는다(짝 게이트는 아직 없다 — 생기면 `inspectSource` 를 부른다).

판정과 고침은 한 함수다: 파일 하나(`SourceFile`)를 받아 고친 완성 글을 `FileFindings.fixedText` 로 돌려준다. 줄 끝(CRLF) · BOM 은 `SourceFile.restore` 가 지킨다.
"""

from __future__ import annotations

import re

from lint.SourceScan import FileFindings, SourceFile

_kLeadingWhitespaceRe = re.compile(r"^[ \t]+")

#: 탭 하나의 칸 수 — 4 칸 = 탭 하나.
_kTabWidth = 4


def advanceStringStateInternal(line: str, bInString: bool) -> bool:
    """줄 하나를 지난 뒤 따옴표 문자열 안인가. 문자열 밖의 `#` 부터는 주석이다."""
    index = 0
    while index < len(line):
        character = line[index]
        if bInString:
            if character == "\\":
                index += 2
                continue
            if character == '"':
                bInString = False
        elif character == '"':
            bInString = True
        elif character == "#":
            break
        index += 1
    return bInString


def indentWithTabsInternal(text: str) -> str | None:
    """문자열 밖 줄의 줄머리 공백을 탭으로 바꾼 글(줄 끝 `\\n`). 바꿀 것이 없으면 None."""
    if "[[" in text or "[=[" in text:
        return None                           # 대괄호 인자 — 줄 안팎을 가를 수 없다, 손대지 않는다
    listLine = text.split("\n")
    bInString = False
    bChanged = False
    for index, line in enumerate(listLine):
        if not bInString:
            match = _kLeadingWhitespaceRe.match(line)
            if match and " " in match.group(0):
                width = 0
                for character in match.group(0):
                    width = (width // _kTabWidth + 1) * _kTabWidth if character == "\t" else width + 1
                newLine = "\t" * (width // _kTabWidth) + " " * (width % _kTabWidth) + line[match.end():]
                if newLine != line:
                    listLine[index] = newLine
                    bChanged = True
        bInString = advanceStringStateInternal(line, bInString)
    return "\n".join(listLine) if bChanged else None


def inspectSource(source: SourceFile) -> FileFindings:
    """파일 하나의 CMake 들여쓰기 — 고친 글(`fixedText`)을 원본의 BOM · 줄 끝으로 되돌려 돌려준다. 맞으면 `fixedText` 는 None 이다."""
    fixedBody = indentWithTabsInternal(source.text)
    return FileFindings(fixedText=None if fixedBody is None else source.restore(fixedBody))
