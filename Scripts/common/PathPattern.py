# -*- coding: utf-8 -*-
"""
경로 마디 단위 패턴 매칭 — C++ `AssetImportPathFilter`(임포트 규칙)와 같은 뜻의 Python 쪽 정본.

`*` · `?` 는 한 마디 안에서만 맞고 폴더(`/`)를 넘지 않는다. 마디 `**` 만 0개 이상의 마디를 넘는다.
`/` 없는 패턴은 파일 이름에만 맞고, 있는 패턴은 경로 전체를 마디별로 맞춘다. 대소문자는 가리지 않는다.
`fnmatch` 는 `*` 가 `/` 를 넘어 임포트 규칙과 같은 패턴이 다르게 읽히므로 이 저장소의 경로 패턴에는 쓰지 않는다.
"""

from __future__ import annotations

import fnmatch
import re
from functools import lru_cache

# 저장소 전수 검사는 같은 패턴을 수십만 경로에 맞춘다 — 패턴 해석과 경로 마디 나누기를 호출마다 되풀이하면 검증 하나가 분 단위가 된다.
_kPathCacheSize = 1 << 17


@lru_cache(maxsize=None)
def _compilePatternInternal(pattern: str) -> tuple[bool, tuple[re.Pattern[str] | None, ...]]:
    """(파일 이름에만 맞는 패턴인가, 마디별 정규식). 마디 `**` 는 None. `[` 는 글자 그대로 다루려고 이스케이프한다."""
    normalized = pattern.replace("\\", "/")
    bNameOnly = "/" not in normalized
    listPatternSegment = [segment for segment in normalized.split("/") if segment]
    listCompiled = tuple(None if segment == "**" and not bNameOnly else re.compile(fnmatch.translate(segment.replace("[", "[[]").lower())) for segment in listPatternSegment)
    return bNameOnly, listCompiled


@lru_cache(maxsize=_kPathCacheSize)
def _splitLowerInternal(path: str) -> tuple[str, ...]:
    return tuple(segment.lower() for segment in path.replace("\\", "/").split("/") if segment)


def _matchesSegmentsInternal(listPattern: tuple[re.Pattern[str] | None, ...], patternIndex: int, listText: tuple[str, ...], textIndex: int) -> bool:
    while patternIndex < len(listPattern):
        compiled = listPattern[patternIndex]
        if compiled is None:
            return any(_matchesSegmentsInternal(listPattern, patternIndex + 1, listText, skip) for skip in range(textIndex, len(listText) + 1))
        if textIndex >= len(listText) or compiled.match(listText[textIndex]) is None:
            return False
        patternIndex += 1
        textIndex += 1
    return textIndex == len(listText)


def matchesPattern(pattern: str, path: str) -> bool:
    """패턴 하나가 경로(`/` 구분)에 맞는가."""
    bNameOnly, listCompiled = _compilePatternInternal(pattern)
    listText = _splitLowerInternal(path)
    if bNameOnly:
        return bool(listText) and bool(listCompiled) and listCompiled[0].match(listText[-1]) is not None
    return _matchesSegmentsInternal(listCompiled, 0, listText, 0)


