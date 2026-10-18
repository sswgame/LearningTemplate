#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/common/MarkdownSections.py

마크다운 문서를 제목 절로 가르는 **유일한 구현**이다. `plans --file <이름> --section` (dev/ListPlans.py) 과 `doc` (dev/RunDoc.py) 가
같이 쓴다 — 둘이 따로 자르면 코드 울타리 안의 `#` 나 하위 절 처리가 달라진다.
순수 함수만 둔다(파일을 읽지 않는다).
"""

from __future__ import annotations

import re
from typing import NamedTuple

_kHeadingRe = re.compile(r"^(#{1,6}) +(.+?)\s*$")
_kFenceRe = re.compile(r"^\s*(```|~~~)")
_kNumberQueryRe = re.compile(r"^\d+(?:[.\-]\d+)*$")


class Heading(NamedTuple):
    index: int  # 0 기반 줄 위치
    level: int
    line: str  # 제목 줄 전체(`### 5-4. ...`)
    title: str  # `#` 를 뺀 글


def findHeadings(listLine: list[str]) -> list[Heading]:
    """코드 울타리 밖의 제목들. 울타리 안의 `#` 줄은 제목이 아니다."""
    listHeading: list[Heading] = []
    bFence = False
    for index, line in enumerate(listLine):
        if _kFenceRe.match(line):
            bFence = not bFence
            continue
        match = None if bFence else _kHeadingRe.match(line)
        if match:
            listHeading.append(Heading(index, len(match.group(1)), line.rstrip(), match.group(2)))
    return listHeading


def sectionEnd(listLine: list[str], listHeading: list[Heading], position: int) -> int:
    """`listHeading[position]` 절의 끝 줄 위치(포함하지 않음) — 같은 수준 이하의 다음 제목 전까지, 하위 절은 든다."""
    level = listHeading[position].level
    for heading in listHeading[position + 1:]:
        if heading.level <= level:
            return heading.index
    return len(listLine)


def _matchesInternal(heading: Heading, query: str) -> bool:
    if _kNumberQueryRe.match(query):  # `5-4` 가 `15-4` · `5-40` 에 맞지 않게 제목 머리의 번호로만 맞춘다
        return re.match(re.escape(query) + r"(?![\w\-]|\.\d)", heading.title.lstrip("`* ")) is not None
    return query.lower() in heading.line.lower()


def extractSection(listLine: list[str], titlePart: str) -> list[str]:
    """제목에 `titlePart` 가 든 절의 줄들(제목 줄 포함, 하위 절 포함). `5-4` 같은 번호는 제목 머리의 번호로 맞춘다.

    맞는 절이 없거나 둘 이상이면 ValueError(메시지에 후보 제목).
    """
    listHeading = findHeadings(listLine)
    listMatch = [position for position, heading in enumerate(listHeading) if _matchesInternal(heading, titlePart)]
    if len(listMatch) != 1:
        listCandidate = [listHeading[position].line for position in listMatch] if listMatch else [heading.line for heading in listHeading]
        reason = "여럿에 맞습니다" if listMatch else "없습니다"
        raise ValueError(f"절 `{titlePart}` 이 {reason}. 후보:\n" + "\n".join(listCandidate))
    position = listMatch[0]
    return [line.rstrip() for line in listLine[listHeading[position].index:sectionEnd(listLine, listHeading, position)]]


def formatOutline(listLine: list[str]) -> str:
    """목차: `행번호  크기  제목 줄` — 크기는 그 절(하위 절 포함)의 UTF-8 바이트 수."""
    listHeading = findHeadings(listLine)
    listRow: list[str] = []
    for position, heading in enumerate(listHeading):
        end = sectionEnd(listLine, listHeading, position)
        size = sum(len(line.encode("utf-8")) + 1 for line in listLine[heading.index:end])
        listRow.append(f"{heading.index + 1:>6}  {size:>8}  {heading.line}")
    return "\n".join(listRow)
