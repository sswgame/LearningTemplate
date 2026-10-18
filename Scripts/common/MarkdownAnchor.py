#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/common/MarkdownAnchor.py

마크다운 제목의 앵커(GitHub 규칙)를 계산하는 **유일한 구현**이다. 문서 경로 게이트(`lint/gate/CheckDocPaths.py`)와
로드맵 점검(`common/StatusBoard.py`)이 같이 쓴다 — 둘이 따로 근사하면 한쪽만 통과하는 링크가 생긴다.
"""

from __future__ import annotations

import re
import unicodedata

_kHeadingRe = re.compile(r"^(#{1,6})\s+(.*?)\s*#*\s*$")


def slugifyHeading(heading: str) -> str:
    """GitHub 의 제목 앵커 — 소문자, 글자 · 숫자 · `_` · `-` 만 남기고 공백은 `-`."""
    text = re.sub(r"[`*~]", "", heading.strip().lower())
    listChar: list[str] = []
    for ch in text:
        if ch == " ":
            listChar.append("-")
        elif ch in "-_" or unicodedata.category(ch)[0] in "LN":
            listChar.append(ch)
    return "".join(listChar)


def collectAnchors(text: str) -> set[str]:
    """문서의 제목 앵커 전부(같은 제목은 `-1` · `-2` …). 코드 울타리 안의 줄은 제목이 아니다."""
    setAnchor: set[str] = set()
    mapCount: dict[str, int] = {}
    bInFence = False
    for line in text.splitlines():
        if line.lstrip().startswith(("```", "~~~")):
            bInFence = not bInFence
            continue
        if bInFence:
            continue
        match = _kHeadingRe.match(line)
        if match is None:
            continue
        slug = slugifyHeading(match.group(2))
        count = mapCount.get(slug, 0)
        mapCount[slug] = count + 1
        setAnchor.add(slug if count == 0 else f"{slug}-{count}")
    return setAnchor
