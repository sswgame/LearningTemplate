"""
Scripts/lint/DiscardReasonRules.py

버린 실패 가능 결과 규칙 — `findFallibleDiscards`(판정). 게이트 `CheckDiscardReason` 과 보고서 `RunDiscardReasons` 가 같이 읽는다.

판정을 린트 틀 층(층 1)에 두는 까닭: 게이트 · 픽서 · 보고서(층 2)가 서로의 모듈을 import 하면 명령끼리 의존한다.
(`docs/decisions/5-2_Build.md`, 층 게이트 `CheckScriptLayers`) 규칙은 여기 한 자리고, 층 2 는 모두 이 모듈을 읽는다.
"""

from __future__ import annotations

import re

from common.CodeText import blankComments, blankCommentsAndLiterals
from lint.SharedRules import readFallibleVerbs
from lint.SourceScan import SourceFile

#: `(void)` 뒤의 호출 — 마지막 이름(`a.b->c::load`)을 뽑는다. 템플릿 인자 `<…>` 는 건너뛴다.
_kVoidCallRe = re.compile(r"\(\s*void\s*\)\s*(?P<callee>[A-Za-z_][\w:.\->]*?)\s*(?:<[^;()]*?>)?\s*\(")
#: 실패 가능 동사(또는 `re` + 동사)로 시작하는 이름 — `CheckFallibleNodiscard` 와 같은 판정.
_kFallibleNameRe = re.compile(r"^(?:re)?(?:" + "|".join(readFallibleVerbs()) + r")(?:[A-Z0-9_]\w*)?$")


def findFallibleDiscards(text: str, source: SourceFile | None = None) -> list[tuple[int, str, str]]:
    """
    파일 하나에서 실패 가능 함수의 결과를 `(void)` 로 버리는 자리를 (줄 번호, 함수 이름, 이유 — 없으면 빈 글자)로 돌려줍니다.
    `RunDiscardReasons` 보고서도 이 판정을 쓴다. 게이트는 함께 도는 게이트와 가린 글을 나눠 쓰려고 `source` 를 넘긴다.
    """
    listOriginal = text.splitlines()
    listNoComment = (blankComments(text) if source is None else source.noComment).splitlines()
    listCode = blankCommentsAndLiterals(text).splitlines() if source is None else source.listMaskedLine
    listResult: list[tuple[int, str, str]] = []

    def commentOfInternal(index: int) -> str:
        """그 줄 끝의 `//` 주석 글자(없으면 빈 글자)."""
        if index >= len(listOriginal) or listOriginal[index] == listNoComment[index]:
            return ""
        position = listOriginal[index].find("//", len(listNoComment[index].rstrip()))
        return listOriginal[index][position + 2:].strip() if position >= 0 else ""

    for index, line in enumerate(listCode):
        for match in _kVoidCallRe.finditer(line):
            name = re.split(r"->|\.|::", match.group("callee"))[-1]
            if _kFallibleNameRe.match(name) is None:
                continue
            reason = ""
            cursor = index
            while cursor < len(listCode):
                reason = commentOfInternal(cursor)
                if reason or ";" in listCode[cursor]:
                    break
                cursor += 1
            if not reason and index > 0 and listOriginal[index - 1].strip().startswith("//"):
                reason = listOriginal[index - 1].strip()[2:].strip()
            listResult.append((index + 1, name, reason))
    return listResult
