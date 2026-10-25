"""
Scripts/lint/AcronymSpellingRules.py

약어 철자 코드모드의 파일 하나 판정 — Pascal 낱말로 쓴 약어를 대문자로 고친 완성 글. 픽서 `FormatAcronymSpelling` 이 읽는다.

철자 규칙은 `Scripts/common/AcronymRegistry.py` 한 자리다 — 게이트 `CheckAcronymSpelling` 과 이 판정이 **같은 이름 자리 목록**(`AcronymRegistry.findNameSites` — 남의 이름을 뺀 것)과
같은 철자 판정(`respellName`)을 부른다. 게이트는 막을 약어(`kEnforced`)로 그 목록을 거르고, 코드모드는 고를 약어(`--acronym`)로 고친다.

판정과 고침은 한 함수다: 파일 하나(`SourceFile`)를 받아 고친 완성 글을 `FileFindings.fixedText` 로 돌려준다. 줄 끝(CRLF) · BOM 은 `SourceFile.restore` 가 지킨다.
가린 글은 함께 도는 게이트가 나눠 쓰는 `SourceFile.masked` 에서 만든다.
"""

from __future__ import annotations

from common import AcronymRegistry as registry
from lint.SourceScan import FileFindings, SourceFile


def inspectSource(source: SourceFile, acronyms: tuple[str, ...], *, bTypeNames: bool = True, bValueNames: bool = True) -> FileFindings:
    """
    파일 하나의 이름을 `acronyms` 의 새 철자로 고친 글. `bTypeNames` 는 대문자로 시작하는 이름(타입 · 이름공간 · 열거형),
    `bValueNames` 는 소문자 · `_` 로 시작하는 이름(함수 · 변수 · 멤버). 고칠 것이 없으면 `fixedText` 는 None 이다.
    """
    newText, mapRename = registry.respellCode(source.text, acronyms, bTypeNames=bTypeNames, bValueNames=bValueNames,
                                              masked=registry.maskCode(source.text, source.masked))
    return FileFindings(fixedText=source.restore(newText) if mapRename else None)
