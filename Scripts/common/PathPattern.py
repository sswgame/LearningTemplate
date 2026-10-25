# -*- coding: utf-8 -*-
"""
경로 마디 단위 패턴 매칭 — C++ `AssetImportPathFilter`(임포트 규칙)와 같은 뜻의 Python 쪽 정본.

`*` · `?` 는 한 마디 안에서만 맞고 폴더(`/`)를 넘지 않는다. 마디 `**` 만 0개 이상의 마디를 넘는다.
`/` 없는 패턴은 파일 이름에만 맞고, 있는 패턴은 경로 전체를 마디별로 맞춘다. 대소문자는 가리지 않는다.
`fnmatch` 는 `*` 가 `/` 를 넘어 임포트 규칙과 같은 패턴이 다르게 읽히므로 이 저장소의 경로 패턴에는 쓰지 않는다.
"""

from __future__ import annotations

import fnmatch


def _matchesSegmentInternal(patternSegment: str, textSegment: str) -> bool:
    # 마디 하나 안에는 `/` 가 없으니 fnmatch 의 `*` 가 폴더를 넘을 일이 없다. `[` 는 글자 그대로 다루려고 이스케이프한다.
    escaped = patternSegment.replace("[", "[[]")
    return fnmatch.fnmatchcase(textSegment.lower(), escaped.lower())


def _matchesSegmentsInternal(listPattern: list[str], patternIndex: int, listText: list[str], textIndex: int) -> bool:
    while patternIndex < len(listPattern):
        if listPattern[patternIndex] == "**":
            return any(_matchesSegmentsInternal(listPattern, patternIndex + 1, listText, skip) for skip in range(textIndex, len(listText) + 1))
        if textIndex >= len(listText) or not _matchesSegmentInternal(listPattern[patternIndex], listText[textIndex]):
            return False
        patternIndex += 1
        textIndex += 1
    return textIndex == len(listText)


def splitSegments(path: str) -> list[str]:
    """역슬래시를 `/` 로 맞추고 빈 마디를 버린 경로 마디 목록."""
    return [segment for segment in path.replace("\\", "/").split("/") if segment]


def matchesPattern(pattern: str, path: str) -> bool:
    """패턴 하나가 경로(`/` 구분)에 맞는가."""
    listText = splitSegments(path)
    if "/" not in pattern.replace("\\", "/"):
        return bool(listText) and _matchesSegmentInternal(pattern, listText[-1])
    return _matchesSegmentsInternal(splitSegments(pattern), 0, listText, 0)


def matchesAnyPattern(listPattern: list[str], path: str) -> bool:
    return any(matchesPattern(pattern, path) for pattern in listPattern)
