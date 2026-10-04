"""
C++ · HLSL 소스 글에서 주석 · 리터럴을 지우는 도구입니다(게이트 공용).

지운 자리는 **같은 길이의 공백**으로 채우고 줄바꿈은 남긴다 — 줄 번호와 오프셋이 원문과 맞아야 위반을 원문 줄로 알리고, 중괄호를 세어 본문을 자를 수 있다.
한 번의 정규식 훑기로 주석과 리터럴을 함께 가린다. 그래서 문자열 안의 `//` 를 주석으로, 주석 안의 `"` 를 문자열로 잘못 읽지 않는다.
"""
from __future__ import annotations

import re

#: 주석 · 문자열 · 문자 리터럴. 앞의 두 갈래가 주석이다.
_kCommentOrLiteralRe = re.compile(r"//[^\n]*|/\*.*?\*/|\"(?:\\.|[^\"\\\n])*\"|'(?:\\.|[^'\\\n])*'", re.DOTALL)


def blankMatchInternal(text: str) -> str:
    """줄바꿈을 남기고 나머지를 공백으로 바꾼다. 주석 · 리터럴마다 불리므로 정규식 대신 줄 단위로 길이만 센다."""
    return "\n".join(" " * len(part) for part in text.split("\n"))


def blankCommentsAndLiterals(text: str) -> str:
    """주석 · 문자열 · 문자 리터럴을 같은 길이의 공백으로 바꿉니다(줄바꿈은 남긴다)."""
    return _kCommentOrLiteralRe.sub(lambda match: blankMatchInternal(match.group(0)), text)


def blankComments(text: str) -> str:
    """주석만 같은 길이의 공백으로 바꿉니다. 문자열 · 문자 리터럴은 그대로 둡니다(그 안의 `//` · `/*` 는 주석이 아니다)."""
    def replace(match: re.Match) -> str:
        found = match.group(0)
        return blankMatchInternal(found) if found.startswith(("//", "/*")) else found

    return _kCommentOrLiteralRe.sub(replace, text)


__all__ = ["blankComments", "blankCommentsAndLiterals"]
